#pragma once

// Gear Loadouts - mouse input for our widgets through a subclass of the game window (UI-UX.md step 5).
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md): core/Runtime.cpp
// (finding "UnrealWindow" and subclassing it), sorting/PickerInput.cpp (left and right button down in a rect,
// swallowing the down and the matching up and a double click). Modified 2026-11-17: only rects and a click queue.
//
// The window procedure NEVER calls Unreal: it hit-tests rectangles (client pixels) handed over by the game thread and
// queues clicks; the game thread takes them with TakeClicks(). Always passes everything else to the previous procedure.

#include <vector>

namespace windowinput
{
	struct Rect
	{
		int left, top, right, bottom;   // client pixels
	};

	struct Click
	{
		int  index;   // which rect
		bool right;   // right button
	};

	void Install();   // game thread; finds and subclasses the game window (tries again at most once a second)

	void SetRects(std::vector<Rect> a_rects);   // empty = inactive: every message passes through
	std::vector<Click> TakeClicks();
	int                HoverIndex();   // the rect under the mouse (last WM_MOUSEMOVE), -1 = none
}
