# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Humanize checks on a full Tracker+ build, with the synth libraries in:

1. The edit on CE's own pattern, through mods::humanize::applyToSelection: only the
   selection changes, only its notes, only their m and V columns, in the windows of
   the settings. FX slots stay as they are, except the value of a Micro-move or Volume
   FX that is already there.
2. The button: not there with the mod Off; with it On, pressing it stores the undo
   revision first, works on the cursor's track when nothing is selected, then refreshes
   the screen and marks the pattern changed.
3. Config: the switches and the settings lines, each setting's range, and a change
   marking the project changed and landing in Workspace/mods.bin, which brings it back.

The ordering rules, the windows' edges and the 1.2.0 side file are in the host tests
(core/mods/tests/test_humanize.cpp). Drawing the button, greying it outside edit mode
and a real selection on screen are checked on the device.

Usage: python tools/emu/test_humanize.py build/emu/TrackerPlus_MT_full.axf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import FakeSd, menu_item_named  # noqa: E402

HUMANIZE_ID = 1
RECORD_ID = HUMANIZE_ID + 1
FX_MICROMOVE, FX_VOLUME, FX_OTHER = 2, 18, 5
SAMPLE, MIDI = 0, 48
NOTE_OFF = -2
TRACK0, TRACK_STRIDE, STEP_SIZE = 28, 1025, 8
MENU_LIST_VALUES, MENU_LIST_TEXT_ACTION = 2, 4

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


def menu_item(m: Machine, group: str, index: int) -> int:
    return m.r32(m.r32(m.addr(group) + 12) + 4 * index)


class Rig:
    def __init__(self, axf: Path):
        self.m = m = Machine(axf)
        m.call(m.addr("_GLOBAL__sub_I_sequencer"))
        self.seq = m.addr("sequencer")
        self.pattern = m.addr("seq")
        m.call(m.addr("_ZN9Sequencer4initEv"), self.seq)
        self.settings = m.addr("_ZN4mods8humanize8settingsE")
        self.switches = m.addr("_ZN4mods8switchesE")
        self.apply = m.addr("_ZN4mods8humanize16applyToSelectionEv")
        self.events: list[str] = []
        m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"),
                        lambda mm: self.events.append("project changed"))

    def clear(self) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer12clearPatternEPNS_10strPatternE"), self.seq, self.pattern)

    def step_addr(self, track: int, step: int) -> int:
        return self.pattern + TRACK0 + track * TRACK_STRIDE + 1 + step * STEP_SIZE

    def put(self, track, step, note, instrument, fx0=(0, 0), fx1=(0, 0), volume=-1, micromove=-1) -> None:
        self.m.write(self.step_addr(track, step),
                     struct.pack("<bBBBBBbb", note, instrument, fx0[0], fx0[1], fx1[0], fx1[1], volume, micromove))

    def get(self, track, step) -> tuple:
        return struct.unpack("<bBBBBBbb", bytes(self.m.read(self.step_addr(track, step), STEP_SIZE)))

    def select(self, first_step, first_track, last_step, last_track) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer12setSelectionEhhhh"), self.seq, first_step, first_track, last_step, last_track)

    def set_settings(self, timing=10, volume_on=1, volume_range=10, midi=50, sample=50) -> None:
        self.m.write(self.settings, bytes([timing - 1, volume_on, volume_range - 1, midi - 1, sample - 1]))

    def image(self) -> bytes:
        return bytes(self.m.read(self.pattern, TRACK0 + 16 * TRACK_STRIDE))


def test_edit(r: Rig) -> None:
    r.set_settings()
    r.clear()
    r.put(0, 0, 60, SAMPLE)
    r.put(1, 0, 60, MIDI)
    r.put(0, 1, 60, SAMPLE, volume=70, micromove=50)
    r.put(0, 2, NOTE_OFF, SAMPLE, volume=70, micromove=50)
    r.put(0, 3, 60, SAMPLE, fx0=(FX_MICROMOVE, 50), fx1=(FX_VOLUME, 70), volume=33, micromove=33)
    r.put(0, 4, 60, SAMPLE, fx0=(FX_OTHER, 77), volume=0)
    r.put(0, 5, 60, SAMPLE)          # outside the selection, below
    r.put(2, 0, 60, SAMPLE)          # outside the selection, beside
    before = r.image()
    r.select(0, 0, 4, 1)
    seen = {name: set() for name in ("m0", "v0", "vmidi", "m1", "v1", "fxm", "fxv", "m4")}
    good = True
    for _ in range(300):
        r.m.call(r.apply)
        s00, s10, s01, s02, s03, s04, s05, s20 = (r.get(t, s) for t, s in
                                                  ((0, 0), (1, 0), (0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (2, 0)))
        seen["m0"].add(s00[7])
        seen["v0"].add(s00[6])
        seen["vmidi"].add(s10[6])
        seen["m1"].add(s01[7])
        seen["v1"].add(s01[6])
        seen["fxm"].add(s03[3])
        seen["fxv"].add(s03[5])
        seen["m4"].add(s04[7])
        good = good and s00[:6] == (60, SAMPLE, 0, 0, 0, 0) and s10[:6] == (60, MIDI, 0, 0, 0, 0)
        good = good and s02 == (NOTE_OFF, SAMPLE, 0, 0, 0, 0, 70, 50)
        good = good and s03[:3] == (60, SAMPLE, FX_MICROMOVE) and s03[4] == FX_VOLUME and s03[6:] == (33, 33)
        good = good and s04[:6] == (60, SAMPLE, FX_OTHER, 77, 0, 0) and s04[6] == 0
        good = good and s05 == (60, SAMPLE, 0, 0, 0, 0, -1, -1) and s20 == s05
        # one press from the empty start again, so the windows stay around the start
        r.put(0, 0, 60, SAMPLE)
        r.put(1, 0, 60, MIDI)
        r.put(0, 1, 60, SAMPLE, volume=70, micromove=50)
        r.put(0, 3, 60, SAMPLE, fx0=(FX_MICROMOVE, 50), fx1=(FX_VOLUME, 70), volume=33, micromove=33)
        r.put(0, 4, 60, SAMPLE, fx0=(FX_OTHER, 77), volume=0)
    check(good, "only the notes' m and V change; FX slots, note off and other steps stay")
    check(seen["m0"] == set(range(0, 11)), "empty m: 0..10", str(sorted(seen["m0"])))
    check(seen["v0"] == set(range(45, 56)), "empty V on a sample: 45..55", str(sorted(seen["v0"])))
    check(seen["vmidi"] == set(range(45, 56)), "empty V on MIDI: 45..55", str(sorted(seen["vmidi"])))
    check(seen["m1"] == set(range(45, 56)), "m 50: 45..55", str(sorted(seen["m1"])))
    check(seen["v1"] == set(range(65, 76)), "V 70: 65..75", str(sorted(seen["v1"])))
    check(seen["fxm"] == set(range(45, 56)), "Micro-move FX 50: 45..55", str(sorted(seen["fxm"])))
    check(seen["fxv"] == set(range(65, 76)), "Volume FX 70: 65..75", str(sorted(seen["fxv"])))
    check(seen["m4"] == set(range(0, 11)), "a step with another FX still gets m", str(sorted(seen["m4"])))

    # the settings
    r.set_settings(timing=20, volume_on=1, volume_range=4, midi=80, sample=30)
    r.clear()
    r.put(0, 0, 60, SAMPLE)
    r.put(1, 0, 60, MIDI)
    r.select(0, 0, 0, 1)
    m, v0, v1 = set(), set(), set()
    for _ in range(300):
        r.m.call(r.apply)
        m.add(r.get(0, 0)[7])
        v0.add(r.get(0, 0)[6])
        v1.add(r.get(1, 0)[6])
        r.put(0, 0, 60, SAMPLE)
        r.put(1, 0, 60, MIDI)
    check(m == set(range(0, 21)), "Timing range 20", str(sorted(m)))
    check(v0 == set(range(28, 33)), "Sample volume 30, Volume range 4", str(sorted(v0)))
    check(v1 == set(range(78, 83)), "MIDI velocity 80, Volume range 4", str(sorted(v1)))

    r.set_settings(volume_on=0)
    r.clear()
    r.put(0, 0, 60, SAMPLE, volume=70)
    r.put(0, 1, 60, SAMPLE)
    r.select(0, 0, 1, 0)
    r.m.call(r.apply)
    check(r.get(0, 0)[6] == 70 and r.get(0, 1)[6] == -1 and r.get(0, 1)[7] >= 0,
          "Volume/velocity Off: V stays, m still changes")
    r.set_settings()

    # a bad rectangle is refused
    r.clear()
    r.put(0, 0, 60, SAMPLE)
    before = r.image()
    r.select(5, 0, 2, 0)
    r.m.call(r.apply)
    check(r.image() == before, "a bad selection changes nothing")


def test_button(r: Rig) -> None:
    m = r.m
    get = m.addr("_ZN4mods19patternEditorButtonEj")
    m.write(r.switches + HUMANIZE_ID, b"\x00")
    check(m.call(get, 0) == 0, "Off: no button")
    m.write(r.switches + HUMANIZE_ID, b"\x01")
    button = m.call(get, 0)
    check(button != 0 and m.cstr(m.r32(button + 4)) == "Humanize", "On: a Humanize button")
    check(m.call(get, 1) == 0, "and only one, with the other mods not ported")

    m.stub_function(m.addr("_ZN12cFileManager24storePatternUndoRevisionEv"),
                    lambda mm: r.events.append("undo " + ("empty" if r.get(0, 0)[7] == -1 else "changed")))
    m.stub_function(m.addr("_ZN14cPatternEditor14refreshPatternEv"), lambda mm: r.events.append("refresh"))
    m.stub_function(m.addr("_ZN12cFileManager29setActualPatternStructChangedEv"),
                    lambda mm: r.events.append("pattern changed"))
    r.clear()
    r.put(0, 0, 60, SAMPLE)
    r.put(0, 7, 60, SAMPLE)
    r.put(1, 0, 60, SAMPLE)
    r.events = []
    m.call(m.r32(button + 8))
    check(r.events == ["undo empty", "refresh", "pattern changed"],
          "a press: undo first, then the edit, refresh, changed", str(r.events))
    check(r.get(0, 0)[7] >= 0 and r.get(0, 7)[7] >= 0, "no selection: the cursor's whole track")
    check(r.get(1, 0)[7] == -1, "and no other track")


def test_config(r: Rig) -> None:
    m = r.m
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))

    item = menu_item_named(m, "menuMods", "Humanize")
    setup = m.r32(item + 16)
    check(m.cstr(m.r32(item + 4)) == "Humanize" and m.r32(setup) == m.addr("_ZN4mods8defaultsE") + HUMANIZE_ID,
          "Config -> Mods: Humanize sets the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Humanize")
    setup = m.r32(item + 16)
    check(m.cstr(m.r32(item + 4)) == "Humanize" and m.r32(setup) == r.switches + HUMANIZE_ID,
          "Project Settings -> Mods: Humanize is the project's switch")

    expected = [("Humanize", MENU_LIST_TEXT_ACTION, r.switches + HUMANIZE_ID),
                ("Timing range", MENU_LIST_VALUES, r.settings + 0),
                ("Volume/velocity", MENU_LIST_TEXT_ACTION, r.settings + 1),
                ("Volume range", MENU_LIST_VALUES, r.settings + 2),
                ("MIDI velocity", MENU_LIST_VALUES, r.settings + 3),
                ("Sample volume", MENU_LIST_VALUES, r.settings + 4)]
    for i, (name, kind, value) in enumerate(expected):
        item = menu_item(m, "menuProjectHumanize", i)
        setup = m.r32(item + 16)
        check(m.cstr(m.r32(item + 4)) == name and m.r32(item + 12) == kind and m.r32(setup) == value,
              f"Project Settings -> Humanize: {name}", f"{m.cstr(m.r32(item + 4))} {m.r32(item + 12)}")
        if kind == MENU_LIST_VALUES:
            check(m.r8(setup + 4) == 0 and m.r8(setup + 5) == 99 and m.r8(setup + 7) == 1,
                  f"{name}: list rows 0..99, shown as 1..100")

    m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
    sd = FakeSd(m)
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), sd.obj)
    m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    defaults = bytes(m.read(r.settings, 5))
    check(defaults == bytes([9, 1, 9, 49, 49]), "a new project starts with the defaults", defaults.hex())

    r.events = []
    edited = m.addr("_ZN4mods20projectSettingEditedEv")
    m.call(edited)
    check(r.events == [], "no edit: the project is not marked changed", str(r.events))
    m.write(r.settings, bytes([33, 0, 44, 55, 66]))
    m.call(edited)
    check(r.events == ["project changed"], "an edit marks the project changed", str(r.events))
    m.call(m.addr("_ZN4mods16onWorkspaceSavedEv"))
    ws = sd.files.get("Workspace/mods.bin", b"")
    check(bytes([RECORD_ID, 1, 5, 0, 33, 0, 44, 55, 66]) in ws, "it lands in Workspace/mods.bin", ws.hex())

    m.write(r.settings, bytes([9, 1, 9, 49, 49]))
    m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    check(bytes(m.read(r.settings, 5)) == bytes([33, 0, 44, 55, 66]), "loading the Workspace brings it back")
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def main() -> int:
    r = Rig(Path(sys.argv[1]))
    test_edit(r)
    test_button(r)
    test_config(r)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
