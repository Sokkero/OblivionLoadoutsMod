#pragma once

// The loaded character's GUID, used as the key of the saved data. Read from
// /Script/Altar.VAltarSaveIdentificationData.CharacterId (one instance per session, changes on load). It is the same
// GUID as in the save file names: "<slot> - <name> (<GUID>).sav". Game thread only.

#include <string>

namespace characterkey
{
	// "5BCF0AF8-484E-82CF-54F4-00A9A16A01D1", or empty when not available (reason logged once)
	std::string Read();
}
