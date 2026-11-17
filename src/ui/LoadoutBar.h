#pragma once

// Gear Loadouts - the loadout button row in the inventory menu (UI-UX.md steps 4-6).
//
// Place (census 2026-11-17): WBP_OriginalMenu_InventoryMainPart_C "inv_main_part" has a CanvasPanel root holding
// inv_background, inv_tabs (the category tab strip) and inv_verticalbox. inv_verticalbox starts with a Spacer, then
// inv_mainContent (the table). The row is a NEW child of that canvas, placed where the Spacer ends; the Spacer is made
// taller by the row height, so the table moves down. No native widget is removed or re-added (re-adding the list broke
// its navigation in SLS), and inv_mainContent (rebuilt by the game at times) is not touched.
//
// Step 5: one test button; a left or right click on it is only logged. Game thread only.

namespace loadoutbar
{
	void Prepare(UE::UObject* a_inventory);       // inventorymenu preparer: build the row (once per widget)
	void OnInventory(UE::UObject* a_inventory);   // inventorymenu listener: nullptr = closed
	void Tick();                                  // every frame: clicks, and the button rects while open
}
