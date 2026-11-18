# Gear Loadouts (Oblivion Remastered) - copyright and licence

This work contains code adapted from
**Simple Loadout System (Oblivion Remastered)**, https://github.com/ApocryphaRealm/SimpleLoadoutSystemOR. The adapted parts are:

| File | What is taken | What is changed |
|---|---|---|
| `src/Scan.h`, `src/Logger.h`, `src/pch.h` | Byte-pattern scan, logger wrapper, precompiled header | Trimmed |
| `src/TesThread.*` | Hook on the TES main loop and its task queue | Per-iteration tick added |
| `src/GameThread.*` | Task queue on the Unreal game thread | Driven by a hook on `UEngine::Tick` instead of the XInput import |
| `src/Loadouts.*` | Reading worn items (`ExtraContainerChanges`, `Worn` / `WornLeft`), the `UnequipObject` / `EquipObject` calls and their rules | Nothing is moved into containers. A loadout is a saved list of forms |
| `src/Console.*` | `ExecuteConsoleCommand` through `ProcessEvent` | Only the HUD message; built on `ui::Call` |
| `src/Push.*` | Byte pattern of the inventory list rebuild | Log lines |
| `src/Reflect.*` | Property-offset reader, self-check, `IsLive`, `Instances` | `Size()`, `FirstOffset()` added |
| `xmake.lua`, `.gitmodules` | Build setup | New project name |
| `LICENSE`, `EXCEPTIONS`, `THIRD_PARTY_NOTICES.md` | Unchanged | - |

This work contains code adapted from **Menus Revamped (Oblivion Remastered)**,
https://github.com/cfa911/Oblivion-Remastered---Mod---Menus-Revamped The
adapted parts are:

| File | What is taken | What is changed |
|---|---|---|
| `src/ui/UiCall.*` | `Call` (parameters by name), guarded `ProcessEvent`, `CallFirst`, the player-controller search | No ProcessEvent watcher; `Name()` added |
| `src/Reflect.*` | `Usable()`, `Handle` / `Hold` / `Get` | - |
| `src/ui/UiWidgets.*` | `FindInTree`, `ShownOnScreen`, `AddToCanvas`, `Measure`, `SetText`, `Ink`, font loading, rounded box brush | Cut down; `Prop`, `Slots`, `Content`, `TreeRoot`, `NewWidget` added |
| `src/ui/WindowInput.*` | Subclassing `UnrealWindow`; swallowing clicks, their button-up and double clicks; typing through `WM_CHAR` | Hit rects, a click queue and one text field; key-state imports of other modules blocked while typing |
| `src/ui/InventoryMenu.*` | Inventory class path and the visibility-based open check | Polling instead of a ProcessEvent watcher |
| `src/ui/LoadoutBar.*` | Colour palette and game fonts | - |

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program (`LICENSE`). If not, see
<https://www.gnu.org/licenses/>.

SPDX-License-Identifier: GPL-3.0-or-later

The plugin links CommonLibOB64 (GPL-3.0), MinHook (BSD-2-Clause) and nlohmann/json (MIT). Notices: `THIRD_PARTY_NOTICES.md`.
