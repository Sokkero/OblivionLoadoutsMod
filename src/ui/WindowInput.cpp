// Gear Loadouts - mouse and keyboard input through a subclass of the game window (see WindowInput.h).
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).

#include "ui/WindowInput.h"

#include <TlHelp32.h>

namespace windowinput
{
	namespace
	{
		WNDPROC            g_previous = nullptr;
		std::mutex         g_lock;   // guards everything below; never held across an Unreal call
		std::vector<Rect>  g_rects;
		std::vector<Click> g_clicks;
		bool               g_swallowLeftUp = false;
		bool               g_swallowRightUp = false;
		ULONGLONG          g_lastLeftDown = 0;
		int                g_hover = -1;

		// popup
		bool           g_popup = false;
		Rect           g_popupRect{}, g_saveRect{}, g_cancelRect{};
		std::u32string g_text;
		bool           g_selected = false;
		int            g_maxLength = 20;
		char16_t       g_highSurrogate = 0;
		std::uint32_t  g_fieldVersion = 0;
		bool           g_keyDown[256]{};   // keys pressed while the popup was open: their key-up is swallowed too

		// Not every key reader uses WM_KEYDOWN: other mods' key binds (UE4SS RegisterKeyBind: CloseMenusMod closed the
		// inventory on I and switched to the map on M) poll the key state, and the game imports GetRawInputData. While
		// the popup is open both paths are closed for keyboard keys too (tested 2026-11-18: the key-state block works).
		std::atomic<bool>          g_popupFlag{ false };   // g_popup for the hooks below (no lock there)
		std::atomic<std::uint32_t> g_rawBlocked{ 0 };
		std::atomic<std::uint32_t> g_stateBlocked{ 0 };


		using KeyState_t = SHORT(WINAPI*)(int);
		KeyState_t g_origAsync = nullptr;
		KeyState_t g_origKeyState = nullptr;

		bool IsKeyboardKey(int a_vk)
		{
			return a_vk > VK_XBUTTON2;   // 1..6 are mouse buttons: they stay readable
		}

		SHORT WINAPI HookGetAsyncKeyState(int a_vk)
		{
			if (g_popupFlag.load(std::memory_order_relaxed) && IsKeyboardKey(a_vk)) {
				g_stateBlocked.fetch_add(1, std::memory_order_relaxed);
				// Still ask Windows and drop the answer: GetAsyncKeyState's low bit means "pressed since the last call".
				// Not calling it kept that bit set, and CloseMenusMod closed the inventory right after the popup closed
				// (test 2026-11-18).
				if (g_origAsync) {
					g_origAsync(a_vk);
				}
				return 0;
			}
			return g_origAsync ? g_origAsync(a_vk) : 0;   // the real user32 function
		}

		SHORT WINAPI HookGetKeyState(int a_vk)
		{
			if (g_popupFlag.load(std::memory_order_relaxed) && IsKeyboardKey(a_vk)) {
				g_stateBlocked.fetch_add(1, std::memory_order_relaxed);
				return 0;
			}
			return g_origKeyState ? g_origKeyState(a_vk) : 0;
		}

		// the game image's import slot for a_dll!a_function is pointed at a_hook; the old target is returned
		// a module's import slot for a_dll!a_function is pointed at a_hook; true when a slot was changed now
		void* PatchImport(std::uint8_t* base, const char* a_dll, const char* a_function, void* a_hook)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!dir.VirtualAddress) {
				return nullptr;
			}
			for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
				if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), a_dll) != 0) {
					continue;
				}
				auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots) {
					if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal) ||
						std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, a_function) != 0) {
						continue;
					}
					DWORD old = 0;
					if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
						return nullptr;
					}
					void* previous = reinterpret_cast<void*>(slots->u1.Function);
					if (previous == a_hook) {
						VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
						return nullptr;   // done before
					}
					slots->u1.Function = reinterpret_cast<ULONGLONG>(a_hook);
					VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
					return previous;
				}
			}
			return nullptr;
		}

		bool Inside(const Rect& a_r, int a_x, int a_y)
		{
			return a_x >= a_r.left && a_x < a_r.right && a_y >= a_r.top && a_y < a_r.bottom;
		}

		int Hit(LPARAM a_position)
		{
			const int x = static_cast<short>(LOWORD(a_position));
			const int y = static_cast<short>(HIWORD(a_position));
			for (std::size_t i = 0; i < g_rects.size(); ++i) {
				if (Inside(g_rects[i], x, y)) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// Menus Revamped's search bar takes the keyboard and its own clicks while typing: then we stay out of the way
		bool MenusRevampedTyping()
		{
			using fn_t = bool (*)();
			static fn_t fn = [] {
				HMODULE mr = GetModuleHandleW(L"MenusRevamped.dll");
				return mr ? reinterpret_cast<fn_t>(GetProcAddress(mr, "MenusRevamped_IsTyping")) : nullptr;
			}();
			return fn && fn();
		}

		std::string Utf8(const std::u32string& a_s)
		{
			std::string out;
			for (char32_t cp : a_s) {
				if (cp < 0x80) {
					out += static_cast<char>(cp);
				} else if (cp < 0x800) {
					out += static_cast<char>(0xC0 | (cp >> 6));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				} else if (cp < 0x10000) {
					out += static_cast<char>(0xE0 | (cp >> 12));
					out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				} else {
					out += static_cast<char>(0xF0 | (cp >> 18));
					out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
					out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				}
			}
			return out;
		}

		std::u32string Decode(const std::string& a_utf8)
		{
			std::u32string out;
			const int      n = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
			std::wstring   w(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
			if (n > 0) {
				MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), w.data(), n);
			}
			for (std::size_t i = 0; i < w.size(); ++i) {
				const char16_t c = static_cast<char16_t>(w[i]);
				if (c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) {
					const char16_t d = static_cast<char16_t>(w[i + 1]);
					out.push_back(0x10000 + ((static_cast<char32_t>(c) - 0xD800) << 10) + (static_cast<char32_t>(d) - 0xDC00));
					++i;
				} else {
					out.push_back(c);
				}
			}
			return out;
		}

		void Typed(char16_t a_c)
		{
			if (a_c >= 0xD800 && a_c < 0xDC00) {   // the first half of a character outside the BMP
				g_highSurrogate = a_c;
				return;
			}
			char32_t cp = a_c;
			if (a_c >= 0xDC00 && a_c < 0xE000) {
				if (!g_highSurrogate) {
					return;
				}
				cp = 0x10000 + ((static_cast<char32_t>(g_highSurrogate) - 0xD800) << 10) + (static_cast<char32_t>(a_c) - 0xDC00);
			}
			g_highSurrogate = 0;
			if (cp == 0x08) {   // Backspace
				if (g_selected) {
					g_text.clear();
				} else if (!g_text.empty()) {
					g_text.pop_back();
				}
				g_selected = false;
				++g_fieldVersion;
				return;
			}
			if (cp == 0x01) {   // Ctrl+A
				g_selected = !g_text.empty();
				++g_fieldVersion;
				return;
			}
			if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
				return;   // other control characters (Enter, Esc, Tab, Ctrl+letters) are not text
			}
			if (g_selected) {
				g_text.clear();
				g_selected = false;
			}
			if (static_cast<int>(g_text.size()) < g_maxLength) {
				g_text.push_back(cp);
			}
			++g_fieldVersion;
		}

		// Every loaded module except ours and Windows' own: its GetAsyncKeyState / GetKeyState imports read keyboard keys as
		// "up" while the popup is open. Needed for other mods' key binds: CloseMenusMod (a UE4SS Lua script) closes the
		// inventory on I and switches to the map on M through UE4SS's RegisterKeyBind, which polls the key state from
		// UE4SS.dll, not from the game image (test 2026-11-18). Run again at every popup open (late-loaded modules).
		void PatchAllModules()
		{
			HMODULE self = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&PatchAllModules), &self);
			wchar_t windows[MAX_PATH]{};
			GetWindowsDirectoryW(windows, MAX_PATH);
			HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
			if (snap == INVALID_HANDLE_VALUE) {
				return;
			}
			MODULEENTRY32W entry{};
			entry.dwSize = sizeof(entry);
			std::string patched;
			for (BOOL ok = Module32FirstW(snap, &entry); ok; ok = Module32NextW(snap, &entry)) {
				if (entry.hModule == self || _wcsnicmp(entry.szExePath, windows, std::wcslen(windows)) == 0) {
					continue;
				}
				auto* base = reinterpret_cast<std::uint8_t*>(entry.hModule);
				const bool a = PatchImport(base, "USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&HookGetAsyncKeyState)) != nullptr;
				const bool k = PatchImport(base, "USER32.dll", "GetKeyState", reinterpret_cast<void*>(&HookGetKeyState)) != nullptr;
				if (a || k) {
					const auto name = std::filesystem::path(entry.szModule).string();
					patched += std::format("{}{} ({}{})", patched.empty() ? "" : ", ", name, a ? "GetAsyncKeyState" : "", k ? (a ? ", GetKeyState" : "GetKeyState") : "");
				}
			}
			CloseHandle(snap);
			if (!patched.empty()) {
				logger::info("input: key-state reads blocked while typing in: {}", patched);
			}
		}

		// popup mode; true = swallowed
		bool PopupMessage(UINT a_message, WPARAM a_w, LPARAM a_l)
		{
			switch (a_message) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				if (a_message == WM_SYSKEYDOWN && (a_w == VK_F4 || a_w == VK_TAB)) {
					return false;   // Alt+F4, Alt+Tab: Windows' own
				}
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
			case WM_SYSCHAR:
			case WM_DEADCHAR:
			case WM_SYSDEADCHAR:
				return true;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				{
					const int x = static_cast<short>(LOWORD(a_l));
					const int y = static_cast<short>(HIWORD(a_l));
					if (Inside(g_saveRect, x, y)) {
						g_clicks.push_back({ Action::Save, -1 });
					} else if (Inside(g_cancelRect, x, y)) {
						g_clicks.push_back({ Action::Cancel, -1 });
					} else if (!Inside(g_popupRect, x, y)) {
						g_clicks.push_back({ Action::Cancel, -1 });   // a click outside the popup = Cancel (user, 2026-11-17)
					}
					g_swallowLeftUp = true;
					return true;   // never reaches the list behind the popup
				}
			case WM_RBUTTONDOWN:
			case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONDBLCLK:
				if (a_message != WM_MBUTTONDOWN && a_message != WM_MBUTTONDBLCLK) {
					g_swallowRightUp = true;
				}
				return true;
			case WM_LBUTTONUP:
				g_swallowLeftUp = false;
				return true;
			case WM_RBUTTONUP:
				g_swallowRightUp = false;
				return true;
			case WM_MBUTTONUP:
			case WM_MOUSEWHEEL:
				return true;   // the list behind must not scroll
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
				if (g_swallowLeftUp) {
					g_swallowLeftUp = false;
					return true;
				}
				return false;
			case WM_RBUTTONUP:
				if (g_swallowRightUp) {
					g_swallowRightUp = false;
					return true;
				}
				return false;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				// a key pressed in the popup (Esc, Enter) must not arrive half at the game after it closed
				if (a_w < 256 && g_keyDown[a_w]) {
					g_keyDown[a_w] = false;
					return true;
				}
				return false;
			default:
				break;
			}
			if (g_rects.empty()) {
				g_hover = -1;
				return false;
			}
			switch (a_message) {
			case WM_MOUSEMOVE:
				g_hover = Hit(a_l);
				return false;   // never swallowed: the game keeps its own hover
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				{
					const int i = Hit(a_l);
					if (i < 0) {
						return false;
					}
					const ULONGLONG now = GetTickCount64();
					// the second half of a double click is not a second switch
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

		LRESULT CALLBACK WindowProc(HWND a_window, UINT a_message, WPARAM a_w, LPARAM a_l)
		{
			bool popup = false;
			{
				std::scoped_lock l(g_lock);
				popup = g_popup;
			}
			if (popup && a_message == WM_INPUT) {
				RAWINPUTHEADER header{};
				UINT           size = sizeof(header);
				if (GetRawInputData(reinterpret_cast<HRAWINPUT>(a_l), RID_HEADER, &header, &size, sizeof(RAWINPUTHEADER)) == sizeof(header) &&
					header.dwType == RIM_TYPEKEYBOARD) {
					g_rawBlocked.fetch_add(1, std::memory_order_relaxed);
					return DefWindowProcW(a_window, a_message, a_w, a_l);   // the system's clean-up only; the game never sees it
				}
			}
			if ((popup || !MenusRevampedTyping()) && Handle(a_message, a_w, a_l)) {
				return 0;
			}
			return CallWindowProcW(g_previous, a_window, a_message, a_w, a_l);
		}

		BOOL CALLBACK FindGameWindow(HWND a_window, LPARAM a_result)
		{
			DWORD process = 0;
			GetWindowThreadProcessId(a_window, &process);
			wchar_t name[64]{};
			GetClassNameW(a_window, name, 64);
			if (process != GetCurrentProcessId() || !IsWindowVisible(a_window) || GetWindow(a_window, GW_OWNER) ||
				std::wcscmp(name, L"UnrealWindow") != 0) {
				return TRUE;
			}
			*reinterpret_cast<HWND*>(a_result) = a_window;
			return FALSE;
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
		if (!g_origAsync) {
			HMODULE user32 = GetModuleHandleW(L"user32.dll");
			g_origAsync = reinterpret_cast<KeyState_t>(GetProcAddress(user32, "GetAsyncKeyState"));
			g_origKeyState = reinterpret_cast<KeyState_t>(GetProcAddress(user32, "GetKeyState"));
			PatchAllModules();
		}
		// Never removed: another plugin may subclass after us and keep our procedure as its "previous".
		g_previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
		if (g_previous) {
			logger::info("input: the game window is subclassed (window thread {}, game thread {}, Menus Revamped {})",
				GetWindowThreadProcessId(window, nullptr), GetCurrentThreadId(), GetModuleHandleW(L"MenusRevamped.dll") ? "loaded" : "not loaded");
		} else {
			logger::error("input: the game window could not be subclassed ({})", GetLastError());
		}
	}

	void SetRects(std::vector<Rect> a_rects)
	{
		std::scoped_lock l(g_lock);
		g_rects = std::move(a_rects);
	}

	std::vector<Click> TakeClicks()
	{
		std::scoped_lock l(g_lock);
		return std::exchange(g_clicks, {});
	}

	int HoverIndex()
	{
		std::scoped_lock l(g_lock);
		return g_rects.empty() || g_popup ? -1 : g_hover;
	}

	void OpenPopup(const std::string& a_utf8, int a_maxLength)
	{
		PatchAllModules();   // modules loaded since the last time
		std::scoped_lock l(g_lock);
		g_popup = true;
		g_popupFlag = true;
		g_rawBlocked = 0;
		g_stateBlocked = 0;
		g_text = Decode(a_utf8);
		g_selected = !g_text.empty();
		g_maxLength = a_maxLength;
		g_highSurrogate = 0;
		g_popupRect = g_saveRect = g_cancelRect = {};
		++g_fieldVersion;
	}

	void ClosePopup()
	{
		std::scoped_lock l(g_lock);
		if (g_popup) {
			logger::info("input: while the popup was open, {} raw keyboard message(s) and {} key-state read(s) were blocked",
				g_rawBlocked.load(), g_stateBlocked.load());
		}
		g_popup = false;
		g_popupFlag = false;
		g_highSurrogate = 0;
	}

	bool PopupOpen()
	{
		std::scoped_lock l(g_lock);
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
		return { Utf8(g_text), g_selected, g_fieldVersion };
	}
}
