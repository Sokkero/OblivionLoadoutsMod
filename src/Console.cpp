// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Simple Loadout System by ApocryphaRealm (see NOTICE.md). Changed: built on ui::Call.

#include "Console.h"

#include "Reflect.h"
#include "ui/UiCall.h"

namespace console
{
	bool Notify(const std::string& a_text)
	{
		auto*        kismet = reflect::SelfCheck() ? ui::Class(L"/Script/Engine.KismetSystemLibrary") : nullptr;
		auto*        cdo = kismet ? kismet->GetDefaultObject(false) : nullptr;
		auto*        ctrl = cdo ? ui::PlayerController() : nullptr;
		ui::Call     exec(cdo, L"ExecuteConsoleCommand");
		if (!ctrl || !exec) {
			logger::warn("console: not ready, message not shown: {}", a_text);
			return false;
		}
		std::wstring command = L"Message \"";
		for (const char c : a_text) {
			if (c != '"' && c >= 0x20 && c < 0x7F) {
				command += static_cast<wchar_t>(c);
			}
		}
		command += L'"';
		struct FStr
		{
			wchar_t*     data;
			std::int32_t num;
			std::int32_t max;
		};
		// the FString borrows our buffer for the duration of the call
		const auto len = static_cast<std::int32_t>(command.size() + 1);
		exec.Set("WorldContextObject", ctrl);
		exec.Set("Command", FStr{ command.data(), len, len });
		exec.Set("SpecificPlayer", ctrl);
		return exec.Run();
	}
}
