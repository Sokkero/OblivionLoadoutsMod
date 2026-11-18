// SPDX-License-Identifier: GPL-3.0-or-later
// Gear Loadouts: switch between saved sets of worn gear in Oblivion Remastered (OBSE64 plugin).

#include "GameThread.h"
#include "Loadouts.h"
#include "Push.h"
#include "TesThread.h"
#include "ui/InventoryMenu.h"
#include "ui/LoadoutBar.h"
#include "ui/WindowInput.h"

namespace
{
	bool GameWindowHasFocus()
	{
		DWORD pid = 0;
		HWND  fg = GetForegroundWindow();
		return fg && GetWindowThreadProcessId(fg, &pid) && pid == GetCurrentProcessId();
	}

	// TES thread, every main loop iteration: Ctrl + Numpad 0..4 switches to loadout 0..4
	void OnTick()
	{
		static bool wasDown[loadouts::kCount]{};
		const bool  ctrl = GameWindowHasFocus() && !windowinput::PopupOpen() && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
		for (int i = 0; i < loadouts::kCount; ++i) {
			const bool down = ctrl && (GetAsyncKeyState(VK_NUMPAD0 + i) & 0x8000);
			if (down && !wasDown[i]) {
				std::string why;
				if (!loadouts::Request(i, why)) {
					logger::info("switch to {} refused: {}", loadouts::Name(i), why);
					MessageBeep(MB_ICONHAND);
				}
			}
			wasDown[i] = down;
		}
	}

	// game thread, every frame
	void OnFrame()
	{
		inventorymenu::Tick();
		loadoutbar::Tick();
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		// the engine object appears at some point during start-up: try at every message
		gamethread::Install();
		if (a_msg->type == OBSE::MessagingInterface::kDataLoaded) {
			if (!gamethread::Installed()) {
				logger::error("game thread hook not installed - no UI, no HUD messages");
			}
			loadouts::Init();
			push::Install();
			testhread::SetTick(&OnTick);
			if (!testhread::Install()) {
				logger::error("TES thread hook not installed - switching disabled");
			}
		}
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse, { .log = false });
	logger::init();
	logger::info("Gear Loadouts {} loaded (game {}, OBSE64 {})", GL_VERSION, a_obse->RuntimeVersion().string(), OBSE::GetOBSEVersion().string());
	gamethread::SetFrameCallback(&OnFrame);
	if (auto* messaging = OBSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(&OnMessage)) {
		logger::error("OBSE messaging unavailable - nothing will be installed");
	}
	return true;
}
