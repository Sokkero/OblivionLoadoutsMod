#pragma once

// The loadout logic. Nothing is ever moved: a loadout is a saved list of the forms that were worn when the player
// switched away from it. Switching A -> B:
//   1. save what is worn now as A (unless no loadout is active: then it is saved nowhere)
//   2. take off every piece B does not hold (shared pieces stay on)
//   3. equip each piece of B that is still in the inventory
// Everything touching the inventory runs on the TES thread.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).

#include <string>

namespace loadouts
{
	inline constexpr int kCount = 5;           // 0 = Default, 1..4 = Loadout 1..4
	inline constexpr int kNone = -1;           // no loadout active: the save has no token; the next switch saves nothing
	inline constexpr int kMaxNameLength = 15;  // Unicode code points

	// HUD message when the character has no GUID yet
	inline constexpr const char* kNotIdentifiedMessage = "Loadouts are not available yet. Save the game once, then try again.";

	const char* Name(int a_loadout);   // default name, for logs

	// Trims whitespace; rejects control characters, empty names and names over kMaxNameLength (a_truncate cuts instead).
	bool CleanName(const std::string& a_utf8, std::string& a_out, bool a_truncate = false);

	std::string   DisplayName(int a_loadout);   // any thread
	std::uint32_t NamesVersion();               // changes whenever a name changes
	bool          SetName(const std::string& a_key, int a_loadout, const std::string& a_name);   // any thread

	void Init();                          // data loaded: map plugin names to load indices
	int  ActiveForUi();                   // any thread; kNone until loaded, or when the save has no active loadout
	void Sync(const std::string& a_key);  // TES thread: load this character's data if not loaded yet

	// TES thread. False (with the reason) when the switch cannot happen now; otherwise it is queued.
	bool Request(int a_to, std::string& a_why);
}
