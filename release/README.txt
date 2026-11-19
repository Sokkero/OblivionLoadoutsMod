Gear Loadouts - The Elder Scrolls IV: Oblivion Remastered
=========================================================

An OBSE64 plugin that lets you switch between 5 saved sets of worn gear with a single click.
A loadout only remembers what you were wearing. Your items stay in your inventory, so the same
sword, ring or helmet can be part of as many loadouts as you like.


FEATURES
--------
- 5 loadouts: "Default" and "Loadout 1" - "Loadout 4"
- A row of buttons in the inventory menu
    - Left click switches to that loadout
    - Right click renames it (max. 15 characters, Enter saves, Esc cancels)
- Hotkeys: Ctrl + Numpad 0 - Ctrl + Numpad 4
- Every equipped item counts: armour, shields, clothing, rings, amulets, weapons, ammo and torches
- Pieces that are in both loadouts stay on, pieces you no longer carry are skipped
- Switching is blocked while something is fighting you
- Loadouts and their names are saved per character


HOW SWITCHING WORKS
-------------------
When switching from loadout A to B:
  1) Everything you are wearing right now is saved as A
  2) Everything B does not hold is taken off
  3) Everything B holds that is still in your inventory is put on

Switching to a loadout for the first time takes everything off, since that loadout is still
empty. Just put your gear on and it gets saved the next time you switch away.

The loadouts are stored in GearLoadouts.json next to the DLL, not in your save file, so loading
an older save keeps your newest loadouts. Which loadout is active is kept in each save (by a
hidden item from GearLoadouts.esp), so after loading an older save your gear still ends up in
the right loadout.


REQUIREMENTS
------------
- OBSE64 (https://github.com/ianpatt/obse64)
- Address Library for OBSE64
Tested with OBSE64 0.2.2 on game version 1.512.105.


INSTALLATION
------------
Install with your mod manager, or manually:
  1) Copy GearLoadouts.dll into
       Oblivion Remastered\OblivionRemastered\Binaries\Win64\OBSE\Plugins\
  2) Copy GearLoadouts.esp into
       Oblivion Remastered\OblivionRemastered\Content\Dev\ObvData\Data\
     and add the line
       GearLoadouts.esp
     to Plugins.txt in that same folder
  3) Start the game through obse64_loader.exe

To uninstall, delete both files and remove the GearLoadouts.esp line from Plugins.txt.


COMPATIBILITY
-------------
- Simple Loadout System: NOT compatible, both hook the same engine call. Disable it.
- Menus Revamped: works alongside it and is a nice match


TROUBLESHOOTING
---------------
If something does not work, check the log at
  Documents\My Games\Oblivion Remastered\OBSE\Logs\GearLoadouts.log
Every game start appends to the same file, so after a crash and restart the crashed session is
still in it. Please include it when reporting a bug.


CREDITS
-------
- Simple Loadout System by ApocryphaRealm and Menus Revamped by cfa911: parts of this mod are
  adapted from them (see NOTICE.txt)
- CommonLibOB64 (https://github.com/libxse/commonlibob64)
- Ian Patterson for OBSE64


LICENCE
-------
GPL-3.0-or-later, see LICENSE.txt and NOTICE.txt.
Source code: https://github.com/Sokkero/OblivionLoadoutsMod
