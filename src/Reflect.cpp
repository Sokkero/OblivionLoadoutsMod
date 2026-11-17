// Gear Loadouts - Unreal reflection helpers.
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).
// Modified 2026-11-17: moved out of Console.cpp; Size() added. Usable() and Handle from Menus Revamped by cfa911
// (GPL-3.0-or-later, engine/Reflect.cpp).

#include "Reflect.h"

namespace reflect
{
	namespace
	{
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kPropertySize = 0x34;
		constexpr std::ptrdiff_t kPropertyOffsetInternal = 0x44;

		std::atomic<int> g_layout{ 0 };   // 0 unchecked, 1 proven, -1 failed

		// returns the FField* of that name, or nullptr
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

		// a garbage-collected object's memory may already be returned to Windows: read its index under SEH
		bool ReadIndex(const UE::UObject* a_o, std::int32_t& a_out)
		{
			__try {
				a_out = a_o->internalIndex;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
	}

	std::string Utf8(const UE::FString& a_s)
	{
		const wchar_t* d = UE::GetData(a_s);
		const int      n = UE::GetNum(a_s);
		if (!d || n <= 0) {
			return {};
		}
		const int   len = d[n - 1] == L'\0' ? n - 1 : n;
		const int   bytes = WideCharToMultiByte(CP_UTF8, 0, d, len, nullptr, 0, nullptr, nullptr);
		std::string out(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
		if (bytes > 0) {
			WideCharToMultiByte(CP_UTF8, 0, d, len, out.data(), bytes, nullptr, nullptr);
		}
		return out;
	}

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name)
	{
		auto* f = Field(a_struct, a_name);
		return f ? *reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal) : -1;
	}

	std::int32_t Size(UE::UStruct* a_struct, std::string_view a_name)
	{
		auto* f = Field(a_struct, a_name);
		return f ? *reinterpret_cast<const std::int32_t*>(f + kPropertySize) : -1;
	}

	bool SelfCheck()
	{
		if (g_layout.load() != 0) {
			return g_layout.load() > 0;
		}
		auto* vm = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VQuickKeysMenuViewModel");
		if (!vm) {
			return false;   // not loaded yet: ask again later
		}
		const auto keyIndex = Offset(vm, "KeyIndex");
		if (keyIndex < 0) {
			return false;   // the class exists before its property chain is linked: ask again, never latch
		}
		g_layout.store(keyIndex == 0xD0 ? 1 : -1);
		if (g_layout.load() > 0) {
			logger::info("reflect: property offsets proven (VQuickKeysMenuViewModel KeyIndex at 0x{:X})", keyIndex);
		} else {
			logger::error("reflect: KeyIndex read at 0x{:X}, expected 0xD0 - the property layout is not UE5's; nothing reflected will run", keyIndex);
		}
		return g_layout.load() > 0;
	}

	bool Failed()
	{
		return g_layout.load() < 0;
	}

	bool Ok()
	{
		return g_layout.load() > 0;
	}

	bool IsLive(UE::UObject* a_o)
	{
		auto*        arr = UE::FUObjectArray::GetSingleton();
		std::int32_t idx = -1;
		if (!a_o || !arr || !ReadIndex(a_o, idx) || idx < 0 || idx >= arr->GetObjectArrayNum()) {
			return false;
		}
		auto* item = arr->IndexToObject(idx);
		return item && reinterpret_cast<UE::UObject*>(item->object) == a_o;
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
			if (o && o->GetClass() == a_class && o != cdo) {
				out.push_back(o);
			}
		}
		arr->UnlockInternalArray();
		return out;
	}

	bool Usable(UE::UObject* a_obj)
	{
		if (!IsLive(a_obj)) {
			return false;
		}
		auto*          item = UE::FUObjectArray::GetSingleton()->IndexToObject(a_obj->internalIndex);
		constexpr auto dying = UE::EInternalObjectFlags::Garbage | UE::EInternalObjectFlags::Unreachable | UE::EInternalObjectFlags::PendingKill;
		if (!item || item->HasAnyFlags(dying)) {
			return false;
		}
		constexpr auto destroyed = static_cast<std::int32_t>(UE::EObjectFlags::BeginDestroyed) | static_cast<std::int32_t>(UE::EObjectFlags::FinishDestroyed);
		return (static_cast<std::int32_t>(a_obj->objectFlags) & destroyed) == 0;
	}

	Handle Hold(UE::UObject* a_live)
	{
		return a_live ? Handle{ a_live, a_live->internalIndex } : Handle{};
	}

	UE::UObject* Get(const Handle& a_handle)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!a_handle.ptr || !arr || a_handle.index < 0 || a_handle.index >= arr->GetObjectArrayNum()) {
			return nullptr;
		}
		auto* item = arr->IndexToObject(a_handle.index);
		return item && reinterpret_cast<UE::UObject*>(item->object) == a_handle.ptr ? a_handle.ptr : nullptr;
	}
}
