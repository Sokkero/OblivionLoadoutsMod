# HANDOVER: Gear Loadouts for Oblivion Remastered

Read this file fully before you change anything. Put it in the repo root. Keep it up to date.

## 1. Goal

An OBSE64 plugin (C++) for **The Elder Scrolls IV: Oblivion Remastered**. It switches between saved sets of worn gear. It replaces "Simple Loadouts" / SimpleLoadoutSystem (SLS), which the user dislikes: SLS moves items into chests, so one item cannot be in several loadouts. Its buttons also do not show on the user's PC.

### Rules (decided with the user)

- 5 loadouts: **Default, Loadout 1, 2, 3, 4**. Default is active at the start.
- **Only the equipped state changes.** No item is ever added, removed or moved.
- **All equipped items are saved** (user, 2026-11-17): armour, clothing, rings, amulet, shield, ammo, weapons (`kIncludeWeapons=true`) and torches (`Light`). The code filters by item type, not by slot. Slots 1 (hair) and 15 (tail) are body parts.
- **Switching is blocked in combat.**
- Skip saved items that are no longer in the inventory.
- UI goal (later stage):
  - 5 buttons in the inventory screen.
  - Left click on an inactive loadout = switch.
  - Left click on the active loadout = nothing.
  - Right click = rename popup (text field, Save, Cancel).
  - All 5 names are renamable. Defaults: "Default", "Loadout 1".."Loadout 4".
  - Empty name = invalid. Whitespace-only counts as empty. Cap at about 20 characters. Reject silently or disable Save.
- Keyboard and mouse only. No controller work.
- The user does not care about achievements.

## 2. User preferences (follow them)

- ADHD. Short answers, headings, bullets. ASD-STE100 style (simple, short sentences).
- Direct and critical. Verify claims. Cite sources. Mark unverified items as **"Not verified"**.
- Do only what was asked. Flag related problems. Do not fix them unasked.
- New to modding and C++. Explain steps clearly. Give exact commands and paths.
- The user tests in the game and sends back logs. They say "ready to test" when they can test. Then send: what to do, plus the files.
- Do not claim something works until the user has tested it.
- **Never commit.** The user commits themselves. Leave changes in the working tree.

## 3. Environment

- PC, Windows, Steam. Game version **1.512.105.0**.
- **OBSE64 0.2.2** (Ian Patterson). It is the newest version on Nexus and on GitHub. Managed with Vortex.
- Address Library file in use: `versionlib-1-512-105-0.bin`. The user has it.
- Build: Visual Studio Build Tools (Desktop development with C++), xmake, Git. Run xmake from PowerShell.
- The old handoff claim "xOBSE64 >= 0.3.0 / rheard fixes form returns" was **not found anywhere. Treat as false.**
- Plugin folder: `...\Oblivion Remastered\OblivionRemastered\Binaries\Win64\OBSE\Plugins\`
- Log (this is where it was found): `Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log`
- Start the game through the OBSE64 loader.
- **Keep SimpleLoadoutSystem disabled.** Both hook the same engine call.

## 4. Why a C++ plugin (script route is dead)

The user tested an ESP script on OBSE64 0.2.2. Results:

- `GetEquippedObject` works in the console but returns a null ref in a compiled script.
- `let :=` kills the script.
- `GetItemCount` with a captured ref kills the script.
- `UnequipItem` / `EquipItem` work with a literal form and with a ref filled from a literal.
- `if ref` is true for a null ref. Not a safe empty check.
- Console `set Quest.ref to formID` does not fill a ref.
- Slot IDs verified in game: 1 helmet, 2, 3, 4, 5, 6/7 rings, 8 amulet, 13 shield, 17 ammo. Slots 0 and 9-12 are empty.
- `player.IsInCombat` works in the console. Not verified: that it returned 1 during a real fight.

The test ESP (`LoadoutTest.esp`) is only in the user's game folder. It is no longer needed. It still shows in the plugin list in the log. The user can remove it.

## 5. Current code state (Stage 1, v0.1.2)

Project: the git repo root (`C:\Users\Blast\Desktop\git\oblivionLoadoutsMod`). The CommonLibOB64 submodule is set up in `lib/commonlibob64` (commit `3e5ee4f`). Build from PowerShell in the repo root:

```
xmake build GearLoadouts
```

The DLL lands in `build\windows\x64\releasedbg\GearLoadouts.dll`. The build works on the user's PC, and Claude can run it there.

| File | Purpose |
|---|---|
| `src/main.cpp` | Entry point. `OnTick` polls **Ctrl+Numpad0-4** with `GetAsyncKeyState` (only while the game window has focus). `OnMessage` on `kDataLoaded` runs `loadouts::Init()`, `testhread::SetTick`, `testhread::Install()`. Error beep = refused. |
| `src/Loadouts.h/.cpp` | Logic. `Request(to, why)` checks: player, index, combat (see section 6), hook, busy. Then it posts `Switch(to)` to the TES thread. |
| `src/TesThread.h/.cpp` | From SLS plus `SetTick`. MinHook on the main-loop call. All inventory changes MUST run on this thread. Other threads crash. |
| `src/GameThread.h/.cpp` | Task queue from SLS. MinHook on `UEngine::Tick` (vtable slot 0x5F of `UEngine::GetSingleton()`). Install is tried at each OBSE message until the engine object exists. Runs posted tasks on the **Unreal game thread** (not the TES thread). The SLS XInput import chain was dropped: in game, another module replaced the import entry after 4 calls (log 21:39). |
| `src/Reflect.h/.cpp` | From SLS. Read Unreal properties by name; self-check; `IsLive`; `Instances`. Game thread only. |
| `src/CharacterKey.h/.cpp` | New. Reads the loaded character GUID (`VAltarSaveIdentificationData.CharacterId`). Game thread only. |
| `src/Console.h/.cpp` | From SLS. Runs console commands via `KismetSystemLibrary::ExecuteConsoleCommand`. `Notify(text)` runs `Message "text"`. Unreal game thread only. |
| `src/Push.h/.cpp` | From SLS. The game's inventory list rebuild (byte pattern), run after every switch so the equipped marks move. Tested 2026-11-18. |
| `src/ui/*` | The inventory UI (5 buttons, rename popup): see `UI-UX.md` (plan, status per step, findings). `WindowInput` subclasses the game window and, while the popup is open, also blocks raw keyboard input and the key-state imports of every module (other mods' key binds, e.g. CloseMenusMod via UE4SS). |
| `src/devtools/Census.*` | Dev tool: widget tree dump on Ctrl+Shift+F11. Remove the hotkey before a release. |
| `src/Logger.h`, `src/Scan.h`, `src/pch.h` | Copied from SLS, unchanged. |
| `xmake.lua` | `GearLoadouts` 0.1.2 (do not change, see section 12), GPL-3.0-or-later, C++23, packages `minhook`, `nlohmann_json`, rule `commonlibob64.plugin`, links `user32`. `author = "CHANGE ME"`. |
| `NOTICE.md`, `LICENSE`, `EXCEPTIONS`, `THIRD_PARTY_NOTICES.md` | Licence files. Put the real name in `NOTICE.md` and `xmake.lua`. |

### Switch flow (`Switch(to)` in `Loadouts.cpp`)

1. Read worn items from `ExtraContainerChanges` (`Worn` / `WornLeft` flags). Save them as the old loadout.
2. Keep pieces that the new loadout also holds.
3. Take off the rest with `player->UnequipObject(item->object, count, extra, false, true)`.
4. For each wanted piece that is still in the inventory: `player->EquipObject(item->object, count, xl, false, false)`. `xl = UnwornList(item, ownData)`. The last argument (NoUnequip lock) is false.
5. Set `g_active`. Call `SaveCharacter()`. Beep.

Rules copied from SLS comments:

- Unequip before you move or change anything.
- Never keep an `ExtraDataList` after an unequip.
- Do not pass the NoUnequip lock on equip.
- Ammo is worn as a whole stack.
- Items without a name are hidden pieces of other mods. `IsGear` skips them.

Storage: `GearLoadouts.json` next to the DLL. Per character key. Items are `{plugin, id, own}`. Atomic write (`.tmp` file, then rename). OBSE64 has no save-game data interface, so loading an older save uses the newest loadouts.

## 6. Test results so far (log: 19:38 to 19:42 session)

- Plugin loaded. Hook installed (`tes thread ... now drains` and `... runs on thread N`).
- Switch Default / Loadout 1 / 2 / 3 worked. Items came off and went on. A shared helmet stayed on (`kept`).
- Empty loadouts take everything off. This follows the spec.
- Not verified: the beeps. The user did not listen.

### Combat check (fixed 2026-11-17, logs 20:47 to 21:04)

- In real fights these all returned "no combat" for the player: `IsInCombat(false)`, `IsInCombat(true)`, `GetCombatTarget()`, `GetCombatController()`, `combatPursue`. They are no longer called.
- These `PlayerCharacter` fields work:
  - `numberActorsInCombat` (+0xB10): `-1` out of combat, N while N actors fight the player.
  - `playerCombatList` (+0x828): `null` out of combat, the actors fighting the player while in combat.
- Rule now: block only if `playerCombatList` has an entry. `numberActorsInCombat` is logged but does not block: after a won fight (log 21:18) the list went back to `null` at once, but the counter stayed `1` for at least 7 seconds.
- Tested in game: no fight = switch works. NPC vs NPC (player not involved) = switch works, fields stay `-1` / `null`. Player fights = `refused: in combat`.
- Log line: `combat check: actorsInCombat=N list=N (names)`. Kept on purpose.
- On refusal in combat, a HUD message shows: "You cannot change your loadout during combat." Path: `Request()` posts to `gamethread`, which runs `console::Notify` → console `Message "..."`. The user confirmed that a console `Message "Test"` shows top-left with no `[NL]` prefix. Tested in game 2026-11-17 21:42: the message shows.
- Tested in game 2026-11-17 21:42: after the fight the list is `null` and the switch works. **Not verified:** whether the list can be empty for a moment during a fight. Allies fighting next to the player.

## 7. Open bugs and questions (priority order)

1. ~~Combat check does not block.~~ Fixed, see section 6.
2. ~~`PlayerKey()` is wrong.~~ **Fixed and tested (2026-11-17, logs 22:48-22:54: two characters, separate data, migration ran once).** The key is now the character GUID: `/Script/Altar.VAltarSaveIdentificationData`, property `CharacterId` (FGuid, +0x38), read by name on the game thread (`src/CharacterKey.cpp`). Found with a memory probe (2026-11-17, logs 22:35-22:44): one instance per session, it always holds the loaded character's GUID, two characters gave two GUIDs. Same GUID as in the save-file names `<slot> - <name> (<GUID>).sav`. Old entry `UI_UserInputText_1_720_0` is moved to the first character that switches. All-zero GUID = switch refused with a HUD message. A new game autosaves at once (user), so a character has its GUID from the start; an all-zero GUID is still refused, as a guard.
3. **Meaning of "the engine said no".** `UnequipObject` returns it for items that did come off (log shows it with working switches). Check the return type and meaning in CommonLibOB64 before you rely on it. Maybe log the real equipped state after the switch instead.
   - Declared `bool` in `lib/commonlibob64/include/RE/A/Actor.h:259`. SLS ignores the return value.
   - Pattern in every log: only the items with own data (the helmet and the enchanted ring) return `ok`. All others return "no" but still come off. **Not verified** what the value means.
4. **Wrong copy of an item.** If two copies of one item differ (own data), the plugin may pick the wrong one. **Not verified.**
   - **Related bug fixed and tested (2026-11-17, log 22:53):** a new character showed no worn item at all. Cause: `ItemChange::count` is the change against the base container (countDelta), not the number carried; starting items have **no change entry at all**. Now: worn detection ignores the count; "still carried" and the ammo count use `Actor::GetItemCountInContainer` (returned 1 for starting clothes); equip works without a change entry (engine picks the copy). Tested: starting clothes, drop and pick up again, Sokkero unchanged.
5. **Persistence design.** JSON beside the DLL is a stopgap. Think about a per-save solution.
6. **Hook timing.** Confirmed: OBSE message 2 (`kDataLoaded`) arrives in OBSE64 0.2.2 and the hook installs. (The earlier worry is closed.)
7. **SLS buttons do not show on the user's PC.** Cause unknown. Menus Revamped works there. It may matter for UI work (byte patterns in SLS are dated 2026-09-29).
8. Edit `author = "CHANGE ME"` in `xmake.lua` and the name in `NOTICE.md` (user's task).

## 8. Diagnostic change (done, removed)

The combat diagnostic was used for the fix in section 6 and then removed. Only the short `combat check:` log line stays.

## 9. Roadmap

**Stage 1: logic (now).** Combat is fixed. Next: the player key. Then confirm all 7 hotkey tests pass.

**Stage 2: names and storage.** Store 5 names in the JSON. Default names. Validation: trim, reject empty, cap about 20 characters. Add functions `GetName(i)` / `SetName(i, text)`.

**Stage 3: UI (agreed, in order).** Reuse techniques from Menus Revamped. **Full plan: `UI-UX.md`** (supersedes the list below where they differ).

1. Census dump of the inventory widget tree (dev tool).
2. Menu detection (`BP_OnActivated` / `BP_OnDeactivated` on `WBP_OriginalMenu_Inventory_C`).
3. One test button.
4. Five buttons with active highlight.
5. Button names read from the JSON.
6. Popup with fixed text (Save / Cancel).
7. Typing with key swallowing (window messages into your own string).
8. Save writes the name. Empty is rejected.

How Menus Revamped adds UI (from its source):

- ProcessEvent vtable-swap watchers (`PEHook.cpp`).
- It clones the class of the native "Name" sort button with `CreateWidget`, reads slot padding / alignment / size, and calls `AddChild` on the sort-bar ScrollBox.
- Its own hit-testing (pixel rectangles) and an XInput import patch.
- `GuardedProcessEvent` and `RunGuarded` catch crashes.
- `Census.cpp` (widget dump), `UiText.cpp`, `PickerPopup.cpp`, `SearchInput.cpp`.
- Its changelog lists many crash and layout bugs (RMI-043, RMI-004, 1.4.1). Read it first.

## 10. Source repos to read (clone them on the user's PC)

- SLS (donor of hook, read-worn-items, equip and unequip code): https://github.com/ApocryphaRealm/SimpleLoadoutSystemOR (GPL-3.0-or-later)
- Menus Revamped (UI technique): https://github.com/cfa911/Oblivion-Remastered---Mod---Menus-Revamped (GPL-3.0)
- CommonLibOB64: https://github.com/libxse/commonlibob64
- OBSE64: https://github.com/ianpatt/obse64

## 11. Licence rules

- Our mod is **GPL-3.0-or-later**. Source must stay public with every release.
- Copied code from SLS or Menus Revamped: keep their copyright lines, mark your changes, update `NOTICE.md`.
- The user said: not a fork. Copy only the useful parts. This is not legal advice.

## 12. How to work with the user

1. Read this file and `src/Loadouts.cpp`, `src/TesThread.cpp`, `src/main.cpp`.
2. Fix one item at a time. Do the combat bug first.
3. After each change: build, then give the user a short test list and say which log lines to look for. Ask for the log.
4. Never say "works" before the user tested it in game.
5. Add logs for every new behavior. The log is the main debug tool.
6. **Do not change the version** in `xmake.lua` (`set_version`). The user asked to keep it at 0.1.2. To check which DLL ran, look at the DLL's modified time.

## 13. Not verified (summary)

- Why `IsInCombat` returned false in the fight (no longer used).
- Allies fighting next to the player (combat block).
- That the beeps play correctly.
- The meaning of the "engine said no" return value.
- Equipping the right copy when two copies of an item differ.
- CommonLibOB64 version match with the one SLS used.
- The log path is a guess that happened to work. The user found the log there.
