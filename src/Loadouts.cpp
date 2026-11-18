// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Simple Loadout System by ApocryphaRealm (see NOTICE.md): reading worn items, the Unequip / Equip calls
// and their rules. Changed: nothing is moved into containers; a loadout is a saved list of forms.

#include "Loadouts.h"

#include "CharacterKey.h"
#include "Console.h"
#include "GameThread.h"
#include "Push.h"
#include "TesThread.h"
#include "Text.h"

#include <cctype>
#include <fstream>

namespace loadouts
{
	namespace
	{
		struct Saved
		{
			std::uint32_t formID;
			bool          ownData;   // the saved copy had its own extra data (health, charge, enchantment, ...)
		};

		using Names = std::array<std::string, kCount>;

		// TES thread
		std::array<std::vector<Saved>, kCount>        g_loadouts;
		int                                           g_active = 0;
		std::string                                   g_key;   // character whose data is loaded
		json                                          g_root;
		std::unordered_map<std::string, std::uint8_t> g_indexByName;   // lower-case plugin name -> load index
		std::array<std::string, 256>                  g_nameByIndex;

		// shared with the UI
		std::atomic<int>           g_uiActive{ -1 };
		std::atomic<bool>          g_busy{ false };
		std::mutex                 g_namesLock;
		Names                      g_names;
		std::atomic<std::uint32_t> g_namesVersion{ 0 };

		Names DefaultNames()
		{
			Names out;
			for (int i = 0; i < kCount; ++i) {
				out[i] = Name(i);
			}
			return out;
		}

		void SetNames(const Names& a_names)
		{
			{
				std::scoped_lock l(g_namesLock);
				g_names = a_names;
			}
			++g_namesVersion;
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::filesystem::path FilePath()
		{
			HMODULE self = nullptr;
			wchar_t buf[MAX_PATH]{};
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&FilePath), &self);
			GetModuleFileNameW(self, buf, MAX_PATH);
			return std::filesystem::path(buf).parent_path() / L"GearLoadouts.json";
		}

		std::string NameOf(RE::TESForm* a_form)
		{
			const char* n = a_form ? RE::TESFullName::GetFullName(a_form) : nullptr;
			return n && *n ? std::string(n) : (a_form ? std::format("0x{:08X}", a_form->GetFormID()) : std::string("?"));
		}

		// -------------------------------------------------------------------------------------------------------
		// reading what is worn
		// -------------------------------------------------------------------------------------------------------

		// Armour, clothing (incl. rings and amulets), ammo, weapons, torches. Nameless items are hidden pieces of other
		// mods and are never touched.
		bool IsGear(RE::TESBoundObject* a_object)
		{
			if (!a_object) {
				return false;
			}
			switch (a_object->GetFormType()) {
			case RE::FormType::Armor:
			case RE::FormType::Clothing:
			case RE::FormType::Ammo:
			case RE::FormType::Light:
			case RE::FormType::Weapon:
				break;
			default:
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

		bool IsWorn(RE::ExtraDataList* a_list)
		{
			return a_list && (a_list->GetExtraData(RE::EXTRA_DATA_TYPE::Worn) || a_list->GetExtraData(RE::EXTRA_DATA_TYPE::WornLeft));
		}

		bool HasOwnData(RE::ExtraDataList* a_list)
		{
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

		// Ammo is worn as a whole stack, everything else one piece per extra data list.
		// GetItemCountInContainer also counts items still only in the base container (no change entry).
		std::int32_t EquipCount(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object)
		{
			return a_object->GetFormType() == RE::FormType::Ammo ? a_player->GetItemCountInContainer(a_object) : 1;
		}

		struct WornEntry
		{
			RE::ItemChange*    item;
			RE::ExtraDataList* extra;
		};

		std::vector<WornEntry> WornEntries(RE::PlayerCharacter* a_player)
		{
			std::vector<WornEntry> out;
			auto*                  items = Items(a_player);
			if (!items) {
				return out;
			}
			// item->count is the delta against the base container, not the carried count: only the Worn flag decides
			for (RE::ItemChange* item : *items) {
				if (!item || !item->extraData || !IsGear(item->object)) {
					continue;
				}
				for (RE::ExtraDataList* xl : *item->extraData) {
					if (IsWorn(xl)) {
						out.push_back({ item, xl });
					}
				}
			}
			return out;
		}

		RE::ItemChange* FindEntry(RE::PlayerCharacter* a_player, std::uint32_t a_formID)
		{
			if (auto* items = Items(a_player)) {
				for (RE::ItemChange* item : *items) {
					if (item && item->object && item->object->GetFormID() == a_formID) {
						return item;
					}
				}
			}
			return nullptr;
		}

		// An unworn copy matching the saved one (own data or not). Looked up fresh after the unequips: an extra data
		// list handed to the engine may be freed. nullptr = the engine picks a copy.
		RE::ExtraDataList* UnwornList(RE::ItemChange* a_item, bool a_ownData)
		{
			if (a_item && a_item->extraData) {
				for (RE::ExtraDataList* xl : *a_item->extraData) {
					if (xl && !IsWorn(xl) && HasOwnData(xl) == a_ownData) {
						return xl;
					}
				}
			}
			return nullptr;
		}

		// -------------------------------------------------------------------------------------------------------
		// storage: GearLoadouts.json beside the DLL, one entry per character GUID. Forms are saved as plugin name +
		// local ID, so load order changes do not break them. Forms created at runtime (0xFF......) are saved as is.
		// -------------------------------------------------------------------------------------------------------

		void ReadRoot()
		{
			const auto    path = FilePath();
			std::ifstream in(path);
			g_root = in ? json::parse(in, nullptr, false) : json::object();
			in.close();
			if (g_root.is_discarded() || !g_root.is_object()) {
				std::error_code ec;
				std::filesystem::copy_file(path, std::filesystem::path(path) += L".bad", std::filesystem::copy_options::overwrite_existing, ec);
				logger::warn("GearLoadouts.json is broken - kept a copy as .bad, starting empty");
				g_root = json::object();
			}
		}

		bool ParseEntry(const json& a_entry, Saved& a_out)
		{
			try {
				const std::string   plugin = a_entry.value("plugin", std::string());
				const std::uint32_t id = static_cast<std::uint32_t>(std::stoul(a_entry.value("id", std::string("0")), nullptr, 16));
				a_out.ownData = a_entry.value("own", false);
				if (plugin.empty()) {
					a_out.formID = id;
					return id != 0;
				}
				const auto it = g_indexByName.find(Lower(plugin));
				if (it == g_indexByName.end()) {
					logger::warn("saved item 0x{:06X} of '{}' skipped: plugin not loaded", id & 0x00FFFFFFu, plugin);
					return false;
				}
				a_out.formID = (static_cast<std::uint32_t>(it->second) << 24) | (id & 0x00FFFFFFu);
				return true;
			} catch (const std::exception&) {
				return false;
			}
		}

		void LoadCharacter(const std::string& a_key)
		{
			if (g_root.is_null()) {
				ReadRoot();
			}
			g_key = a_key;
			g_active = 0;
			for (auto& l : g_loadouts) {
				l.clear();
			}
			Names names = DefaultNames();
			try {
				const json* c = nullptr;
				if (auto chars = g_root.find("characters"); chars != g_root.end() && chars->is_object() && chars->contains(a_key)) {
					c = &(*chars)[a_key];
				}
				if (!c) {
					logger::info("no saved loadouts for {} yet", a_key);
					SetNames(names);
					return;
				}
				g_active = std::clamp(c->value("active", 0), 0, kCount - 1);
				if (auto n = c->find("names"); n != c->end() && n->is_array()) {
					for (std::size_t i = 0; i < n->size() && i < names.size(); ++i) {
						std::string clean;
						if ((*n)[i].is_string() && CleanName((*n)[i].get<std::string>(), clean, true)) {
							names[i] = clean;
						}
					}
				}
				if (auto arr = c->find("loadouts"); arr != c->end() && arr->is_array()) {
					for (std::size_t i = 0; i < arr->size() && i < static_cast<std::size_t>(kCount); ++i) {
						for (const auto& e : (*arr)[i]) {
							if (Saved s{}; ParseEntry(e, s)) {
								g_loadouts[i].push_back(s);
							}
						}
					}
				}
				logger::info("loaded loadouts of {}: active {}", a_key, Name(g_active));
			} catch (const std::exception& ex) {
				logger::error("saved loadouts of {} could not be read: {}", a_key, ex.what());
			}
			SetNames(names);
		}

		void SaveCharacter()
		{
			try {
				json all = json::array();
				for (const auto& list : g_loadouts) {
					json one = json::array();
					for (const auto& s : list) {
						const bool         runtime = s.formID >= 0xFF000000u;
						const std::string& plugin = runtime ? std::string() : g_nameByIndex[s.formID >> 24];
						if (!runtime && plugin.empty()) {
							continue;   // no plugin at that load index
						}
						one.push_back({ { "plugin", plugin }, { "id", std::format("0x{:08X}", runtime ? s.formID : s.formID & 0x00FFFFFFu) }, { "own", s.ownData } });
					}
					all.push_back(std::move(one));
				}
				json names;
				{
					std::scoped_lock l(g_namesLock);
					names = g_names;
				}
				g_root["version"] = 1;
				g_root["characters"][g_key] = { { "active", g_active }, { "names", std::move(names) }, { "loadouts", std::move(all) } };

				// write to .tmp, then rename: a crash never leaves a half-written file
				const auto path = FilePath();
				const auto tmp = std::filesystem::path(path) += L".tmp";
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
				logger::error("loadouts could not be saved: {}", ex.what());
			}
		}

		// TES thread: make a_key the loaded character
		void Use(const std::string& a_key)
		{
			if (g_indexByName.empty()) {
				Init();
			}
			if (a_key != g_key) {
				LoadCharacter(a_key);
			}
			g_uiActive = g_active;
		}

		// -------------------------------------------------------------------------------------------------------
		// the switch (TES thread)
		// -------------------------------------------------------------------------------------------------------

		void Switch(int a_to, const std::string& a_key)
		{
			struct Release
			{
				~Release() { g_busy = false; }
			} release;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			Use(a_key);
			if (a_to == g_active) {
				MessageBeep(MB_ICONHAND);
				return;
			}
			const int from = g_active;

			// 1. what is worn now becomes the old loadout; pieces the new one also holds stay on
			const auto         worn = WornEntries(player);
			std::vector<Saved> wanted = g_loadouts[a_to];
			std::vector<Saved> now;
			std::vector<WornEntry> takeOff;
			for (const auto& w : worn) {
				const std::uint32_t id = w.item->object->GetFormID();
				now.push_back({ id, HasOwnData(w.extra) });
				if (const auto it = std::ranges::find(wanted, id, &Saved::formID); it != wanted.end()) {
					wanted.erase(it);
				} else {
					takeOff.push_back(w);
				}
			}
			g_loadouts[from] = std::move(now);

			// 2. take off the rest
			for (const auto& w : takeOff) {
				player->UnequipObject(w.item->object, EquipCount(player, w.item->object), w.extra, false, true);
			}

			// 3. put on what the new loadout holds and is still carried
			std::size_t missing = 0;
			for (const auto& s : wanted) {
				auto* form = RE::TESForm::LookupByID(s.formID);
				auto* object = form && IsGear(static_cast<RE::TESBoundObject*>(form)) ? static_cast<RE::TESBoundObject*>(form) : nullptr;
				if (!object || player->GetItemCountInContainer(object) <= 0) {
					++missing;
					continue;
				}
				RE::ExtraDataList* xl = UnwornList(FindEntry(player, s.formID), s.ownData);
				// the last argument is the NoUnequip lock: true would make the piece impossible to take off again
				player->EquipObject(object, EquipCount(player, object), xl, false, false);
			}

			g_active = a_to;
			g_uiActive = a_to;
			SaveCharacter();
			push::Rebuild();
			logger::info("switch {} -> {}: {} kept, {} taken off, {} equipped, {} missing", Name(from), Name(a_to),
				worn.size() - takeOff.size(), takeOff.size(), wanted.size() - missing, missing);
			MessageBeep(MB_OK);
		}

		bool InCombat(RE::PlayerCharacter* a_player)
		{
			// IsInCombat / GetCombatTarget never report the player's fights. playerCombatList holds the actors fighting
			// the player and is empty right after the fight (numberActorsInCombat lags behind for seconds).
			if (a_player->playerCombatList) {
				for (RE::TESObjectREFR* ref : *a_player->playerCombatList) {
					if (ref) {
						return true;
					}
				}
			}
			return false;
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
		g_nameByIndex.fill({});
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return;
		}
		for (RE::TESFile* f : dh->listFiles) {
			if (f && f->GetCompileIndex() != 0xFF) {
				const std::string name = std::filesystem::path(f->filename).filename().string();
				g_indexByName[Lower(name)] = f->GetCompileIndex();
				g_nameByIndex[f->GetCompileIndex()] = name;
			}
		}
	}

	bool CleanName(const std::string& a_utf8, std::string& a_out, bool a_truncate)
	{
		auto cps = text::Decode(a_utf8);
		if (!cps || std::ranges::any_of(*cps, text::IsControl)) {
			return false;
		}
		const auto blank = [](char32_t c) { return c == U' ' || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200B); };
		const auto trim = [&] {
			while (!cps->empty() && blank(cps->back())) {
				cps->pop_back();
			}
		};
		cps->erase(cps->begin(), std::ranges::find_if_not(*cps, blank));
		trim();
		if (a_truncate && cps->size() > kMaxNameLength) {
			cps->resize(kMaxNameLength);
			trim();
		}
		if (cps->empty() || cps->size() > kMaxNameLength) {
			return false;
		}
		a_out = text::Encode(*cps);
		return true;
	}

	std::string DisplayName(int a_loadout)
	{
		if (a_loadout < 0 || a_loadout >= kCount) {
			return "?";
		}
		std::scoped_lock l(g_namesLock);
		return g_names[a_loadout].empty() ? Name(a_loadout) : g_names[a_loadout];
	}

	std::uint32_t NamesVersion()
	{
		return g_namesVersion.load();
	}

	bool SetName(const std::string& a_key, int a_loadout, const std::string& a_name)
	{
		std::string clean;
		if (a_key.empty() || a_loadout < 0 || a_loadout >= kCount || !CleanName(a_name, clean)) {
			return false;
		}
		const auto apply = [a_loadout, clean] {
			{
				std::scoped_lock l(g_namesLock);
				g_names[a_loadout] = clean;
			}
			++g_namesVersion;
		};
		apply();   // shown at once
		testhread::Post([a_key, a_loadout, clean, apply] {
			Use(a_key);
			apply();   // again: loading the character may have replaced the names
			SaveCharacter();
			logger::info("{} renamed to \"{}\"", Name(a_loadout), clean);
		});
		return true;
	}

	int ActiveForUi()
	{
		return g_uiActive.load();
	}

	void Sync(const std::string& a_key)
	{
		if (!a_key.empty()) {
			Use(a_key);
		}
	}

	bool Request(int a_to, std::string& a_why)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || a_to < 0 || a_to >= kCount) {
			a_why = "no player or no such loadout";
			return false;
		}
		if (!testhread::Installed() || !gamethread::Installed()) {
			a_why = "hooks not installed";
			return false;
		}
		if (InCombat(player)) {
			a_why = "in combat";
			gamethread::Post([] { console::Notify("You cannot change your loadout during combat."); });
			return false;
		}
		if (g_busy.exchange(true)) {
			a_why = "a switch is already running";
			return false;
		}
		// the character key is read on the game thread, the switch runs on the TES thread
		gamethread::Post([a_to] {
			std::string key = characterkey::Read();
			if (key.empty()) {
				g_busy = false;
				console::Notify(kNotIdentifiedMessage);
				MessageBeep(MB_ICONHAND);
				return;
			}
			testhread::Post([a_to, key = std::move(key)] { Switch(a_to, key); });
		});
		return true;
	}
}
