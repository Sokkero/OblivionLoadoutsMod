// Gear Loadouts - DEV TOOL: widget census (see Census.h).
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).

#include "devtools/Census.h"

#include "GameThread.h"
#include "Reflect.h"
#include "ui/UiCall.h"

#include <ShlObj.h>
#include <fstream>

namespace census
{
	namespace
	{
		struct Node
		{
			UE::UObject* widget;
			UE::UObject* slot;    // the PanelSlot that holds it, nullptr for a tree root
			int          depth;
			int          index;   // its index in the parent panel, -1 when not in a panel
		};

		struct RawArray
		{
			UE::UObject** data;
			std::int32_t  num;
			std::int32_t  max;
		};

		UE::UObject* Prop(UE::UObject* a_object, const char* a_name)
		{
			const int offset = reflect::Offset(a_object->GetClass(), a_name);
			auto**    at = offset >= 0 ? reflect::At<UE::UObject*>(a_object, offset) : nullptr;
			return at ? *at : nullptr;
		}

		int Visibility(UE::UObject* a_widget)   // ESlateVisibility: 0 visible, 1 collapsed, 2 hidden, 3/4 hit-test invisible
		{
			const int   offset = reflect::Offset(a_widget->GetClass(), "Visibility");
			const auto* at = offset >= 0 ? reflect::At<std::uint8_t>(a_widget, offset) : nullptr;
			return at ? *at : -1;
		}

		// every widget under a_widget; collapsed and hidden ones are listed but not entered (what is on screen)
		void Walk(UE::UObject* a_widget, UE::UObject* a_slot, int a_depth, int a_index, int& a_budget, std::vector<Node>& a_out)
		{
			static auto* userWidget = ui::Class(L"/Script/UMG.UserWidget");
			static auto* panelWidget = ui::Class(L"/Script/UMG.PanelWidget");
			if (!a_widget || a_depth > 48 || a_budget <= 0) {
				return;
			}
			--a_budget;
			a_out.push_back({ a_widget, a_slot, a_depth, a_index });
			const int visibility = Visibility(a_widget);
			if (visibility == 1 || visibility == 2) {
				return;
			}
			if (userWidget && a_widget->GetClass()->IsChildOf(userWidget)) {
				auto* tree = Prop(a_widget, "WidgetTree");
				if (auto* root = tree ? Prop(tree, "RootWidget") : nullptr) {
					Walk(root, nullptr, a_depth + 1, -1, a_budget, a_out);
				}
			}
			if (panelWidget && a_widget->GetClass()->IsChildOf(panelWidget)) {
				const int   offset = reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots");
				const auto* slots = offset >= 0 ? reflect::At<RawArray>(a_widget, offset) : nullptr;
				for (std::int32_t i = 0; slots && slots->data && i < slots->num && i < 512; ++i) {
					if (auto* slot = slots->data[i]) {
						Walk(Prop(slot, "Content"), slot, a_depth + 1, i, a_budget, a_out);
					}
				}
			}
			// a CommonUI activatable stack is not a panel: its menus are the shown DisplayedWidget, or every WidgetList entry
			if (const int listOffset = reflect::Offset(a_widget->GetClass(), "WidgetList"); listOffset >= 0) {
				if (auto* shown = Prop(a_widget, "DisplayedWidget")) {
					Walk(shown, nullptr, a_depth + 1, -1, a_budget, a_out);
				} else {
					const auto* list = reflect::At<RawArray>(a_widget, listOffset);
					for (std::int32_t i = 0; list && list->data && i < list->num && i < 32; ++i) {
						Walk(list->data[i], nullptr, a_depth + 1, -1, a_budget, a_out);
					}
				}
			}
		}

		bool WalkGuarded(UE::UObject* a_root, int a_budget, std::vector<Node>* a_out)
		{
			__try {
				Walk(a_root, nullptr, 0, -1, a_budget, *a_out);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		std::string Line(const Node& a_node)
		{
			static auto* textClass = ui::Class(L"/Script/UMG.TextLayoutWidget");
			auto*        type = a_node.widget->GetClass();
			std::string  line = std::format("{}{} [{}] vis={}", std::string(static_cast<std::size_t>(a_node.depth) * 2, ' '),
				 ui::Name(a_node.widget), ui::Name(type), Visibility(a_node.widget));
			if (a_node.slot) {
				line += std::format(" slot={}#{}", ui::Name(a_node.slot->GetClass()), a_node.index);
			}
			if (textClass && type->IsChildOf(textClass)) {
				auto text = ui::GetText(a_node.widget);
				if (text.size() > 60) {
					text = text.substr(0, 60) + "...";
				}
				line += std::format(" text=\"{}\"", text);
			}
			return line;
		}

		std::filesystem::path OutputFile()
		{
			PWSTR                 documents = nullptr;
			std::filesystem::path folder;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents) {
				folder = documents;
			}
			CoTaskMemFree(documents);
			folder /= L"My Games/Oblivion Remastered/OBSE/Logs";
			const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
			return folder / std::format("GearLoadouts-census-{:%Y%m%d-%H%M%S}.txt", now);
		}

		void Write()
		{
			if (!reflect::SelfCheck()) {
				logger::warn("census: the property layout is not proven - no census");
				return;
			}
			auto* layoutClass = ui::Class(L"/Game/UI/WBP_PrimaryGameLayout.WBP_PrimaryGameLayout_C");
			if (!layoutClass) {
				logger::warn("census: WBP_PrimaryGameLayout_C is not loaded");
				return;
			}
			std::vector<Node> nodes;
			bool              faulted = false;
			for (auto* layout : reflect::Instances(layoutClass)) {
				if (reflect::Usable(layout) && Prop(layout, "WidgetTree") && !WalkGuarded(layout, 20000, &nodes)) {
					faulted = true;
				}
			}
			const auto    path = OutputFile();
			std::ofstream out(path);
			if (!out) {
				logger::warn("census: cannot write {}", path.string());
				return;
			}
			out << "Gear Loadouts widget census (dev tool, Ctrl+Shift+F11). Widgets of WBP_PrimaryGameLayout; collapsed (vis=1)\n"
				   "and hidden (vis=2) ones are listed but not entered. slot=<slot class>#<index in the parent panel>.\n";
			if (faulted) {
				out << "NOTE: the walk hit an access violation and stopped early (a widget was being built or destroyed).\n";
			}
			std::size_t written = 0;
			for (const auto& node : nodes) {
				if (reflect::Usable(node.widget)) {
					out << Line(node) << '\n';
					++written;
				}
			}
			logger::info("census: {} widgets written to {}", written, path.string());
		}
	}

	void Request()
	{
		if (!gamethread::Installed()) {
			logger::warn("census: no game thread hook");
			return;
		}
		gamethread::Post([] { Write(); });
	}
}
