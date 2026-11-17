#pragma once

// Gear Loadouts - is the inventory menu open, and which widget is it? (UI-UX.md step 3)
//
// Polling, no ProcessEvent hook (Menus Revamped swaps the vtable of the same class; polling cannot clash with it).
// The class /Game/UI/Original/GameMenuLayer/Inventory/WBP_OriginalMenu_Inventory.WBP_OriginalMenu_Inventory_C is only
// loaded on the first open, so the first open after a start is found too (SLS missed it).
// Game thread only: Tick() runs from the UEngine::Tick hook.

namespace inventorymenu
{
	using Listener = void (*)(UE::UObject* a_inventory);   // nullptr = closed
	using Preparer = void (*)(UE::UObject* a_inventory);   // every live inventory widget, also before it is shown

	void SetListener(Listener a_listener);
	void SetPreparer(Preparer a_preparer);
	void Tick();   // every frame; works every 50 ms, and searches only while a menu is up and the inventory is not shown
}
