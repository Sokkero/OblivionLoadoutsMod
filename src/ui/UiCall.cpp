// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Menus Revamped by cfa911 (see NOTICE.md).

#include "ui/UiCall.h"

#include "Reflect.h"

namespace ui
{
	namespace
	{
		// __try cannot share a function with destructors
		bool ProcessEventGuarded(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			__try {
				a_obj->ProcessEvent(a_fn, a_params);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		UE::UFunction* Find(UE::UObject* a_obj, const wchar_t* a_fn)
		{
			return a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
		}

		std::size_t FrameSize(UE::UFunction* a_fn)
		{
			return static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(a_fn)->propertiesSize, 0));
		}
	}

	Call::Call(UE::UObject* a_obj, const wchar_t* a_fn) :
		m_obj(a_obj),
		m_fn(Find(a_obj, a_fn))
	{
		if (m_fn) {
			m_params.assign(FrameSize(m_fn) + 16, 0);
		}
	}

	void* Call::At(std::string_view a_name)
	{
		const auto off = m_fn ? reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name) : -1;
		return off >= 0 ? m_params.data() + off : nullptr;
	}

	std::int32_t Call::Size(std::string_view a_name) const
	{
		return m_fn ? reflect::Size(reinterpret_cast<UE::UStruct*>(m_fn), a_name) : -1;
	}

	bool Call::Run()
	{
		if (!m_fn) {
			return false;
		}
		m_obj->ProcessEvent(m_fn, m_params.data());
		return true;
	}

	bool Call::RunGuarded()
	{
		return m_fn && ProcessEventGuarded(m_obj, m_fn, m_params.data());
	}

	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
	{
		auto*      fn = Find(a_obj, a_fn);
		const auto off = fn ? reflect::FirstOffset(reinterpret_cast<UE::UStruct*>(fn)) : -1;
		if (off < 0) {
			return false;
		}
		std::vector<std::uint8_t> params(FrameSize(fn) + a_size + 16, 0);
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
		// the game's controller is a subclass (VAltarPlayerController), so match with IsChildOf
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
				constexpr std::int32_t kDefaultOrArchetype = 0x30;   // RF_ClassDefaultObject | RF_ArchetypeObject
				if (oc && oc->IsChildOf(cls) && (static_cast<std::int32_t>(o->objectFlags) & kDefaultOrArchetype) == 0) {
					found = o;
				}
			}
			arr->UnlockInternalArray();
		}
		cached = reflect::Hold(found);
		return found;
	}

	std::string Name(UE::UObject* a_object)
	{
		return a_object ? reflect::Utf8(a_object->GetFName().ToString()) : std::string("-");
	}
}
