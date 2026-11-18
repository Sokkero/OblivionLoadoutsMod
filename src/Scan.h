#pragma once

// First match of a byte pattern ("49 3B ?? 07", ?? = any byte) in the game's executable sections, or nullptr.
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).

namespace scan
{
	inline std::uint8_t* First(const char* a_pattern)
	{
		std::vector<int> pat;
		for (const char* p = a_pattern; *p;) {
			if (*p == ' ') {
				++p;
				continue;
			}
			pat.push_back(*p == '?' ? -1 : static_cast<int>(std::strtoul(std::string(p, 2).c_str(), nullptr, 16)));
			p += 2;
		}
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
		auto* sec = IMAGE_FIRST_SECTION(nt);
		for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
			if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
				continue;
			}
			std::uint8_t*     start = base + sec->VirtualAddress;
			const std::size_t size = sec->Misc.VirtualSize;
			for (std::size_t at = 0; at + pat.size() <= size; ++at) {
				std::size_t k = 0;
				while (k < pat.size() && (pat[k] < 0 || start[at + k] == static_cast<std::uint8_t>(pat[k]))) {
					++k;
				}
				if (k == pat.size()) {
					return start + at;
				}
			}
		}
		return nullptr;
	}
}
