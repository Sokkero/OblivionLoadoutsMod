// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm (see NOTICE.md).

#include "Push.h"

#include "Hook.h"
#include "Scan.h"

namespace push
{
	namespace
	{
		// 0x18 bytes into the function: frame set-up, stack cookie and the GetMenuByClass(0x3EA = InventoryMenu) call
		constexpr const char* kPattern = "48 8D A8 98 FE FF FF 48 81 EC 40 02 00 00 0F 29 70 C8 0F 29 78 B8 44 0F 29 40 A8 44 0F 29 48 98 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 F0 00 00 00 B9 EA 03 00 00 E8";
		constexpr std::size_t kPatternOffset = 0x18;

		void (*g_rebuild)() = nullptr;
	}

	bool Install()
	{
		if (g_rebuild) {
			return true;
		}
		std::uint8_t* site = scan::First(kPattern);
		if (!site) {
			logger::error("push: inventory list rebuild not found - equipped marks update only on reopening the menu");
			return false;
		}
		g_rebuild = reinterpret_cast<void (*)()>(site - kPatternOffset);
		logger::info("push: inventory list rebuild at game+0x{:X}", hook::Rva(site - kPatternOffset));
		return true;
	}

	bool Rebuild()
	{
		if (!g_rebuild) {
			return false;
		}
		g_rebuild();
		return true;
	}
}
