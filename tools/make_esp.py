# SPDX-License-Identifier: GPL-3.0-or-later
# Writes release/GearLoadouts.esp: one hidden token item whose count in the player's inventory is the active loadout + 1.
# The game keeps it in each save, so loading any save brings back the loadout that was active when it was made.
#
# The token is clothing with no body slot, no name and the Non-Playable flag: hidden in the inventory, never worn.
# Usage: python tools/make_esp.py

import pathlib
import struct

TOKEN_FORM_ID = 0x800  # first ID available to a plugin; keep in sync with kTokenID in src/Loadouts.cpp


def sub(tag: bytes, data: bytes) -> bytes:
    return tag + struct.pack("<H", len(data)) + data


def record(tag: bytes, form_id: int, data: bytes, flags: int = 0) -> bytes:
    # Oblivion record header: type, data size, flags, form ID, version control info
    return tag + struct.pack("<IIII", len(data), flags, form_id, 0) + data


def group(label: bytes, data: bytes) -> bytes:
    # top-level group: size includes its own 20-byte header, group type 0, stamp 0
    return b"GRUP" + struct.pack("<I", len(data) + 20) + label + struct.pack("<II", 0, 0) + data


def zstr(text: str) -> bytes:
    return text.encode("ascii") + b"\0"


token = record(b"CLOT", TOKEN_FORM_ID, b"".join([
    sub(b"EDID", zstr("GearLoadoutsActiveToken")),
    sub(b"BMDT", struct.pack("<HBB", 0, 0x40, 0)),  # no body slots; 0x40 = Non-Playable
    sub(b"DATA", struct.pack("<If", 0, 0.0)),        # value 0, weight 0
]))
clothing = group(b"CLOT", token)

header = record(b"TES4", 0, b"".join([
    sub(b"HEDR", struct.pack("<fII", 1.0, 2, TOKEN_FORM_ID + 1)),  # version, records + groups, next object ID
    sub(b"CNAM", zstr("Sokkero")),
    sub(b"SNAM", zstr("Gear Loadouts: remembers the active loadout in each save.")),
]))

out = pathlib.Path(__file__).resolve().parent.parent / "release" / "GearLoadouts.esp"
out.parent.mkdir(exist_ok=True)
out.write_bytes(header + clothing)
print(f"wrote {out} ({out.stat().st_size} bytes)")
