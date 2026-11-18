// SPDX-License-Identifier: GPL-3.0-or-later
// Class path and the visibility-based open check from Menus Revamped by cfa911 (see NOTICE.md).

#include "ui/InventoryMenu.h"

#include "Reflect.h"
#include "ui/LoadoutBar.h"
#include "ui/UiCall.h"
#include "ui/UiWidgets.h"

namespace inventorymenu
{
	namespace
	{
		constexpr const wchar_t* kClassPath = L"/Game/UI/Original/GameMenuLayer/Inventory/WBP_OriginalMenu_Inventory.WBP_OriginalMenu_Inventory_C";
		constexpr ULONGLONG      kPollMs = 50;

		reflect::Handle g_open;
		ULONGLONG       g_nextPoll = 0;

		bool InMenuMode()
		{
			auto* im = RE::InterfaceManager::GetInstance(false, false);
			return im && im->menuMode != 1;   // 1 = gameplay
		}

		// Prepares every live inventory widget (the game often creates a new one per open, and the row must exist
		// before it fades in) and returns the one on screen, if any.
		UE::UObject* ScanAndPrepare()
		{
			auto* cls = ui::Class(kClassPath);   // only loaded after the first open
			if (!cls) {
				return nullptr;
			}
			UE::UObject* shown = nullptr;
			for (auto* inst : reflect::Instances(cls)) {
				if (reflect::Usable(inst)) {
					loadoutbar::Prepare(inst);
					if (!shown && ui::ShownOnScreen(inst)) {
						shown = inst;
					}
				}
			}
			return shown;
		}
	}

	void Tick()
	{
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextPoll) {
			return;
		}
		g_nextPoll = now + kPollMs;
		if (!reflect::SelfCheck()) {
			return;
		}
		UE::UObject* was = reflect::Get(g_open);
		UE::UObject* shown = nullptr;
		if (InMenuMode()) {
			shown = was && reflect::Usable(was) && ui::ShownOnScreen(was) ? was : ScanAndPrepare();
		}
		if (shown != was) {
			g_open = reflect::Hold(shown);
			loadoutbar::OnInventory(shown);
		}
	}
}
