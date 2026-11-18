#pragma once

// The loadout button row and rename popup in the inventory menu. Game thread only.
//
// inv_main_part has a CanvasPanel root holding the tab strip and inv_verticalbox (a Spacer, then the item table).
// The row is a new child of that canvas, placed where the Spacer ends; the Spacer grows by the row height so the table
// moves down. No native widget is removed or re-added (that breaks list navigation).

namespace loadoutbar
{
	void Prepare(UE::UObject* a_inventory);       // build the row into an inventory widget (once per widget)
	void OnInventory(UE::UObject* a_inventory);   // the shown inventory changed; nullptr = closed
	void Tick();                                  // every frame: input, painting, hit rects
}
