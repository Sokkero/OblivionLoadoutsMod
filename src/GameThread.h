#pragma once

// Task queue on the Unreal game thread (hook on UEngine::Tick). All Unreal calls (ProcessEvent, UMG, console) run
// here, never on the TES thread.
//
// Task queue adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).

#include <functional>

namespace gamethread
{
	using Task = std::function<void()>;
	using FrameCallback = void (*)();

	bool Install();   // false while the engine object does not exist yet: call again later
	bool Installed();
	void Post(Task a_task);                            // runs next frame
	void SetFrameCallback(FrameCallback a_callback);   // runs every frame, before the tasks
}
