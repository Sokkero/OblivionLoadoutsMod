#pragma once

// Gear Loadouts - DEV TOOL: dump of the on-screen widget tree to a text file (UI-UX.md step 2).
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md), devtools/Census.cpp.
// Taken: the bounded, SEH-guarded walk of WBP_PrimaryGameLayout (UserWidget WidgetTree, PanelWidget Slots, CommonUI
// WidgetList / DisplayedWidget) and the line format. Changed: the slot class and the index in the parent are printed
// (where our button row can be inserted); no list rows, no brush details; hotkey Ctrl+Shift+F11 (MR uses F12),
// polled on the TES thread. Modified 2026-11-17.
//
// Remove the hotkey before a public release.

namespace census
{
	void Request();   // any thread: the dump runs on the game thread
}
