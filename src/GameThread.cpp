// SPDX-License-Identifier: GPL-3.0-or-later
// Task queue adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm (see NOTICE.md).
// Changed: driven by a hook on UEngine::Tick instead of the XInput import (other mods replace that import).

#include "GameThread.h"

#include "Hook.h"

namespace gamethread
{
	namespace
	{
		constexpr std::size_t kTickSlot = 0x5F;   // UEngine::Tick(float, bool)

		using Tick_t = void (*)(UE::UEngine*, float, bool);

		Tick_t                     g_original = nullptr;
		std::atomic<bool>          g_pending{ false };   // skips the lock on frames without tasks
		std::atomic<FrameCallback> g_frame{ nullptr };
		std::mutex                 g_lock;
		std::deque<Task>           g_tasks;

		void Detour(UE::UEngine* a_engine, float a_delta, bool a_idle)
		{
			if (auto cb = g_frame.load(std::memory_order_acquire)) {
				cb();
			}
			if (g_pending.exchange(false, std::memory_order_acq_rel)) {
				std::deque<Task> run;
				{
					std::scoped_lock l(g_lock);
					run.swap(g_tasks);
				}
				for (auto& t : run) {
					t();
				}
			}
			g_original(a_engine, a_delta, a_idle);
		}
	}

	bool Install()
	{
		if (g_original) {
			return true;
		}
		auto* engine = UE::UEngine::GetSingleton();
		if (!engine) {
			return false;
		}
		void** vtable = *reinterpret_cast<void***>(engine);
		return vtable && vtable[kTickSlot] && hook::Detour(vtable[kTickSlot], &Detour, g_original, "game thread: UEngine::Tick");
	}

	bool Installed() { return g_original != nullptr; }

	void Post(Task a_task)
	{
		{
			std::scoped_lock l(g_lock);
			g_tasks.push_back(std::move(a_task));
		}
		g_pending.store(true, std::memory_order_release);
	}

	void SetFrameCallback(FrameCallback a_callback)
	{
		g_frame.store(a_callback, std::memory_order_release);
	}
}
