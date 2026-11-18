// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Menus Revamped by cfa911 (see NOTICE.md). Changed: rects, a click queue, one text field, and key-state
// blocking for other modules.

#include "ui/WindowInput.h"

#include "Text.h"

#include <TlHelp32.h>

namespace windowinput
{
	namespace
	{
		WNDPROC           g_previous = nullptr;
		std::atomic<bool> g_popup{ false };   // also read lock-free by the key-state hooks

		std::mutex         g_lock;   // guards everything below
		std::vector<Rect>  g_rects;
		std::vector<Click> g_clicks;
		bool               g_swallowLeftUp = false;
		bool               g_swallowRightUp = false;
		ULONGLONG          g_lastLeftDown = 0;
		int                g_hover = -1;
		Rect               g_popupRect{}, g_saveRect{}, g_cancelRect{};
		std::u32string     g_text;
		bool               g_selected = false;
		int                g_maxLength = 0;
		char32_t           g_highSurrogate = 0;
		std::uint32_t      g_fieldVersion = 0;
		bool               g_keyDown[256]{};   // pressed while the popup was open: their key-up is swallowed too

		using KeyState_t = SHORT(WINAPI*)(int);
		KeyState_t g_origAsync = nullptr;
		KeyState_t g_origKeyState = nullptr;

		bool Blocked(int a_vk)
		{
			return g_popup.load(std::memory_order_relaxed) && a_vk > VK_XBUTTON2;   // mouse buttons stay readable
		}

		SHORT WINAPI HookGetAsyncKeyState(int a_vk)
		{
			// still call the original: its "pressed since last call" bit must be consumed, or the key fires after the popup closes
			const SHORT state = g_origAsync(a_vk);
			return Blocked(a_vk) ? 0 : state;
		}

		SHORT WINAPI HookGetKeyState(int a_vk)
		{
			return Blocked(a_vk) ? 0 : g_origKeyState(a_vk);
		}

		// points a module's import of a_dll!a_function at a_hook; true when a slot was changed now
		bool PatchImport(std::uint8_t* a_base, const char* a_dll, const char* a_function, void* a_hook)
		{
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(a_base + reinterpret_cast<const IMAGE_DOS_HEADER*>(a_base)->e_lfanew);
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!dir.VirtualAddress) {
				return false;
			}
			for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(a_base + dir.VirtualAddress); desc->Name; ++desc) {
				if (_stricmp(reinterpret_cast<const char*>(a_base + desc->Name), a_dll) != 0) {
					continue;
				}
				auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(a_base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(a_base + desc->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots) {
					if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal) ||
						std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(a_base + names->u1.AddressOfData)->Name, a_function) != 0 ||
						slots->u1.Function == reinterpret_cast<ULONGLONG>(a_hook)) {
						continue;
					}
					DWORD old = 0;
					if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
						return false;
					}
					slots->u1.Function = reinterpret_cast<ULONGLONG>(a_hook);
					VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
					return true;
				}
			}
			return false;
		}

		// every loaded module except ours and Windows' own; run at each popup open to catch late-loaded modules
		void PatchAllModules()
		{
			if (!g_origAsync) {
				HMODULE user32 = GetModuleHandleW(L"user32.dll");
				g_origAsync = reinterpret_cast<KeyState_t>(GetProcAddress(user32, "GetAsyncKeyState"));
				g_origKeyState = reinterpret_cast<KeyState_t>(GetProcAddress(user32, "GetKeyState"));
			}
			HMODULE self = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&PatchAllModules), &self);
			wchar_t windows[MAX_PATH]{};
			const auto windowsLen = GetWindowsDirectoryW(windows, MAX_PATH);
			HANDLE     snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
			if (snap == INVALID_HANDLE_VALUE) {
				return;
			}
			MODULEENTRY32W entry{ .dwSize = sizeof(MODULEENTRY32W) };
			for (BOOL ok = Module32FirstW(snap, &entry); ok; ok = Module32NextW(snap, &entry)) {
				if (entry.hModule == self || _wcsnicmp(entry.szExePath, windows, windowsLen) == 0) {
					continue;
				}
				auto*      base = reinterpret_cast<std::uint8_t*>(entry.hModule);
				const bool a = PatchImport(base, "USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&HookGetAsyncKeyState));
				const bool k = PatchImport(base, "USER32.dll", "GetKeyState", reinterpret_cast<void*>(&HookGetKeyState));
				if (a || k) {
					logger::info("input: key-state reads of {} blocked while typing", std::filesystem::path(entry.szModule).string());
				}
			}
			CloseHandle(snap);
		}

		bool Inside(const Rect& a_r, LPARAM a_position)
		{
			const int x = static_cast<short>(LOWORD(a_position));
			const int y = static_cast<short>(HIWORD(a_position));
			return x >= a_r.left && x < a_r.right && y >= a_r.top && y < a_r.bottom;
		}

		int Hit(LPARAM a_position)
		{
			const auto it = std::ranges::find_if(g_rects, [&](const Rect& r) { return Inside(r, a_position); });
			return it == g_rects.end() ? -1 : static_cast<int>(it - g_rects.begin());
		}

		// Menus Revamped's search bar owns the keyboard and its clicks while typing
		bool MenusRevampedTyping()
		{
			using fn_t = bool (*)();
			static fn_t fn = [] {
				HMODULE mr = GetModuleHandleW(L"MenusRevamped.dll");
				return mr ? reinterpret_cast<fn_t>(GetProcAddress(mr, "MenusRevamped_IsTyping")) : nullptr;
			}();
			return fn && fn();
		}

		void Typed(char32_t a_c)
		{
			if (text::IsHighSurrogate(a_c)) {
				g_highSurrogate = a_c;
				return;
			}
			if (text::IsLowSurrogate(a_c)) {
				if (!g_highSurrogate) {
					return;
				}
				a_c = text::Combine(g_highSurrogate, a_c);
			}
			g_highSurrogate = 0;
			if (a_c == 0x08) {   // Backspace
				if (g_selected) {
					g_text.clear();
				} else if (!g_text.empty()) {
					g_text.pop_back();
				}
				g_selected = false;
			} else if (a_c == 0x01) {   // Ctrl+A
				g_selected = !g_text.empty();
			} else if (!text::IsControl(a_c)) {
				if (g_selected) {
					g_text.clear();
					g_selected = false;
				}
				if (static_cast<int>(g_text.size()) < g_maxLength) {
					g_text.push_back(a_c);
				}
			} else {
				return;   // Enter, Esc, Tab, Ctrl+letter: not text
			}
			++g_fieldVersion;
		}

		// popup open: every key and mouse button is ours; true = swallowed
		bool PopupMessage(UINT a_message, WPARAM a_w, LPARAM a_l)
		{
			switch (a_message) {
			case WM_SYSKEYDOWN:
				if (a_w == VK_F4 || a_w == VK_TAB) {
					return false;   // Alt+F4, Alt+Tab
				}
				[[fallthrough]];
			case WM_KEYDOWN:
				if (a_w < 256) {
					g_keyDown[a_w] = true;
				}
				if (a_w == VK_RETURN) {
					g_clicks.push_back({ Action::Save, -1 });
				} else if (a_w == VK_ESCAPE) {
					g_clicks.push_back({ Action::Cancel, -1 });
				} else if (a_w == VK_DELETE) {
					g_text.clear();
					g_selected = false;
					++g_fieldVersion;
				}
				return true;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				if (a_w < 256) {
					g_keyDown[a_w] = false;
				}
				return true;
			case WM_CHAR:
				Typed(static_cast<char16_t>(a_w));
				return true;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				if (Inside(g_saveRect, a_l)) {
					g_clicks.push_back({ Action::Save, -1 });
				} else if (Inside(g_cancelRect, a_l) || !Inside(g_popupRect, a_l)) {
					g_clicks.push_back({ Action::Cancel, -1 });
				}
				g_swallowLeftUp = true;
				return true;
			case WM_RBUTTONDOWN:
			case WM_RBUTTONDBLCLK:
				g_swallowRightUp = true;
				return true;
			case WM_LBUTTONUP:
				g_swallowLeftUp = false;
				return true;
			case WM_RBUTTONUP:
				g_swallowRightUp = false;
				return true;
			case WM_SYSCHAR:
			case WM_DEADCHAR:
			case WM_SYSDEADCHAR:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONDBLCLK:
			case WM_MBUTTONUP:
			case WM_MOUSEWHEEL:
				return true;
			default:
				return false;
			}
		}

		// true = swallowed
		bool Handle(UINT a_message, WPARAM a_w, LPARAM a_l)
		{
			std::scoped_lock l(g_lock);
			if (g_popup) {
				return PopupMessage(a_message, a_w, a_l);
			}
			switch (a_message) {
			case WM_LBUTTONUP:
				return std::exchange(g_swallowLeftUp, false);
			case WM_RBUTTONUP:
				return std::exchange(g_swallowRightUp, false);
			case WM_KEYUP:
			case WM_SYSKEYUP:
				// a key pressed in the popup (Esc, Enter) must not reach the game half
				return a_w < 256 && std::exchange(g_keyDown[a_w], false);
			case WM_MOUSEMOVE:
				g_hover = Hit(a_l);
				return false;   // the game keeps its own hover
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				{
					const int i = Hit(a_l);
					if (i < 0) {
						return false;
					}
					// the second half of a double click is not a second switch
					const ULONGLONG now = GetTickCount64();
					if (a_message == WM_LBUTTONDOWN || now - g_lastLeftDown > GetDoubleClickTime()) {
						g_clicks.push_back({ Action::Left, i });
					}
					g_lastLeftDown = now;
					g_swallowLeftUp = true;
					return true;
				}
			case WM_RBUTTONDOWN:
			case WM_RBUTTONDBLCLK:
				{
					const int i = Hit(a_l);
					if (i < 0) {
						return false;
					}
					g_clicks.push_back({ Action::Right, i });
					g_swallowRightUp = true;
					return true;
				}
			default:
				return false;
			}
		}

		bool IsRawKeyboard(LPARAM a_l)
		{
			RAWINPUTHEADER header{};
			UINT           size = sizeof(header);
			return GetRawInputData(reinterpret_cast<HRAWINPUT>(a_l), RID_HEADER, &header, &size, sizeof(header)) == sizeof(header) &&
			       header.dwType == RIM_TYPEKEYBOARD;
		}

		LRESULT CALLBACK WindowProc(HWND a_window, UINT a_message, WPARAM a_w, LPARAM a_l)
		{
			const bool popup = g_popup;
			if (popup && a_message == WM_INPUT && IsRawKeyboard(a_l)) {
				return DefWindowProcW(a_window, a_message, a_w, a_l);   // system clean-up only; the game never sees it
			}
			if ((popup || !MenusRevampedTyping()) && Handle(a_message, a_w, a_l)) {
				return 0;
			}
			return CallWindowProcW(g_previous, a_window, a_message, a_w, a_l);
		}

		BOOL CALLBACK FindGameWindow(HWND a_window, LPARAM a_result)
		{
			DWORD   process = 0;
			wchar_t name[64]{};
			GetWindowThreadProcessId(a_window, &process);
			GetClassNameW(a_window, name, 64);
			if (process == GetCurrentProcessId() && IsWindowVisible(a_window) && !GetWindow(a_window, GW_OWNER) && std::wcscmp(name, L"UnrealWindow") == 0) {
				*reinterpret_cast<HWND*>(a_result) = a_window;
				return FALSE;
			}
			return TRUE;
		}
	}

	void Install()
	{
		static ULONGLONG next = 0;
		if (g_previous || GetTickCount64() < next) {
			return;
		}
		next = GetTickCount64() + 1000;
		HWND window = nullptr;
		EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&window));
		if (!window) {
			return;
		}
		PatchAllModules();
		// never removed: another plugin may subclass after us and chain to our procedure
		g_previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
		if (!g_previous) {
			logger::error("input: game window could not be subclassed ({})", GetLastError());
		}
	}

	void SetRects(std::vector<Rect> a_rects)
	{
		std::scoped_lock l(g_lock);
		g_rects = std::move(a_rects);
		if (g_rects.empty()) {
			g_hover = -1;
		}
	}

	std::vector<Click> TakeClicks()
	{
		std::scoped_lock l(g_lock);
		return std::exchange(g_clicks, {});
	}

	int HoverIndex()
	{
		std::scoped_lock l(g_lock);
		return g_popup ? -1 : g_hover;
	}

	void OpenPopup(const std::string& a_utf8, int a_maxLength)
	{
		PatchAllModules();
		std::scoped_lock l(g_lock);
		g_text = text::Decode(a_utf8).value_or(U"");
		g_selected = !g_text.empty();
		g_maxLength = a_maxLength;
		g_highSurrogate = 0;
		g_popupRect = g_saveRect = g_cancelRect = {};
		++g_fieldVersion;
		g_popup = true;
	}

	void ClosePopup()
	{
		std::scoped_lock l(g_lock);
		g_popup = false;
		g_highSurrogate = 0;
	}

	bool PopupOpen()
	{
		return g_popup;
	}

	void SetPopupRects(const Rect& a_popup, const Rect& a_save, const Rect& a_cancel)
	{
		std::scoped_lock l(g_lock);
		g_popupRect = a_popup;
		g_saveRect = a_save;
		g_cancelRect = a_cancel;
	}

	Field GetField()
	{
		std::scoped_lock l(g_lock);
		return { text::Encode(g_text), g_selected, g_fieldVersion };
	}
}
