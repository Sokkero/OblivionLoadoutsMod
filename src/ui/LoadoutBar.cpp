// Gear Loadouts - the loadout button row (see LoadoutBar.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/LoadoutBar.h"

#include "Loadouts.h"
#include "Reflect.h"
#include "ui/UiCall.h"
#include "ui/UiWidgets.h"
#include "ui/WindowInput.h"
#include "CharacterKey.h"
#include "TesThread.h"

namespace loadoutbar
{
	namespace
	{
		constexpr const wchar_t* kRowName = L"GearLoadouts_Row";
		constexpr double         kRowHeight = 40.0;   // canvas units; the Spacer grows by kTopGap + this + kGap
		constexpr double         kTopGap = 10.0;      // clear of the tab strip's lower edge (it overlapped, test 23:07)
		constexpr double         kGap = 6.0;

		// FAnchorData as UMG declares it (/Script/UMG.AnchorData): FMargin Offsets (4 floats), FAnchors Anchors
		// (Minimum, Maximum: FVector2D = 2 doubles each), FVector2D Alignment. Checked against reflection in Layout().
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

		enum class State
		{
			Normal,
			Hover,
			Active,
			ActiveHover
		};

		// cell background and text colours (linear RGBA)
		void Paint(UE::UObject* a_cell, UE::UObject* a_label, State a_state)
		{
			static constexpr float kBack[4][4] = {
				{ 0.05f, 0.04f, 0.03f, 0.35f },   // normal
				{ 0.10f, 0.08f, 0.05f, 0.60f },   // hover
				{ 0.42f, 0.28f, 0.08f, 0.85f },   // active
				{ 0.50f, 0.34f, 0.10f, 0.95f },   // active + hover
			};
			static constexpr float kInk[4][4] = {
				{ 0.85f, 0.80f, 0.70f, 1.0f },
				{ 1.00f, 0.95f, 0.85f, 1.0f },
				{ 1.00f, 0.92f, 0.70f, 1.0f },
				{ 1.00f, 0.96f, 0.80f, 1.0f },
			};
			const auto i = static_cast<std::size_t>(a_state);
			if (a_cell) {
				ui::CallFirst(a_cell, L"SetBrushColor", kBack[i], sizeof(kBack[i]));
			}
			if (a_label) {
				ui::Ink(a_label, kInk[i]);
			}
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

			// layout numbers (logged: the placement may need tuning per resolution)
			AnchorData vboxLayout{};
			auto*      vboxSlot = ui::Prop(vbox, "Slot");
			if (!vboxSlot || !ReadLayout(vboxSlot, vboxLayout)) {
				if (a_logProblems) {
					logger::warn("ui: bar not built: inv_verticalbox's canvas slot layout could not be read");
				}
				return false;
			}
			const auto  sizeOff = reflect::Offset(spacer->GetClass(), "Size");
			const auto* spacerSize = reflect::At<Vec2>(spacer, sizeOff);
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
				auto* cell = ui::NewWidget(canvas->GetOuter(), L"/Script/UMG.Border", std::format(L"GearLoadouts_Cell{}", i).c_str());
				auto* label = ui::NewWidget(canvas->GetOuter(), L"/Script/UMG.TextBlock", std::format(L"GearLoadouts_Label{}", i).c_str());
				if (!cell || !label) {
					logger::warn("ui: bar: cell {} could not be created", i);
					continue;
				}
				ui::CallFirst(cell, L"SetContent", &label, sizeof(label));
				const std::uint8_t center = 2;   // EHorizontalAlignment / EVerticalAlignment: Center
				ui::CallFirst(cell, L"SetHorizontalAlignment", &center, sizeof(center));
				ui::CallFirst(cell, L"SetVerticalAlignment", &center, sizeof(center));
				ui::SetText(label, loadouts::Name(i));   // hardcoded names for now (UI-UX.md step 7 makes them editable)

				ui::Call add(row, L"AddChildToHorizontalBox");
				add.Set("Content", cell);
				add.Run();
				if (auto* cellSlot = add.Get<UE::UObject*>("ReturnValue")) {
					struct SlateChildSize
					{
						float        value;
						std::uint8_t rule;   // ESlateSizeRule: 0 Automatic, 1 Fill
						std::uint8_t pad[3];
					} fill{ 1.0f, 1, {} };
					ui::CallFirst(cellSlot, L"SetSize", &fill, sizeof(fill));
					const float padding[4] = { i == 0 ? 0.0f : 3.0f, 0.0f, i == loadouts::kCount - 1 ? 0.0f : 3.0f, 0.0f };
					ui::CallFirst(cellSlot, L"SetPadding", padding, sizeof(padding));
				}
				Paint(cell, label, State::Normal);
			}

			// where the Spacer is now: the top of inv_verticalbox. The row goes there, the Spacer grows below it.
			AnchorData rowLayout = vboxLayout;
			rowLayout.maxY = rowLayout.minY;                 // no vertical stretch for the row
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

	namespace
	{
		struct Cell
		{
			reflect::Handle border;
			reflect::Handle label;
		};
		std::array<Cell, loadouts::kCount> g_cells;   // the cells of the open inventory's row; empty handles when closed
		ULONGLONG                          g_nextMeasure = 0;
		int                                g_painted[loadouts::kCount]{};   // the State last painted, -1 = unknown
		std::string                        g_lastRects;   // logged when it changes

		bool AnyCell()
		{
			return std::ranges::any_of(g_cells, [](const Cell& c) { return reflect::Get(c.border) != nullptr; });
		}

		// ask the TES thread to load the character's data, so the highlight is right before the first switch
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
	}

	void OnInventory(UE::UObject* a_inventory)
	{
		g_cells = {};
		g_lastRects.clear();
		std::ranges::fill(g_painted, -1);
		windowinput::SetRects({});   // closed (or a new widget): no clicks until measured
		if (!a_inventory) {
			return;
		}
		Prepare(a_inventory);   // normally done already, before the menu was shown
		windowinput::Install();
		if (auto* mainPart = ui::FindInTree(a_inventory, "inv_main_part")) {
			for (int i = 0; i < loadouts::kCount; ++i) {
				auto* border = ui::FindInTree(mainPart, std::format("GearLoadouts_Cell{}", i));
				auto* label = ui::FindInTree(mainPart, std::format("GearLoadouts_Label{}", i));
				g_cells[static_cast<std::size_t>(i)] = { border ? reflect::Hold(border) : reflect::Handle{}, label ? reflect::Hold(label) : reflect::Handle{} };
			}
		}
		if (!AnyCell()) {
			logger::warn("ui: inventory open, but no loadout cell was found");
		}
		g_nextMeasure = 0;
		SyncActive();
	}

	void Tick()
	{
		// clicks first: they were queued by the window procedure since the last frame
		for (const auto& click : windowinput::TakeClicks()) {
			if (click.right) {
				logger::info("ui: right click on {} - rename comes in a later step", loadouts::Name(click.index));
			} else {
				OnLeftClick(click.index);
			}
		}
		if (!AnyCell()) {
			return;
		}

		// states: every frame (cheap), so hover and a hotkey switch show at once
		const int active = loadouts::ActiveForUi();
		const int hover = windowinput::HoverIndex();
		for (int i = 0; i < loadouts::kCount; ++i) {
			const bool isActive = i == active;
			const bool isHover = i == hover;
			const auto state = isActive ? (isHover ? State::ActiveHover : State::Active) : (isHover ? State::Hover : State::Normal);
			if (g_painted[i] != static_cast<int>(state)) {
				auto* border = reflect::Get(g_cells[static_cast<std::size_t>(i)].border);
				auto* label = reflect::Get(g_cells[static_cast<std::size_t>(i)].label);
				if (border && reflect::Usable(border)) {
					Paint(border, label && reflect::Usable(label) ? label : nullptr, state);
					g_painted[i] = static_cast<int>(state);
				}
			}
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
			auto*  border = reflect::Get(g_cells[static_cast<std::size_t>(i)].border);
			double x = 0, y = 0, w = 0, h = 0;
			if (!border || !reflect::Usable(border) || !ui::Measure(border, x, y, w, h)) {
				rects.push_back({ 0, 0, 0, 0 });   // keeps the index; never hit
				continue;
			}
			const windowinput::Rect r{ static_cast<int>(x * scale), static_cast<int>(y * scale), static_cast<int>((x + w) * scale),
				static_cast<int>((y + h) * scale) };
			rects.push_back(r);
			text += std::format("[{}: {},{}-{},{}] ", i, r.left, r.top, r.right, r.bottom);
		}
		windowinput::SetRects(rects);
		if (text != g_lastRects) {
			g_lastRects = text;
			logger::info("ui: buttons at {}(scale {:.3f})", text, scale);
		}
	}
}
