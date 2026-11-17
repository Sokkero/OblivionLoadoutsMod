#pragma once

// ============================================================================================================
// Gear Loadouts: the loadout logic. Nothing is ever moved. A loadout is a saved LIST of the forms that were worn
// when the player switched away from it. Switching A -> B:
//   1. read what is worn now and save it as A
//   2. take every piece of gear off
//   3. equip each saved piece of B that is still in the inventory (missing pieces are skipped)
// Gear = armour (shields too), clothing (rings and amulets are clothing) and ammunition. Weapons are off by default.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later. See NOTICE.md.
// Everything that touches the inventory runs on the TES thread (testhread::Post).
// ============================================================================================================

namespace loadouts
{
	inline constexpr int kCount = 5;   // 0 = Default, 1..4 = Loadout 1..4

	const char* Name(int a_loadout);

	void Init();   // data loaded: remember which plugin has which load index

	// Ask for a switch (any thread). The work is posted to the TES thread. False - with the reason - when it cannot
	// happen now (in combat, a switch is running, the hook is not installed).
	bool Request(int a_to, std::string& a_why);
}
