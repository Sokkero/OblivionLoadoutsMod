// SPDX-License-Identifier: GPL-3.0-or-later
// Colours and fonts follow Menus Revamped by cfa911 (see NOTICE.md).

#include "ui/LoadoutBar.h"

#include "CharacterKey.h"
#include "Console.h"
#include "Loadouts.h"
#include "Reflect.h"
#include "TesThread.h"
#include "ui/UiCall.h"
#include "ui/UiWidgets.h"
#include "ui/WindowInput.h"

namespace loadoutbar
{
	namespace
	{
		// layout, in canvas units
		constexpr double kRowHeight = 40.0;
		constexpr double kTopGap = 10.0;   // clear of the tab strip
		constexpr double kGap = 6.0;
		constexpr double kPopupHeight = 150.0;
		constexpr float  kButtonFont = 17.0f;
		constexpr float  kTitleFont = 19.0f;
		constexpr float  kFieldFont = 18.0f;

		// the game's UI fonts: Kingthings for headings, Robinson for lists
		struct Font
		{
			const wchar_t* asset;
			const wchar_t* face;
			float          nudge;   // Kingthings sits high in its line box: pushed down by uneven padding
		};
		constexpr Font kHeading{ L"/Game/UI/CommonStyle/Fonts/Font_Kingthings_Localized.Font_Kingthings_Localized", L"Regular-Regular", 6.0f };
		constexpr Font kText{ L"/Game/UI/CommonStyle/Fonts/Font_Robinson_Localized.Font_Robinson_Localized", nullptr, 0.0f };

		// colours (linear RGBA), Menus Revamped's parchment palette
		constexpr float kInk[4] = { 0.20f, 0.14f, 0.09f, 1.0f };
		constexpr float kInkStrong[4] = { 0.06f, 0.035f, 0.016f, 1.0f };
		constexpr float kInkDim[4] = { 0.34f, 0.27f, 0.19f, 0.45f };
		constexpr float kLine[4] = { 0.20f, 0.14f, 0.09f, 0.85f };
		constexpr float kPaper[4] = { 0.672443f, 0.630757f, 0.577580f, 1.0f };
		constexpr float kPaperLight[4] = { 0.82f, 0.77f, 0.68f, 1.0f };
		constexpr float kSelection[4] = { 0.78f, 0.56f, 0.22f, 0.55f };
		constexpr float kButtonFill[4] = { 0.30f, 0.24f, 0.16f, 0.30f };
		constexpr float kButtonFillDisabled[4] = { 0.30f, 0.24f, 0.16f, 0.10f };

		// UMG enum values
		constexpr std::uint8_t kVisible = 0;    // ESlateVisibility: drawn and hit-testable (the popup blocks the list behind)
		constexpr std::uint8_t kCollapsed = 1;
		constexpr std::uint8_t kAlignFill = 0;   // EHorizontalAlignment
		constexpr std::uint8_t kAlignCenter = 2;

		const std::wstring kPrefix = L"GearLoadouts_";

		// /Script/UMG.AnchorData: FMargin offsets, FAnchors (min, max), alignment
		struct AnchorData
		{
			float  left, top, right, bottom;
			double minX, minY, maxX, maxY;
			double alignX, alignY;
		};
		static_assert(sizeof(AnchorData) == 64);

		struct Vec2
		{
			double x, y;
		};

		bool ReadLayout(UE::UObject* a_canvasSlot, AnchorData& a_out)
		{
			ui::Call get(a_canvasSlot, L"GetLayout");
			if (get.Size("ReturnValue") != sizeof(AnchorData) || !get.Run()) {
				return false;
			}
			a_out = get.Get<AnchorData>("ReturnValue");
			return true;
		}

		void WriteLayout(UE::UObject* a_canvasSlot, const AnchorData& a_layout, std::int32_t a_zOrder)
		{
			ui::Call set(a_canvasSlot, L"SetLayout");
			if (set.Size("InLayoutData") == sizeof(AnchorData) && set.Set("InLayoutData", a_layout)) {
				set.Run();
			}
			ui::CallFirst(a_canvasSlot, L"SetZOrder", a_zOrder);
		}

		void SetVisibility(UE::UObject* a_widget, std::uint8_t a_visibility)
		{
			if (a_widget) {
				ui::CallFirst(a_widget, L"SetVisibility", a_visibility);
			}
		}

		void SetPadding(UE::UObject* a_widgetOrSlot, float a_left, float a_top, float a_right, float a_bottom)
		{
			const float padding[4] = { a_left, a_top, a_right, a_bottom };
			ui::CallFirst(a_widgetOrSlot, L"SetPadding", padding);
		}

		// a Border holding a TextBlock named <name>_Text, so both can be found again later
		UE::UObject* LabelledBorder(UE::UObject* a_tree, const std::wstring& a_name, const std::string& a_text, const Font& a_font, float a_size,
			UE::UObject** a_label = nullptr, bool a_centred = true)
		{
			auto* border = ui::NewWidget(a_tree, L"/Script/UMG.Border", (kPrefix + a_name).c_str());
			auto* label = ui::NewWidget(a_tree, L"/Script/UMG.TextBlock", (kPrefix + a_name + L"_Text").c_str());
			if (!border || !label) {
				return nullptr;
			}
			ui::CallFirst(border, L"SetContent", label);
			ui::CallFirst(border, L"SetHorizontalAlignment", a_centred ? kAlignFill : kAlignCenter);   // fill: long names end in "..."
			ui::CallFirst(border, L"SetVerticalAlignment", kAlignCenter);
			if (a_centred) {
				ui::CallFirst(label, L"SetJustification", std::uint8_t{ 1 });   // ETextJustify::Center
			}
			if (a_font.nudge > 0.0f) {
				SetPadding(border, 4.0f, 2.0f + a_font.nudge, 4.0f, 2.0f - a_font.nudge / 4.0f);
			}
			ui::SetText(label, a_text);
			ui::SetFont(label, a_font.asset, a_font.face, a_size);
			ui::CallFirst(label, L"SetClipping", std::uint8_t{ 1 });             // ClipToBounds
			ui::CallFirst(label, L"SetTextOverflowPolicy", std::uint8_t{ 1 });   // Ellipsis
			if (a_label) {
				*a_label = label;
			}
			return border;
		}

		// adds a child to a Horizontal/VerticalBox; a_fill: share the free space
		void AddToBox(UE::UObject* a_box, const wchar_t* a_addFunction, UE::UObject* a_child, bool a_fill, float a_left, float a_top, float a_right)
		{
			ui::Call add(a_box, a_addFunction);
			add.Set("Content", a_child);
			add.Run();
			if (auto* slot = add.Get<UE::UObject*>("ReturnValue")) {
				struct SlateChildSize
				{
					float        value;
					std::uint8_t rule;   // 0 Automatic, 1 Fill
					std::uint8_t pad[3];
				} size{ 1.0f, static_cast<std::uint8_t>(a_fill), {} };
				ui::CallFirst(slot, L"SetSize", size);
				SetPadding(slot, a_left, a_top, a_right, 0.0f);
			}
		}

		enum class State
		{
			Normal,
			Hover,
			Active,
			ActiveHover
		};

		void Paint(UE::UObject* a_cell, UE::UObject* a_label, State a_state)
		{
			struct Look
			{
				float fill[4];
				float line[4];
				float ink[4];
			};
			static constexpr Look kLooks[] = {
				{ { 0.55f, 0.47f, 0.38f, 0.18f }, { 0.20f, 0.14f, 0.09f, 0.55f }, { 0.20f, 0.14f, 0.09f, 1.0f } },
				{ { 0.7913f, 0.7758f, 0.7379f, 1.0f }, { 0.20f, 0.14f, 0.09f, 0.85f }, { 0.06f, 0.035f, 0.016f, 1.0f } },
				{ { 0.30f, 0.19f, 0.08f, 0.92f }, { 0.12f, 0.07f, 0.03f, 1.0f }, { 0.94f, 0.89f, 0.77f, 1.0f } },
				{ { 0.38f, 0.25f, 0.11f, 0.95f }, { 0.12f, 0.07f, 0.03f, 1.0f }, { 1.0f, 0.95f, 0.84f, 1.0f } },
			};
			const auto& look = kLooks[static_cast<std::size_t>(a_state)];
			ui::RoundedBorder(a_cell, look.fill, look.line, 1.0f, 4.0f);
			if (a_label) {
				ui::Ink(a_label, look.ink);
			}
		}

		// Border > VerticalBox > title, field, [Save, Cancel]; starts collapsed
		void BuildPopup(UE::UObject* a_canvas, AnchorData a_layout)
		{
			auto*        tree = a_canvas->GetOuter();
			UE::UObject* popupSlot = nullptr;
			auto*        popup = ui::AddToCanvas(a_canvas, L"/Script/UMG.Border", (kPrefix + L"Popup").c_str(), &popupSlot);
			auto*        column = ui::NewWidget(tree, L"/Script/UMG.VerticalBox", (kPrefix + L"PopupColumn").c_str());
			auto*        title = ui::NewWidget(tree, L"/Script/UMG.TextBlock", (kPrefix + L"PopupTitle").c_str());
			auto*        buttons = ui::NewWidget(tree, L"/Script/UMG.HorizontalBox", (kPrefix + L"PopupButtons").c_str());
			UE::UObject* saveLabel = nullptr;
			UE::UObject* cancelLabel = nullptr;
			auto*        field = LabelledBorder(tree, L"Field", "", kText, kFieldFont, nullptr, false);
			auto*        save = LabelledBorder(tree, L"Save", "Save", kHeading, kButtonFont, &saveLabel);
			auto*        cancel = LabelledBorder(tree, L"Cancel", "Cancel", kHeading, kButtonFont, &cancelLabel);
			if (!popup || !column || !title || !buttons || !field || !save || !cancel) {
				logger::warn("ui: rename popup could not be built");
				return;
			}
			ui::RoundedBorder(popup, kPaper, kLine, 2.0f, 6.0f);
			SetPadding(popup, 14.0f, 10.0f, 14.0f, 10.0f);
			ui::CallFirst(popup, L"SetContent", column);
			ui::SetFont(title, kHeading.asset, kHeading.face, kTitleFont);
			ui::Ink(title, kInkStrong);
			ui::RoundedBorder(field, kPaperLight, kLine, 1.0f, 4.0f);
			SetPadding(field, 8.0f, 4.0f, 8.0f, 4.0f);
			ui::RoundedBorder(save, kButtonFill, kLine, 1.0f, 4.0f);
			ui::RoundedBorder(cancel, kButtonFill, kLine, 1.0f, 4.0f);
			ui::Ink(saveLabel, kInkStrong);
			ui::Ink(cancelLabel, kInkStrong);
			AddToBox(column, L"AddChildToVerticalBox", title, false, 0, 0, 0);
			AddToBox(column, L"AddChildToVerticalBox", field, true, 0, 8, 0);
			AddToBox(column, L"AddChildToVerticalBox", buttons, false, 0, 8, 0);
			AddToBox(buttons, L"AddChildToHorizontalBox", save, true, 0, 0, 6);
			AddToBox(buttons, L"AddChildToHorizontalBox", cancel, true, 6, 0, 0);

			// right below the row, as wide as the row, above everything
			a_layout.top = static_cast<float>(a_layout.top + kRowHeight + 4.0);
			a_layout.bottom = static_cast<float>(kPopupHeight);
			WriteLayout(popupSlot, a_layout, 4000);
			SetVisibility(popup, kCollapsed);
		}

		// true when the row exists (built now or before)
		bool Build(UE::UObject* a_inventory, bool a_logProblems)
		{
			const auto fail = [&](const char* a_why) {
				if (a_logProblems) {
					logger::warn("ui: row not built: {}", a_why);
				}
				return false;
			};
			auto* mainPart = ui::FindInTree(a_inventory, "inv_main_part");
			auto* canvas = ui::TreeRoot(mainPart);
			auto* vbox = ui::FindInTree(mainPart, "inv_verticalbox");
			if (!canvas || !vbox || ui::Name(canvas->GetClass()) != "CanvasPanel") {
				return fail("inventory layout not found");
			}
			if (ui::FindInTree(mainPart, "GearLoadouts_Row")) {
				return true;
			}
			const auto slots = ui::Slots(vbox);
			auto*      spacer = slots.empty() ? nullptr : ui::Content(slots.front());
			if (!spacer || ui::Name(spacer->GetClass()) != "Spacer") {
				return fail("first child of inv_verticalbox is not a Spacer");
			}
			AnchorData vboxLayout{};
			if (!ReadLayout(ui::Prop(vbox, "Slot"), vboxLayout)) {
				return fail("inv_verticalbox layout unreadable");
			}
			const auto* spacerSize = reflect::At<Vec2>(spacer, reflect::Offset(spacer->GetClass(), "Size"));
			if (!spacerSize || reflect::Size(spacer->GetClass(), "Size") != sizeof(Vec2)) {
				return fail("Spacer size unreadable");
			}
			const Vec2 oldSpacer = *spacerSize;

			UE::UObject* rowSlot = nullptr;
			auto*        row = ui::AddToCanvas(canvas, L"/Script/UMG.HorizontalBox", (kPrefix + L"Row").c_str(), &rowSlot);
			if (!row) {
				return fail("row could not be created");
			}
			for (int i = 0; i < loadouts::kCount; ++i) {
				UE::UObject* label = nullptr;
				if (auto* cell = LabelledBorder(canvas->GetOuter(), std::format(L"Cell{}", i), loadouts::DisplayName(i), kHeading, kButtonFont, &label)) {
					AddToBox(row, L"AddChildToHorizontalBox", cell, true, i == 0 ? 0.0f : 3.0f, 0.0f, i == loadouts::kCount - 1 ? 0.0f : 3.0f);
					Paint(cell, label, State::Normal);
				}
			}

			// the row goes where the Spacer ends; the Spacer grows to push the table down
			AnchorData rowLayout = vboxLayout;
			rowLayout.maxY = rowLayout.minY;   // no vertical stretch: bottom is the height
			rowLayout.alignY = 0.0;
			const double vboxTop = vboxLayout.top - (vboxLayout.minY == vboxLayout.maxY ? vboxLayout.alignY * vboxLayout.bottom : 0.0);
			rowLayout.top = static_cast<float>(vboxTop + oldSpacer.y + kTopGap);
			rowLayout.bottom = static_cast<float>(kRowHeight);
			WriteLayout(rowSlot, rowLayout, 10);
			ui::CallFirst(spacer, L"SetSize", Vec2{ oldSpacer.x, oldSpacer.y + kTopGap + kRowHeight + kGap });

			BuildPopup(canvas, rowLayout);
			logger::info("ui: row built into {}", ui::Name(a_inventory));
			return true;
		}

		// ---------------------------------------------------------------------------------------------------------
		// state of the open inventory
		// ---------------------------------------------------------------------------------------------------------

		struct Seen
		{
			reflect::Handle widget;
			bool            built = false;
			bool            logged = false;
		};
		std::vector<Seen> g_seen;   // inventory widgets prepared so far

		struct Labelled
		{
			reflect::Handle border;
			reflect::Handle label;
		};

		std::array<Labelled, loadouts::kCount> g_cells;
		reflect::Handle                        g_popup, g_title;
		Labelled                               g_field, g_save, g_cancel;
		std::array<int, loadouts::kCount>      g_painted{};   // last painted State, -1 = unknown
		std::uint32_t                          g_namesShown = 0;
		ULONGLONG                              g_nextMeasure = 0;

		// popup
		int           g_renaming = -1;   // loadout being renamed, -1 = closed
		std::string   g_renameKey;
		std::uint32_t g_fieldShown = 0;
		bool          g_caretOn = false;
		ULONGLONG     g_nextBlink = 0;
		int           g_saveEnabled = -1;   // -1 = not painted yet

		UE::UObject* Live(const reflect::Handle& a_h)
		{
			auto* o = reflect::Get(a_h);
			return o && reflect::Usable(o) ? o : nullptr;
		}

		reflect::Handle FindHandle(UE::UObject* a_root, const std::string& a_name)
		{
			return reflect::Hold(ui::FindInTree(a_root, "GearLoadouts_" + a_name));
		}

		Labelled FindLabelled(UE::UObject* a_root, const std::string& a_name)
		{
			return { FindHandle(a_root, a_name), FindHandle(a_root, a_name + "_Text") };
		}

		bool Rect(UE::UObject* a_widget, double a_scale, windowinput::Rect& a_out)
		{
			double x = 0, y = 0, w = 0, h = 0;
			if (!a_widget || !ui::Measure(a_widget, x, y, w, h)) {
				return false;
			}
			a_out = { static_cast<int>(x * a_scale), static_cast<int>(y * a_scale), static_cast<int>((x + w) * a_scale), static_cast<int>((y + h) * a_scale) };
			return true;
		}

		void OnLeftClick(int a_index)
		{
			if (a_index == loadouts::ActiveForUi()) {
				return;
			}
			// Request reads the combat state: TES thread
			testhread::Post([a_index] {
				std::string why;
				if (!loadouts::Request(a_index, why)) {
					logger::info("switch to {} refused: {}", loadouts::Name(a_index), why);
					MessageBeep(MB_ICONHAND);
				}
			});
		}

		void ClosePopup()
		{
			if (g_renaming >= 0) {
				windowinput::ClosePopup();
				SetVisibility(Live(g_popup), kCollapsed);
				g_renaming = -1;
				g_renameKey.clear();
			}
		}

		void OpenPopup(int a_index)
		{
			auto* popup = Live(g_popup);
			if (!popup) {
				return;
			}
			std::string key = characterkey::Read();
			if (key.empty()) {
				console::Notify(loadouts::kNotIdentifiedMessage);
				return;
			}
			g_renaming = a_index;
			g_renameKey = std::move(key);
			const std::string name = loadouts::DisplayName(a_index);
			if (auto* title = Live(g_title)) {
				ui::SetText(title, std::format("Rename \"{}\"", name));
			}
			windowinput::OpenPopup(name, loadouts::kMaxNameLength);
			g_fieldShown = 0;
			g_saveEnabled = -1;
			g_nextMeasure = 0;   // measure the popup rects now
			SetVisibility(popup, kVisible);
		}

		void Save()
		{
			if (g_renaming >= 0 && loadouts::SetName(g_renameKey, g_renaming, windowinput::GetField().text)) {
				ClosePopup();
			}
		}

		// field text with a blinking caret or selection; Save greyed out while the name is invalid
		void PaintPopup()
		{
			const ULONGLONG now = GetTickCount64();
			const auto      field = windowinput::GetField();
			const bool      blink = now >= g_nextBlink;
			if (blink) {
				g_caretOn = !g_caretOn;
				g_nextBlink = now + 500;
			}
			if (field.version != g_fieldShown || blink) {
				g_fieldShown = field.version;
				if (auto* label = Live(g_field.label)) {
					ui::SetText(label, field.text + (field.selected ? "" : (g_caretOn ? "|" : " ")));
					ui::Ink(label, field.selected ? kInkStrong : kInk);
				}
				if (auto* border = Live(g_field.border)) {
					ui::RoundedBorder(border, field.selected ? kSelection : kPaperLight, kLine, 1.0f, 4.0f);
				}
			}
			std::string clean;
			const int   enabled = loadouts::CleanName(field.text, clean);
			if (enabled != g_saveEnabled) {
				g_saveEnabled = enabled;
				if (auto* border = Live(g_save.border)) {
					ui::RoundedBorder(border, enabled ? kButtonFill : kButtonFillDisabled, enabled ? kLine : kInkDim, 1.0f, 4.0f);
				}
				if (auto* label = Live(g_save.label)) {
					ui::Ink(label, enabled ? kInkStrong : kInkDim);
				}
			}
		}

		void MeasureRects()
		{
			const double                   scale = ui::ViewportScale();
			std::vector<windowinput::Rect> rects(loadouts::kCount);   // an unmeasured cell keeps an empty rect: never hit
			for (int i = 0; i < loadouts::kCount; ++i) {
				Rect(Live(g_cells[i].border), scale, rects[i]);
			}
			windowinput::SetRects(std::move(rects));
			if (g_renaming >= 0) {
				windowinput::Rect popup{}, save{}, cancel{};
				Rect(Live(g_popup), scale, popup);
				Rect(Live(g_save.border), scale, save);
				Rect(Live(g_cancel.border), scale, cancel);
				windowinput::SetPopupRects(popup, save, cancel);
			}
		}
	}

	void Prepare(UE::UObject* a_inventory)
	{
		std::erase_if(g_seen, [](const Seen& s) { return !reflect::Get(s.widget); });
		auto it = std::ranges::find_if(g_seen, [&](const Seen& s) { return reflect::Get(s.widget) == a_inventory; });
		if (it == g_seen.end()) {
			g_seen.push_back({ reflect::Hold(a_inventory) });
			it = std::prev(g_seen.end());
		}
		if (!it->built) {
			it->built = Build(a_inventory, !it->logged);   // a widget still being set up fails a few times: log once
			it->logged = true;
		}
	}

	void OnInventory(UE::UObject* a_inventory)
	{
		ClosePopup();
		g_cells = {};
		g_popup = g_title = {};
		g_field = g_save = g_cancel = {};
		g_painted.fill(-1);
		g_namesShown = 0;
		windowinput::SetRects({});   // no clicks until measured
		if (!a_inventory) {
			return;
		}
		Prepare(a_inventory);
		windowinput::Install();
		if (auto* mainPart = ui::FindInTree(a_inventory, "inv_main_part")) {
			for (int i = 0; i < loadouts::kCount; ++i) {
				g_cells[i] = FindLabelled(mainPart, std::format("Cell{}", i));
			}
			g_popup = FindHandle(mainPart, "Popup");
			g_title = FindHandle(mainPart, "PopupTitle");
			g_field = FindLabelled(mainPart, "Field");
			g_save = FindLabelled(mainPart, "Save");
			g_cancel = FindLabelled(mainPart, "Cancel");
		}
		g_nextMeasure = 0;
		// load the character's data now, so highlight and names are right before the first switch
		if (std::string key = characterkey::Read(); !key.empty()) {
			testhread::Post([key = std::move(key)] { loadouts::Sync(key); });
		}
	}

	void Tick()
	{
		for (const auto& click : windowinput::TakeClicks()) {
			switch (click.action) {
			case windowinput::Action::Left:
				OnLeftClick(click.index);
				break;
			case windowinput::Action::Right:
				OpenPopup(click.index);
				break;
			case windowinput::Action::Save:
				Save();
				break;
			case windowinput::Action::Cancel:
				ClosePopup();
				break;
			}
		}
		if (std::ranges::none_of(g_cells, [](const Labelled& c) { return reflect::Get(c.border); })) {
			return;
		}

		if (const auto version = loadouts::NamesVersion(); version != g_namesShown) {
			g_namesShown = version;
			for (int i = 0; i < loadouts::kCount; ++i) {
				if (auto* label = Live(g_cells[i].label)) {
					ui::SetText(label, loadouts::DisplayName(i));
				}
			}
		}

		const int active = loadouts::ActiveForUi();
		const int hover = windowinput::HoverIndex();
		for (int i = 0; i < loadouts::kCount; ++i) {
			const int state = (i == active ? 2 : 0) + (i == hover ? 1 : 0);   // State
			if (g_painted[i] != state) {
				if (auto* border = Live(g_cells[i].border)) {
					Paint(border, Live(g_cells[i].label), static_cast<State>(state));
					g_painted[i] = state;
				}
			}
		}
		if (g_renaming >= 0) {
			PaintPopup();
		}

		// hit rects 10 times a second (resolution and UI scale can change)
		if (const ULONGLONG now = GetTickCount64(); now >= g_nextMeasure) {
			g_nextMeasure = now + 100;
			MeasureRects();
		}
	}
}
