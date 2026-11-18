#pragma once

// Task queue on the TES (Gamebryo) simulation thread. Equip / unequip MUST run here: the engine deliberately crashes
// when they are called from another thread. Reads are safe from anywhere.
//
// Hooks the one call the "Oblivion Main loop" makes at the top of every iteration (found by byte pattern).
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).

#include <functional>

namespace testhread
{
	using Task = std::function<void()>;

	bool Install();
	bool Installed();
	void Post(Task a_task);
	void SetTick(Task a_tick);   // runs every iteration; set before Install()
}
