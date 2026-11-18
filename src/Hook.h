#pragma once

// MinHook detour with error logging. Shared by the TES thread and game thread hooks.

#include <MinHook.h>

namespace hook
{
	inline std::uintptr_t Rva(const void* a_address)
	{
		return reinterpret_cast<std::uintptr_t>(a_address) - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
	}

	template <class T>
	bool Detour(void* a_target, T a_detour, T& a_original, const char* a_what)
	{
		MH_STATUS mh = MH_Initialize();
		if (mh == MH_OK || mh == MH_ERROR_ALREADY_INITIALIZED) {
			void* original = nullptr;
			mh = MH_CreateHook(a_target, reinterpret_cast<void*>(a_detour), &original);
			if (mh == MH_OK) {
				a_original = reinterpret_cast<T>(original);
				mh = MH_EnableHook(a_target);
				if (mh == MH_OK) {
					logger::info("{} hooked (game+0x{:X})", a_what, Rva(a_target));
					return true;
				}
				MH_RemoveHook(a_target);
				a_original = nullptr;
			}
		}
		logger::error("{} could not be hooked ({})", a_what, MH_StatusToString(mh));
		return false;
	}
}
