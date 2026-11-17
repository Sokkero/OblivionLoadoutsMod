// Gear Loadouts - inventory menu detection (see InventoryMenu.h).
// SPDX-License-Identifier: GPL-3.0-or-later
// Ideas (class path, open check by the widget's own visibility, menu mode) from Menus Revamped by cfa911,
// engine/Menus.cpp.

#include "ui/InventoryMenu.h"

#include "Reflect.h"
#include "ui/UiCall.h"
#include "ui/UiWidgets.h"

namespace inventorymenu
{
	namespace
	{
		constexpr const wchar_t* kClassPath = L"/Game/UI/Original/GameMenuLayer/Inventory/WBP_OriginalMenu_Inventory.WBP_OriginalMenu_Inventory_C";
		constexpr ULONGLONG      kPollMs = 50;

		Listener        g_listener = nullptr;
		Preparer        g_preparer = nullptr;
		reflect::Handle g_open;   // the open inventory widget, empty when closed
		ULONGLONG       g_nextPoll = 0;

		// The game's menu mode (InterfaceManager+0x10): 1 = gameplay, anything else = a menu is up (Menus Revamped,
		// engine/Menus.cpp). Cheap; decides whether the object array is searched at all.
		bool InMenuMode()
		{
			auto* im = RE::InterfaceManager::GetInstance(false, false);
			return im && im->menuMode != 1;
		}

		// Every live inventory widget goes to the preparer (it builds our row before the menu fades in: the game makes
		// a NEW widget on most opens, log 2026-11-17 23:07); returns the one on screen, if any.
		UE::UObject* ScanAndPrepare()
		{
			auto* cls = ui::Class(kClassPath);
			if (!cls) {
				return nullptr;   // never opened yet this session
			}
			UE::UObject* shown = nullptr;
			for (auto* inst : reflect::Instances(cls)) {
				if (!reflect::Usable(inst)) {
					continue;
				}
				if (g_preparer) {
					g_preparer(inst);
				}
				if (!shown && ui::ShownOnScreen(inst)) {
					shown = inst;
				}
			}
			return shown;
		}
	}

	void SetListener(Listener a_listener)
	{
		g_listener = a_listener;
	}

	void SetPreparer(Preparer a_preparer)
	{
		g_preparer = a_preparer;
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
			// the open one stays open while it is shown (cheap); otherwise search (only while a menu is up)
			if (was && reflect::Usable(was) && ui::ShownOnScreen(was)) {
				shown = was;
			} else {
				shown = ScanAndPrepare();
			}
		}

		if (shown == was) {
			return;
		}
		if (was && !shown) {
			logger::info("ui: inventory closed");
		} else if (shown) {
			logger::info("ui: inventory opened ({})", ui::Name(shown));
		}
		g_open = shown ? reflect::Hold(shown) : reflect::Handle{};
		if (g_listener) {
			g_listener(shown);
		}
	}
}
