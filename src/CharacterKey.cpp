// SPDX-License-Identifier: GPL-3.0-or-later

#include "CharacterKey.h"

#include "Reflect.h"

namespace characterkey
{
	namespace
	{
		UE::UClass*  g_class = nullptr;
		std::int32_t g_offset = -1;
		UE::UObject* g_instance = nullptr;
		std::string  g_lastProblem;

		std::string Fail(std::string a_why)
		{
			if (a_why != g_lastProblem) {
				logger::warn("character key: {}", a_why);
				g_lastProblem = std::move(a_why);
			}
			return {};
		}
	}

	std::string Read()
	{
		if (!reflect::SelfCheck()) {
			return Fail("property layout not proven yet");
		}
		if (!g_class) {
			g_class = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VAltarSaveIdentificationData");
			if (!g_class) {
				return Fail("VAltarSaveIdentificationData not found");
			}
			g_offset = reflect::Offset(g_class, "CharacterId");
			if (g_offset < 0 || reflect::Size(g_class, "CharacterId") != sizeof(UE::FGuid)) {
				g_class = nullptr;
				return Fail("CharacterId not found or not a GUID");
			}
		}
		if (!reflect::IsLive(g_instance)) {
			const auto all = reflect::Instances(g_class);
			g_instance = all.empty() ? nullptr : all.front();
			if (!g_instance) {
				return Fail("no VAltarSaveIdentificationData instance");
			}
		}
		const auto* g = reflect::At<const UE::FGuid>(g_instance, g_offset);
		if (g->a == 0 && g->b == 0 && g->c == 0 && g->d == 0) {
			return Fail("character ID is all zero (not saved yet?)");
		}
		g_lastProblem.clear();
		return std::format("{:08X}-{:04X}-{:04X}-{:04X}-{:04X}{:08X}", g->a, g->b >> 16, g->b & 0xFFFF, g->c >> 16, g->c & 0xFFFF, g->d);
	}
}
