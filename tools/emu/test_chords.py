# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Chords checks on Tracker+ test builds (MT_NO_SYNTH=1), against the reference build
with every mod compiled out (MT_NO_MODS, so plain CE):

1. Playing: the notes Sequencer::sendMidiChordOn and sendMidiChordOff send for every
   Chord value 0..255, in FX1 and FX2, with the mod Off and On. Off sends exactly what
   the reference sends; On sends the same for 0..29 and 237..255, and for 30..236 the
   step's note plus the intervals the cell shows.
2. Cell text (makeFxValLabelWithAlgin): Off as the reference for CE's chords and no
   interval text past them; On, the intervals for 30..236.
3. Config: Config -> Mods' Chords line is the default for new projects (mods.cfg) and
   Project Settings -> Mods' Chords line is the project's switch (Workspace/mods.bin),
   each through its own action on a fake SD card.

The encoder edit (past 29 only with the mod On) and the Arp are checked on the device:
the encoder needs Sequencer internals the image doesn't expose by symbol, and the Arp's
mask lookup (getChordBitmaskForArp) is inlined into the arpeggiator, whose state is a
player-track struct. The Arp hook is the same two lines as getChordBitmask's.

Usage: python tools/emu/test_chords.py build/emu/TrackerPlus_MT_nosynth.axf \\
           build/emu/TrackerPlus_ref_nosynth.axf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import FakeSd  # noqa: E402

FX_CHORD = 14
FX_ARP = 21
FX_VOLUME = 18
CHORDS_ID = 14
FIRST, LAST = 30, 236
MIDI_INSTRUMENT = 48            # MIDI channel 1
MENU_TYPE_ITEM_LIST_TEXT_WITH_ACTION = 4

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


class Rig:
    """One image, with MIDI note on/off replaced by recorders."""

    def __init__(self, axf: Path):
        self.m = m = Machine(axf)
        self.seq = m.addr("sequencer")
        self.sent: list[tuple] = []
        m.stub_function(m.addr("_Z14sendMidiNoteOnhhhb"),
                        lambda mm: self.sent.append(("on", mm.arg(0) & 0xFF)))
        m.stub_function(m.addr("_Z15sendMidiNoteOffhhhb"),
                        lambda mm: self.sent.append(("off", mm.arg(0) & 0xFF)))
        step = "EhPNS_9strPlayer14strPlayerTrack11strSendStepE"
        self.chord_on = m.addr("_ZN9Sequencer15sendMidiChordOn" + step)
        self.chord_off = m.addr("_ZN9Sequencer16sendMidiChordOff" + step)
        self.label_fn = m.addr("_ZN9Sequencer23makeFxValLabelWithAlginEPcihhNS_17enFxValLabelAlignE")
        self.step = m.alloc(8, 4)
        self.text = m.alloc(16, 4)
        self.switches = m.addr("_ZN4mods8switchesE") if "_ZN4mods8switchesE" in m.elf.symbols else None

    def set_chords(self, on: bool) -> None:
        if self.switches is not None:
            self.m.write(self.switches + CHORDS_ID, bytes([int(on)]))

    def notes(self, note: int, value: int, column: int) -> list[tuple]:
        fx = [(FX_VOLUME, 0), (FX_VOLUME, 0)]
        fx[column] = (FX_CHORD, value)
        self.m.write(self.step, struct.pack("<bBBBBBbb", note, MIDI_INSTRUMENT,
                                            fx[0][0], fx[0][1], fx[1][0], fx[1][1], -1, -1))
        self.sent = []
        self.m.call(self.chord_on, self.seq, 0, self.step)
        self.m.call(self.chord_off, self.seq, 0, self.step)
        return self.sent

    def label(self, value: int) -> str:
        self.m.write(self.text, b"\xAA" * 16)
        self.m.call(self.label_fn, self.seq, self.text, 4, FX_CHORD, value, 0)
        return self.m.cstr(self.text, 4)


def expected(note: int, shown: str) -> list[tuple]:
    """The notes a chord whose cell shows `shown` sends: the note, then each interval."""
    notes = [note] + [note + int(c, 16) for c in shown if c != " "]
    notes = [n for n in notes if n <= 127]
    return [("on", n) for n in notes] + [("off", n) for n in notes]


def test_playing(mt: Rig, ref: Rig) -> None:
    mt.set_chords(False)
    same_off = all(mt.notes(note, v, col) == ref.notes(note, v, col)
                   for v in range(256) for col in (0, 1) for note in (48, 120))
    check(same_off, "Off: every value sends what plain CE sends (FX1, FX2, notes 48 and 120)")
    check(all(mt.label(v) == ref.label(v) for v in range(30)), "Off: CE's chords' text as plain CE")
    check(all(mt.label(v) != mt_on_text(mt, v) for v in range(FIRST, LAST + 1)),
          "Off: no interval text past 29")

    mt.set_chords(True)
    outside = [v for v in range(256) if not FIRST <= v <= LAST]
    check(all(mt.notes(48, v, col) == ref.notes(48, v, col) for v in outside for col in (0, 1)),
          "On: 0-29 and 237-255 send what plain CE sends")
    check(all(mt.label(v) == ref.label(v) for v in range(30)), "On: CE's chords' text unchanged")

    texts = {v: mt.label(v) for v in range(FIRST, LAST + 1)}
    check(texts[31] == " 59" and texts[40] == "368" and texts[236] == "9AB",
          "On: 31, 40, 236 show \" 59\", \"368\", \"9AB\"", f"{texts[31]!r} {texts[40]!r} {texts[236]!r}")
    bad = [v for v in range(FIRST, LAST + 1) for col in (0, 1)
           if mt.notes(48, v, col) != expected(48, texts[v])]
    check(not bad, "On: 30-236 send the note plus the intervals shown, on and off", str(bad[:8]))
    high = [v for v in range(FIRST, LAST + 1) if mt.notes(120, v, 0) != expected(120, texts[v])]
    check(not high, "On: notes above 127 left out", str(high[:8]))


def mt_on_text(mt: Rig, v: int) -> str:
    mt.set_chords(True)
    text = mt.label(v)
    mt.set_chords(False)
    return text


def menu_item(m: Machine, group: str, index: int) -> int:
    g = m.addr(group)
    return m.r32(m.r32(g + 12) + 4 * index)


def test_config(mt: Rig) -> None:
    m = mt.m
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    defaults = m.addr("_ZN4mods8defaultsE")

    item = menu_item(m, "menuMods", 1)
    setup = m.r32(item + 16)
    check(m.cstr(m.r32(item + 4)) == "Chords" and m.r32(item + 12) == MENU_TYPE_ITEM_LIST_TEXT_WITH_ACTION,
          "Config -> Mods: a Chords Off/On line")
    check(m.r32(setup) == defaults + CHORDS_ID and m.r8(setup + 4) == 2,
          "it sets the default for new projects")
    check(m.r32(setup + 12) & ~1 == m.addr("_ZN4mods15defaultsChangedEv") & ~1, "its action saves mods.cfg")

    item = menu_item(m, "menuProjectMods", 0)
    setup = m.r32(item + 16)
    check(m.cstr(m.r32(item + 4)) == "Chords" and m.r32(item + 12) == MENU_TYPE_ITEM_LIST_TEXT_WITH_ACTION,
          "Project Settings -> Mods: a Chords Off/On line")
    check(m.r32(setup) == mt.switches + CHORDS_ID, "it sets the project's switch")
    check(m.r32(setup + 12) & ~1 == m.addr("_ZN4mods20projectSwitchChangedEv") & ~1,
          "its action records the project's switch")

    # The actions, on a fake SD card.
    m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
    sd = FakeSd(m)
    m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: None)
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), sd.obj)
    m.write(defaults + CHORDS_ID, b"\x01")
    m.call(m.addr("_ZN4mods15defaultsChangedEv"))
    cfg = sd.files.get("Mods/mods.cfg", b"")
    check(cfg[:4] == b"MTC1" and cfg[12:16] == bytes([0, 1, 15, 0]) and cfg[16 + CHORDS_ID] == 1,
          "Config -> Mods: Chords On saved as the default in mods.cfg", cfg.hex())

    mt.set_chords(True)
    m.call(m.addr("_ZN4mods20projectSwitchChangedEv"))
    m.call(m.addr("_ZN4mods16onWorkspaceSavedEv"))
    ws = sd.files.get("Workspace/mods.bin", b"")
    check(ws[:4] == b"MTP1" and ws[12:16] == bytes([0, 1, 15, 0]) and ws[16 + CHORDS_ID] == 1,
          "Project Settings -> Mods: Chords On saved in the Workspace's mods.bin", ws.hex())
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def main() -> int:
    mt, ref = Rig(Path(sys.argv[1])), Rig(Path(sys.argv[2]))
    check(mt.switches is not None and ref.switches is None,
          "first image has the mods, the reference has none")
    test_playing(mt, ref)
    test_config(mt)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
