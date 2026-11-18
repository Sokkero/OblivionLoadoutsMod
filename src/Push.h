#pragma once

// Gear Loadouts - the game's own rebuild of the inventory menu's item list (equipped marks after a switch).
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md),
// src/Push.h/.cpp, unchanged except for the log lines. From the original's notes: the UE inventory menu shows what the
// Gamebryo side last pushed into UVInventoryMenuViewModel::SetInventory. ONE function on the TES thread queues that
// push: it looks the legacy InventoryMenu up itself (GetMenuByClass(0x3EA); nothing to do when the menu is closed),
// rebuilds the item array from the player's inventory and queues the push - the same function the game runs when the
// menu opens or after its own equip. No arguments. Found by byte pattern (no address-library ID).
// NOT VERIFIED in game by us; the original's changelog marks its version untested.

namespace push
{
	bool Install();     // the rebuild function found; false (logged) when the pattern is missing
	bool Rebuild();     // TES thread only: the game's rebuild-and-push; false when it is not found
	bool Installed();
}
