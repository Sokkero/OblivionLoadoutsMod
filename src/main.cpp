// Gear Loadouts (Oblivion Remastered) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The entry point follows the one in Simple Loadout System (Oblivion Remastered) by ApocryphaRealm (GPL-3.0-or-later,
// see NOTICE.md). New here: no UI. A switch is started with a hotkey polled on the TES thread:
//   Ctrl + Numpad 0  -> Default      Ctrl + Numpad 1..4 -> Loadout 1..4
// Feedback: the log, and a system sound (normal beep = done, error beep = refused).

#include "Loadouts.h"
#include "TesThread.h"

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
		const bool  ctrl = GameWindowHasFocus() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
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
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		logger::info("OBSE message {} from '{}'", a_msg->type, a_msg->sender ? a_msg->sender : "?");
		if (a_msg->type == OBSE::MessagingInterface::kDataLoaded) {
			loadouts::Init();
			testhread::SetTick(&OnTick);
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
