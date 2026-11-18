#pragma once

// Detects the inventory menu by polling (no ProcessEvent hook, so no clash with Menus Revamped, which swaps the vtable
// of the same class). Hands every inventory widget to loadoutbar::Prepare and reports open / close to
// loadoutbar::OnInventory. Game thread only.

namespace inventorymenu
{
	void Tick();   // every frame; does work every 50 ms and only scans while a menu is up
}
