#pragma once

// Top-left HUD message through the console command  Message "text". Game thread only.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).

#include <string>

namespace console
{
	bool Notify(const std::string& a_text);   // ASCII only; false when the console is not reachable yet
}
