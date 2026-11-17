# Gear Loadouts (Oblivion Remastered), stage 1

OBSE64 plugin. Switch between 5 saved sets of worn gear. **No item is ever moved.** No UI yet.

**Status: written, NOT compiled, NOT tested.**

## What it does

- 5 loadouts: Default, Loadout 1-4. Default is active at the start.
- Hotkeys (game window in focus): **Ctrl + Numpad 0** = Default, **Ctrl + Numpad 1..4** = Loadout 1..4.
- Switch A -> B:
  1. Save the gear worn now as A.
  2. Take off every piece that B does not hold. A piece that both hold stays on.
  3. Equip each piece of B that is still in the inventory. Missing pieces are skipped.
- Gear = armour, shields, clothing, rings, amulets, ammo. Weapons are off (`kIncludeWeapons` in `Loadouts.cpp`).
- Blocked in combat.
- Feedback: normal beep = done, error beep = refused. Details in the log.
- **The first switch to a loadout that was never saved takes everything off.** This follows the spec.

## Where the data lives

`GearLoadouts.json` beside the DLL. One entry per character name. Items are saved as plugin name + local ID.
It is not part of the save file (OBSE64 has no save data interface). Loading an older save uses the newest loadouts.

## Build

Needs Windows, Git, Visual Studio (MSVC, C++23) and [xmake](https://xmake.io).

```
git init
git submodule add https://github.com/libxse/commonlibob64.git lib/commonlibob64
git submodule update --init --recursive
xmake build GearLoadouts
```

Run xmake from PowerShell. Edit `author` in `xmake.lua` and the name in `NOTICE.md` first.
The DLL lands in `build\...\GearLoadouts.dll`. Copy it to
`...\Oblivion Remastered\OblivionRemastered\Binaries\Win64\OBSE\Plugins\`.

## Before the first run

- Keep **SimpleLoadoutSystem disabled**. Both hook the same main-loop call.
- Needs OBSE64 0.2.2 and the Address Library for 1.512.105.
- Start the game with `obse64_loader.exe`.

## First test

1. Log: `Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log`. Look for
   `Gear Loadouts 0.1.0 loaded`, `OBSE message 2`, `plugins loaded by the game`, `tes thread: ... now drains`.
2. Wear set A. Press **Ctrl+Numpad1**. Expect: all gear off, one beep.
3. Wear set B. Press **Ctrl+Numpad0**. Expect: set A back on.
4. Press **Ctrl+Numpad1**. Expect: set B back on.
5. Put the same helmet in both sets. Expect: it stays on across the switch.
6. Send the log and say what you saw.

## Licence

GPL-3.0-or-later. Contains code adapted from Simple Loadout System (Oblivion Remastered) by ApocryphaRealm.
See `NOTICE.md` and `THIRD_PARTY_NOTICES.md`. Source must stay public with every release.
