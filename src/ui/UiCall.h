#pragma once

// Gear Loadouts - calling Unreal (UMG) functions through reflection.
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md), engine/Ui.h and
// engine/UiCall.cpp. Taken: Call (parameters laid out by NAME from the UFunction's own properties), the SEH-guarded
// ProcessEvent, CallFirst, the player-controller search (a SUBCLASS of PlayerController), GetText.
// Removed: the ProcessEvent watcher bookkeeping (pe::OwnCall; we have no ProcessEvent hook). Modified 2026-11-17.
//
// CommonLibOB64 declares no UMG types, so every UI call goes through here. Game thread only (GameThread.h).

#include <string>
#include <string_view>
#include <vector>

namespace ui
{
	class Call
	{
	public:
		Call(UE::UObject* a_obj, const wchar_t* a_fn);
		explicit operator bool() const { return m_fn != nullptr; }

		void*        At(std::string_view a_name);   // the parameter's place in the frame, nullptr when absent
		std::int32_t Size(std::string_view a_name) const;

		template <class T>
		bool Set(std::string_view a_name, const T& a_value)
		{
			if (void* p = At(a_name)) {
				std::memcpy(p, &a_value, sizeof(T));
				return true;
			}
			return false;
		}

		template <class T>
		T Get(std::string_view a_name)
		{
			T v{};
			if (void* p = At(a_name)) {
				std::memcpy(&v, p, sizeof(T));
			}
			return v;
		}

		bool Run();
		bool RunGuarded();   // for a call that takes a world-context object: false when the engine faulted

	private:
		UE::UObject*              m_obj;
		UE::UFunction*            m_fn;
		std::vector<std::uint8_t> m_params;
	};

	// ProcessEvent under a structured-exception guard (MR: world-context calls crashed on quit / load)
	bool GuardedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params);
	// a call whose only (first) parameter is given as raw bytes
	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size);

	UE::UClass*  Class(const wchar_t* a_path);   // a loaded class by path, nullptr when not loaded
	UE::UObject* PlayerController();             // looked for at most every 2 s; a slot-checked handle in between
	std::string  GetText(UE::UObject* a_widget);   // a text widget's GetText(), UTF-8; empty when it has none
	std::string  Name(UE::UObject* a_object);      // the object's FName, "-" for nullptr
}
