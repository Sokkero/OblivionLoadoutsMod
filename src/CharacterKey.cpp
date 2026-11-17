// Gear Loadouts - the loaded character's GUID (see CharacterKey.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CharacterKey.h"

#include "Reflect.h"

namespace characterkey
{
	namespace
	{
		constexpr const wchar_t* kClassPath = L"/Script/Altar.VAltarSaveIdentificationData";

		UE::UClass*  g_class = nullptr;
		std::int32_t g_offset = -1;
		UE::UObject* g_instance = nullptr;   // checked with IsLive before every use
		std::string  g_lastProblem;

		std::string Fail(std::string a_why)
		{
			if (a_why != g_lastProblem) {   // log each new problem once, not on every switch
				logger::warn("character key: {}", a_why);
				g_lastProblem = std::move(a_why);
			}
			return {};
		}
	}

	std::string Read()
	{
		if (!reflect::SelfCheck()) {
			return Fail(reflect::Failed() ? "the property layout is not UE5's" : "the property layout is not checked yet");
		}
		if (!g_class) {
			g_class = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kClassPath);
			if (!g_class) {
				return Fail("VAltarSaveIdentificationData not found");
			}
			auto* st = reinterpret_cast<UE::UStruct*>(g_class);
			g_offset = reflect::Offset(st, "CharacterId");
			const auto size = reflect::Size(st, "CharacterId");
			if (g_offset < 0 || size != 16 || g_offset + 16 > st->propertiesSize) {
				g_class = nullptr;
				return Fail(std::format("CharacterId not found or not 16 bytes (offset {}, size {})", g_offset, size));
			}
			logger::info("character key: CharacterId at +0x{:X}", g_offset);
		}
		if (!g_instance || !reflect::IsLive(g_instance)) {
			const auto all = reflect::Instances(g_class);
			if (all.empty()) {
				g_instance = nullptr;
				return Fail("no VAltarSaveIdentificationData instance");
			}
			if (all.size() > 1) {
				logger::warn("character key: {} VAltarSaveIdentificationData instances - the first one is used", all.size());
			}
			g_instance = all.front();
		}
		const auto* g = reinterpret_cast<const UE::FGuid*>(reinterpret_cast<const std::uint8_t*>(g_instance) + g_offset);
		if (g->a == 0 && g->b == 0 && g->c == 0 && g->d == 0) {
			return Fail("the character ID is all zero (a new character before its first save?)");
		}
		g_lastProblem.clear();
		// the save-file format: %08X-%04X-%04X-%04X-%04X%08X
		return std::format("{:08X}-{:04X}-{:04X}-{:04X}-{:04X}{:08X}", g->a, g->b >> 16, g->b & 0xFFFF, g->c >> 16, g->c & 0xFFFF, g->d);
	}
}
