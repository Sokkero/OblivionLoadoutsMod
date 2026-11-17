// Gear Loadouts - the loadout logic.
//
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).
// Taken from it: how worn items are read (ExtraContainerChanges, Worn / WornLeft), the UnequipObject / EquipObject
// calls with their arguments, the combat check, the nameless-item rule, and the rules in the comments below that
// the original author learned the hard way.
// New here: nothing is moved into containers. A loadout is a saved list of forms (JSON beside the DLL).
// Modified 2026-11-17.

#include "Loadouts.h"

#include "Console.h"
#include "GameThread.h"
#include "TesThread.h"

#include <cctype>
#include <fstream>

namespace loadouts
{
	namespace
	{
		// Every equipped item is tracked (the owner, 2026-11-17): armour, shields, clothing, rings, amulets, ammo,
		// weapons (bows and staffs too) and torches. Slots 1 (hair) and 15 (tail) are body parts, never items.
		constexpr bool kIncludeWeapons = true;

		struct Saved
		{
			std::uint32_t formID;    // runtime form ID of the current session
			bool          ownData;   // the saved copy had its own data (health, enchantment charge, a name ...)
		};

		std::array<std::vector<Saved>, kCount>        g_loadouts;
		int                                           g_active = 0;
		std::string                                   g_key;   // the character whose data is loaded
		json                                          g_root = json::object();
		bool                                          g_rootRead = false;
		std::unordered_map<std::string, std::uint8_t> g_indexByName;   // lower-case plugin name -> load index
		std::array<std::string, 256>                  g_nameByIndex;
		std::atomic<bool>                             g_busy{ false };

		// ---------------------------------------------------------------------------------------------------
		// helpers
		// ---------------------------------------------------------------------------------------------------

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::filesystem::path ThisModule()
		{
			HMODULE self = nullptr;
			wchar_t buf[MAX_PATH]{};
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&ThisModule), &self) &&
				GetModuleFileNameW(self, buf, MAX_PATH)) {
				return std::filesystem::path(buf);
			}
			return std::filesystem::path(L"OBSE") / L"Plugins" / L"GearLoadouts.dll";
		}

		std::filesystem::path FilePath()
		{
			return ThisModule().parent_path() / L"GearLoadouts.json";
		}

		std::string NameOf(RE::TESForm* a_form)
		{
			const char* n = a_form ? RE::TESFullName::GetFullName(a_form) : nullptr;
			return n && *n ? std::string(n) : (a_form ? std::format("0x{:08X}", a_form->GetFormID()) : std::string("?"));
		}

		// The player's name is the key of the saved data. 0x7 is the player's base NPC. NOT VERIFIED that its full name
		// is the character's name in the remaster; the fallback is one shared key.
		std::string PlayerKey()
		{
			auto*       base = RE::TESForm::LookupByID(0x00000007);
			const char* n = base ? RE::TESFullName::GetFullName(base) : nullptr;
			return n && *n ? std::string(n) : std::string("player");
		}

		// ---------------------------------------------------------------------------------------------------
		// reading what is worn (adapted from the original: Loadouts.cpp, IsGear .. WornEntries)
		// ---------------------------------------------------------------------------------------------------

		bool IsGear(RE::TESBoundObject* a_object)
		{
			// Armour (shields too), clothing (rings and amulets are clothing), ammunition, weapons, torches (Light).
			// Nameless items are other mods' hidden pieces and are never touched (the original's rule).
			if (!a_object) {
				return false;
			}
			const auto t = a_object->GetFormType();
			if (t != RE::FormType::Armor && t != RE::FormType::Clothing && t != RE::FormType::Ammo && t != RE::FormType::Light &&
				!(kIncludeWeapons && t == RE::FormType::Weapon)) {
				return false;
			}
			const char* n = RE::TESFullName::GetFullName(a_object);
			return n && *n;
		}

		RE::BSSimpleList<RE::ItemChange*>* Items(RE::TESObjectREFR* a_ref)
		{
			auto* changes = a_ref ? a_ref->extra.GetExtraData<RE::ExtraContainerChanges>() : nullptr;
			return changes && changes->changes ? changes->changes->list : nullptr;
		}

		bool IsWorn(RE::ExtraDataList* a_list, bool& a_left)
		{
			if (!a_list) {
				return false;
			}
			a_left = a_list->GetExtraData(RE::EXTRA_DATA_TYPE::WornLeft) != nullptr;
			return a_left || a_list->GetExtraData(RE::EXTRA_DATA_TYPE::Worn) != nullptr;
		}

		bool HasOwnData(RE::ExtraDataList* a_list)
		{
			// what makes one copy of an item differ from another: health, charge, a poison, ownership, a soul
			for (RE::BSExtraData* x = a_list ? a_list->head : nullptr; x; x = x->next) {
				switch (x->type.get()) {
				case RE::EXTRA_DATA_TYPE::Worn:
				case RE::EXTRA_DATA_TYPE::WornLeft:
				case RE::EXTRA_DATA_TYPE::Count:
				case RE::EXTRA_DATA_TYPE::QuickKey:
					break;
				default:
					return true;
				}
			}
			return false;
		}

		struct WornEntry
		{
			RE::ItemChange*    item;
			RE::ExtraDataList* extra;
			std::int32_t       count;
		};

		std::vector<WornEntry> WornEntries(RE::PlayerCharacter* a_player)
		{
			std::vector<WornEntry> out;
			auto*                  items = Items(a_player);
			if (!items) {
				return out;
			}
			for (RE::ItemChange* item : *items) {
				if (!item || !item->object || !item->extraData || item->count <= 0 || !IsGear(item->object)) {
					continue;
				}
				for (RE::ExtraDataList* xl : *item->extraData) {
					bool left = false;
					if (!IsWorn(xl, left)) {
						continue;
					}
					// ammunition is worn as a whole stack; everything else one piece per list (the original's rule)
					const std::int32_t n = item->object->GetFormType() == RE::FormType::Ammo ? item->count : 1;
					out.push_back({ item, xl, n });
				}
			}
			return out;
		}

		// An item of the player's inventory by form ID, with at least one copy.
		RE::ItemChange* FindCarried(RE::PlayerCharacter* a_player, std::uint32_t a_formID)
		{
			auto* items = Items(a_player);
			if (!items) {
				return nullptr;
			}
			for (RE::ItemChange* item : *items) {
				if (item && item->object && item->object->GetFormID() == a_formID && item->count > 0) {
					return item;
				}
			}
			return nullptr;
		}

		// A copy that is not worn, preferably one that matches the saved copy (own data or none). Found fresh after the
		// unequips: an ExtraDataList handed to the engine is not ours afterwards (the original's rule). nullptr means
		// "any copy" - the engine then picks one.
		RE::ExtraDataList* UnwornList(RE::ItemChange* a_item, bool a_ownData)
		{
			if (!a_item || !a_item->extraData) {
				return nullptr;
			}
			for (RE::ExtraDataList* xl : *a_item->extraData) {
				bool left = false;
				if (xl && !IsWorn(xl, left) && HasOwnData(xl) == a_ownData) {
					return xl;
				}
			}
			return nullptr;
		}

		// ---------------------------------------------------------------------------------------------------
		// saving: GearLoadouts.json beside the DLL, one entry per character name. A form is saved as (plugin name,
		// local ID) so a changed load order does not break it; forms made during play (0xFF......) are saved as they are.
		// OBSE64 has no save-game data interface (checked in CommonLibOB64), so the data is not part of the save file.
		// ---------------------------------------------------------------------------------------------------

		void ReadRoot()
		{
			g_rootRead = true;
			g_root = json::object();
			const auto   path = FilePath();
			std::ifstream in(path);
			if (!in) {
				return;
			}
			json parsed = json::parse(in, nullptr, false);
			in.close();
			if (parsed.is_discarded() || !parsed.is_object()) {
				std::error_code ec;
				auto            bad = path;
				bad += L".bad";
				std::filesystem::copy_file(path, bad, std::filesystem::copy_options::overwrite_existing, ec);
				logger::warn("{} could not be read - a copy was kept as .bad and the loadouts start empty", path.filename().string());
				return;
			}
			g_root = std::move(parsed);
		}

		bool ParseEntry(const json& a_entry, Saved& a_out)
		{
			try {
				const std::string   plugin = a_entry.value("plugin", std::string());
				const std::uint32_t local = static_cast<std::uint32_t>(std::stoul(a_entry.value("id", std::string("0")), nullptr, 16));
				a_out.ownData = a_entry.value("own", false);
				if (plugin.empty()) {
					a_out.formID = local;   // a form made during play
					return local != 0;
				}
				const auto it = g_indexByName.find(Lower(plugin));
				if (it == g_indexByName.end()) {
					logger::warn("saved item 0x{:06X} of '{}' skipped: that plugin is not loaded", local & 0x00FFFFFFu, plugin);
					return false;
				}
				a_out.formID = (static_cast<std::uint32_t>(it->second) << 24) | (local & 0x00FFFFFFu);
				return true;
			} catch (const std::exception&) {
				return false;
			}
		}

		void LoadCharacter(const std::string& a_key)
		{
			if (!g_rootRead) {
				ReadRoot();
			}
			g_key = a_key;
			g_active = 0;
			for (auto& l : g_loadouts) {
				l.clear();
			}
			try {
				if (!g_root.contains("characters") || !g_root["characters"].is_object() || !g_root["characters"].contains(a_key)) {
					logger::info("no saved loadouts for '{}' yet - Default is active", a_key);
					return;
				}
				const json& c = g_root["characters"][a_key];
				g_active = std::clamp(c.value("active", 0), 0, kCount - 1);
				if (c.contains("loadouts") && c["loadouts"].is_array()) {
					const auto& arr = c["loadouts"];
					for (std::size_t i = 0; i < arr.size() && i < static_cast<std::size_t>(kCount); ++i) {
						if (!arr[i].is_array()) {
							continue;
						}
						for (const auto& e : arr[i]) {
							Saved s{};
							if (ParseEntry(e, s)) {
								g_loadouts[i].push_back(s);
							}
						}
					}
				}
				logger::info("loaded the loadouts of '{}': active {}, pieces per loadout {}/{}/{}/{}/{}", a_key, Name(g_active),
					g_loadouts[0].size(), g_loadouts[1].size(), g_loadouts[2].size(), g_loadouts[3].size(), g_loadouts[4].size());
			} catch (const std::exception& ex) {
				logger::error("the saved loadouts of '{}' could not be read: {}", a_key, ex.what());
			}
		}

		void SaveCharacter()
		{
			try {
				json all = json::array();
				for (const auto& list : g_loadouts) {
					json one = json::array();
					for (const auto& s : list) {
						json e;
						if (s.formID >= 0xFF000000u) {
							e["plugin"] = "";
							e["id"] = std::format("0x{:08X}", s.formID);
						} else {
							const std::string& name = g_nameByIndex[s.formID >> 24];
							if (name.empty()) {
								logger::warn("0x{:08X} not saved: no plugin has load index 0x{:02X}", s.formID, s.formID >> 24);
								continue;
							}
							e["plugin"] = name;
							e["id"] = std::format("0x{:08X}", s.formID & 0x00FFFFFFu);
						}
						e["own"] = s.ownData;
						one.push_back(std::move(e));
					}
					all.push_back(std::move(one));
				}
				g_root["version"] = 1;
				g_root["characters"][g_key] = json{ { "active", g_active }, { "loadouts", std::move(all) } };

				const auto path = FilePath();
				auto       tmp = path;
				tmp += L".tmp";
				{
					std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
					if (!out) {
						logger::error("cannot write {}", tmp.string());
						return;
					}
					out << g_root.dump(2, ' ', false, json::error_handler_t::replace);
				}
				std::error_code ec;
				std::filesystem::rename(tmp, path, ec);
				if (ec) {
					logger::error("cannot replace {}: {}", path.string(), ec.message());
				}
			} catch (const std::exception& ex) {
				logger::error("the loadouts could not be saved: {}", ex.what());
			}
		}

		// ---------------------------------------------------------------------------------------------------
		// the switch (runs on the TES thread)
		// ---------------------------------------------------------------------------------------------------

		void Switch(int a_to)
		{
			struct Release
			{
				~Release() { g_busy = false; }
			} release;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			if (g_indexByName.empty()) {
				Init();
			}
			const std::string key = PlayerKey();
			if (key != g_key) {
				LoadCharacter(key);
			}
			if (a_to == g_active) {
				logger::info("switch: {} is already active", Name(a_to));
				MessageBeep(MB_ICONHAND);
				return;
			}
			const int from = g_active;

			// 1. what is worn now becomes the old loadout
			const auto         worn = WornEntries(player);
			std::vector<Saved> now;
			for (const auto& w : worn) {
				now.push_back({ w.item->object->GetFormID(), HasOwnData(w.extra) });
			}
			g_loadouts[static_cast<std::size_t>(from)] = now;

			// Pieces worn now that the new loadout also holds stay on: nothing is taken off and put on again.
			std::vector<Saved>     wanted = g_loadouts[static_cast<std::size_t>(a_to)];
			std::vector<WornEntry> takeOff;
			int                    kept = 0;
			for (const auto& w : worn) {
				const std::uint32_t id = w.item->object->GetFormID();
				const auto          it = std::ranges::find_if(wanted, [id](const Saved& s) { return s.formID == id; });
				if (it != wanted.end()) {
					wanted.erase(it);
					++kept;
				} else {
					takeOff.push_back(w);
				}
			}

			// 2. take off the rest. Names first: the engine may free the lists during the call.
			for (const auto& w : takeOff) {
				const std::string name = NameOf(w.item->object);
				const bool        ok = player->UnequipObject(w.item->object, w.count, w.extra, false, true);
				logger::info("unequip: {} x{} -> {}", name, w.count, ok ? "ok" : "the engine said no");
			}

			// 3. put on what the new loadout holds and is still in the inventory
			int equipped = 0;
			int missing = 0;
			for (const auto& s : wanted) {
				RE::ItemChange* item = FindCarried(player, s.formID);
				if (!item) {
					++missing;
					logger::info("equip: 0x{:08X} is not in the inventory - skipped", s.formID);
					continue;
				}
				RE::ExtraDataList*  xl = UnwornList(item, s.ownData);
				const std::int32_t  count = item->object->GetFormType() == RE::FormType::Ammo ? item->count : 1;
				const std::string   name = NameOf(item->object);
				// The last argument is the console's NoUnequip lock. true made every restored piece impossible to take
				// off again in the original mod, so it stays false.
				player->EquipObject(item->object, count, xl, false, false);
				++equipped;
				logger::info("equip: {} x{}{}", name, count, xl ? " (own data)" : "");
			}

			// 4. done
			g_active = a_to;
			SaveCharacter();
			logger::info("switch {} -> {}: {} kept, {} taken off, {} equipped, {} missing", Name(from), Name(a_to), kept, takeOff.size(),
				equipped, missing);
			MessageBeep(MB_OK);
		}
	}

	const char* Name(int a_loadout)
	{
		static constexpr const char* kNames[kCount] = { "Default", "Loadout 1", "Loadout 2", "Loadout 3", "Loadout 4" };
		return a_loadout >= 0 && a_loadout < kCount ? kNames[a_loadout] : "?";
	}

	void Init()
	{
		g_indexByName.clear();
		for (auto& n : g_nameByIndex) {
			n.clear();
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return;
		}
		std::string files;
		for (RE::TESFile* f : dh->listFiles) {
			if (!f || f->GetCompileIndex() == 0xFF) {
				continue;
			}
			const std::string name = std::filesystem::path(f->filename).filename().string();
			g_indexByName[Lower(name)] = f->GetCompileIndex();
			g_nameByIndex[f->GetCompileIndex()] = name;
			files += std::format("{}{} (0x{:02X})", files.empty() ? "" : ", ", name, f->GetCompileIndex());
		}
		logger::info("plugins loaded by the game: {}", files);
	}

	bool Request(int a_to, std::string& a_why)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			a_why = "no player";
			return false;
		}
		if (a_to < 0 || a_to >= kCount) {
			a_why = "no such loadout";
			return false;
		}
		// Combat check. Actor::IsInCombat, GetCombatTarget and GetCombatController all said "no combat" for the player in
		// a real fight (tested 2026-11-17). These two PlayerCharacter fields do work (same day, in game):
		//   numberActorsInCombat (+0xB10): -1 out of combat, N while actors fight the player.
		//   playerCombatList (+0x828): null out of combat, the actors fighting the player while in combat.
		// Two NPCs fighting each other without the player changed neither.
		// After a won fight the list went back to null at once, but numberActorsInCombat stayed 1 (at least 7 s, test
		// 21:18). So only the list blocks; the counter is logged for information.
		int         listCount = 0;
		std::string listNames;
		if (player->playerCombatList) {
			for (RE::TESObjectREFR* ref : *player->playerCombatList) {
				if (!ref) {
					continue;
				}
				if (++listCount <= 3) {
					listNames += std::format("{}{}", listNames.empty() ? "" : ", ", NameOf(ref->GetObjectReference()));
				}
			}
		}
		logger::info("combat check: actorsInCombat={} list={} ({})", player->numberActorsInCombat,
			player->playerCombatList ? std::to_string(listCount) : std::string("null"), listNames);
		if (listCount > 0) {
			a_why = "in combat";
			if (gamethread::Installed()) {
				gamethread::Post([] { console::Notify("You cannot change your loadout during combat."); });   // Unreal thread only
			}
			return false;
		}
		if (!testhread::Installed()) {
			a_why = "the TES thread hook is not installed";
			return false;
		}
		if (g_busy.exchange(true)) {
			a_why = "a switch is already running";
			return false;
		}
		testhread::Post([a_to] { Switch(a_to); });   // the engine's equipment path runs on the TES thread only
		return true;
	}
}
