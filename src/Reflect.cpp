// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Simple Loadout System by ApocryphaRealm; Usable() and Handle from Menus Revamped by cfa911 (see NOTICE.md).

#include "Reflect.h"

#include "Text.h"

namespace reflect
{
	namespace
	{
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kPropertySize = 0x34;
		constexpr std::ptrdiff_t kPropertyOffset = 0x44;

		std::atomic<int> g_layout{ 0 };   // 0 unchecked, 1 proven, -1 failed

		std::uint8_t* Field(UE::UStruct* a_struct, std::string_view a_name)
		{
			for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
				for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
					if (Utf8(reinterpret_cast<const UE::FName*>(f + kFieldName)->ToString()) == a_name) {
						return f;
					}
				}
			}
			return nullptr;
		}

		std::int32_t Read(const std::uint8_t* a_field, std::ptrdiff_t a_at)
		{
			return a_field ? *reinterpret_cast<const std::int32_t*>(a_field + a_at) : -1;
		}

		// a collected object's memory may already be freed: read its index under SEH
		bool ReadIndex(const UE::UObject* a_o, std::int32_t& a_out)
		{
			__try {
				a_out = a_o->internalIndex;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		UE::FUObjectItem* SlotHolding(std::int32_t a_index, UE::UObject* a_obj)
		{
			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!a_obj || !arr || a_index < 0 || a_index >= arr->GetObjectArrayNum()) {
				return nullptr;
			}
			auto* item = arr->IndexToObject(a_index);
			return item && reinterpret_cast<UE::UObject*>(item->object) == a_obj ? item : nullptr;
		}
	}

	std::string Utf8(const UE::FString& a_s)
	{
		const wchar_t* d = UE::GetData(a_s);
		const int      n = UE::GetNum(a_s);
		return d && n > 0 ? text::Utf8(d, d[n - 1] == L'\0' ? n - 1 : n) : std::string();
	}

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name) { return Read(Field(a_struct, a_name), kPropertyOffset); }

	std::int32_t Size(UE::UStruct* a_struct, std::string_view a_name) { return Read(Field(a_struct, a_name), kPropertySize); }

	std::int32_t FirstOffset(UE::UStruct* a_struct)
	{
		return a_struct ? Read(reinterpret_cast<const std::uint8_t*>(a_struct->childProperties), kPropertyOffset) : -1;
	}

	bool SelfCheck()
	{
		if (const int state = g_layout.load(); state != 0) {
			return state > 0;
		}
		// a known property at a known offset
		auto*      vm = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VQuickKeysMenuViewModel");
		const auto keyIndex = vm ? Offset(vm, "KeyIndex") : -1;
		if (keyIndex < 0) {
			return false;   // not loaded / linked yet: ask again later
		}
		g_layout = keyIndex == 0xD0 ? 1 : -1;
		if (g_layout > 0) {
			logger::info("reflect: property layout proven");
		} else {
			logger::error("reflect: KeyIndex at 0x{:X}, expected 0xD0 - unknown property layout, UI and character key disabled", keyIndex);
		}
		return g_layout > 0;
	}

	bool IsLive(UE::UObject* a_obj)
	{
		std::int32_t idx = -1;
		return a_obj && ReadIndex(a_obj, idx) && SlotHolding(idx, a_obj);
	}

	bool Usable(UE::UObject* a_obj)
	{
		std::int32_t idx = -1;
		auto*        item = a_obj && ReadIndex(a_obj, idx) ? SlotHolding(idx, a_obj) : nullptr;
		constexpr auto dying = UE::EInternalObjectFlags::Garbage | UE::EInternalObjectFlags::Unreachable | UE::EInternalObjectFlags::PendingKill;
		if (!item || item->HasAnyFlags(dying)) {
			return false;
		}
		constexpr auto destroyed = static_cast<std::int32_t>(UE::EObjectFlags::BeginDestroyed) | static_cast<std::int32_t>(UE::EObjectFlags::FinishDestroyed);
		return (static_cast<std::int32_t>(a_obj->objectFlags) & destroyed) == 0;
	}

	std::vector<UE::UObject*> Instances(UE::UClass* a_class)
	{
		std::vector<UE::UObject*> out;
		auto*                     arr = UE::FUObjectArray::GetSingleton();
		if (!arr || !a_class) {
			return out;
		}
		auto* cdo = a_class->GetDefaultObject(false);
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			if (o && o != cdo && o->GetClass() == a_class) {
				out.push_back(o);
			}
		}
		arr->UnlockInternalArray();
		return out;
	}

	Handle Hold(UE::UObject* a_live)
	{
		return a_live ? Handle{ a_live, a_live->internalIndex } : Handle{};
	}

	UE::UObject* Get(const Handle& a_handle)
	{
		return SlotHolding(a_handle.index, a_handle.ptr) ? a_handle.ptr : nullptr;
	}
}
