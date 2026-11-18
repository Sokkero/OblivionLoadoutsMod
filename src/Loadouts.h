#pragma once

// ============================================================================================================
// Gear Loadouts: the loadout logic. Nothing is ever moved. A loadout is a saved LIST of the forms that were worn
// when the player switched away from it. Switching A -> B:
//   1. read what is worn now and save it as A
//   2. take every piece of gear off
//   3. equip each saved piece of B that is still in the inventory (missing pieces are skipped)
// Gear = every equipped item: armour (shields too), clothing (rings, amulets), ammunition, weapons, torches.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later. See NOTICE.md.
// Everything that touches the inventory runs on the TES thread (testhread::Post).
// ============================================================================================================

namespace loadouts
{
	inline constexpr int kCount = 5;   // 0 = Default, 1..4 = Loadout 1..4

	inline constexpr int kMaxNameLength = 15;   // characters (Unicode code points); was 20 until 2026-11-18 (user)

	const char* Name(int a_loadout);   // the default name ("Default", "Loadout 1".."Loadout 4"); used in logs

	// Names (UI-UX.md step 7). Validation: no control characters, whitespace trimmed, not empty, at most
	// kMaxNameLength characters. a_out gets the trimmed name.
	// a_truncate: a longer name is cut to kMaxNameLength instead of rejected (names saved under the old limit of 20)
	bool          CleanName(const std::string& a_utf8, std::string& a_out, bool a_truncate = false);
	std::string   DisplayName(int a_loadout);   // any thread: the loaded character's name for the loadout
	std::uint32_t NamesVersion();               // changes whenever a name changes (the UI repaints its labels)
	// any thread: validates, shows at once, saves on the TES thread. False when the name is not valid.
	bool SetName(const std::string& a_key, int a_loadout, const std::string& a_name);

	void Init();   // data loaded: remember which plugin has which load index

	// The active loadout for the UI (any thread): -1 while the character's data is not loaded yet.
	int ActiveForUi();
	// TES thread: load the data of this character (key from characterkey::Read) if it is not loaded, so that
	// ActiveForUi() is right before the first switch of a session.
	void Sync(const std::string& a_key);

	// Ask for a switch (TES thread: it reads the player's combat state). The work is posted to the TES thread. False - with the reason - when it cannot
	// happen now (in combat, a switch is running, the hook is not installed).
	bool Request(int a_to, std::string& a_why);
}
