// Gear Loadouts - the engine's console from C++.
// Adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm, GPL-3.0-or-later (see NOTICE.md).
// Modified 2026-11-17: Console.cpp and the needed parts of Reflect.cpp merged into one file; Notify() added.

#include "Console.h"

namespace console
{
	namespace
	{
		// ---------------------------------------------------------------------------------------------------
		// reflection (from the original's Reflect.cpp): CommonLibOB64 has no FProperty, so walk a struct's FField
		// chain (next +0x18, name +0x20) and read FProperty::Offset_Internal at +0x44 (UE5's layout). The layout is
		// proven on a property whose place is known before anything trusts it (SelfCheck).
		// ---------------------------------------------------------------------------------------------------
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kPropertyOffsetInternal = 0x44;

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
			for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
				for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
					const auto& name = *reinterpret_cast<const UE::FName*>(f + kFieldName);
					if (Utf8(name.ToString()) == a_name) {
						return *reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal);
					}
				}
			}
			return -1;
		}

		std::atomic<int> g_layout{ 0 };   // 0 unchecked, 1 proven, -1 failed

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
				logger::info("console: property offsets proven (VQuickKeysMenuViewModel KeyIndex at 0x{:X})", keyIndex);
			} else {
				logger::error("console: KeyIndex read at 0x{:X}, expected 0xD0 - the property layout is not UE5's; no console commands", keyIndex);
			}
			return g_layout.load() > 0;
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

		// ---------------------------------------------------------------------------------------------------
		// the console (from the original's Console.cpp)
		// ---------------------------------------------------------------------------------------------------
		constexpr const wchar_t* kKismetSystemPath = L"/Script/Engine.KismetSystemLibrary";
		constexpr const wchar_t* kPlayerClassPath = L"/Game/Dev/PlayerBlueprints/BP_OblivionPlayerCharacter.BP_OblivionPlayerCharacter_C";

		UE::UObject*   g_kismetCdo = nullptr;
		UE::UFunction* g_fnExec = nullptr;
		std::int32_t   g_offWorld = -1, g_offCommand = -1, g_offPlayer = -1, g_frame = 0;
		UE::UClass*    g_playerClass = nullptr;
		std::int32_t   g_offController = -1;
		std::string    g_problem;

		UE::UObject* PlayerController()
		{
			if (!g_playerClass) {
				g_playerClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPlayerClassPath);
				if (g_playerClass) {
					g_offController = Offset(g_playerClass, "Controller");
				}
			}
			if (!g_playerClass || g_offController < 0) {
				return nullptr;
			}
			for (UE::UObject* pawn : Instances(g_playerClass)) {
				auto** ctrl = reinterpret_cast<UE::UObject**>(reinterpret_cast<std::uint8_t*>(pawn) + g_offController);
				if (*ctrl && IsLive(*ctrl)) {
					return *ctrl;
				}
			}
			return nullptr;
		}

		bool Ready()
		{
			if (g_fnExec) {
				return true;
			}
			if (!SelfCheck()) {
				g_problem = g_layout.load() < 0 ? "the property layout is not UE5's" : "the property layout is not checked yet";
				return false;
			}
			auto* k = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kKismetSystemPath);
			if (!k) {
				g_problem = "KismetSystemLibrary not found";
				return false;
			}
			g_kismetCdo = k->GetDefaultObject(false);
			auto* fn = g_kismetCdo ? g_kismetCdo->FindFunction(UE::FName(L"ExecuteConsoleCommand", UE::EFindName::Find)) : nullptr;
			if (!fn) {
				g_problem = "ExecuteConsoleCommand not found on KismetSystemLibrary";
				return false;
			}
			auto* st = reinterpret_cast<UE::UStruct*>(fn);
			g_offWorld = Offset(st, "WorldContextObject");
			g_offCommand = Offset(st, "Command");
			g_offPlayer = Offset(st, "SpecificPlayer");
			g_frame = st->propertiesSize;
			if (g_offWorld < 0 || g_offCommand < 0 || g_offPlayer < 0 || g_frame < 32 || g_frame > 64 || g_offWorld + 8 > g_frame ||
				g_offCommand + 16 > g_frame || g_offPlayer + 8 > g_frame) {
				g_problem = std::format("ExecuteConsoleCommand's frame is not the expected shape (world @{}, command @{}, player @{}, frame {})",
					g_offWorld, g_offCommand, g_offPlayer, g_frame);
				logger::error("console: {}", g_problem);
				return false;
			}
			g_fnExec = fn;
			logger::info("console: ExecuteConsoleCommand resolved (frame {} bytes: world @{}, command @{}, player @{})", g_frame, g_offWorld,
				g_offCommand, g_offPlayer);
			return true;
		}
	}

	bool Run(const std::string& a_command)
	{
		if (!Ready()) {
			logger::warn("console: '{}' not run: {}", a_command, g_problem);
			return false;
		}
		UE::UObject* ctrl = PlayerController();
		if (!ctrl) {
			g_problem = "no player controller yet";
			logger::warn("console: '{}' not run: {}", a_command, g_problem);
			return false;
		}
		// The FString's buffer points at our own wide string for the duration of the call (taken by const reference).
		std::wstring w(a_command.begin(), a_command.end());   // ASCII commands only
		struct FStr
		{
			wchar_t*     data;
			std::int32_t num;
			std::int32_t max;
		};
		alignas(16) std::uint8_t params[64]{};
		*reinterpret_cast<UE::UObject**>(params + g_offWorld) = ctrl;
		*reinterpret_cast<FStr*>(params + g_offCommand) = FStr{ w.data(), static_cast<std::int32_t>(w.size() + 1), static_cast<std::int32_t>(w.size() + 1) };
		*reinterpret_cast<UE::UObject**>(params + g_offPlayer) = ctrl;
		g_kismetCdo->ProcessEvent(g_fnExec, params);
		logger::info("console: ran '{}'", a_command);
		return true;
	}

	bool Notify(const std::string& a_text)
	{
		std::string clean;
		for (char c : a_text) {
			if (c != '"' && static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F) {
				clean += c;   // no quotes (they end the argument), ASCII only
			}
		}
		return Run(std::format("Message \"{}\"", clean));
	}

	std::string Status()
	{
		return g_fnExec ? "ready" : (g_problem.empty() ? "not resolved yet" : g_problem);
	}
}
