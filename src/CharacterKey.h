#pragma once

// Gear Loadouts - the key of the saved data: the loaded character's GUID.
//
// The game keeps it in /Script/Altar.VAltarSaveIdentificationData, property CharacterId (an FGuid). There is one
// instance for the whole session and it always holds the loaded character's GUID: found with a memory probe and
// tested in game 2026-11-17 (two characters, two GUIDs; it changes on load). The same GUID is in the save-file names:
// "<slot> - <name> (<GUID>).sav".
//
// Game thread only (GameThread.h).

#include <string>

namespace characterkey
{
	// "5BCF0AF8-484E-82CF-54F4-00A9A16A01D1" (the save-file format), or empty when the game has no character ID now
	// (class or instance not found, or an all-zero GUID). The reason is logged.
	std::string Read();
}
