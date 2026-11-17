#pragma once

// Gear Loadouts - building and reading UMG widgets through reflection.
//
// Adapted from Menus Revamped (Oblivion Remastered) by cfa911, GPL-3.0-or-later (see NOTICE.md): engine/UiTree.cpp
// (FindInTree, ShownOnScreen), engine/UiWidgets.cpp (AddToCanvas, Measure, ViewportScale), engine/UiText.cpp (SetText, Ink).
// Modified 2026-11-17: cut down to what the loadout bar needs; Prop / Slots / Content helpers added.
//
// Game thread only (GameThread.h).

#include <string>
#include <string_view>
#include <vector>

namespace ui
{
	UE::UObject*              Prop(UE::UObject* a_object, std::string_view a_name);   // an object property by name
	std::vector<UE::UObject*> Slots(UE::UObject* a_panel);       // a PanelWidget's slots (empty when not a panel)
	UE::UObject*              Content(UE::UObject* a_slot);      // a PanelSlot's content widget
	UE::UObject*              TreeRoot(UE::UObject* a_userWidget);   // a UserWidget's WidgetTree.RootWidget

	// a widget by name anywhere under a user widget (its tree, panels' slots, nested user widgets' trees);
	// bounded and SEH-guarded (a widget being built or destroyed can hold a garbage slot pointer)
	UE::UObject* FindInTree(UE::UObject* a_userWidget, std::string_view a_name);

	// on screen for real: the widget and every widget above it visible and not faded out; under a WidgetSwitcher only
	// the active child counts (IsVisible alone lies there)
	bool ShownOnScreen(UE::UObject* a_widget);

	// a new widget of a_classPath inside a CanvasPanel; *a_slot gets its CanvasPanelSlot
	UE::UObject* AddToCanvas(UE::UObject* a_canvas, const wchar_t* a_classPath, const wchar_t* a_name, UE::UObject** a_slot);
	// a new widget of a_classPath with a_outerTree as outer (to be put into a panel or content widget by the caller)
	UE::UObject* NewWidget(UE::UObject* a_outerTree, const wchar_t* a_classPath, const wchar_t* a_name);

	// where a laid-out widget is on the viewport (viewport units): top-left and size (world-context calls, guarded)
	bool  Measure(UE::UObject* a_widget, double& a_x, double& a_y, double& a_width, double& a_height);
	float ViewportScale();   // WidgetLayoutLibrary.GetViewportScale (viewport units -> pixels); 1 when unreadable

	bool SetText(UE::UObject* a_textWidget, const std::string& a_utf8);
	void Ink(UE::UObject* a_textWidget, const float a_rgba[4]);   // text colour (linear RGBA), no shadow
}
