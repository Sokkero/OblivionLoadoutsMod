# Nexus Mods page

## Brief overview
```
Switch between 5 saved sets of worn gear with one click from the inventory or a hotkey. Items stay in your inventory, so the same item can be part of every loadout. Rename loadouts, saved per character.
```

## Description
```
[size=5][b]Gear Loadouts[/b][/size]

Switch between 5 saved sets of worn gear with a single click.
A loadout only remembers what you were wearing. Your items stay in your inventory, so the same sword, ring or helmet can be part of as many loadouts as you like.

[img]https://raw.githubusercontent.com/Sokkero/OblivionLoadoutsMod/master/assets/inv.png[/img]

[size=4][b]Features[/b][/size]
[list]
[*]5 loadouts: [b]Default[/b] and [b]Loadout 1 - 4[/b]
[*]A row of buttons right in the inventory menu
[list]
[*][b]Left click[/b] switches to that loadout
[*][b]Right click[/b] renames it (max. 15 characters, [b]Enter[/b] saves, [b]Esc[/b] cancels)
[/list]
[*]Hotkeys: [b]Ctrl + Numpad 0[/b] - [b]Ctrl + Numpad 4[/b]
[*]Every equipped item counts: armour, shields, clothing, rings, amulets, weapons, ammo and torches
[*]Pieces that are in both loadouts stay on, pieces you no longer carry are skipped
[*]Switching is blocked while something is fighting you
[*]Loadouts and names are saved per character
[/list]

[size=4][b]How it works[/b][/size]
When switching from loadout A to B:
[list=1]
[*]Everything you are wearing right now is saved as A
[*]Everything B does not hold is taken off
[*]Everything B holds that is still in your inventory is put on
[/list]
[b]Note:[/b] Switching to a loadout for the first time takes everything off, since that loadout is still empty. Just put your gear on and it gets saved the next time you switch away.

The loadouts are stored in [b]GearLoadouts.json[/b] next to the DLL, not in your save file. Loading an older save keeps your newest loadouts.
Which loadout is active is kept in each save (by a hidden item from [b]GearLoadouts.esp[/b]), so after loading an older save your gear still ends up in the right loadout.

[size=4][b]Requirements[/b][/size]
[list]
[*][url=https://github.com/ianpatt/obse64]OBSE64[/url]
[*]Address Library for OBSE64
[/list]
Tested with OBSE64 0.2.2 on game version 1.512.105.

[size=4][b]Installation[/b][/size]
Install with your mod manager, or manually:
[list=1]
[*]Copy [b]GearLoadouts.dll[/b] into [b]Oblivion Remastered\OblivionRemastered\Binaries\Win64\OBSE\Plugins\[/b]
[*]Copy [b]GearLoadouts.esp[/b] into [b]Oblivion Remastered\OblivionRemastered\Content\Dev\ObvData\Data\[/b] and add the line [b]GearLoadouts.esp[/b] to [b]Plugins.txt[/b] in that folder
[*]Start the game through [b]obse64_loader.exe[/b]
[/list]

[size=4][b]Compatibility[/b][/size]
[list]
[*][b]Simple Loadout System[/b]: not compatible, both hook the same engine call. Disable it.
[*][b]Menus Revamped[/b]: works alongside it
[/list]

[size=4][b]Troubleshooting[/b][/size]
If something does not work, check the log at [b]Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log[/b] and post it in the comments or bug reports.
If the game crashed, the log of that session is [b]GearLoadouts.1.log[/b] in the same folder (the previous session's log is kept on every start).

[size=4][b]Credits[/b][/size]
[list]
[*][url=https://github.com/ApocryphaRealm/SimpleLoadoutSystemOR]Simple Loadout System[/url] by ApocryphaRealm and [url=https://github.com/cfa911/Oblivion-Remastered---Mod---Menus-Revamped]Menus Revamped[/url] by cfa911, parts of this mod are adapted from them
[*][url=https://github.com/libxse/commonlibob64]CommonLibOB64[/url]
[*]Ian Patterson for OBSE64
[/list]

[size=4][b]Source[/b][/size]
The mod is open source under GPL-3.0-or-later: [url=https://github.com/Sokkero/OblivionLoadoutsMod]github.com/Sokkero/OblivionLoadoutsMod[/url]
```
