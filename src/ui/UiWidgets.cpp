// Gear Loadouts - building and reading UMG widgets.
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).
// Modified 2026-11-17 (see UiWidgets.h).

#include "ui/UiWidgets.h"

#include "Reflect.h"
#include "ui/UiCall.h"

namespace ui
{
	namespace
	{
		struct RawArray
		{
			UE::UObject** data;
			std::int32_t  num;
			std::int32_t  max;
		};

		bool IsA(UE::UObject* a_o, UE::UClass* a_class)
		{
			return a_o && a_class && a_o->GetClass() && a_o->GetClass()->IsChildOf(a_class);
		}

		UE::UObject* Search(UE::UObject* a_w, std::string_view a_name, int a_depth, int& a_budget)
		{
			static auto* userWidget = Class(L"/Script/UMG.UserWidget");
			static auto* panelWidget = Class(L"/Script/UMG.PanelWidget");
			if (!a_w || a_depth > 24 || a_budget-- <= 0) {
				return nullptr;
			}
			if (a_depth > 0 && Name(a_w) == a_name) {
				return a_w;
			}
			if (IsA(a_w, userWidget)) {
				if (auto* found = Search(TreeRoot(a_w), a_name, a_depth + 1, a_budget)) {
					return found;
				}
			}
			if (IsA(a_w, panelWidget)) {
				for (auto* slot : Slots(a_w)) {
					if (auto* found = Search(Content(slot), a_name, a_depth + 1, a_budget)) {
						return found;
					}
				}
			}
			return nullptr;
		}

		UE::UObject* SearchGuarded(UE::UObject* a_w, std::string_view a_name, bool* a_faulted)
		{
			__try {
				int budget = 4000;
				return Search(a_w, a_name, 0, budget);
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				*a_faulted = true;
				return nullptr;
			}
		}

		std::wstring Wide(const std::string& a_utf8)
		{
			if (a_utf8.empty()) {
				return {};
			}
			const int    n = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
			std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
			if (n > 0) {
				MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), out.data(), n);
			}
			return out;
		}

		float RenderOpacity(UE::UObject* a_widget)
		{
			const auto  off = reflect::Offset(a_widget->GetClass(), "RenderOpacity");
			const auto* at = off >= 0 ? reflect::At<float>(a_widget, off) : nullptr;
			return at ? *at : 1.0f;
		}
	}

	UE::UObject* Prop(UE::UObject* a_object, std::string_view a_name)
	{
		if (!a_object || !a_object->GetClass()) {
			return nullptr;
		}
		auto** at = reflect::At<UE::UObject*>(a_object, reflect::Offset(a_object->GetClass(), a_name));
		return at ? *at : nullptr;
	}

	std::vector<UE::UObject*> Slots(UE::UObject* a_panel)
	{
		static auto* panelWidget = Class(L"/Script/UMG.PanelWidget");
		std::vector<UE::UObject*> out;
		if (!IsA(a_panel, panelWidget)) {
			return out;
		}
		const auto* arr = reflect::At<RawArray>(a_panel, reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots"));
		for (std::int32_t i = 0; arr && arr->data && i < arr->num && i < 512; ++i) {
			out.push_back(arr->data[i]);
		}
		return out;
	}

	UE::UObject* Content(UE::UObject* a_slot)
	{
		return Prop(a_slot, "Content");
	}

	UE::UObject* TreeRoot(UE::UObject* a_userWidget)
	{
		auto* tree = Prop(a_userWidget, "WidgetTree");
		return tree ? Prop(tree, "RootWidget") : nullptr;
	}

	UE::UObject* FindInTree(UE::UObject* a_userWidget, std::string_view a_name)
	{
		bool  faulted = false;
		auto* found = SearchGuarded(a_userWidget, a_name, &faulted);
		if (faulted) {
			logger::warn("ui: a widget tree could not be read (access violation) while looking for {} - skipped", a_name);
		}
		return found;
	}

	bool ShownOnScreen(UE::UObject* a_widget)
	{
		static auto* widgetClass = Class(L"/Script/UMG.Widget");
		static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		static auto* slotClass = Class(L"/Script/UMG.PanelSlot");
		static auto* switcherClass = Class(L"/Script/UMG.WidgetSwitcher");
		const auto   slotOff = widgetClass ? reflect::Offset(reinterpret_cast<UE::UStruct*>(widgetClass), "Slot") : -1;
		const auto   parentOff = slotClass ? reflect::Offset(reinterpret_cast<UE::UStruct*>(slotClass), "Parent") : -1;
		UE::UObject* w = a_widget;
		for (int depth = 0; w && depth < 40; ++depth) {
			Call vis(w, L"IsVisible");
			if (!vis) {
				return depth > 0;   // above the widgets: shown so far
			}
			if (!vis.Run() || !vis.Get<bool>("ReturnValue") || RenderOpacity(w) < 0.05f) {
				return false;
			}
			auto*        slot = slotOff >= 0 ? *reflect::At<UE::UObject*>(w, slotOff) : nullptr;
			UE::UObject* next = slot && parentOff >= 0 ? *reflect::At<UE::UObject*>(slot, parentOff) : nullptr;
			if (next && switcherClass && next->GetClass()->IsChildOf(switcherClass)) {
				Call active(next, L"GetActiveWidget");
				if (active && active.Run() && active.Get<UE::UObject*>("ReturnValue") != w) {
					return false;
				}
			}
			if (!next) {   // a tree's root widget: the user widget that owns the tree
				auto* outer = w->GetOuter();
				next = outer && treeClass && outer->GetClass() == treeClass ? outer->GetOuter() : nullptr;
			}
			w = next;
		}
		return true;
	}

	UE::UObject* NewWidget(UE::UObject* a_outerTree, const wchar_t* a_classPath, const wchar_t* a_name)
	{
		auto* cls = Class(a_classPath);
		return cls && a_outerTree ? UE::NewObject<UE::UObject>(a_outerTree, cls, UE::FName(a_name)) : nullptr;
	}

	UE::UObject* AddToCanvas(UE::UObject* a_canvas, const wchar_t* a_classPath, const wchar_t* a_name, UE::UObject** a_slot)
	{
		*a_slot = nullptr;
		auto* w = NewWidget(a_canvas ? a_canvas->GetOuter() : nullptr, a_classPath, a_name);   // the canvas's outer: its widget tree
		if (!w) {
			return nullptr;
		}
		Call add(a_canvas, L"AddChildToCanvas");
		add.Set("Content", w);
		add.Run();
		*a_slot = add.Get<UE::UObject*>("ReturnValue");
		if (!*a_slot) {
			return nullptr;
		}
		const bool no = false;
		CallFirst(*a_slot, L"SetAutoSize", &no, sizeof(no));
		return w;
	}

	bool SetText(UE::UObject* a_textWidget, const std::string& a_utf8)
	{
		Call  c(a_textWidget, L"SetText");
		void* at = c ? c.At("InText") : nullptr;
		if (!at) {
			return false;
		}
		const std::wstring w = Wide(a_utf8);
		auto*              text = new (at) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
		c.Run();
		text->~FText();
		return true;
	}

	void Ink(UE::UObject* a_textWidget, const float a_rgba[4])
	{
		struct SlateColor
		{
			float        rgba[4];
			std::uint8_t rule;
			std::uint8_t pad[7];
		} ink{ { a_rgba[0], a_rgba[1], a_rgba[2], a_rgba[3] }, 0, {} };
		CallFirst(a_textWidget, L"SetColorAndOpacity", &ink, sizeof(ink));
		const float shadow[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		CallFirst(a_textWidget, L"SetShadowColorAndOpacity", shadow, sizeof(shadow));
	}

	bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_width, double& a_height)
	{
		auto*        pc = PlayerController();
		static auto* lib = Class(L"/Script/UMG.SlateBlueprintLibrary");
		auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!a_w || !pc || !cdo) {
			return false;
		}
		Call       geo(a_w, L"GetCachedGeometry");
		const auto gsize = geo.Size("ReturnValue");
		if (!geo || gsize <= 0 || !geo.Run()) {
			return false;
		}
		Call  size(cdo, L"GetLocalSize");
		void* g = size.At("Geometry");
		if (!size || !g || size.Size("Geometry") != gsize) {
			return false;
		}
		std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
		size.Run();
		const auto local = size.Get<std::array<double, 2>>("ReturnValue");
		const auto toViewport = [&](double a_lx, double a_ly, double& a_vx, double& a_vy) {
			Call  c(cdo, L"LocalToViewport");
			void* gg = c.At("Geometry");
			if (!c || !gg || c.Size("Geometry") != gsize) {
				return false;
			}
			c.Set("WorldContextObject", pc);
			std::memcpy(gg, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
			const double lc[2] = { a_lx, a_ly };
			c.Set("LocalCoordinate", lc);
			if (!c.RunGuarded()) {
				return false;   // a world-context call: fault-guarded (a quit, a load)
			}
			const auto vpos = c.Get<std::array<double, 2>>("ViewportPosition");
			a_vx = vpos[0];
			a_vy = vpos[1];
			return true;
		};
		double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!toViewport(0.0, 0.0, x0, y0) || !toViewport(local[0], local[1], x1, y1)) {
			return false;
		}
		a_x = x0;
		a_y = y0;
		a_width = x1 - x0;
		a_height = y1 - y0;
		return local[0] > 0.0 && a_width > 1.0 && a_height > 1.0;
	}

	float ViewportScale()
	{
		static auto* lib = Class(L"/Script/UMG.WidgetLayoutLibrary");
		auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		auto*        pc = PlayerController();
		if (!cdo || !pc) {
			return 1.0f;
		}
		Call c(cdo, L"GetViewportScale");
		if (!c) {
			return 1.0f;
		}
		c.Set("WorldContextObject", pc);
		return c.RunGuarded() ? c.Get<float>("ReturnValue") : 1.0f;
	}

	bool SetFontSize(UE::UObject* a_textWidget, float a_size)
	{
		// copy the widget's FSlateFontInfo, change Size, hand it to SetFont (as Menus Revamped's ApplyFont does)
		static auto* fontInfo = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo");
		if (!a_textWidget || !fontInfo) {
			return false;
		}
		auto*      cls = a_textWidget->GetClass();
		const auto fontOff = reflect::Offset(cls, "Font");
		const auto fontSize = reflect::Size(cls, "Font");
		const auto sizeOff = reflect::Offset(fontInfo, "Size");
		if (fontOff < 0 || fontSize <= 0 || sizeOff < 0 || reflect::Size(fontInfo, "Size") != 4 || sizeOff + 4 > fontSize) {
			return false;
		}
		Call set(a_textWidget, L"SetFont");
		void* at = set ? set.At("InFontInfo") : nullptr;
		if (!at || set.Size("InFontInfo") != fontSize) {
			return false;
		}
		std::memcpy(at, reflect::At<std::uint8_t>(a_textWidget, fontOff), static_cast<std::size_t>(fontSize));
		std::memcpy(static_cast<std::uint8_t*>(at) + sizeOff, &a_size, sizeof(a_size));
		return set.Run();
	}

	UE::UObject* Load(const wchar_t* a_path)
	{
		if (!a_path) {
			return nullptr;
		}
		if (auto* o = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path)) {
			return o;
		}
		// not loaded yet: MakeSoftObjectPath -> Conv_SoftObjPathToSoftObjRef -> LoadAsset_Blocking (Menus Revamped)
		static auto* lib = Class(L"/Script/Engine.KismetSystemLibrary");
		auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!cdo) {
			return nullptr;
		}
		Call       mk(cdo, L"MakeSoftObjectPath");
		Call       conv(cdo, L"Conv_SoftObjPathToSoftObjRef");
		Call       load(cdo, L"LoadAsset_Blocking");
		void*      path = mk.At("PathString");
		const auto pathSize = mk.Size("ReturnValue");
		const auto refSize = conv.Size("ReturnValue");
		if (!mk || !conv || !load || !path || pathSize <= 0 || refSize <= 0 || conv.Size("SoftObjectPath") != pathSize || load.Size("Asset") != refSize) {
			return nullptr;
		}
		new (path) UE::FString(a_path);   // never destroyed: a small one-time leak per asset (as in Menus Revamped)
		mk.Run();
		std::memcpy(conv.At("SoftObjectPath"), mk.At("ReturnValue"), static_cast<std::size_t>(pathSize));
		conv.Run();
		std::memcpy(load.At("Asset"), conv.At("ReturnValue"), static_cast<std::size_t>(refSize));
		load.Run();
		return load.Get<UE::UObject*>("ReturnValue");
	}

	bool SetFont(UE::UObject* a_textWidget, const wchar_t* a_fontAsset, const wchar_t* a_typeface, float a_size)
	{
		// a copy of the widget's FSlateFontInfo with another font object (and face) and size, handed to SetFont; the live
		// property is not changed first (Menus Revamped: the setter may then see no change)
		static auto* info = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo");
		auto*        font = Load(a_fontAsset);
		if (!a_textWidget || !info || !font) {
			return false;
		}
		auto*      cls = a_textWidget->GetClass();
		const auto fontOff = reflect::Offset(cls, "Font");
		const auto fontSize = reflect::Size(cls, "Font");
		const auto objOff = reflect::Offset(info, "FontObject");
		const auto faceOff = reflect::Offset(info, "TypefaceFontName");
		const auto sizeOff = reflect::Offset(info, "Size");
		if (fontOff < 0 || fontSize <= 0 || fontSize > 512 || objOff < 0 || faceOff < 0 || sizeOff < 0 || reflect::Size(info, "Size") != 4 ||
			objOff + 8 > fontSize || faceOff + 8 > fontSize || sizeOff + 4 > fontSize) {
			return false;
		}
		const auto*               current = reflect::At<std::uint8_t>(a_textWidget, fontOff);
		std::vector<std::uint8_t> copy(current, current + fontSize);
		std::memcpy(copy.data() + objOff, &font, sizeof(font));
		if (a_typeface) {
			const UE::FName face(a_typeface);
			std::memcpy(copy.data() + faceOff, &face, sizeof(face));
		}
		std::memcpy(copy.data() + sizeOff, &a_size, sizeof(a_size));
		return CallFirst(a_textWidget, L"SetFont", copy.data(), copy.size());
	}

	bool RoundedBorder(UE::UObject* a_border, const float a_fill[4], const float a_outline[4], float a_width, float a_radius)
	{
		// Menus Revamped's RoundedBox (engine/UiWidgets.cpp), for a Border's Background brush instead of an Image's Brush
		static auto* brushStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
		static auto* outlineStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrushOutlineSettings");
		static auto* colorStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateColor");
		if (!a_border || !brushStruct || !outlineStruct || !colorStruct || !a_border->GetClass()) {
			return false;
		}
		const auto brushOff = reflect::Offset(a_border->GetClass(), "Background");
		const auto brushSize = reflect::Size(a_border->GetClass(), "Background");
		const auto drawAs = reflect::Offset(brushStruct, "DrawAs");
		const auto tint = reflect::Offset(brushStruct, "TintColor");
		const auto outline = reflect::Offset(brushStruct, "OutlineSettings");
		const auto radii = reflect::Offset(outlineStruct, "CornerRadii");
		const auto radiiSize = reflect::Size(outlineStruct, "CornerRadii");
		const auto oColor = reflect::Offset(outlineStruct, "Color");
		const auto rounding = reflect::Offset(outlineStruct, "RoundingType");
		const auto width = reflect::Offset(outlineStruct, "Width");
		const auto spec = reflect::Offset(colorStruct, "SpecifiedColor");
		const auto rule = reflect::Offset(colorStruct, "ColorUseRule");
		if (brushOff < 0 || brushSize <= 0 || brushSize > 1024 || drawAs < 0 || tint < 0 || outline < 0 || radii < 0 || oColor < 0 ||
			rounding < 0 || width < 0 || spec < 0 || rule < 0) {
			return false;
		}
		// built in a copy: the live brush is only changed through SetBrush
		const auto*               live = reflect::At<std::uint8_t>(a_border, brushOff);
		std::vector<std::uint8_t> b(live, live + brushSize);
		b[static_cast<std::size_t>(drawAs)] = 4;   // ESlateBrushDrawType::RoundedBox
		std::memcpy(b.data() + tint + spec, a_fill, sizeof(float) * 4);
		b[static_cast<std::size_t>(tint + rule)] = 0;   // ESlateColorStylingMode::UseColor_Specified
		std::memcpy(b.data() + outline + oColor + spec, a_outline, sizeof(float) * 4);
		b[static_cast<std::size_t>(outline + oColor + rule)] = 0;
		if (radiiSize == 32) {
			const double v[4] = { a_radius, a_radius, a_radius, a_radius };
			std::memcpy(b.data() + outline + radii, v, sizeof(v));
		} else if (radiiSize == 16) {
			const float v[4] = { a_radius, a_radius, a_radius, a_radius };
			std::memcpy(b.data() + outline + radii, v, sizeof(v));
		}
		b[static_cast<std::size_t>(outline + rounding)] = 0;   // ESlateBrushRoundingType::FixedRadius
		std::memcpy(b.data() + outline + width, &a_width, sizeof(a_width));
		return CallFirst(a_border, L"SetBrush", b.data(), b.size());
	}
}
