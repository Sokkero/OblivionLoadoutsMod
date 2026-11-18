#pragma once

// Mouse and keyboard input for our widgets through a subclass of the game window. The window procedure never calls
// Unreal: the game thread hands over hit rects (client pixels) and takes clicks and typed text.
//
// While the rename popup is open, every key goes to the text field: window messages, raw keyboard input and the
// GetAsyncKeyState / GetKeyState imports of other modules (e.g. UE4SS key binds) are blocked.
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).

#include <string>
#include <vector>

namespace windowinput
{
	struct Rect
	{
		int left, top, right, bottom;
	};

	enum class Action
	{
		Left,    // left click on button `index`
		Right,   // right click on button `index`
		Save,    // popup: Save button or Enter
		Cancel   // popup: Cancel button, Esc or a click outside
	};

	struct Click
	{
		Action action;
		int    index;
	};

	void Install();   // game thread; retries at most once a second until the window exists

	void               SetRects(std::vector<Rect> a_rects);   // the buttons; empty = everything passes through
	std::vector<Click> TakeClicks();
	int                HoverIndex();   // -1 = none

	void OpenPopup(const std::string& a_utf8, int a_maxLength);
	void ClosePopup();
	bool PopupOpen();
	void SetPopupRects(const Rect& a_popup, const Rect& a_save, const Rect& a_cancel);

	struct Field
	{
		std::string   text;       // UTF-8
		bool          selected;   // all text selected: the next character replaces it
		std::uint32_t version;    // changes with every edit
	};
	Field GetField();
}
