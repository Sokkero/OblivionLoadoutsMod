#pragma once

// The game's own rebuild of the inventory menu item list, so the equipped marks follow a switch. Does nothing when
// the menu is closed. Found by byte pattern.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).

namespace push
{
	bool Install();
	bool Rebuild();   // TES thread only; false when not installed
}
