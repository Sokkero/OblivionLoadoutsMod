#pragma once

// Gear Loadouts - a task queue on the Unreal game thread.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md),
// src/GameThread.h/.cpp. Taken: the task queue. Changed 2026-11-17: tasks run from a MinHook on UEngine::Tick
// (once per frame), not from the original's XInput import chain (another module replaced that entry in game).
//
// This is NOT the TES thread (TesThread.h). Unreal calls (ProcessEvent, console commands) run here.

#include <functional>

namespace gamethread
{
	bool Install();   // false while the engine object does not exist yet: call again later
	bool Installed();

	using Task = std::function<void()>;
	void Post(Task a_task);   // runs at the next frame, on the game thread

	using FrameCallback = void (*)();
	void SetFrameCallback(FrameCallback a_callback);   // runs every frame on the game thread, before the tasks
}
