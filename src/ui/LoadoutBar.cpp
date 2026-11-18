// Gear Loadouts - the loadout button row and the rename popup (see LoadoutBar.h).
// SPDX-License-Identifier: GPL-3.0-or-later

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
		constexpr const wchar_t* kRowName = L"GearLoadouts_Row";
		constexpr double         kRowHeight = 40.0;   // canvas units; the Spacer grows by kTopGap + this + kGap
		constexpr double         kTopGap = 10.0;      // clear of the tab strip's lower edge (it overlapped, test 23:07)
		constexpr double         kGap = 6.0;
		constexpr double         kPopupHeight = 150.0;
		constexpr float          kButtonFont = 17.0f;
		constexpr float          kTitleFont = 19.0f;
		constexpr float          kFieldFont = 18.0f;

		// the game's own UI fonts (Menus Revamped, sorting/PickerWidgets.cpp, engine/UiText.cpp): Kingthings for headings
		// ("Weapons"), Robinson for the item lists
		constexpr const wchar_t* kKingthings = L"/Game/UI/CommonStyle/Fonts/Font_Kingthings_Localized.Font_Kingthings_Localized";
		constexpr const wchar_t* kKingthingsFace = L"Regular-Regular";
		constexpr float          kHeadingNudge = 6.0f;   // canvas units of extra top padding for Kingthings labels
		constexpr const wchar_t* kRobinson = L"/Game/UI/CommonStyle/Fonts/Font_Robinson_Localized.Font_Robinson_Localized";

		enum class Font
		{
			Heading,   // Kingthings
			Text       // Robinson
		};

		// ESlateVisibility
		constexpr std::uint8_t kVisible = 0;    // drawn and hit-testable: the popup blocks hover and clicks behind it
		constexpr std::uint8_t kCollapsed = 1;

		// FAnchorData as UMG declares it (/Script/UMG.AnchorData): FMargin Offsets (4 floats), FAnchors Anchors
		// (Minimum, Maximum: FVector2D = 2 doubles each), FVector2D Alignment. The size is checked against reflection.
		struct AnchorData
		{
			float  left, top, right, bottom;
			double minX, minY, maxX, maxY;
			double alignX, alignY;
		};
		static_assert(sizeof(AnchorData) == 64);

		bool ReadLayout(UE::UObject* a_canvasSlot, AnchorData& a_out)
		{
			ui::Call get(a_canvasSlot, L"GetLayout");
			if (!get || get.Size("ReturnValue") != static_cast<std::int32_t>(sizeof(AnchorData)) || !get.Run()) {
				return false;
			}
			a_out = get.Get<AnchorData>("ReturnValue");
			return true;
		}

		bool WriteLayout(UE::UObject* a_canvasSlot, const AnchorData& a_layout)
		{
			ui::Call set(a_canvasSlot, L"SetLayout");
			return set && set.Size("InLayoutData") == static_cast<std::int32_t>(sizeof(AnchorData)) && set.Set("InLayoutData", a_layout) && set.Run();
		}

		std::string Describe(const AnchorData& a)
		{
			return std::format("offsets L{:.1f} T{:.1f} R{:.1f} B{:.1f}, anchors ({:.2f},{:.2f})-({:.2f},{:.2f}), align ({:.2f},{:.2f})",
				a.left, a.top, a.right, a.bottom, a.minX, a.minY, a.maxX, a.maxY, a.alignX, a.alignY);
		}

		struct Vec2
		{
			double x, y;
		};

		void SetVisibility(UE::UObject* a_widget, std::uint8_t a_visibility)
		{
			if (a_widget) {
				ui::CallFirst(a_widget, L"SetVisibility", &a_visibility, sizeof(a_visibility));
			}
		}

		void Centre(UE::UObject* a_contentWidget)
		{
			const std::uint8_t center = 2;   // EHorizontalAlignment / EVerticalAlignment: Center
			ui::CallFirst(a_contentWidget, L"SetHorizontalAlignment", &center, sizeof(center));
			ui::CallFirst(a_contentWidget, L"SetVerticalAlignment", &center, sizeof(center));
		}

		// a Border holding a TextBlock (a button cell, a field); both named, so they can be found again
		UE::UObject* LabelledBorder(UE::UObject* a_tree, const std::wstring& a_name, const std::string& a_text, Font a_font, float a_fontSize, UE::UObject** a_label, bool a_centreText = true)
		{
			auto* border = ui::NewWidget(a_tree, L"/Script/UMG.Border", a_name.c_str());
			auto* label = ui::NewWidget(a_tree, L"/Script/UMG.TextBlock", (a_name + L"_Text").c_str());
			if (!border || !label) {
				return nullptr;
			}
			ui::CallFirst(border, L"SetContent", &label, sizeof(label));
			Centre(border);
			if (a_centreText) {
				// the label fills the cell and centres its own text: centred, and long names still end in "..."
				const std::uint8_t fill = 0;   // EHorizontalAlignment::Fill
				ui::CallFirst(border, L"SetHorizontalAlignment", &fill, sizeof(fill));
				const std::uint8_t centre = 1;   // ETextJustify::Center
				ui::CallFirst(label, L"SetJustification", &centre, sizeof(centre));
			}
			ui::SetText(label, a_text);
			const bool font = a_font == Font::Heading ? ui::SetFont(label, kKingthings, kKingthingsFace, a_fontSize) : ui::SetFont(label, kRobinson, nullptr, a_fontSize);
			if (a_font == Font::Heading) {
				// Kingthings' letters sit high in their line box (much room below them): centred by the box, they looked too
				// high (screenshot 2026-11-18). Uneven padding moves them down by about kHeadingNudge / 2.
				const float padding[4] = { 4.0f, 2.0f + kHeadingNudge, 4.0f, 2.0f - kHeadingNudge / 4.0f };
				ui::CallFirst(border, L"SetPadding", padding, sizeof(padding));
			}
			if (!font) {
				ui::SetFontSize(label, a_fontSize);   // the game font could not be loaded: the default font at the size
			}
			const std::uint8_t clip = 1;   // EWidgetClipping::ClipToBounds
			ui::CallFirst(label, L"SetClipping", &clip, sizeof(clip));
			const std::uint8_t ellipsis = 1;   // ETextOverflowPolicy::Ellipsis (UE 5.1+; does nothing when missing)
			ui::CallFirst(label, L"SetTextOverflowPolicy", &ellipsis, sizeof(ellipsis));
			if (a_label) {
				*a_label = label;
			}
			return border;
		}

		UE::UObject* AddToBox(UE::UObject* a_box, const wchar_t* a_addFunction, UE::UObject* a_child, bool a_fill, const float a_padding[4])
		{
			ui::Call add(a_box, a_addFunction);
			add.Set("Content", a_child);
			add.Run();
			auto* slot = add.Get<UE::UObject*>("ReturnValue");
			if (slot) {
				struct SlateChildSize
				{
					float        value;
					std::uint8_t rule;   // ESlateSizeRule: 0 Automatic, 1 Fill
					std::uint8_t pad[3];
				} size{ 1.0f, static_cast<std::uint8_t>(a_fill ? 1 : 0), {} };
				ui::CallFirst(slot, L"SetSize", &size, sizeof(size));
				ui::CallFirst(slot, L"SetPadding", a_padding, sizeof(float) * 4);
			}
			return slot;
		}

		// colours (linear RGBA): Menus Revamped's "Classic" palette (core/UiTheme.h), made for the parchment menus.
		// #E6E4DF is the game's own focus fill (MR docs/native-highlight.md).
		constexpr float kInk[4] = { 0.20f, 0.14f, 0.09f, 1.0f };          // dark brown text
		constexpr float kInkStrong[4] = { 0.06f, 0.035f, 0.016f, 1.0f };   // headings
		constexpr float kInkLight[4] = { 0.94f, 0.89f, 0.77f, 1.0f };      // on the dark active button
		constexpr float kInkDim[4] = { 0.34f, 0.27f, 0.19f, 0.45f };       // a disabled button
		constexpr float kLine[4] = { 0.20f, 0.14f, 0.09f, 0.85f };         // outlines
		constexpr float kPaper[4] = { 0.672443f, 0.630757f, 0.577580f, 1.0f };
		constexpr float kPaperLight[4] = { 0.82f, 0.77f, 0.68f, 1.0f };
		constexpr float kSelection[4] = { 0.78f, 0.56f, 0.22f, 0.55f };
		constexpr float kButtonFill[4] = { 0.30f, 0.24f, 0.16f, 0.30f };
		constexpr float kButtonFillDisabled[4] = { 0.30f, 0.24f, 0.16f, 0.10f };

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
			static constexpr Look kLooks[4] = {
				{ { 0.55f, 0.47f, 0.38f, 0.18f }, { 0.20f, 0.14f, 0.09f, 0.55f }, { 0.20f, 0.14f, 0.09f, 1.0f } },        // normal
				{ { 0.7913f, 0.7758f, 0.7379f, 1.0f }, { 0.20f, 0.14f, 0.09f, 0.85f }, { 0.06f, 0.035f, 0.016f, 1.0f } },   // hover
				{ { 0.30f, 0.19f, 0.08f, 0.92f }, { 0.12f, 0.07f, 0.03f, 1.0f }, { 0.94f, 0.89f, 0.77f, 1.0f } },           // active
				{ { 0.38f, 0.25f, 0.11f, 0.95f }, { 0.12f, 0.07f, 0.03f, 1.0f }, { 1.0f, 0.95f, 0.84f, 1.0f } },            // active + hover
			};
			const auto& look = kLooks[static_cast<std::size_t>(a_state)];
			if (a_cell) {
				ui::RoundedBorder(a_cell, look.fill, look.line, 1.0f, 4.0f);
			}
			if (a_label) {
				ui::Ink(a_label, look.ink);
			}
		}

		// the popup (built with the row, collapsed): Border > VerticalBox > title, field, buttons
		bool BuildPopup(UE::UObject* a_canvas, const AnchorData& a_rowLayout)
		{
			auto*        tree = a_canvas->GetOuter();
			UE::UObject* popupSlot = nullptr;
			auto*        popup = ui::AddToCanvas(a_canvas, L"/Script/UMG.Border", L"GearLoadouts_Popup", &popupSlot);
			auto*        column = ui::NewWidget(tree, L"/Script/UMG.VerticalBox", L"GearLoadouts_PopupColumn");
			auto*        title = ui::NewWidget(tree, L"/Script/UMG.TextBlock", L"GearLoadouts_PopupTitle");
			auto*        buttons = ui::NewWidget(tree, L"/Script/UMG.HorizontalBox", L"GearLoadouts_PopupButtons");
			auto*        field = LabelledBorder(tree, L"GearLoadouts_Field", "", Font::Text, kFieldFont, nullptr, false);
			UE::UObject* saveLabel = nullptr;
			UE::UObject* cancelLabel = nullptr;
			auto*        save = LabelledBorder(tree, L"GearLoadouts_Save", "Save", Font::Heading, kButtonFont, &saveLabel);
			auto*        cancel = LabelledBorder(tree, L"GearLoadouts_Cancel", "Cancel", Font::Heading, kButtonFont, &cancelLabel);
			if (!popup || !popupSlot || !column || !title || !buttons || !field || !save || !cancel) {
				logger::warn("ui: the rename popup could not be built");
				return false;
			}
			ui::RoundedBorder(popup, kPaper, kLine, 2.0f, 6.0f);
			const float popupPadding[4] = { 14.0f, 10.0f, 14.0f, 10.0f };
			ui::CallFirst(popup, L"SetPadding", popupPadding, sizeof(popupPadding));
			ui::CallFirst(popup, L"SetContent", &column, sizeof(column));
			ui::SetText(title, "Rename loadout");
			if (!ui::SetFont(title, kKingthings, kKingthingsFace, kTitleFont)) {
				ui::SetFontSize(title, kTitleFont);
			}
			ui::Ink(title, kInkStrong);
			ui::RoundedBorder(field, kPaperLight, kLine, 1.0f, 4.0f);
			const float fieldPadding[4] = { 8.0f, 4.0f, 8.0f, 4.0f };
			ui::CallFirst(field, L"SetPadding", fieldPadding, sizeof(fieldPadding));
			ui::RoundedBorder(save, kButtonFill, kLine, 1.0f, 4.0f);
			ui::RoundedBorder(cancel, kButtonFill, kLine, 1.0f, 4.0f);
			ui::Ink(saveLabel, kInkStrong);
			ui::Ink(cancelLabel, kInkStrong);

			const float none[4] = { 0, 0, 0, 0 };
			const float below[4] = { 0, 8, 0, 0 };
			AddToBox(column, L"AddChildToVerticalBox", title, false, none);
			AddToBox(column, L"AddChildToVerticalBox", field, true, below);
			AddToBox(column, L"AddChildToVerticalBox", buttons, false, below);
			const float left[4] = { 0, 0, 6, 0 };
			const float right[4] = { 6, 0, 0, 0 };
			AddToBox(buttons, L"AddChildToHorizontalBox", save, true, left);
			AddToBox(buttons, L"AddChildToHorizontalBox", cancel, true, right);
			const std::uint8_t fill = 0;   // EHorizontalAlignment::Fill
			ui::CallFirst(save, L"SetHorizontalAlignment", &fill, sizeof(fill));

			// right below the row, as wide as the row; above everything (ZOrder)
			AnchorData layout = a_rowLayout;
			layout.top = static_cast<float>(a_rowLayout.top + kRowHeight + 4.0);
			layout.bottom = static_cast<float>(kPopupHeight);
			WriteLayout(popupSlot, layout);
			const std::int32_t z = 4000;
			ui::CallFirst(popupSlot, L"SetZOrder", &z, sizeof(z));
			SetVisibility(popup, kCollapsed);
			return true;
		}

		// true = the row is there (built now or before); false = not possible yet (logged once per widget)
		bool Build(UE::UObject* a_inventory, bool a_logProblems)
		{
			auto* mainPart = ui::FindInTree(a_inventory, "inv_main_part");
			auto* canvas = mainPart ? ui::TreeRoot(mainPart) : nullptr;
			auto* vbox = mainPart ? ui::FindInTree(mainPart, "inv_verticalbox") : nullptr;
			if (!canvas || !vbox || ui::Name(canvas->GetClass()) != "CanvasPanel") {
				if (a_logProblems) {
					logger::warn("ui: bar not built yet: inv_main_part {}, its canvas {}, inv_verticalbox {}", mainPart != nullptr, canvas != nullptr, vbox != nullptr);
				}
				return false;
			}
			if (ui::FindInTree(mainPart, "GearLoadouts_Row")) {
				return true;
			}
			const auto vboxSlots = ui::Slots(vbox);
			auto*      spacer = vboxSlots.empty() ? nullptr : ui::Content(vboxSlots.front());
			if (!spacer || ui::Name(spacer->GetClass()) != "Spacer") {
				if (a_logProblems) {
					logger::warn("ui: bar not built: the first child of inv_verticalbox is {} ({}), not a Spacer", ui::Name(spacer),
						spacer ? ui::Name(spacer->GetClass()) : "-");
				}
				return false;
			}
			AnchorData vboxLayout{};
			auto*      vboxSlot = ui::Prop(vbox, "Slot");
			if (!vboxSlot || !ReadLayout(vboxSlot, vboxLayout)) {
				if (a_logProblems) {
					logger::warn("ui: bar not built: inv_verticalbox's canvas slot layout could not be read");
				}
				return false;
			}
			const auto* spacerSize = reflect::At<Vec2>(spacer, reflect::Offset(spacer->GetClass(), "Size"));
			if (!spacerSize || reflect::Size(spacer->GetClass(), "Size") != static_cast<std::int32_t>(sizeof(Vec2))) {
				if (a_logProblems) {
					logger::warn("ui: bar not built: the Spacer's Size could not be read");
				}
				return false;
			}
			const Vec2 oldSpacer = *spacerSize;

			// the row: a HorizontalBox (new child of the canvas) with 5 cells; a cell is a Border (its background shows
			// the state) holding a centred TextBlock
			UE::UObject* rowSlot = nullptr;
			auto*        row = ui::AddToCanvas(canvas, L"/Script/UMG.HorizontalBox", kRowName, &rowSlot);
			if (!row || !rowSlot) {
				logger::warn("ui: bar not built: the row could not be created (row {}, slot {})", row != nullptr, rowSlot != nullptr);
				return false;
			}
			for (int i = 0; i < loadouts::kCount; ++i) {
				UE::UObject* label = nullptr;
				auto* cell = LabelledBorder(canvas->GetOuter(), std::format(L"GearLoadouts_Cell{}", i), loadouts::DisplayName(i), Font::Heading, kButtonFont, &label);
				if (!cell) {
					logger::warn("ui: bar: cell {} could not be created", i);
					continue;
				}
				const float padding[4] = { i == 0 ? 0.0f : 3.0f, 0.0f, i == loadouts::kCount - 1 ? 0.0f : 3.0f, 0.0f };
				AddToBox(row, L"AddChildToHorizontalBox", cell, true, padding);
				Paint(cell, label, State::Normal);
			}

			// where the Spacer is now: the top of inv_verticalbox. The row goes there, the Spacer grows below it.
			AnchorData rowLayout = vboxLayout;
			rowLayout.maxY = rowLayout.minY;   // no vertical stretch for the row
			rowLayout.alignY = 0.0;
			const double vboxTop = vboxLayout.top - (vboxLayout.minY == vboxLayout.maxY ? vboxLayout.alignY * vboxLayout.bottom : 0.0);
			rowLayout.top = static_cast<float>(vboxTop + oldSpacer.y + kTopGap);
			rowLayout.bottom = static_cast<float>(kRowHeight);   // with no vertical stretch, Bottom is the height
			if (!WriteLayout(rowSlot, rowLayout)) {
				logger::warn("ui: the row's layout could not be set");
			}
			const std::int32_t z = 10;
			ui::CallFirst(rowSlot, L"SetZOrder", &z, sizeof(z));

			const Vec2 newSpacer{ oldSpacer.x, oldSpacer.y + kTopGap + kRowHeight + kGap };
			ui::CallFirst(spacer, L"SetSize", &newSpacer, sizeof(newSpacer));

			BuildPopup(canvas, rowLayout);
			logger::info("ui: bar built into {} (shown: {}): row {}, spacer {:.1f} -> {:.1f}", ui::Name(a_inventory),
				ui::ShownOnScreen(a_inventory), Describe(rowLayout), oldSpacer.y, newSpacer.y);
			return true;
		}

		struct Seen
		{
			reflect::Handle widget;
			bool            built = false;
			bool            problemLogged = false;
		};
		std::vector<Seen> g_seen;   // inventory widgets met so far (dead ones are dropped)

		// ------------------------------------------------------------------------------------------------------
		// the open inventory: its widgets, states, clicks
		// ------------------------------------------------------------------------------------------------------

		struct Labelled
		{
			reflect::Handle border;
			reflect::Handle label;
		};

		std::array<Labelled, loadouts::kCount> g_cells;   // the open inventory's cells; empty handles when closed
		reflect::Handle                        g_popup, g_title;
		Labelled                               g_field, g_save, g_cancel;
		ULONGLONG                              g_nextMeasure = 0;
		int                                    g_painted[loadouts::kCount]{};   // the State last painted, -1 = unknown
		std::uint32_t                          g_namesShown = 0;
		std::string                            g_lastRects;   // logged when it changes

		// the popup while open
		int           g_renaming = -1;   // the loadout being renamed, -1 = popup closed
		std::string   g_renameKey;       // the character it belongs to
		std::uint32_t g_fieldShown = 0;
		bool          g_caretOn = false;
		ULONGLONG     g_nextBlink = 0;
		int           g_saveEnabled = -1;   // -1 = not painted yet

		UE::UObject* Live(const reflect::Handle& a_h)
		{
			auto* o = reflect::Get(a_h);
			return o && reflect::Usable(o) ? o : nullptr;
		}

		Labelled Find(UE::UObject* a_mainPart, const std::string& a_name)
		{
			auto* border = ui::FindInTree(a_mainPart, a_name);
			auto* label = ui::FindInTree(a_mainPart, a_name + "_Text");
			return { border ? reflect::Hold(border) : reflect::Handle{}, label ? reflect::Hold(label) : reflect::Handle{} };
		}

		bool AnyCell()
		{
			return std::ranges::any_of(g_cells, [](const Labelled& c) { return reflect::Get(c.border) != nullptr; });
		}

		bool Rect(UE::UObject* a_widget, double a_scale, windowinput::Rect& a_out)
		{
			double x = 0, y = 0, w = 0, h = 0;
			if (!a_widget || !ui::Measure(a_widget, x, y, w, h)) {
				return false;
			}
			a_out = { static_cast<int>(x * a_scale), static_cast<int>(y * a_scale), static_cast<int>((x + w) * a_scale),
				static_cast<int>((y + h) * a_scale) };
			return true;
		}

		// ask the TES thread to load the character's data, so highlight and names are right before the first switch
		void SyncActive()
		{
			std::string key = characterkey::Read();
			if (!key.empty()) {
				testhread::Post([key = std::move(key)] { loadouts::Sync(key); });
			}
		}

		void OnLeftClick(int a_index)
		{
			if (a_index == loadouts::ActiveForUi()) {
				logger::info("ui: click on {} - already active, nothing to do", loadouts::Name(a_index));
				return;
			}
			logger::info("ui: click on {} - switch requested", loadouts::Name(a_index));
			// Request reads the player's combat state: TES thread (it posts the key read back here, then the switch)
			testhread::Post([a_index] {
				std::string why;
				if (!loadouts::Request(a_index, why)) {
					logger::info("switch to {} refused: {}", loadouts::Name(a_index), why);
					MessageBeep(MB_ICONHAND);
				}
			});
		}

		void ClosePopup(const char* a_why)
		{
			if (g_renaming < 0) {
				return;
			}
			logger::info("ui: rename popup for {} closed ({})", loadouts::Name(g_renaming), a_why);
			windowinput::ClosePopup();
			SetVisibility(Live(g_popup), kCollapsed);
			g_renaming = -1;
			g_renameKey.clear();
		}

		void OpenPopup(int a_index)
		{
			auto* popup = Live(g_popup);
			if (!popup) {
				logger::warn("ui: right click on {}, but the rename popup is missing", loadouts::Name(a_index));
				return;
			}
			std::string key = characterkey::Read();
			if (key.empty()) {
				console::Notify("Loadouts are not available yet. Save the game once, then try again.");
				return;
			}
			g_renaming = a_index;
			g_renameKey = std::move(key);
			const std::string name = loadouts::DisplayName(a_index);
			if (auto* title = Live(g_title)) {
				ui::SetText(title, std::format("Rename \"{}\"", name));
			}
			windowinput::OpenPopup(name, loadouts::kMaxNameLength);
			g_fieldShown = 0;   // repaint the field
			g_saveEnabled = -1;
			g_nextMeasure = 0;  // measure the popup's rects at once
			SetVisibility(popup, kVisible);
			logger::info("ui: rename popup for {} opened", loadouts::Name(a_index));
		}

		void Save()
		{
			if (g_renaming < 0) {
				return;
			}
			const auto  field = windowinput::GetField();
			std::string clean;
			if (!loadouts::CleanName(field.text, clean)) {
				logger::info("ui: Save ignored: the name is empty or not valid");
				return;   // Save is greyed out; Enter does nothing either
			}
			if (loadouts::SetName(g_renameKey, g_renaming, clean)) {
				ClosePopup("saved");
			}
		}

		// the field's text, caret and selection; the Save button greyed out while the name is not valid
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
			const int   enabled = loadouts::CleanName(field.text, clean) ? 1 : 0;
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
	}

	void Prepare(UE::UObject* a_inventory)
	{
		std::erase_if(g_seen, [](const Seen& a) { return reflect::Get(a.widget) == nullptr; });
		auto it = std::ranges::find_if(g_seen, [&](const Seen& a) { return reflect::Get(a.widget) == a_inventory; });
		if (it == g_seen.end()) {
			g_seen.push_back({ reflect::Hold(a_inventory) });
			it = std::prev(g_seen.end());
		}
		if (it->built) {
			return;
		}
		it->built = Build(a_inventory, !it->problemLogged);
		it->problemLogged = true;   // a widget still being built up fails once or twice: log only the first try
	}

	void OnInventory(UE::UObject* a_inventory)
	{
		ClosePopup("the inventory closed or changed");
		g_cells = {};
		g_popup = g_title = {};
		g_field = g_save = g_cancel = {};
		g_lastRects.clear();
		std::ranges::fill(g_painted, -1);
		g_namesShown = 0;
		windowinput::SetRects({});   // closed (or a new widget): no clicks until measured
		if (!a_inventory) {
			return;
		}
		Prepare(a_inventory);   // normally done already, before the menu was shown
		windowinput::Install();
		if (auto* mainPart = ui::FindInTree(a_inventory, "inv_main_part")) {
			for (int i = 0; i < loadouts::kCount; ++i) {
				g_cells[static_cast<std::size_t>(i)] = Find(mainPart, std::format("GearLoadouts_Cell{}", i));
			}
			if (auto* popup = ui::FindInTree(mainPart, "GearLoadouts_Popup")) {
				g_popup = reflect::Hold(popup);
			}
			if (auto* title = ui::FindInTree(mainPart, "GearLoadouts_PopupTitle")) {
				g_title = reflect::Hold(title);
			}
			g_field = Find(mainPart, "GearLoadouts_Field");
			g_save = Find(mainPart, "GearLoadouts_Save");
			g_cancel = Find(mainPart, "GearLoadouts_Cancel");
		}
		if (!AnyCell()) {
			logger::warn("ui: inventory open, but no loadout cell was found");
		}
		g_nextMeasure = 0;
		SyncActive();
	}

	void Tick()
	{
		// actions first: queued by the window procedure since the last frame
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
				ClosePopup("cancelled");
				break;
			}
		}
		if (!AnyCell()) {
			return;
		}

		// labels: when a name changed (a rename, or another character's data loaded)
		if (const auto version = loadouts::NamesVersion(); version != g_namesShown) {
			g_namesShown = version;
			for (int i = 0; i < loadouts::kCount; ++i) {
				if (auto* label = Live(g_cells[static_cast<std::size_t>(i)].label)) {
					ui::SetText(label, loadouts::DisplayName(i));
				}
			}
		}

		// states: every frame (cheap), so hover and a hotkey switch show at once
		const int active = loadouts::ActiveForUi();
		const int hover = windowinput::HoverIndex();
		for (int i = 0; i < loadouts::kCount; ++i) {
			const bool isActive = i == active;
			const bool isHover = i == hover;
			const auto state = isActive ? (isHover ? State::ActiveHover : State::Active) : (isHover ? State::Hover : State::Normal);
			if (g_painted[i] != static_cast<int>(state)) {
				auto* border = Live(g_cells[static_cast<std::size_t>(i)].border);
				if (border) {
					Paint(border, Live(g_cells[static_cast<std::size_t>(i)].label), state);
					g_painted[i] = static_cast<int>(state);
				}
			}
		}
		if (g_renaming >= 0) {
			PaintPopup();
		}

		// rects: 10 times a second (the layout can move: resolution, UI scale)
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextMeasure) {
			return;
		}
		g_nextMeasure = now + 100;
		const double                   scale = ui::ViewportScale();
		std::vector<windowinput::Rect> rects;
		std::string                    text;
		for (int i = 0; i < loadouts::kCount; ++i) {
			windowinput::Rect r{};
			if (!Rect(Live(g_cells[static_cast<std::size_t>(i)].border), scale, r)) {
				rects.push_back({ 0, 0, 0, 0 });   // keeps the index; never hit
				continue;
			}
			rects.push_back(r);
			text += std::format("[{}: {},{}-{},{}] ", i, r.left, r.top, r.right, r.bottom);
		}
		windowinput::SetRects(rects);
		if (text != g_lastRects) {
			g_lastRects = text;
			logger::info("ui: buttons at {}(scale {:.3f})", text, scale);
		}
		if (g_renaming >= 0) {
			windowinput::Rect popup{}, save{}, cancel{};
			Rect(Live(g_popup), scale, popup);
			Rect(Live(g_save.border), scale, save);
			Rect(Live(g_cancel.border), scale, cancel);
			windowinput::SetPopupRects(popup, save, cancel);
		}
	}
}
