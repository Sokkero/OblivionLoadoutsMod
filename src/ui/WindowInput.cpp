// Gear Loadouts - mouse input through a subclass of the game window (see WindowInput.h).
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).

#include "ui/WindowInput.h"

namespace windowinput
{
	namespace
	{
		WNDPROC           g_previous = nullptr;
		std::mutex        g_lock;   // guards everything below; never held across an Unreal call
		std::vector<Rect> g_rects;
		std::vector<Click> g_clicks;
		bool              g_swallowLeftUp = false;
		bool              g_swallowRightUp = false;
		ULONGLONG         g_lastLeftDown = 0;
		int               g_hover = -1;   // the rect under the mouse, -1 = none

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

		int Hit(LPARAM a_position)
		{
			const int x = static_cast<short>(LOWORD(a_position));
			const int y = static_cast<short>(HIWORD(a_position));
			for (std::size_t i = 0; i < g_rects.size(); ++i) {
				const auto& r = g_rects[i];
				if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// true = swallowed
		bool Handle(UINT a_message, LPARAM a_position)
		{
			std::scoped_lock l(g_lock);
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
			default:
				break;
			}
			if (g_rects.empty()) {
				g_hover = -1;
				return false;
			}
			if (a_message == WM_MOUSEMOVE) {
				g_hover = Hit(a_position);
				return false;   // never swallowed: the game keeps its own hover
			}
			switch (a_message) {
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				{
					const int i = Hit(a_position);
					if (i < 0) {
						return false;
					}
					const ULONGLONG now = GetTickCount64();
					// a double click on our button is two clicks for us only if it is not the second half of one
					if (a_message == WM_LBUTTONDOWN || now - g_lastLeftDown > GetDoubleClickTime()) {
						g_clicks.push_back({ i, false });
					}
					g_lastLeftDown = now;
					g_swallowLeftUp = true;
					return true;
				}
			case WM_RBUTTONDOWN:
			case WM_RBUTTONDBLCLK:
				{
					const int i = Hit(a_position);
					if (i < 0) {
						return false;
					}
					g_clicks.push_back({ i, true });
					g_swallowRightUp = true;
					return true;
				}
			default:
				return false;
			}
		}

		LRESULT CALLBACK WindowProc(HWND a_window, UINT a_message, WPARAM a_w, LPARAM a_l)
		{
			if (!MenusRevampedTyping() && Handle(a_message, a_l)) {
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

	int HoverIndex()
	{
		std::scoped_lock l(g_lock);
		return g_rects.empty() ? -1 : g_hover;
	}

	std::vector<Click> TakeClicks()
	{
		std::scoped_lock l(g_lock);
		return std::exchange(g_clicks, {});
	}
}
