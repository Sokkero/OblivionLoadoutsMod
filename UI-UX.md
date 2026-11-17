# UI / UX plan: loadout buttons and rename popup

Status: **plan only. Nothing here is built or tested.** Written 2026-11-17 from a source review of Menus Revamped (MR),
Simple Loadout System (SLS) and CommonLibOB64. Items marked **Not verified** are not proven in game.

Source paths below:
- `MR:` = `Oblivion-Remastered---Mod---Menus-Revamped/src/` (GPL-3.0-or-later, "Copyright (C) 2026 cfa911")
- `SLS:` = `SimpleLoadoutSystemOR/src/` (GPL-3.0-or-later, "Copyright (C) 2026 ApocryphaRealm")
- `CL:` = `lib/commonlibob64/include/`

---

## 1. Target UX

### 1.1 Buttons
- 5 buttons in the inventory menu: **Default, Loadout 1, Loadout 2, Loadout 3, Loadout 4** (or the saved names).
- The active loadout is highlighted.
- Mouse hover: light highlight (so the user sees the button is clickable).
- **Left click on an inactive button** = switch to that loadout.
- **Left click on the active button** = nothing.
- **Right click on any button** = open the rename popup for that button.
- In combat: the switch is refused, the HUD message shows (already built), the highlight does not change.
- The hotkeys (Ctrl+Numpad0-4) keep working. The highlight follows a hotkey switch while the menu is open.
- Only in the inventory menu. Not in container, barter or other menus.
- Position (user, 2026-11-17): **a row between the category tab strip and the table** (see section 3.3).
- No sounds. No hint text or tooltip (user, 2026-11-17).

### 1.2 Rename popup
- Opens near the clicked button. Title: "Rename loadout".
- One text field. It starts with the current name, all text selected (typing replaces it).
- Buttons: **Save** and **Cancel**.
- Keys while the popup is open:
  - Printable characters: typed into the field.
  - Backspace: delete the last character. Ctrl+A: select all. Delete: clear.
  - **Enter = Save** (only if the name is valid). **Esc = Cancel.**
  - All other keys: swallowed. The game must not react (no "I" closing the menu, no Tab, no Esc closing the inventory).
- Validation:
  - Leading and trailing whitespace is trimmed.
  - Empty after trimming = invalid. Save is shown greyed out and does nothing.
  - Cap: **20 characters**. Extra characters are not accepted.
  - **Any printable character is allowed** (user, 2026-11-17), not only ASCII. Control characters are rejected.
    One character = one Unicode code point (a surrogate pair counts as one).
    **Not verified:** that the game font draws every character. Missing glyphs may show as boxes.
- Mouse while the popup is open:
  - Click on Save / Cancel: as above.
  - Click inside the popup elsewhere: nothing.
  - Click outside the popup: **Cancel** (user, 2026-11-17). The click is swallowed (it does not reach the list).
  - Right click: nothing.
- The popup blocks hover and clicks on the inventory list behind it.
- If the inventory closes while the popup is open (for example, by a script): the popup closes, nothing is saved.

---

## 2. What the research found

### 2.1 Why SLS buttons do not show on this PC
SLS runs **all** UI work (class watch, self-check, bar build) from its XInputGetState import chain (`SLS: main.cpp:40-81`,
`GameThread.cpp:70-83`). We proved in game that this chain stops after about 4 calls on this PC (log 2026-11-17 21:39).
So the SLS bar is never built. Strong hypothesis; it matches our own test.

**Lesson: do not drive UI from the XInput import.** We already have a working per-frame hook on `UEngine::Tick`
(`src/GameThread.cpp`, tested 2026-11-17 21:42).

### 2.2 CommonLibOB64 has almost no UI support
- No UMG or Slate types: no UWidget, UButton, UTextBlock, UCanvasPanel, UWidgetTree, no CreateWidget/AddChild/SetText.
- Present: ProcessEvent (vtable 0x4D), `FindFunction`, `StaticFindObject`, `NewObject` / `StaticConstructObject_Internal`
  (ID 117210), FText from string (ID 108973, `CL: UE/F/FText.h:45-82`), FString, FName, `FUObjectArray`.
- So every UI call goes through **reflection + ProcessEvent** with the parameter frame filled by property name.
  MR and SLS both do this. Our `src/Console.cpp` already does it for one function (proven in game).

### 2.3 Menus Revamped works on this PC and has every piece we need
| Need | MR technique | Where |
|---|---|---|
| Inventory open / close | Watch `BP_OnActivated` / `BP_OnDeactivated` on `WBP_OriginalMenu_Inventory_C` (ProcessEvent vtable swap); also `InterfaceManager::menuMode` | `MR: engine/Menus.cpp:11-173`, `engine/PEHook.cpp` |
| Add own widgets | Own Image + TextBlock as children of the menu's root CanvasPanel, `SetPosition` / `SetSize` / `SetZOrder` | `MR: search/SearchBar.cpp:64-81, 180-240` |
| Call UMG functions | `ui::Call`: fill params by name (offset +0x44, size +0x34), guarded ProcessEvent | `MR: engine/UiCall.cpp:13-58` |
| Text, colour, font | `SetText` with `FText::AsCultureInvariant`; `SetColorAndOpacity`; copy game font | `MR: engine/UiText.cpp:44-69`, `sorting/PickerWidgets.cpp:91-128` |
| Highlight | Image brush as RoundedBox, `SetBrush`; theme colours | `MR: engine/UiWidgets.cpp:65-105`, `core/UiTheme.h` |
| Mouse clicks, left **and right** | Own pixel-rect hit test in a WndProc subclass of `UnrealWindow`; swallow DOWN and matching UP | `MR: core/Runtime.cpp:31-67`, `sorting/PickerInput.cpp:49-98` |
| Rects in pixels | `GetCachedGeometry` → `LocalToViewport` × `GetViewportScale`, re-measured every 100-150 ms on the game thread | `MR: engine/UiWidgets.cpp:136-226` |
| Text input | WndProc: swallow every `WM_KEYDOWN`, read `WM_CHAR`, draw text in a TextBlock with a fake caret | `MR: search/SearchInput.cpp:21-262`, `SearchBar.cpp:270-280` |
| Popup | CanvasPanel at ZOrder 4000 with a `Visible` background (blocks hover/clicks); click outside = close; Esc closes and its KEYUP is swallowed | `MR: sorting/PickerPopup.cpp:17-92`, `PickerInput.cpp:54-94` |
| Crash safety | SEH-guarded ProcessEvent; weak handles (pointer + internalIndex); `Usable` checks; rebuild when the menu instance changes; never call UE from the WndProc | `MR: engine/UiCall.cpp`, `engine/Reflect.h:40-57`, `Reflect.cpp:164-174`, `PickerTick.cpp:161-174, 352` |
| Find widget names | Census: dump of the widget tree to a file | `MR: devtools/Census.cpp` |

**Not verified:** right click via WndProc in our plugin. MR does it on its sort header (`PickerInput.cpp:73-79`) and MR
works on this PC, so this is likely.

### 2.4 Conflicts with Menus Revamped (the user has it installed)
1. **MR rebuilds the native sort-bar ScrollBox** (`ClearChildren` + re-add). Our buttons must **not** go into that
   ScrollBox. Put them in the root CanvasPanel instead (as MR's own search bar does).
2. **MR's search bar** sits at about `{x 293, y 109.5, w 482, h 36}` (`MR: search/SearchConfig.h:35`) and takes clicks
   there. Our buttons must not overlap it, and must not overlap MR's sort header.
3. **WndProc subclass:** both plugins subclass `UnrealWindow`. This works if each calls `CallWindowProcW(previous)`.
   The last installed sees messages first. MR installs within about 1 s of start. We install at the first inventory
   open, so we are probably first in line. **Not verified.**
4. **MR typing mode:** MR exports `MenusRevamped_IsTyping()` (`MR: core/Plugin.cpp:21`). While MR is typing (its
   search bar), our plugin must not react to clicks or keys. While our popup is open, we swallow keys first.
5. **ProcessEvent vtable swap** on `WBP_OriginalMenu_Inventory_C`: MR swaps slot 0x4D. If we also swap, we must save
   the current value and call it, and never restore it. **Plan: avoid the swap. Poll instead** (section 3.2).

---

## 3. Design

### 3.1 Threads (three, keep them apart)
| Thread | What runs there |
|---|---|
| **Unreal game thread** (`UEngine::Tick` hook, `src/GameThread.cpp`) | Every UMG call: find menu, build widgets, measure rects, set text and colours, show/hide popup |
| **Window thread** (WndProc subclass) | Only reads and writes plain C++ state under a mutex: mouse position, clicked button, typed text. **No UE call.** Returns 0 to swallow a message |
| **TES thread** (`src/TesThread.cpp`) | The loadout switch (unchanged) |

Flow for a click: WndProc hit-tests the cached rects → queues a command → game thread runs it next tick →
`loadouts::Request(i)` → TES thread switches → posts "refresh highlight" back to the game thread.

**Not verified:** whether the window thread and the game thread are the same thread in this game (MR's code works either
way, because of the mutex).

### 3.2 Menu detection (polling, no vtable swap)
Each tick (cheap check first):
1. `InterfaceManager::menuMode` (`CL: RE/I/InterfaceManager.h`, offset 0x10). If gameplay: inventory is closed.
2. Otherwise, every 200 ms: find the live `WBP_OriginalMenu_Inventory_C` instance (class via `StaticFindObject`,
   instance via the object array, skipping the CDO and archetypes; `MR: engine/Reflect.cpp:196-218`) and check that it
   is shown (`MR: ui::ShownOnScreen`, because `IsVisible` lies under a WidgetSwitcher).
3. Open → build (once per instance). Closed or instance changed → tear down and forget all handles.

This also covers the first open of a session, which SLS misses (the class is only loaded on first open).
Fallback if polling is not reliable: the ProcessEvent watch (MR's pattern), with chaining.

### 3.3 Widgets (MR search-bar pattern)
- Parent: the root CanvasPanel of the user widget that owns the inventory list (`MR: SearchBar.cpp:64-81`). Exact
  name to be confirmed with the census (step 2).
- Per button: one Image (RoundedBox brush, colour by state) + one TextBlock (centred label, game font, no wrap, clipped).
  Created with `NewObject(outer = widgetTree)` and `AddChildToCanvas`; positioned with `SetPosition` / `SetSize` /
  `SetZOrder` (about 1000).
- **No UButton.** Clicks come from our own hit test, so no delegate binding is needed (binding a native C++ callback to
  a script delegate is not supported by CommonLib).
- New widgets start with `SetRenderOpacity 0` and fade in after the first layout pass (`MR: PickerHeader.cpp:205`).
- Position (user, 2026-11-17, screenshot `inv.png`, 2560x1440 with MR): **between the category tab strip
  (the A / D icon tabs: weapons, armour, potions, misc, ...) and the table** (the "Weapons" title row with MR's sort
  icons A-Z / heart / sword / weight / coin, then the item rows).
  - From the screenshot: MR's "Search" bar is **above** the category tabs, so it does not conflict.
  - There is **no free gap** between the tab strip and the title row. The table must move down by the row height
    (about 40-50 px at 1440p). The list has free space at the bottom, so only very long lists lose a visible row.
  - Width: the 5 buttons share the parchment width (about the width of the tab strip).
  - How: insert our row into the vertical container that holds the tab strip and the table, at the index after the
    tab strip. SLS did a similar insert (`SLS: Bar.cpp:881-923`: re-add siblings, re-apply slots, then re-activate
    the list's navigation). The census (step 2) gives the exact container and index.
  - **Do not** use the sort-header ScrollBox (MR rebuilds it, section 2.4).
  - Risk: re-adding siblings of the table may clash with MR's own layout changes in `InventoryHeaders.cpp`
    (**not verified**). Fallback: a canvas overlay at a measured position plus a top padding on the table.
- Button states: normal / hover / active / active+hover / disabled (in combat, optional).

### 3.4 Input (WndProc subclass on `UnrealWindow`)
- Install with `SetWindowLongPtrW(GWLP_WNDPROC)` at the first inventory open. Always pass unhandled messages to the
  previous WndProc. Never uninstall (another plugin may have chained after us).
- When the inventory is not open: pass every message through, do nothing.
- Buttons mode (popup closed):
  - `WM_MOUSEMOVE`: update the hover index (not swallowed).
  - `WM_LBUTTONDOWN` in a button rect: swallow, queue "switch i", swallow the matching `WM_LBUTTONUP` and a
    double-click within `GetDoubleClickTime()`.
  - `WM_RBUTTONDOWN` in a button rect: swallow, queue "open rename i", swallow the matching `WM_RBUTTONUP`.
  - Outside our rects: pass through.
  - If `MenusRevamped_IsTyping()` is true: pass everything through.
- Popup mode: section 1.2. Swallow all `WM_KEYDOWN`, `WM_KEYUP`, `WM_CHAR`, `WM_SYSKEYDOWN` (except Alt+Tab / Alt+F4,
  let Windows handle those). Swallow all mouse buttons; act on our rects only.
- Text filter: any printable character (Unicode, `WM_CHAR` is UTF-16; join surrogate pairs), no control characters,
  max 20 code points. Stored as UTF-8 in the JSON. Shown through FText (not through the console, so the ASCII limit of
  `console::Notify` does not apply). IME input: MR handles it (`MR: SearchInput.cpp:196-222`); copy that part.
- Rects: measured on the game thread every 100 ms while the menu is open (`MR: ui::Measure`, `GetViewportScale`),
  stored under the mutex. Rects are in client pixels, so window size, fullscreen and UI scale are handled by the
  measure step. **Not verified** for every resolution: test matrix in section 5.

### 3.5 Popup
- One CanvasPanel at ZOrder 4000, child of the same root canvas. Starts Collapsed (visibility 1).
- Inside: background Image (`Visible`, so it blocks hover and clicks on the list), title TextBlock, field Image +
  TextBlock (text + blinking caret `|`), Save Image + label, Cancel Image + label.
- Position: under (or above) the clicked button, clamped inside the canvas.
- Save greyed out while the trimmed text is empty.

### 3.6 Data (step 7; the player key is done)
- `GetName(i)` / `SetName(i, text)` in `Loadouts.cpp`. Names saved in `GearLoadouts.json`, per character.
- Defaults: "Default", "Loadout 1".."Loadout 4". A missing or invalid saved name falls back to the default.
- **Prerequisite:** the player key (open bug 2). Today all characters share one key, so names (and loadouts) are
  shared across characters. See open question Q5.

### 3.7 Code layout (new files)
| File | Content | Source |
|---|---|---|
| `src/ui/Reflect.*` | Offset/size by name, weak handle, `Usable`, instances | from `src/Console.cpp` + MR `engine/Reflect.*` |
| `src/ui/UiCall.*` | Call a UFunction by name with named params, SEH-guarded | MR `engine/UiCall.*` |
| `src/ui/UiWidgets.*` | Create Image / TextBlock / CanvasPanel, add to canvas, brush, text, colour, font, measure | MR `engine/UiWidgets.*`, `UiText.*` |
| `src/ui/InventoryMenu.*` | Open/close detection (polling) | new, idea from MR `engine/Menus.cpp` |
| `src/ui/LoadoutBar.*` | 5 buttons, layout, states, commands | new |
| `src/ui/RenamePopup.*` | Popup, text field, validation | new, idea from MR `PickerPopup.cpp`, `SearchInput.cpp` |
| `src/ui/WindowInput.*` | WndProc subclass, hit test, key capture | MR `core/Runtime.cpp`, `PickerInput.cpp`, `SearchInput.cpp` |
| `src/devtools/Census.*` | Widget tree dump (dev builds only) | MR `devtools/Census.cpp` |

`src/Console.cpp` then uses the shared `ui/Reflect`. `NOTICE.md` gets an MR row for every copied part.

---

## 4. Build steps (one at a time; user tests after each)

Each step: build → user test → log check → next step. No step starts before the previous one passed.

| # | Step | User test | Log lines |
|---|---|---|---|
| 0 | ~~Player key (bug 2)~~ **Done and tested 2026-11-17** (character GUID, `src/CharacterKey.cpp`). Names moved to step 7 (user, 2026-11-17: UI first, renaming last) | — | — |
| 1 | **Done 2026-11-17.** UI base: `ui/Reflect`, `ui/UiCall`, `ui/UiWidgets` (no visible change) | Game starts, hotkeys still work | `ui: property offsets proven` |
| 2 | **Done 2026-11-17** (`GearLoadouts-census-*.txt`). Census dump on a dev hotkey (**Ctrl+Shift+F11**; MR uses F12) | Open inventory, press the key, send the dump file | `census: written <file>` |
| 3 | **Done 2026-11-17** (polling in menu mode, 50 ms; the game makes a new inventory widget on most opens). Menu detection | Open / close inventory several times, also the first open after start | `ui: inventory opened (instance …)`, `ui: inventory closed` |
| 4 | **Done 2026-11-17** (row built before the menu fades in: no delay, no table jump; tested across tab and menu switches). One static test button (no input) at a fixed place | Button shows; stays after tab change / reopen; no crash on close, on quit to main menu, on load | `ui: bar built`, `ui: bar removed` |
| 5 | WndProc + hit test, left click on the test button only logs | Click logs once; clicks elsewhere still work; MR search bar still works | `ui: click on button 0 (left)` |
| 6 | 5 buttons with **hardcoded names** ("Default", "Loadout 1".."Loadout 4"), highlight, hover, left click = switch, highlight follows hotkeys, combat refusal | All switch tests; combat message; active click does nothing | `ui: switch requested i`, existing switch lines |
| 7 | Names storage (was step 0): 5 names per character in the JSON, `GetName` / `SetName` with validation; labels from the JSON | Edit names in the JSON by hand, reopen menu | `ui: labels …` |
| 8 | Right click opens the popup with fixed text; Cancel / Esc / click outside close it; Esc does not close the inventory | As stated | `ui: rename popup open i`, `… closed (cancel)` |
| 9 | Typing in the field; all game keys swallowed while open | Type "I", Tab, Esc, letters, Backspace, 25 characters | `ui: typed length n` (no text in the log) |
| 10 | Save writes the name; empty / whitespace rejected; Enter = Save | Rename, reopen menu, restart game | `ui: renamed i` |
| 11 | Polish: theme colours, final position (no sounds, no hint text: user decision) | Test matrix (section 5) | — |

Remove the census hotkey and extra logs before a public release.

---

## 5. Test matrix (steps 4-11)
- Resolutions: 1080p, 1440p, 4K; windowed, borderless, fullscreen; game UI scale settings.
- With and without Menus Revamped. MR search bar typing (Ctrl+F) while our popup is closed and open.
- First inventory open after start; reopen; switch inventory tabs; drop / equip items with the bar shown.
- Close the inventory with I, Tab, Esc; while the popup is open.
- Alt+Tab while typing; console (`~`) while the popup is open.
- Quit to main menu with the inventory open; load a save with the inventory open; save and load.
- In combat: click a button.

---

## 6. Risks
| Risk | Effect | Mitigation |
|---|---|---|
| A UMG call crashes (world torn down, GC'd widget) | Game crash | SEH-guarded calls; weak handles; rebuild on instance change; refuse when parent or child not `Usable` (MR RMI-043) |
| Hit rects wrong at some resolution / UI scale | Clicks miss | Measure from geometry every 100 ms; test matrix |
| Our WndProc and MR's fight over keys | Typing goes to the wrong mod | Check `MenusRevamped_IsTyping()`; swallow only while our popup is open |
| MR or a game patch changes the inventory widget tree | Buttons do not show | Find the parent by name with fallbacks; log a clear `ui: …` problem line; census to re-find |
| Text in FText with a null pointer | Engine crash (MR, SLS lesson) | Only build FText through the constructor; never write zeroes |
| A game update | Offsets / IDs move | Address Library IDs; the reflection self-check refuses to run on a wrong layout |

---

## 7. Decisions (user, 2026-11-17)
1. Q1 position: **top, above the item list, below the inventory tabs.**
2. Q2 click outside the popup: **Cancel.**
3. Q3: **Enter = Save, Esc = Cancel.**
4. Q4 characters: **any printable character.**
5. Q5 player key first (step 0): **yes.** Candidate key: the per-character GUID in the save-file names
   (`<slot> - <name> (<GUID>).sav`). Where it lives at runtime: being probed (`src/devtools/KeyProbe.cpp`).
6. Q6 sounds: **no.**
7. Q7 hint text: **no.**

Screenshot received: `inv.png` (project root).
