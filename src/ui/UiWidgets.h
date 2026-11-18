#pragma once

// Building and reading UMG widgets through reflection. Game thread only.
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md).

#include <string>
#include <string_view>
#include <vector>

namespace ui
{
	UE::UObject*              Prop(UE::UObject* a_object, std::string_view a_name);   // object property by name
	std::vector<UE::UObject*> Slots(UE::UObject* a_panel);                            // a PanelWidget's slots
	UE::UObject*              Content(UE::UObject* a_slot);                           // a PanelSlot's widget
	UE::UObject*              TreeRoot(UE::UObject* a_userWidget);                    // WidgetTree.RootWidget

	// a widget by name anywhere under a user widget (nested trees too); bounded and SEH-guarded
	UE::UObject* FindInTree(UE::UObject* a_userWidget, std::string_view a_name);

	// the widget and every parent are visible and not faded out; under a WidgetSwitcher only the active child counts
	bool ShownOnScreen(UE::UObject* a_widget);

	UE::UObject* NewWidget(UE::UObject* a_outerTree, const wchar_t* a_classPath, const wchar_t* a_name);
	// a new widget as a child of a CanvasPanel; *a_slot gets its CanvasPanelSlot
	UE::UObject* AddToCanvas(UE::UObject* a_canvas, const wchar_t* a_classPath, const wchar_t* a_name, UE::UObject** a_slot);

	bool  Measure(UE::UObject* a_widget, double& a_x, double& a_y, double& a_width, double& a_height);   // viewport units
	float ViewportScale();   // viewport units -> pixels; 1 when unreadable

	bool SetText(UE::UObject* a_textWidget, const std::string& a_utf8);
	void Ink(UE::UObject* a_textWidget, const float a_rgba[4]);   // text colour (linear RGBA), no shadow
	// font asset ("/Game/X/Y.Y") and typeface at a size; a missing asset or nullptr typeface keeps the current one
	bool SetFont(UE::UObject* a_textWidget, const wchar_t* a_fontAsset, const wchar_t* a_typeface, float a_size);
	// a Border's background as a rounded box with fill and outline (linear RGBA)
	bool RoundedBorder(UE::UObject* a_border, const float a_fill[4], const float a_outline[4], float a_width, float a_radius);
}
