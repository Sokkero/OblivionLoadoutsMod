# Nexus Mods page

## Brief overview
```
Five sets of worn gear, one click apart. A row of loadout buttons in the inventory menu and Ctrl + Numpad hotkeys. Items stay in your inventory, so one sword can be in every loadout. Renamable, saved per character.
```

## Description
```
[size=5][b]Gear Loadouts[/b][/size]

Gear Loadouts puts a row of loadout buttons into the inventory menu and switches everything you wear with one click.
A loadout only remembers what you had on. Nothing is moved anywhere: your items stay in your inventory and keep their weight, so the same sword, ring or helmet can be part of every loadout.

[img]https://raw.githubusercontent.com/Sokkero/OblivionLoadoutsMod/master/assets/inv.png[/img]

[size=4][b]What you get[/b][/size]
[list]
[*][b]Five loadouts.[/b] Default and Loadout 1 - 4, as a row of buttons right in the inventory menu.
[*][b]One click.[/b] Left click a button and you switch to that loadout. What you were wearing is saved to the old one first.
[*][b]Your own names.[/b] Right click a button to rename it, up to 15 characters.
[*][b]Hotkeys.[/b] Ctrl + Numpad 0 - 4 switch loadouts without opening the menu.
[*][b]Everything you wear.[/b] Armour, shields, clothing, rings, amulets, weapons, ammo and torches.
[*][b]No needless swapping.[/b] Pieces both loadouts share stay on. Pieces you no longer carry are skipped.
[*][b]Per character.[/b] Each character has their own loadouts and names.
[/list]

[size=4][b]Controls[/b][/size]
[list]
[*]Switch loadout: left click its button, or Ctrl + Numpad 0 - 4
[*]Rename: right click the button, type, Enter saves, Escape cancels
[/list]

[size=4][b]Installation[/b][/size]
[list=1]
[*]Install with your mod manager and deploy.
[*]Make sure [b]GearLoadouts.esp[/b] is enabled in your load order.
[*]Start the game through OBSE64.
[/list]
Manual install: copy [b]GearLoadouts.dll[/b] into [b]OblivionRemastered\Binaries\Win64\OBSE\Plugins\[/b], copy [b]GearLoadouts.esp[/b] into [b]OblivionRemastered\Content\Dev\ObvData\Data\[/b] and add the line [b]GearLoadouts.esp[/b] to [b]Plugins.txt[/b] in that folder.
Uninstall: delete both files and remove the line from Plugins.txt.

[size=4][b]Requirements[/b][/size]
[url=https://github.com/ianpatt/obse64]OBSE64[/url] and [url=https://www.nexusmods.com/oblivionremastered/mods/4475]Address Library for OBSE[/url]. 
Tested with OBSE64 0.2.2 on 1.512.105.

[size=4][b]Where your loadouts live[/b][/size]
The loadouts are stored in [b]GearLoadouts.json[/b] beside the DLL, not in your save, so loading an older save keeps your newest loadouts.
Which loadout is active is kept in each save by a hidden item from GearLoadouts.esp, so after loading an older save your gear still ends up in the right loadout.
In a save from before the mod (or without the .esp) no loadout is active: the first switch puts the chosen loadout on without saving what you wore.

[size=4][b]Compatibility issues[/b][/size]
[list]
[*][b]Simple Loadout System[/b]: not compatible, both hook the same engine call. Disable it.
[*]Other mods that change the inventory menu may conflict.
[/list]

[size=4][b]Found a bug?[/b][/size]
Post your game and mod versions, what you did and the log on the Bugs tab:
[b]Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log[/b]
Every game start appends to the same file, so after a crash the crashed session is still in it.

[size=4][b]Credits[/b][/size]
Made by Sokkero. 
Parts are adapted from [url=https://github.com/ApocryphaRealm/SimpleLoadoutSystemOR]Simple Loadout System[/url] by ApocryphaRealm and [url=https://github.com/cfa911/Oblivion-Remastered---Mod---Menus-Revamped]Menus Revamped[/url] by cfa911. 
Built with [url=https://github.com/libxse/commonlibob64]CommonLibOB64[/url]; thanks to Ian Patterson for OBSE64.
GPL-3.0-or-later, source on [url=https://github.com/Sokkero/OblivionLoadoutsMod]GitHub[/url].
```
