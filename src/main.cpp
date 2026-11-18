// Gear Loadouts (Oblivion Remastered) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The entry point follows the one in Simple Loadout System (Oblivion Remastered) by ApocryphaRealm (GPL-3.0-or-later,
// see NOTICE.md). New here: no UI. A switch is started with a hotkey polled on the TES thread:
//   Ctrl + Numpad 0  -> Default      Ctrl + Numpad 1..4 -> Loadout 1..4
// Feedback: the log, and a system sound (normal beep = done, error beep = refused).

#include "GameThread.h"
#include "Loadouts.h"
#include "Push.h"
#include "TesThread.h"
#include "devtools/Census.h"
#include "ui/InventoryMenu.h"
#include "ui/LoadoutBar.h"
#include "ui/WindowInput.h"

namespace
{
	bool GameWindowHasFocus()
	{
		HWND fg = GetForegroundWindow();
		if (!fg) {
			return false;
		}
		DWORD pid = 0;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId();
	}

	// Runs on the TES thread, once per main-loop iteration (TesThread.cpp).
	void OnTick()
	{
		static bool wasDown[loadouts::kCount]{};
		// no hotkey while the rename popup takes the keyboard (Ctrl+Numpad would switch under it)
		const bool  ctrl = GameWindowHasFocus() && !windowinput::PopupOpen() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		for (int i = 0; i < loadouts::kCount; ++i) {
			const bool down = ctrl && (GetAsyncKeyState(VK_NUMPAD0 + i) & 0x8000) != 0;
			if (down && !wasDown[i]) {
				std::string why;
				if (!loadouts::Request(i, why)) {
					logger::info("switch to {} refused: {}", loadouts::Name(i), why);
					MessageBeep(MB_ICONHAND);
				}
			}
			wasDown[i] = down;
		}
		// DEV TOOL: Ctrl+Shift+F11 = widget census (UI-UX.md step 2). Remove before a public release.
		static bool censusWasDown = false;
		const bool  censusDown = ctrl && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 && (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
		if (censusDown && !censusWasDown) {
			census::Request();
		}
		censusWasDown = censusDown;
	}

	// Runs every frame on the Unreal game thread (GameThread.cpp).
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
		logger::info("OBSE message {} from '{}'", a_msg->type, a_msg->sender ? a_msg->sender : "?");
		// the inventory UI runs from the game thread's frame callback
		inventorymenu::SetListener(&loadoutbar::OnInventory);
		inventorymenu::SetPreparer(&loadoutbar::Prepare);
		gamethread::SetFrameCallback(&OnFrame);
		// the Unreal game thread (HUD messages through the console): try at each message until the engine object exists
		if (!gamethread::Installed() && !gamethread::Install() && a_msg->type == OBSE::MessagingInterface::kDataLoaded) {
			logger::error("the game thread hook could not be installed - no HUD messages");
		}
		if (a_msg->type == OBSE::MessagingInterface::kDataLoaded) {
			loadouts::Init();
			testhread::SetTick(&OnTick);
			push::Install();   // the game's inventory list rebuild, run after a switch (Push.h)
			if (!testhread::Install()) {
				logger::error("the TES thread hook could not be installed - no hotkey, no switch");
			}
		}
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	logger::set_level(logger::level::info, logger::level::info);
	logger::info("Gear Loadouts {} loaded (Oblivion Remastered)", GL_VERSION);
	if (auto* messaging = OBSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(&OnMessage)) {
		logger::error("OBSE messaging unavailable - nothing will be installed");
	}
	return true;
}
