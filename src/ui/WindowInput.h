#pragma once

// Gear Loadouts - mouse and keyboard input for our widgets through a subclass of the game window (UI-UX.md 3.4).
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md): core/Runtime.cpp
// (finding "UnrealWindow" and subclassing it), sorting/PickerInput.cpp (button down in a rect, swallowing the down and
// the matching up and a double click; Esc closes a popup and its key-up is swallowed), search/SearchInput.cpp (text
// typed through WM_CHAR while every WM_KEYDOWN is swallowed). Modified 2026-11-17: rects, a click queue and one text
// field for the rename popup; no IME composition handling (committed IME text still arrives as WM_CHAR).
//
// The window procedure NEVER calls Unreal: it works on plain state under a mutex; the game thread hands over the
// rectangles (client pixels) and takes the clicks and the text.

#include <string>
#include <vector>

namespace windowinput
{
	struct Rect
	{
		int left, top, right, bottom;   // client pixels
	};

	enum class Action
	{
		Left,    // left click on button `index`
		Right,   // right click on button `index`
		Save,    // popup: Save button or Enter
		Cancel   // popup: Cancel button, Esc, or a click outside the popup
	};

	struct Click
	{
		Action action;
		int    index;   // the button for Left / Right
	};

	void Install();   // game thread; finds and subclasses the game window (tries again at most once a second)

	void SetRects(std::vector<Rect> a_rects);   // the buttons; empty = inactive: every message passes through
	std::vector<Click> TakeClicks();
	int                HoverIndex();   // the button under the mouse (last WM_MOUSEMOVE), -1 = none

	// Popup mode: every key goes to the text field and none to the game. a_maxLength in characters.
	void OpenPopup(const std::string& a_utf8, int a_maxLength);
	void ClosePopup();
	bool PopupOpen();
	// the popup's own rects: the whole popup, Save, Cancel (a click outside the popup = Cancel)
	void SetPopupRects(const Rect& a_popup, const Rect& a_save, const Rect& a_cancel);

	struct Field
	{
		std::string   text;        // UTF-8
		bool          selected;    // all text selected: the next character replaces it
		std::uint32_t version;     // changes with every edit
	};
	Field GetField();
}
