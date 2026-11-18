// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Simple Loadout System by ApocryphaRealm (see NOTICE.md): reading worn items, the Unequip / Equip calls
// and their rules. Changed: nothing is moved into containers; a loadout is a saved list of forms.

#include "Loadouts.h"

#include "CharacterKey.h"
#include "Console.h"
#include "Hook.h"
#include "Scan.h"
#include "GameThread.h"
#include "Push.h"
#include "TesThread.h"
#include "Text.h"

#include <cctype>
#include <fstream>
#include <optional>

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
		int                                           g_active = kNone;   // read from the loaded save's token
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

		// -------------------------------------------------------------------------------------------------------
		// diagnostics: what a piece is, which body slots it takes, and whether the worn set is consistent
		// -------------------------------------------------------------------------------------------------------

		std::uint16_t SlotMask(RE::TESBoundObject* a_object)
		{
			switch (a_object ? a_object->GetFormType() : RE::FormType::None) {
			case RE::FormType::Armor:
				return static_cast<RE::TESObjectARMO*>(a_object)->bipedModelData.bipedObjectSlots;
			case RE::FormType::Clothing:
				return static_cast<RE::TESObjectCLOT*>(a_object)->bipedModelData.bipedObjectSlots;
			default:
				return 0;
			}
		}

		std::string SlotNames(RE::TESBoundObject* a_object)
		{
			static constexpr const char* kSlots[16] = { "head", "hair", "upper body", "lower body", "hand", "foot", "right ring", "left ring",
				"amulet", "weapon", "back weapon", "side weapon", "quiver", "shield", "torch", "tail" };
			switch (a_object ? a_object->GetFormType() : RE::FormType::None) {
			case RE::FormType::Weapon:
				return "weapon";
			case RE::FormType::Ammo:
				return "ammo";
			case RE::FormType::Light:
				return "torch";
			default:
				break;
			}
			std::string out;
			const std::uint16_t mask = SlotMask(a_object);
			for (int i = 0; i < 16; ++i) {
				if (mask & (1u << i)) {
					out += out.empty() ? "" : ", ";
					out += kSlots[i];
				}
			}
			return out.empty() ? "no slot" : out;
		}

		std::string Describe(RE::TESBoundObject* a_object)
		{
			return a_object ? std::format("'{}' 0x{:08X} [{}]", NameOf(a_object), a_object->GetFormID(), SlotNames(a_object)) : std::string("(none)");
		}

		std::string Describe(const WornEntry& a_entry)
		{
			const bool left = a_entry.extra && a_entry.extra->GetExtraData(RE::EXTRA_DATA_TYPE::WornLeft);
			return std::format("{}{}{}", Describe(a_entry.item->object), HasOwnData(a_entry.extra) ? " own-data" : "", left ? " left" : "");
		}

		bool IsWornNow(RE::PlayerCharacter* a_player, std::uint32_t a_formID)
		{
			RE::ItemChange* item = FindEntry(a_player, a_formID);
			if (item && item->extraData) {
				for (RE::ExtraDataList* xl : *item->extraData) {
					if (IsWorn(xl)) {
						return true;
					}
				}
			}
			return false;
		}

		// -------------------------------------------------------------------------------------------------------
		// equip / unequip, done the way the inventory menu does it (game+0x676ADB5 in 1.512.105):
		//   EquipObject   (object, count, list, false, false)
		//   UnequipObject (object, count, list, true, false, false)
		//   after an unequip:  AfterUnequip(player, object, false)
		//   after both:        Refresh(player); player->currentProcess->vfunc[0x8A8 / 8](player, true, nullptr, false, false)
		//
		// Bare Equip/UnequipObject calls only flag the piece worn / not worn and leave the body model (BipedAnim)
		// stale. A robe put on by hand and then taken off by a switch stayed in the model's upper-body slot; the
		// cuirass put on next never reached the model: flagged as worn and counted for armour, but invisible, and the
		// slot open for a second cuirass (2026-11-19). The follow-up calls are what keeps the model in step.
		// -------------------------------------------------------------------------------------------------------

		struct MenuFollowUp
		{
			using AfterUnequip_t = void (*)(RE::Actor*, RE::TESBoundObject*, bool);
			using Refresh_t = void (*)(RE::Actor*);
			using ProcessUpdate_t = void (*)(void*, RE::Actor*, bool, void*, bool, bool);
			static constexpr std::size_t kProcessUpdateSlot = 0x8A8 / sizeof(void*);

			AfterUnequip_t afterUnequip = nullptr;
			Refresh_t      refresh = nullptr;
			bool           found = false;

			static const MenuFollowUp& Get()
			{
				static const MenuFollowUp s = [] {
					MenuFollowUp m;
					//  mov rdx,[rdi+10h] / xor r8d,r8d / mov rcx,player / call AfterUnequip / mov rcx,player / call Refresh /
					//  mov rdx,player / xor r9d,r9d / mov [rsp+28h],sil / movzx r8d,bl / mov [rsp+20h],sil /
					//  mov rcx,[rdx+138h] / mov rax,[rcx] / call [rax+8A8h]
					constexpr const char* kPattern = "48 8B 57 10 45 33 C0 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? "
					                                 "48 8B 15 ?? ?? ?? ?? 45 33 C9 40 88 74 24 28 44 0F B6 C3 40 88 74 24 20 "
					                                 "48 8B 8A 38 01 00 00 48 8B 01 FF 90 A8 08 00 00";
					const auto target = [](std::uint8_t* a_call) {
						return a_call + 5 + *reinterpret_cast<std::int32_t*>(a_call + 1);
					};
					if (std::uint8_t* site = scan::First(kPattern)) {
						m.afterUnequip = reinterpret_cast<AfterUnequip_t>(target(site + 14));
						m.refresh = reinterpret_cast<Refresh_t>(target(site + 26));
						m.found = true;
						logger::info("menu follow-up found: after-unequip game+0x{:X}, refresh game+0x{:X}", hook::Rva(m.afterUnequip), hook::Rva(m.refresh));
					} else {
						logger::error("menu follow-up not found in this game build - switched gear may not show on the character");
					}
					return m;
				}();
				return s;
			}

			void Run(RE::PlayerCharacter* a_player) const
			{
				refresh(a_player);
				if (void* process = a_player->currentProcess) {
					auto* update = reinterpret_cast<ProcessUpdate_t>((*reinterpret_cast<void***>(process))[kProcessUpdateSlot]);
					update(process, a_player, true, nullptr, false, false);
				}
			}
		};

		void Wear(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, std::int32_t a_count, RE::ExtraDataList* a_extra)
		{
			// the last argument is the NoUnequip lock: true would make the piece impossible to take off again
			a_player->EquipObject(a_object, a_count, a_extra, false, false);
			if (const auto& m = MenuFollowUp::Get(); m.found) {
				m.Run(a_player);
			}
		}

		void TakeOff(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, std::int32_t a_count, RE::ExtraDataList* a_extra)
		{
			// CommonLib's UnequipObject always passes false as the fourth argument: called directly
			using func_t = bool (RE::Actor::*)(RE::TESBoundObject*, std::int32_t, RE::ExtraDataList*, bool, bool, bool);
			static REL::Relocation<func_t> func{ RE::ID::Actor::UnequipObject };
			func(a_player, a_object, a_count, a_extra, true, false, false);
			if (const auto& m = MenuFollowUp::Get(); m.found) {
				m.afterUnequip(a_player, a_object, false);
				m.Run(a_player);
			}
		}

		// copies memory that may not be readable; no C++ objects in here (SEH)
		bool SafeCopy(const void* a_from, void* a_to, std::size_t a_size)
		{
			__try {
				std::memcpy(a_to, a_from, a_size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// What the body model (BipedAnim) references. CommonLib does not describe BipedAnim, so its memory is scanned
		// for pointers to carried items. Seen in 1.512.105: one 0x20-byte entry per body slot from +0x98 (hair +0xB8,
		// upper body +0xD8, ...). nullopt = no model or unreadable.
		struct ModelRef
		{
			std::size_t         offset;
			RE::TESBoundObject* object;
		};

		std::optional<std::vector<ModelRef>> ModelRefs(RE::PlayerCharacter* a_player)
		{
			constexpr std::size_t kScanBytes = 0x600;
			RE::BipedAnim*        biped = a_player ? a_player->GetBiped() : nullptr;
			std::array<std::uintptr_t, kScanBytes / sizeof(std::uintptr_t)> mem{};
			if (!biped || !SafeCopy(biped, mem.data(), sizeof(mem))) {
				return std::nullopt;
			}
			std::unordered_map<std::uintptr_t, RE::TESBoundObject*> carried;
			if (auto* items = Items(a_player)) {
				for (RE::ItemChange* item : *items) {
					if (item && item->object) {
						carried.emplace(reinterpret_cast<std::uintptr_t>(item->object), item->object);
					}
				}
			}
			std::vector<ModelRef> out;
			for (std::size_t i = 0; i < mem.size(); ++i) {
				if (const auto it = carried.find(mem[i]); it != carried.end()) {
					out.push_back({ i * sizeof(std::uintptr_t), it->second });
				}
			}
			return out;
		}

		std::string Describe(const std::optional<std::vector<ModelRef>>& a_refs)
		{
			if (!a_refs) {
				return "body model: unreadable";
			}
			std::string out;
			for (const auto& r : *a_refs) {
				out += std::format("{}+0x{:X} '{}'", out.empty() ? "" : ", ", r.offset, NameOf(r.object));
			}
			return "body model: " + (out.empty() ? std::string("nothing carried") : out);
		}

		// Logs a warning with the worn set and the body model when something is off:
		//   - two worn pieces claiming the same body slot (the engine never allows that)
		//   - a worn armour / clothing piece the body model does not reference (worn, but not shown)
		//   - with a_expected: expected pieces not worn, unexpected pieces worn
		// Silent when all is well.
		void CheckWorn(const std::string& a_label, const std::vector<std::uint32_t>* a_expected)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto               worn = WornEntries(player);
			const auto               model = ModelRefs(player);
			std::vector<std::string> problems;

			if (model) {
				for (const auto& w : worn) {
					// a shield next to a bow is worn but not shown; weapons, ammo and torches are not checked
					const std::uint16_t slots = SlotMask(w.item->object);
					const bool          shieldOnly = slots == (1u << 13);
					if (slots && !shieldOnly && std::ranges::none_of(*model, [&](const ModelRef& r) { return r.object == w.item->object; })) {
						problems.push_back(std::format("worn but not on the body model: {}", Describe(w)));
					}
				}
			}
			for (std::size_t i = 0; i < worn.size(); ++i) {
				for (std::size_t j = i + 1; j < worn.size(); ++j) {
					const std::uint16_t shared = SlotMask(worn[i].item->object) & SlotMask(worn[j].item->object);
					// two rings legitimately share the ring bits, one per hand
					const bool rings = (shared & ~0x00C0u) == 0;
					if (shared && !rings) {
						problems.push_back(std::format("slot clash: {} and {}", Describe(worn[i]), Describe(worn[j])));
					}
				}
			}
			if (a_expected) {
				for (const std::uint32_t id : *a_expected) {
					if (std::ranges::none_of(worn, [id](const WornEntry& w) { return w.item->object->GetFormID() == id; })) {
						auto* form = RE::TESForm::LookupByID(id);
						problems.push_back(std::format("expected but not worn: {}", Describe(static_cast<RE::TESBoundObject*>(form))));
					}
				}
				for (const auto& w : worn) {
					if (std::ranges::find(*a_expected, w.item->object->GetFormID()) == a_expected->end()) {
						problems.push_back(std::format("worn but not expected: {}", Describe(w)));
					}
				}
			}
			if (problems.empty()) {
				return;
			}
			std::string text;
			for (const auto& p : problems) {
				text += std::format("\n    ! {}", p);
			}
			for (const auto& w : worn) {
				text += std::format("\n    worn  {}", Describe(w));
			}
			logger::warn("{}: {} problem(s){}\n    {}", a_label, problems.size(), text, Describe(model));
		}

		// TES thread: run a_task on the first main loop iteration at or after a_at (GetTickCount64)
		void PostAt(ULONGLONG a_at, std::function<void()> a_task)
		{
			testhread::Post([a_at, task = std::move(a_task)]() mutable {
				if (GetTickCount64() >= a_at) {
					task();
				} else {
					PostAt(a_at, std::move(task));
				}
			});
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
		// the active loadout, kept in the save: GearLoadouts.esp holds a hidden token item (non-playable, nameless
		// clothing, never worn). The player carries active + 1 of it, so every save remembers which loadout the worn
		// gear belongs to. No token (plugin missing, or a save made before it): no loadout is active - the next switch
		// files the worn gear nowhere, it only puts the chosen loadout on.
		// -------------------------------------------------------------------------------------------------------

		constexpr const char*   kPlugin = "GearLoadouts.esp";
		constexpr std::uint32_t kTokenID = 0x800;   // keep in sync with tools/make_esp.py

		RE::TESBoundObject* Token()
		{
			static bool warned = false;
			const auto  it = g_indexByName.find(Lower(kPlugin));
			auto*       form = it != g_indexByName.end() ? RE::TESForm::LookupByID((static_cast<std::uint32_t>(it->second) << 24) | kTokenID) : nullptr;
			if (!form || form->GetFormType() != RE::FormType::Clothing) {
				if (!warned) {
					logger::warn("{} not loaded - no loadout is ever active, so worn gear is never saved into one", kPlugin);
					warned = true;
				}
				return nullptr;
			}
			return static_cast<RE::TESBoundObject*>(form);
		}

		// the active loadout stored in the loaded save, -1 = none
		int ReadToken(RE::PlayerCharacter* a_player)
		{
			auto* token = a_player ? Token() : nullptr;
			const std::int32_t count = token ? a_player->GetItemCountInContainer(token) : 0;
			return count >= 1 && count <= kCount ? count - 1 : -1;
		}

		void WriteToken(RE::PlayerCharacter* a_player, int a_active)
		{
			auto* token = a_player ? Token() : nullptr;
			if (!token) {
				return;
			}
			const std::int32_t have = a_player->GetItemCountInContainer(token);
			const std::int32_t want = a_active + 1;
			if (have < want) {
				a_player->AddObjectToContainer(token, nullptr, want - have);
			} else if (have > want) {
				a_player->RemoveItem(token, nullptr, have - want, false, false, nullptr, nullptr, nullptr, true, false);
			}
			if (const std::int32_t now = a_player->GetItemCountInContainer(token); now != want) {
				logger::error("active loadout token: carrying {} instead of {}", now, want);
			}
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
			g_active = kNone;
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
				logger::info("loaded loadouts of {}", a_key);
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
				// the active loadout lives in the save (token), not here
				g_root["characters"][g_key] = { { "names", std::move(names) }, { "loadouts", std::move(all) } };

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

		// TES thread: make a_key the loaded character; the loaded save decides which loadout is active
		void Use(const std::string& a_key)
		{
			if (g_indexByName.empty()) {
				Init();
			}
			if (a_key != g_key) {
				LoadCharacter(a_key);
			}
			if (const int saved = ReadToken(RE::PlayerCharacter::GetSingleton()); saved != g_active) {
				logger::info("the loaded save has {} active (was {})", Name(saved), Name(g_active));
				g_active = saved;
			}
			g_uiActive = g_active;
		}

		// -------------------------------------------------------------------------------------------------------
		// the switch (TES thread)
		// -------------------------------------------------------------------------------------------------------

		constexpr ULONGLONG kCheckDelayMs = 500;   // the worn set is checked this long after a switch

		std::uint32_t g_switches = 0;   // TES thread

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
			const int           from = g_active;
			const std::uint32_t n = ++g_switches;
			const std::string   tag = std::format("switch #{} {} -> {}", n, Name(from), Name(a_to));
			std::string         steps;   // one line per piece, logged with the summary

			// 1. what is worn now becomes the old loadout; pieces the new one also holds stay on
			const auto             worn = WornEntries(player);
			std::vector<Saved>     wanted = g_loadouts[a_to];
			std::vector<Saved>     now;
			std::vector<WornEntry> takeOff;
			std::vector<std::uint32_t> expected;   // worn once the switch is done
			for (const auto& w : worn) {
				const std::uint32_t id = w.item->object->GetFormID();
				now.push_back({ id, HasOwnData(w.extra) });
				if (const auto it = std::ranges::find(wanted, id, &Saved::formID); it != wanted.end()) {
					wanted.erase(it);
					expected.push_back(id);
					steps += std::format("\n    keep  {}", Describe(w));
				} else {
					takeOff.push_back(w);
				}
			}
			if (from != kNone) {
				g_loadouts[from] = std::move(now);
			} else {
				steps += "\n    (no loadout was active: the worn gear is not saved anywhere)";
			}

			// 2. take off the rest
			for (const auto& w : takeOff) {
				const std::string what = Describe(w);   // before: the extra data list may be freed by the unequip
				TakeOff(player, w.item->object, EquipCount(player, w.item->object), w.extra);
				steps += std::format("\n    off   {}{}", what, IsWornNow(player, w.item->object->GetFormID()) ? " -> still worn" : "");
			}

			// 3. put on what the new loadout holds and is still carried
			std::size_t missing = 0;
			for (const auto& s : wanted) {
				auto* form = RE::TESForm::LookupByID(s.formID);
				auto* object = form && IsGear(static_cast<RE::TESBoundObject*>(form)) ? static_cast<RE::TESBoundObject*>(form) : nullptr;
				if (!object || player->GetItemCountInContainer(object) <= 0) {
					++missing;
					steps += std::format("\n    miss  0x{:08X} {}", s.formID, !form ? "form not found" : !object ? "not gear" : std::format("{} not carried", Describe(object)));
					continue;
				}
				RE::ExtraDataList* xl = UnwornList(FindEntry(player, s.formID), s.ownData);
				Wear(player, object, EquipCount(player, object), xl);
				expected.push_back(s.formID);
				steps += std::format("\n    on    {} (copy: {}){}", Describe(object), xl ? (s.ownData ? "own data" : "plain") : "engine's pick",
					IsWornNow(player, s.formID) ? "" : " -> not worn");
			}

			g_active = a_to;
			g_uiActive = a_to;
			WriteToken(player, a_to);
			SaveCharacter();
			push::Rebuild();
			logger::info("{}: {} kept, {} taken off, {} equipped, {} missing{}", tag, worn.size() - takeOff.size(), takeOff.size(),
				wanted.size() - missing, missing, steps);
			MessageBeep(MB_OK);

			// once the engine had time to settle (the body model follows a frame later); a newer switch makes it moot
			PostAt(GetTickCount64() + kCheckDelayMs, [n, tag, expected = std::move(expected)] {
				if (n == g_switches) {
					CheckWorn(tag, &expected);
				}
			});
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
		return a_loadout >= 0 && a_loadout < kCount ? kNames[a_loadout] : a_loadout == kNone ? "no loadout" : "?";
	}

	void Init()
	{
		MenuFollowUp::Get();   // scan now, not on the first switch; logs whether it was found
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
			CheckWorn("inventory opened", nullptr);   // logs only a slot clash
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
