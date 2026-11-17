#pragma once

// Gear Loadouts - just enough Unreal reflection to read a property by NAME.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md),
// src/Reflect.h/.cpp. CommonLibOB64 has no FProperty: walk a struct's FField chain (next +0x18, name +0x20) and read
// FProperty::Offset_Internal at +0x44, ElementSize at +0x34 (UE5's layout). SelfCheck() proves the layout on a known
// property before anything trusts it. Modified 2026-11-17: moved out of Console.cpp; Size(), Usable() and Handle
// added (the last two from Menus Revamped by cfa911, GPL-3.0-or-later, engine/Reflect.cpp).
//
// Game thread only (GameThread.h).

#include <string>
#include <string_view>
#include <vector>

namespace reflect
{
	std::string Utf8(const UE::FString& a_s);

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name);   // -1 when there is no such property
	std::int32_t Size(UE::UStruct* a_struct, std::string_view a_name);     // -1 when there is no such property

	bool SelfCheck();   // true once the layout is proven; false while not checkable yet or when it failed
	bool Failed();      // the layout check ran and failed (nothing reflected may run)

	bool Ok();          // SelfCheck() passed (cheap; does not run the check)

	template <class T>
	T* At(void* a_base, std::int32_t a_offset)
	{
		return a_base && a_offset >= 0 ? reinterpret_cast<T*>(static_cast<std::uint8_t*>(a_base) + a_offset) : nullptr;
	}

	// For an object read THIS frame from a live owner only (it reads the object's own index). Kept pointers: Handle.
	bool IsLive(UE::UObject* a_obj);
	// IsLive and not being destroyed (not Garbage / Unreachable / PendingKill, not BeginDestroyed / FinishDestroyed).
	// A closing menu's widgets stay in their slots until GC purges them (Menus Revamped RMI-043).
	bool Usable(UE::UObject* a_obj);

	std::vector<UE::UObject*> Instances(UE::UClass* a_class);   // every live instance, never the class default object

	// A pointer kept across frames with the object-array slot it was found in (Menus Revamped). Get() asks the SLOT
	// whether it still holds that object and never reads the object itself.
	struct Handle
	{
		UE::UObject* ptr = nullptr;
		std::int32_t index = -1;
	};
	Handle       Hold(UE::UObject* a_live);   // a_live must be live now
	UE::UObject* Get(const Handle& a_handle);   // nullptr once the slot holds anything else
}
