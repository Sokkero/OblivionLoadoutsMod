// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Menus Revamped by cfa911 (see NOTICE.md).

#include "ui/UiWidgets.h"

#include "Reflect.h"
#include "Text.h"
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

		// a widget being built or destroyed can hold a garbage slot pointer
		UE::UObject* SearchGuarded(UE::UObject* a_w, std::string_view a_name)
		{
			__try {
				int budget = 4000;
				return Search(a_w, a_name, 0, budget);
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return nullptr;
			}
		}

		float RenderOpacity(UE::UObject* a_widget)
		{
			const auto* at = reflect::At<float>(a_widget, reflect::Offset(a_widget->GetClass(), "RenderOpacity"));
			return at ? *at : 1.0f;
		}

		UE::UObject* Default(const wchar_t* a_classPath)
		{
			auto* cls = Class(a_classPath);
			return cls ? cls->GetDefaultObject(false) : nullptr;
		}

		// an asset by object path: the loaded one, else loaded now through KismetSystemLibrary
		UE::UObject* Load(const wchar_t* a_path)
		{
			if (!a_path) {
				return nullptr;
			}
			if (auto* o = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path)) {
				return o;
			}
			auto*      cdo = Default(L"/Script/Engine.KismetSystemLibrary");
			Call       mk(cdo, L"MakeSoftObjectPath");
			Call       conv(cdo, L"Conv_SoftObjPathToSoftObjRef");
			Call       load(cdo, L"LoadAsset_Blocking");
			void*      path = mk.At("PathString");
			const auto pathSize = mk.Size("ReturnValue");
			const auto refSize = conv.Size("ReturnValue");
			if (!mk || !conv || !load || !path || pathSize <= 0 || refSize <= 0 || conv.Size("SoftObjectPath") != pathSize || load.Size("Asset") != refSize) {
				return nullptr;
			}
			new (path) UE::FString(a_path);   // never destroyed: a tiny one-time leak per asset
			mk.Run();
			std::memcpy(conv.At("SoftObjectPath"), mk.At("ReturnValue"), static_cast<std::size_t>(pathSize));
			conv.Run();
			std::memcpy(load.At("Asset"), conv.At("ReturnValue"), static_cast<std::size_t>(refSize));
			load.Run();
			return load.Get<UE::UObject*>("ReturnValue");
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
		const auto* arr = reflect::At<RawArray>(a_panel, reflect::Offset(panelWidget, "Slots"));
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
		return Prop(Prop(a_userWidget, "WidgetTree"), "RootWidget");
	}

	UE::UObject* FindInTree(UE::UObject* a_userWidget, std::string_view a_name)
	{
		return SearchGuarded(a_userWidget, a_name);
	}

	bool ShownOnScreen(UE::UObject* a_widget)
	{
		static auto* widgetClass = Class(L"/Script/UMG.Widget");
		static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		static auto* slotClass = Class(L"/Script/UMG.PanelSlot");
		static auto* switcherClass = Class(L"/Script/UMG.WidgetSwitcher");
		const auto   slotOff = widgetClass ? reflect::Offset(widgetClass, "Slot") : -1;
		const auto   parentOff = slotClass ? reflect::Offset(slotClass, "Parent") : -1;
		UE::UObject* w = a_widget;
		for (int depth = 0; w && depth < 40; ++depth) {
			Call vis(w, L"IsVisible");
			if (!vis) {
				return depth > 0;   // walked above the widgets
			}
			if (!vis.Run() || !vis.Get<bool>("ReturnValue") || RenderOpacity(w) < 0.05f) {
				return false;
			}
			auto*        slot = slotOff >= 0 ? *reflect::At<UE::UObject*>(w, slotOff) : nullptr;
			UE::UObject* next = slot && parentOff >= 0 ? *reflect::At<UE::UObject*>(slot, parentOff) : nullptr;
			if (IsA(next, switcherClass)) {
				Call active(next, L"GetActiveWidget");
				if (active && active.Run() && active.Get<UE::UObject*>("ReturnValue") != w) {
					return false;
				}
			}
			if (!next) {   // a tree's root widget: continue with the user widget owning the tree
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
		auto* w = NewWidget(a_canvas ? a_canvas->GetOuter() : nullptr, a_classPath, a_name);   // outer = the canvas's widget tree
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
		CallFirst(*a_slot, L"SetAutoSize", false);
		return w;
	}

	bool SetText(UE::UObject* a_textWidget, const std::string& a_utf8)
	{
		Call  c(a_textWidget, L"SetText");
		void* at = c.At("InText");
		if (!at) {
			return false;
		}
		const std::wstring w = text::Wide(a_utf8);
		auto*              t = new (at) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
		c.Run();
		t->~FText();
		return true;
	}

	void Ink(UE::UObject* a_textWidget, const float a_rgba[4])
	{
		struct SlateColor
		{
			float        rgba[4];
			std::uint8_t rule;   // UseColor_Specified
			std::uint8_t pad[7];
		} ink{ { a_rgba[0], a_rgba[1], a_rgba[2], a_rgba[3] }, 0, {} };
		CallFirst(a_textWidget, L"SetColorAndOpacity", ink);
		const float noShadow[4] = {};
		CallFirst(a_textWidget, L"SetShadowColorAndOpacity", noShadow);
	}

	bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_width, double& a_height)
	{
		auto* pc = PlayerController();
		auto* cdo = Default(L"/Script/UMG.SlateBlueprintLibrary");
		if (!a_w || !pc || !cdo) {
			return false;
		}
		Call       geo(a_w, L"GetCachedGeometry");
		const auto gsize = geo.Size("ReturnValue");
		if (!geo || gsize <= 0 || !geo.Run()) {
			return false;
		}
		// copy the geometry into another call's Geometry parameter
		const auto withGeometry = [&](Call& a_call) {
			void* g = a_call.At("Geometry");
			if (!g || a_call.Size("Geometry") != gsize) {
				return false;
			}
			std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
			return true;
		};
		Call size(cdo, L"GetLocalSize");
		if (!withGeometry(size)) {
			return false;
		}
		size.Run();
		const auto local = size.Get<std::array<double, 2>>("ReturnValue");
		const auto toViewport = [&](double a_lx, double a_ly, double& a_vx, double& a_vy) {
			Call c(cdo, L"LocalToViewport");
			if (!withGeometry(c)) {
				return false;
			}
			c.Set("WorldContextObject", pc);
			c.Set("LocalCoordinate", std::array{ a_lx, a_ly });
			if (!c.RunGuarded()) {
				return false;
			}
			const auto v = c.Get<std::array<double, 2>>("ViewportPosition");
			a_vx = v[0];
			a_vy = v[1];
			return true;
		};
		double x1 = 0, y1 = 0;
		if (!toViewport(0.0, 0.0, a_x, a_y) || !toViewport(local[0], local[1], x1, y1)) {
			return false;
		}
		a_width = x1 - a_x;
		a_height = y1 - a_y;
		return local[0] > 0.0 && a_width > 1.0 && a_height > 1.0;
	}

	float ViewportScale()
	{
		auto* pc = PlayerController();
		Call  c(Default(L"/Script/UMG.WidgetLayoutLibrary"), L"GetViewportScale");
		if (!c || !pc) {
			return 1.0f;
		}
		c.Set("WorldContextObject", pc);
		return c.RunGuarded() ? c.Get<float>("ReturnValue") : 1.0f;
	}

	bool SetFont(UE::UObject* a_textWidget, const wchar_t* a_fontAsset, const wchar_t* a_typeface, float a_size)
	{
		// edit a copy of the widget's FSlateFontInfo and hand it to SetFont (the setter ignores in-place changes)
		static auto* info = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo");
		if (!a_textWidget || !info) {
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
		if (auto* font = Load(a_fontAsset)) {
			std::memcpy(copy.data() + objOff, &font, sizeof(font));
			if (a_typeface) {
				const UE::FName face(a_typeface);
				std::memcpy(copy.data() + faceOff, &face, sizeof(face));
			}
		}
		std::memcpy(copy.data() + sizeOff, &a_size, sizeof(a_size));
		return CallFirst(a_textWidget, L"SetFont", copy.data(), copy.size());
	}

	bool RoundedBorder(UE::UObject* a_border, const float a_fill[4], const float a_outline[4], float a_width, float a_radius)
	{
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
		// edit a copy; the live brush only changes through SetBrush
		const auto*               live = reflect::At<std::uint8_t>(a_border, brushOff);
		std::vector<std::uint8_t> b(live, live + brushSize);
		b[drawAs] = 4;   // ESlateBrushDrawType::RoundedBox
		std::memcpy(b.data() + tint + spec, a_fill, sizeof(float) * 4);
		b[tint + rule] = 0;   // UseColor_Specified
		std::memcpy(b.data() + outline + oColor + spec, a_outline, sizeof(float) * 4);
		b[outline + oColor + rule] = 0;
		if (radiiSize == 32) {
			const double v[4] = { a_radius, a_radius, a_radius, a_radius };
			std::memcpy(b.data() + outline + radii, v, sizeof(v));
		} else if (radiiSize == 16) {
			const float v[4] = { a_radius, a_radius, a_radius, a_radius };
			std::memcpy(b.data() + outline + radii, v, sizeof(v));
		}
		b[outline + rounding] = 0;   // ESlateBrushRoundingType::FixedRadius
		std::memcpy(b.data() + outline + width, &a_width, sizeof(a_width));
		return CallFirst(a_border, L"SetBrush", b.data(), b.size());
	}
}
