// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm (see NOTICE.md).

#include "TesThread.h"

#include "Hook.h"
#include "Scan.h"

namespace testhread
{
	namespace
	{
		// Top of the main loop, right after its message pump:
		//   cmp rax, r14 / je +7 / mov rax, [rip+Y] / mov rcx, [rip+Z] / call PerIteration <- hooked / cmp byte [rip+W], 0 / je +10
		constexpr const char* kPattern = "49 3B C6 74 07 48 8B 05 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 74 0A";
		constexpr std::size_t kCallOffset = 19;   // the E8 within the pattern

		using PerIteration_t = std::int64_t (*)(void*);

		PerIteration_t   g_original = nullptr;
		std::mutex       g_lock;
		std::deque<Task> g_tasks;
		Task             g_tick;

		std::int64_t Detour(void* a_this)
		{
			const auto r = g_original(a_this);
			if (g_tick) {
				g_tick();
			}
			std::deque<Task> run;
			{
				std::scoped_lock l(g_lock);
				run.swap(g_tasks);
			}
			for (auto& t : run) {
				t();
			}
			return r;
		}
	}

	bool Install()
	{
		if (g_original) {
			return true;
		}
		std::uint8_t* site = scan::First(kPattern);
		if (!site) {
			logger::error("tes thread: main loop call site not found in this game build");
			return false;
		}
		std::uint8_t* call = site + kCallOffset;
		void*         target = call + 5 + *reinterpret_cast<std::int32_t*>(call + 1);
		return hook::Detour(target, &Detour, g_original, "tes thread: main loop");
	}

	bool Installed() { return g_original != nullptr; }

	void SetTick(Task a_tick) { g_tick = std::move(a_tick); }

	void Post(Task a_task)
	{
		std::scoped_lock l(g_lock);
		g_tasks.push_back(std::move(a_task));
	}
}
