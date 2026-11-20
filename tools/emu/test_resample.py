# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Resample checks on a full Tracker+ build, with the synth libraries in.

Step 1 of docs/mt/RESAMPLE_PORT_PLAN.md, the skeleton (no recorder hook yet):

1. Config: Resample in Config -> Mods (the default for new projects) and in Project
   Settings -> Mods (the project's switch).
2. Projects, through the hooks CE's file manager calls: a 1.2.0 project's resample.bin is
   imported once, its record 12 goes out with Save and Save As and comes back with Open,
   and New Project starts with both buttons Off.

The record's rules, the importer's refusals and the record through mods.bin are also in the
host tests (core/mods/tests/test_resample.cpp). The recorder itself comes with steps 2 to 6.

Usage: python tools/emu/test_resample.py build/emu/TrackerPlus_MT_resample_step1.axf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_step0 import menu_item_named, store_image  # noqa: E402
from test_swing import Projects, Rig  # noqa: E402

RESAMPLE_ID = 11
RECORD_ID = RESAMPLE_ID + 1
HUMANIZE_ID = 1
MENU_LIST_TEXT_ACTION = 4
# resample.bin as a 1.2.0 unit with the MT patch saved it, both buttons On
SAVED_ON_120 = bytes.fromhex("52534d31 03000000 2634c29b")

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


def side_file(bits: int) -> bytes:
    """The 1.2.0 layout: magic, the settings byte, three zero bytes, FNV-1a over bytes 4 to 7."""
    data = bytes([bits, 0, 0, 0])
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return b"RSM1" + data + struct.pack("<I", h)


def record_in(image: bytes) -> bytes | None:
    """Record 12's data in a mods.bin image, or None."""
    at = 12
    while at + 4 <= len(image):
        rid, _version, length = struct.unpack_from("<BBH", image, at)
        if rid == RECORD_ID:
            return image[at + 4:at + 4 + length]
        at += 4 + length
    return None


def test_config(r: Rig) -> None:
    m = r.m
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    item = menu_item_named(m, "menuMods", "Resample")
    setup = m.r32(item + 16)
    check(m.r32(item + 12) == MENU_LIST_TEXT_ACTION and m.r32(setup) == m.addr("_ZN4mods8defaultsE") + RESAMPLE_ID,
          "Config -> Mods: Resample sets the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Resample")
    setup = m.r32(item + 16)
    check(m.r32(item + 12) == MENU_LIST_TEXT_ACTION and m.r32(setup) == m.addr("_ZN4mods8switchesE") + RESAMPLE_ID,
          "Project Settings -> Mods: Resample is the project's switch")


def test_projects(r: Rig) -> None:
    p = Projects(r)
    m, sd = p.m, p.sd
    settings = m.addr("_ZN4mods8resample8settingsE")

    def state() -> tuple[int, int, int]:
        """The switch, Trk Audio, Trk Record."""
        return m.r8(p.switches + RESAMPLE_ID), m.r8(settings), m.r8(settings + 1)

    check(side_file(3) == SAVED_ON_120, "the test's layout is the one 1.2.0 wrote")

    sd.files["Projects/Old/resample.bin"] = SAVED_ON_120
    p.open("Projects", "Old")
    check(state() == (1, 1, 1), "a 1.2.0 project, both On: the mod On with both buttons", str(state()))
    check(sd.files["Projects/Old/resample.bin"] == SAVED_ON_120, "its resample.bin stays as it was")

    sd.files["Projects/OldOff/resample.bin"] = side_file(0)
    p.open("Projects", "OldOff")
    check(state() == (0, 0, 0), "a 1.2.0 project, both Off: the mod Off", str(state()))

    sd.files["Projects/Bad/resample.bin"] = SAVED_ON_120[:-1] + b"\x00"
    p.open("Projects", "Bad")
    check(state() == (0, 0, 0), "a damaged resample.bin: Off", str(state()))

    # a mods.bin saved by a build without Resample: the import still gets its turn
    older_set = bytearray(16)
    older_set[HUMANIZE_ID] = 1
    sd.files["Projects/Saved/mods.bin"] = store_image(b"MTP1", [(0, 1, bytes(older_set))])
    sd.files["Projects/Saved/resample.bin"] = side_file(1)
    p.open("Projects", "Saved")
    check(state() == (1, 1, 0), "an older mods.bin: resample.bin still imported", str(state()))
    check(m.r8(p.switches + HUMANIZE_ID) == 1, "and the project's Humanize switch kept")

    # Save and Save As carry record 12; Open brings it back
    p.open("Projects", "Old")
    p.save()
    check(record_in(sd.files.get("Projects/Old/mods.bin", b"")) == b"\x03", "Save: record 12 in the project's mods.bin")
    p.save("Projects", "Copy")
    check(record_in(sd.files.get("Projects/Copy/mods.bin", b"")) == b"\x03", "Save As: record 12 in the new folder")
    p.new_project()
    check(state() == (0, 0, 0), "New Project: Off, both buttons Off", str(state()))
    p.open("Projects", "Copy")
    check(state() == (1, 1, 1), "Open: the copy has both buttons", str(state()))
    sd.files["Projects/Copy/resample.bin"] = side_file(0)
    p.open("Projects", "OldOff")
    p.open("Projects", "Copy")
    check(state() == (1, 1, 1), "imported once: a resample.bin put there later is ignored", str(state()))
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def main() -> int:
    r = Rig(Path(sys.argv[1]))
    test_config(r)
    test_projects(r)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
