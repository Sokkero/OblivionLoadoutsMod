# Gear Loadouts (Oblivion Remastered) - copyright and licence

Copyright (C) 2026 CHANGE ME (modifications and new code)

Portions copyright (C) 2026 ApocryphaRealm. This work contains code adapted from
**Simple Loadout System (Oblivion Remastered)**, https://github.com/ApocryphaRealm/SimpleLoadoutSystemOR
(GPL-3.0-or-later). The adapted parts are:

| File | What is taken | What is changed |
|---|---|---|
| `src/Logger.h`, `src/Scan.h`, `src/pch.h` | Taken as they are | Nothing |
| `src/TesThread.h`, `src/TesThread.cpp` | The hook on the TES main loop and its task queue | Added `SetTick`: a function that runs every loop iteration |
| `src/Loadouts.cpp` | The way worn items are read (`ExtraContainerChanges`, `Worn` / `WornLeft` flags), the `UnequipObject` / `EquipObject` calls and their arguments, the rules found in the original comments | The storage idea. Nothing is moved into containers. A loadout is a saved list of forms |
| `src/GameThread.h`, `src/GameThread.cpp` | The task queue on the Unreal game thread | The XInput import chain replaced by a MinHook on `UEngine::Tick`. Pad rules, injected steps, `Call()` and the frame callback removed |
| `src/Console.h`, `src/Console.cpp` | `ExecuteConsoleCommand` through `ProcessEvent`, the player-controller lookup (from `Console.cpp`); property-offset reader, self-check, `IsLive`, `Instances` (from `Reflect.cpp`) | Merged into one file. Added `Notify()` (runs `Message "text"`) |
| `xmake.lua`, `.gitignore`, `.gitmodules` | Build setup | New project name, no UI sources |
| `LICENSE`, `EXCEPTIONS`, `THIRD_PARTY_NOTICES.md` | Unchanged | - |

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program (`LICENSE`). If not, see
<https://www.gnu.org/licenses/>.

SPDX-License-Identifier: GPL-3.0-or-later

The plugin links CommonLibOB64 (GPL-3.0) and MinHook (BSD-2-Clause). Notices: `THIRD_PARTY_NOTICES.md`.
