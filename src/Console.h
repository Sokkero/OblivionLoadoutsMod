#pragma once

// Gear Loadouts - the engine's console from C++, and a HUD message through it.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md),
// src/Console.h/.cpp and the parts of src/Reflect.cpp it needs. Taken: UKismetSystemLibrary::ExecuteConsoleCommand
// through ProcessEvent with a hand-laid parameter frame, the player controller lookup, the property-offset reader and
// its self-check. New here: Notify(). Modified 2026-11-17.
//
// Game thread only (GameThread.h), never the TES thread.

#include <string>

namespace console
{
	bool        Run(const std::string& a_command);   // false when not ready yet (logged)
	bool        Notify(const std::string& a_text);   // the script command  Message "text"  (top-left HUD message)
	std::string Status();
}
