#pragma once

// Minimal Unreal reflection: property offsets by name, object liveness, instance lookup.
// CommonLibOB64 has no FProperty, so the FField chain is walked by hand (UE5 layout, proven by SelfCheck()).
// Game thread only.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm and Menus Revamped (Oblivion
// Remastered) by cfa911, both GPL-3.0-or-later (see NOTICE.md).

#include <string>
#include <string_view>
#include <vector>

namespace reflect
{
	std::string Utf8(const UE::FString& a_s);

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name);   // -1 when absent
	std::int32_t Size(UE::UStruct* a_struct, std::string_view a_name);     // -1 when absent
	std::int32_t FirstOffset(UE::UStruct* a_struct);                      // offset of the first property, -1 when none

	bool SelfCheck();   // true once the layout is proven; nothing reflected may run before that

	template <class T>
	T* At(void* a_base, std::int32_t a_offset)
	{
		return a_base && a_offset >= 0 ? reinterpret_cast<T*>(static_cast<std::uint8_t*>(a_base) + a_offset) : nullptr;
	}

	bool IsLive(UE::UObject* a_obj);   // still in its object-array slot
	bool Usable(UE::UObject* a_obj);   // live and not being garbage collected or destroyed

	std::vector<UE::UObject*> Instances(UE::UClass* a_class);   // live instances, without the class default object

	// A pointer kept across frames together with its object-array slot. Get() checks the slot, never the object.
	struct Handle
	{
		UE::UObject* ptr = nullptr;
		std::int32_t index = -1;
	};
	Handle       Hold(UE::UObject* a_live);
	UE::UObject* Get(const Handle& a_handle);   // nullptr once the slot holds anything else
}
