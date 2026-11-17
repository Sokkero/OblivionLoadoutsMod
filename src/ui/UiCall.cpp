// Gear Loadouts - calling Unreal functions through reflection.
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md), engine/UiCall.cpp
// and engine/UiText.cpp. Modified 2026-11-17: no ProcessEvent watcher bookkeeping; GetText and Name added here.

#include "ui/UiCall.h"

#include "Reflect.h"

namespace ui
{
	namespace
	{
		// __try cannot share a function with a destructor: the guard is its own function
		bool ProcessEventGuarded(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			__try {
				a_obj->ProcessEvent(a_fn, a_params);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
	}

	Call::Call(UE::UObject* a_obj, const wchar_t* a_fn) :
		m_obj(a_obj),
		m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
	{
		if (m_fn) {
			m_params.assign(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize, 0)) + 16, 0);
		}
	}

	void* Call::At(std::string_view a_name)
	{
		if (!m_fn) {
			return nullptr;
		}
		const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
		return off >= 0 ? m_params.data() + off : nullptr;
	}

	std::int32_t Call::Size(std::string_view a_name) const
	{
		return m_fn ? reflect::Size(reinterpret_cast<UE::UStruct*>(m_fn), a_name) : -1;
	}

	bool Call::Run()
	{
		if (!m_fn || !m_obj) {
			return false;
		}
		m_obj->ProcessEvent(m_fn, m_params.data());
		return true;
	}

	bool Call::RunGuarded()
	{
		return m_fn && m_obj && GuardedProcessEvent(m_obj, m_fn, m_params.data());
	}

	bool GuardedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
	{
		return a_obj && a_fn && ProcessEventGuarded(a_obj, a_fn, a_params);
	}

	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
	{
		auto*      fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
		auto*      st = reinterpret_cast<UE::UStruct*>(fn);
		auto*      f = st ? reinterpret_cast<std::uint8_t*>(st->childProperties) : nullptr;
		const auto off = f ? *reinterpret_cast<const std::int32_t*>(f + 0x44) : -1;
		if (off < 0) {
			return false;
		}
		std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(st->propertiesSize, 0)) + a_size + 16, 0);
		std::memcpy(params.data() + off, a_bytes, a_size);
		a_obj->ProcessEvent(fn, params.data());
		return true;
	}

	UE::UClass* Class(const wchar_t* a_path)
	{
		return UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path);
	}

	UE::UObject* PlayerController()
	{
		static reflect::Handle cached;
		static ULONGLONG       lastScan = 0;
		if (auto* pc = reflect::Get(cached)) {
			return pc;
		}
		const ULONGLONG now = GetTickCount64();
		if (now - lastScan < 2000) {
			return nullptr;
		}
		lastScan = now;
		// the game's controller is a SUBCLASS (VAltarPlayerController): an exact class match never finds it (MR)
		auto*        cls = Class(L"/Script/Engine.PlayerController");
		auto*        arr = UE::FUObjectArray::GetSingleton();
		UE::UObject* found = nullptr;
		if (cls && arr) {
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n && !found; ++i) {
				auto* item = arr->IndexToObject(i);
				auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
				auto* oc = o ? o->GetClass() : nullptr;
				// not a class default object, not an archetype
				if (oc && oc->IsChildOf(cls) && (static_cast<std::int32_t>(o->objectFlags) & 0x30) == 0) {
					found = o;
				}
			}
			arr->UnlockInternalArray();
		}
		cached = found ? reflect::Hold(found) : reflect::Handle{};
		return found;
	}

	std::string GetText(UE::UObject* a_widget)
	{
		Call  c(a_widget, L"GetText");
		void* ret = c ? c.At("ReturnValue") : nullptr;
		if (!ret || !c.Run()) {
			return {};
		}
		auto* ft = static_cast<UE::FText*>(ret);
		// a zeroed FText (never set) crashes the engine when read (MR, SLS lesson)
		const auto* raw = reinterpret_cast<void* const*>(ft);
		if (!raw[0] || !raw[1]) {
			return {};
		}
		std::string out = reflect::Utf8(ft->ToString());
		ft->~FText();   // the returned copy holds a reference
		return out;
	}

	std::string Name(UE::UObject* a_object)
	{
		return a_object ? reflect::Utf8(a_object->GetFName().ToString()) : std::string("-");
	}
}
