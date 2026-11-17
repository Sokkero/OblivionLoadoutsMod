// Gear Loadouts - a task queue on the Unreal game thread.
// The task queue is adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later
// (see NOTICE.md). Modified 2026-11-17: the original's XInputGetState import chain is replaced by a MinHook on
// UEngine::Tick (vtable slot 0x5F in CommonLibOB64's UEngine.h). Reason: in the test of 2026-11-17 21:39 another
// module replaced the XInput import entry after 4 calls and did not chain, so queued tasks never ran.

#include "GameThread.h"

#include <MinHook.h>

namespace gamethread
{
	namespace
	{
		constexpr std::size_t kTickSlot = 0x5F;   // UEngine::Tick(float, bool)

		using Tick_t = void (*)(UE::UEngine*, float, bool);

		Tick_t            g_original = nullptr;
		void*             g_target = nullptr;
		std::atomic<bool> g_firstRun{ true };
		std::atomic<bool> g_pending{ false };   // cheap check: Tick runs every frame
		std::atomic<FrameCallback> g_frame{ nullptr };

		std::mutex       g_taskLock;
		std::deque<Task> g_tasks;

		void RunTasks()
		{
			for (;;) {
				Task t;
				{
					std::scoped_lock l(g_taskLock);
					if (g_tasks.empty()) {
						g_pending.store(false);
						return;
					}
					t = std::move(g_tasks.front());
					g_tasks.pop_front();
				}
				t();
			}
		}

		void Detour(UE::UEngine* a_engine, float a_delta, bool a_idle)
		{
			if (g_firstRun.exchange(false)) {
				logger::info("game thread: UEngine::Tick runs on thread {} - Unreal calls run here", GetCurrentThreadId());
			}
			if (auto cb = g_frame.load(std::memory_order_acquire)) {
				cb();
			}
			if (g_pending.load(std::memory_order_acquire)) {
				RunTasks();
			}
			g_original(a_engine, a_delta, a_idle);
		}
	}

	bool Install()
	{
		if (g_target) {
			return true;
		}
		auto* engine = UE::UEngine::GetSingleton();
		if (!engine) {
			logger::info("game thread: the engine object does not exist yet - trying again later");
			return false;
		}
		void** vtable = *reinterpret_cast<void***>(engine);
		void*  target = vtable ? vtable[kTickSlot] : nullptr;
		if (!target) {
			logger::error("game thread: the engine has no Tick entry");
			return false;
		}
		if (const MH_STATUS mh = MH_Initialize(); mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
			logger::error("game thread: MinHook could not start ({})", MH_StatusToString(mh));
			return false;
		}
		void* original = nullptr;
		if (const MH_STATUS mh = MH_CreateHook(target, reinterpret_cast<void*>(&Detour), &original); mh != MH_OK) {
			logger::error("game thread: hook on UEngine::Tick could not be made ({})", MH_StatusToString(mh));
			return false;
		}
		if (const MH_STATUS mh = MH_EnableHook(target); mh != MH_OK) {
			logger::error("game thread: hook on UEngine::Tick could not be enabled ({})", MH_StatusToString(mh));
			MH_RemoveHook(target);
			return false;
		}
		g_original = reinterpret_cast<Tick_t>(original);
		g_target = target;
		logger::info("game thread: UEngine::Tick hooked (game+0x{:X})",
			reinterpret_cast<std::uintptr_t>(target) - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
		return true;
	}

	bool Installed()
	{
		return g_target != nullptr;
	}

	void Post(Task a_task)
	{
		std::size_t queued = 0;
		{
			std::scoped_lock l(g_taskLock);
			g_tasks.push_back(std::move(a_task));
			queued = g_tasks.size();
		}
		g_pending.store(true, std::memory_order_release);
		logger::info("game thread: task posted ({} waiting)", queued);
	}

	void SetFrameCallback(FrameCallback a_callback)
	{
		g_frame.store(a_callback, std::memory_order_release);
	}
}
