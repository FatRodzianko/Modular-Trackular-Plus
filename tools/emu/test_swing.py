# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Global Swing checks on full Tracker+ builds, with the synth libraries in. The second
build is the same source with MT_NO_MODS, as plain CE.

The sequencer runs in time, as the audio interrupt runs it: Sequencer::tick, 0.5 ms at a
time, with the real pattern, a note on every step of track 1 marking when each step
starts. Notes go to a stub (sendNoteOn): nothing reaches the audio engine or the synth
libraries. Swing shows as steps alternating long and short: at swing s a pair of steps
lasts two plain steps, split (s + 50) : (150 - s).

1. Off: the same step times as plain CE, block for block, with Swing FX in the pattern,
   in song mode and in a looped selection, with the swing cleared at Stop.
2. On: a take starts on the long step at the amount, from any start: play, a restart
   while playing, MIDI Start, a looped selection. Each new pass starts the same way, also
   after an odd number of steps. A Swing FX works until the pattern starts again; on the
   first step it wins for that pass, since it runs after the pattern start; on a muted
   track it changes nothing. Song mode: the next pattern starts at the amount. Stop
   clears swing. Performance mode: a take's start only.
3. Changes while playing: the amount and the switch at once, the pair's position kept;
   another mod's switch or an unchanged amount leave a Swing FX alone. Stopped: nothing
   changes until the next take.
4. Config and files: the switches and the settings lines, the amount's range, a change
   marking the project changed and landing in Workspace/mods.bin, which brings it back.
5. Projects, through CE's project hooks on a fake SD card: a 1.2.0 project's swing.bin, also
   when a build without Swing already saved a mods.bin there; Save, Save As, New Project then
   Save As, Save As over another project. This doesn't run CE's file manager itself; its hook
   calls are unchanged since they were tested on hardware with Humanize.

The file rules are in the host tests (core/mods/tests/test_swing.cpp). Rendering to a file
goes through the same play functions and Sequencer::tick.

Usage: python tools/emu/test_swing.py build/emu/TrackerPlus_MT_full.axf build/emu/TrackerPlus_ref_full.axf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from unicorn.arm_const import UC_ARM_REG_S0

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import FakeSd, menu_item_named, store_image  # noqa: E402

SWING_ID = 4
RECORD_ID = SWING_ID + 1
HUMANIZE_ID = 1
FX_SWING = 17
SAMPLE = 0
TRACK0, TRACK_STRIDE, STEP_SIZE = 28, 1025, 8
TRACKS = 16
MENU_LIST_VALUES, MENU_LIST_TEXT_ACTION = 2, 4
DT = 0.0005                     # seconds per tick call
TEMPO = 130.0
PATCH_SIZE = 255

# CE functions that reach hardware or the file manager. None of them is in a closed library.
QUIET = [
    "_ZN9Sequencer11sendNoteOffEhPNS_9strPlayer14strPlayerTrack11strSendStepEa",
    "_ZN9Sequencer16stopAllOpenNotesEv",
    "_ZN9Sequencer10cancelFxesEv",
    "_Z13sendMidiStartv",
    "_Z12sendMidiStopv",
    "_Z13sendMidiClockv",
    "_Z28midiSetClockSubBlockOffsetUsm",
    "_ZN10cSongTimer5startEv",
    "_ZN10cSongTimer4stopEv",
    "_ZN11audioEngine8endAllFxEb",
    "_ZN14cPatternEditor33refreshEditStateFromOutsideModuleEv",
    "_ZN12cFileManager20loadWorkspacePatternEh",
    "_ZN12cFileManager23saveWorkspacePatternNowEh",
    "_ZN12cFileManager23loadWorkspacePatternNowEh",
    "_ZN12cFileManager10setSongPosEh",
    "_ZN9Interface29isPerformanceModeScreenActiveEv",
]

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


def f32(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


class Rig:
    def __init__(self, axf: Path):
        self.m = m = Machine(axf)
        m.call(m.addr("_GLOBAL__sub_I_sequencer"))
        self.seq = m.addr("sequencer")
        self.patterns = m.addr("seq")
        self.pattern_size = m.sym("seq").size // 2
        m.call(m.addr("_ZN9Sequencer4initEv"), self.seq)

        self.block = 0
        self.starts: list[int] = []     # the block each step's note went out in
        self.muted: set[int] = set()
        self.next_pattern_index = 2

        for name in QUIET:
            m.stub_function(m.addr(name), lambda mm: 0)
        m.stub_function(m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"), self._note)
        m.stub_function(m.sym("_ZN9Sequencer18isTrackEngineMutedEh").addr,
                        lambda mm: 1 if mm.arg(1) in self.muted else 0)
        m.stub_function(m.sym("_ZN12cFileManager23switchNextPatternInSongEv").addr, lambda mm: 1)
        m.stub_function(m.sym("_ZN12cFileManager21getLoadedPatternIndexEv").addr, self._loaded_index)
        m.stub_function(m.sym("_ZN12cFileManager14getSongPatternEh").addr, lambda mm: 1)

        self.tick_addr = m.addr("_ZN9Sequencer4tickEf")
        self.period_addr = m.addr("_ZN9Sequencer17getTimerPeriod_usEv")
        self._set_tempo(TEMPO)
        self.clear()

    def _note(self, m: Machine) -> None:
        if m.arg(1) == 0:
            self.starts.append(self.block)

    def _loaded_index(self, m: Machine) -> int:
        self.next_pattern_index = 3 - self.next_pattern_index
        return self.next_pattern_index

    def _set_tempo(self, tempo: float) -> None:
        """mtProject.values.globalTempo is wherever getCurrentTempo reads it from."""
        m = self.m
        project = m.sym("mtProject")
        tempo_fn = m.addr("_ZN9Sequencer15getCurrentTempoEv")
        bits = f32(tempo)
        for offset in range(0, project.size, 4):
            before = m.r32(project.addr + offset)
            m.w32(project.addr + offset, bits)
            m.call(tempo_fn, self.seq)
            if m.uc.reg_read(UC_ARM_REG_S0) == bits:
                return
            m.w32(project.addr + offset, before)
        raise RuntimeError("no tempo field found in mtProject")

    # -- the pattern ------------------------------------------------------------------
    def pattern(self, index: int) -> int:
        return self.patterns + index * self.pattern_size

    def clear(self, length: int = 8) -> None:
        """Both patterns: `length` steps with a note on every step of track 1."""
        for index in range(2):
            self.m.call(self.m.addr("_ZN9Sequencer12clearPatternEPNS_10strPatternE"),
                        self.seq, self.pattern(index))
            for track in range(TRACKS):
                self.m.write(self.pattern(index) + TRACK0 + track * TRACK_STRIDE, bytes([length - 1]))
            for step in range(length):
                self.put(index, 0, step, 60)

    def put(self, index, track, step, note=-1, fx=(0, 0)) -> None:
        a = self.pattern(index) + TRACK0 + track * TRACK_STRIDE + 1 + step * STEP_SIZE
        self.m.write(a, struct.pack("<bBBBBBbb", note, SAMPLE, fx[0], fx[1], 0, 0, -1, -1))

    def swing_fx(self, index, track, step, amount) -> None:
        self.put(index, track, step, 60 if track == 0 else -1, (FX_SWING, amount))

    # -- playing ----------------------------------------------------------------------
    def run_steps(self, count: int) -> None:
        """Until `count` more steps have started, or something's wrong."""
        target = len(self.starts) + count
        limit = self.block + count * 2000
        while len(self.starts) < target and self.block < limit:
            self.m.uc.reg_write(UC_ARM_REG_S0, f32(DT))
            self.m.call(self.tick_addr, self.seq, max_insns=20_000_000)
            self.block += 1

    def play(self, from_pos: int = 0) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer11playPatternEh"), self.seq, from_pos)

    def stop(self) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer4stopEv"), self.seq)

    def take(self, steps: int) -> list[int]:
        """Stops, plays and gives the lengths of `steps` steps, in blocks."""
        self.stop()
        self.starts = []
        self.play()
        self.run_steps(steps + 1)
        return self.lengths()

    def lengths(self) -> list[int]:
        return [b - a for a, b in zip(self.starts, self.starts[1:])]

    def correction(self) -> float:
        """The swing in force: (s + 50) on a long step, (150 - s) on a short one, 100 if none."""
        self.m.call(self.period_addr, self.seq)
        period = struct.unpack("<f", struct.pack("<I", self.m.uc.reg_read(UC_ARM_REG_S0)))[0]
        return period * TEMPO * 12 / 3125

    def set_switch(self, on: bool) -> None:
        self.m.write(self.m.addr("_ZN4mods8switchesE") + SWING_ID, bytes([1 if on else 0]))

    def set_amount(self, amount: int) -> None:
        self.m.write(self.m.addr("_ZN4mods5swing8settingsE"), bytes([amount - 25]))


PLAIN = 312500.0 / TEMPO / 12 * 12 * 48 / 1e6 / DT      # a plain step, in blocks


def expected(amount: int, long_first: bool = True) -> tuple[float, float]:
    long_step = PLAIN * (amount + 50) / 100
    short_step = PLAIN * (150 - amount) / 100
    return (long_step, short_step) if long_first else (short_step, long_step)


def fits(lengths: list[int], amounts: list[tuple[int, bool]], within: float = 2.5) -> bool:
    """Each step's length against (amount, is the long step), within the block rounding."""
    if len(lengths) != len(amounts):
        return False
    for length, (amount, is_long) in zip(lengths, amounts):
        want = PLAIN * ((amount + 50) if is_long else (150 - amount)) / 100
        if abs(length - want) > within:
            return False
    return True


def pairs(amount: int, steps: int) -> list[tuple[int, bool]]:
    return [(amount, i % 2 == 0) for i in range(steps)]


def scenarios_off(r: Rig) -> list:
    """Step times of a set of takes, for comparing the Off build with plain CE."""
    out = []
    r.clear()
    out.append(("plain", r.take(20)))

    r.clear()
    r.swing_fx(0, 2, 2, 66)
    r.swing_fx(0, 2, 6, 40)
    out.append(("Swing FX", r.take(30)))
    out.append(("the next take", r.take(30)))

    r.clear(7)
    r.swing_fx(0, 2, 1, 70)
    out.append(("odd length", r.take(30)))

    r.clear()
    r.swing_fx(0, 2, 2, 66)
    r.swing_fx(1, 2, 3, 40)
    r.stop()
    r.starts = []
    r.m.call(r.m.addr("_ZN9Sequencer20playSongFromPositionEh"), r.seq, 0)
    r.run_steps(30)
    out.append(("song mode", r.lengths()))

    r.stop()
    r.clear()
    r.swing_fx(0, 2, 3, 66)
    r.m.call(r.m.addr("_ZN9Sequencer12setSelectionEhhhh"), r.seq, 2, 0, 4, 15)
    r.starts = []
    r.m.call(r.m.addr("_ZN9Sequencer13playSelectionEb"), r.seq, 1)
    r.run_steps(30)
    out.append(("looped selection", r.lengths()))
    r.stop()
    return out


def test_off(mt: Path, ref: Path) -> None:
    a = Rig(mt)
    a.set_switch(False)
    a.set_amount(66)         # the amount set but the mod Off changes nothing
    b = Rig(ref)
    for (name, got), (_, want) in zip(scenarios_off(a), scenarios_off(b)):
        check(got == want and len(got) > 0, f"Off: the same step times as plain CE: {name}",
              f"{got[:12]} vs {want[:12]}")
        if name == "plain":
            check(all(abs(x - PLAIN) <= 2 for x in want), "the rig's plain steps are plain", str(want[:8]))
        if name == "Swing FX":
            check(abs(want[3] - PLAIN * 1.16) <= 2.5 or abs(want[3] - PLAIN * 0.84) <= 2.5,
                  "the rig's Swing FX swings", str(want[:8]))
        if name == "odd length":
            check(abs(want[7] - PLAIN * 0.8) <= 2.5, "CE runs the pair on across a pattern's end", str(want[:10]))


def test_on(mt: Path) -> None:
    r = Rig(mt)
    r.set_switch(True)
    r.set_amount(66)

    r.clear()
    got = r.take(20)
    check(fits(got, pairs(66, 20)), "a take: long first at the amount, all through", str(got))
    r.stop()
    check(abs(r.correction() - 100) < 0.01, "Stop clears swing", str(r.correction()))

    got = r.take(20)
    check(fits(got, pairs(66, 20)), "the next take is the same", str(got))

    r.set_amount(50)
    got = r.take(20)
    check(all(abs(x - PLAIN) <= 2 for x in got), "amount 50: plain steps", str(got))
    r.set_amount(25)
    got = r.take(8)
    check(fits(got, pairs(25, 8)), "amount 25: the first step of a pair is the short one", str(got))
    r.set_amount(75)
    got = r.take(8)
    check(fits(got, pairs(75, 8)), "amount 75", str(got))
    r.set_amount(66)

    # a restart while playing, from a short step and another swing
    r.clear()
    r.swing_fx(0, 2, 0, 40)
    r.stop()
    r.starts = []
    r.play()
    r.run_steps(4)
    r.clear()
    r.starts = []
    r.play()
    r.run_steps(9)
    got = r.lengths()
    check(fits(got, pairs(66, 8)), "a restart while playing: long first at the amount", str(got))

    # MIDI Start, which comes in the audio interrupt
    r.stop()
    r.starts = []
    r.m.call(r.m.addr("_ZN9Sequencer17handleMidiInStartEbb"), r.seq, 0, 0)
    r.run_steps(9)
    got = r.lengths()
    check(fits(got, pairs(66, 8)), "MIDI Start: the same", str(got))

    # each pass, with an odd length: CE would run the pair on across the pattern's end
    r.clear(7)
    got = r.take(21)
    want = pairs(66, 7) * 3
    check(fits(got, want), "an odd length: every pass starts long", str(got))

    # a Swing FX until the pattern starts again
    r.clear()
    r.swing_fx(0, 2, 2, 70)
    got = r.take(16)
    want = pairs(66, 2) + pairs(70, 6)[0:] + pairs(66, 2) + pairs(70, 6)
    check(fits(got, want), "a Swing FX works until the next pass", str(got))

    # on the first step it wins, since it runs after the pattern start
    r.clear()
    r.swing_fx(0, 2, 0, 40)
    got = r.take(16)
    check(fits(got, pairs(40, 16)), "a Swing FX on the first step wins for each pass", str(got))

    # muted
    r.clear()
    r.swing_fx(0, 2, 2, 70)
    r.muted = {2}
    got = r.take(16)
    check(fits(got, pairs(66, 16)), "a Swing FX on a muted track changes nothing", str(got))
    r.muted = set()

    # play from a later step, as Shift+Play does
    r.clear()
    r.stop()
    r.starts = []
    r.play(3)
    r.run_steps(5)
    got = r.lengths()
    check(fits(got, pairs(66, 4)), "from a later step: long first", str(got))

    # song mode: the song starts on the other RAM pattern, then the one with the FX, then
    # the first again, which the FX's swing doesn't reach
    r.clear(7)
    r.swing_fx(0, 2, 1, 70)
    r.stop()
    r.starts = []
    r.next_pattern_index = 2
    r.m.call(r.m.addr("_ZN9Sequencer20playSongFromPositionEh"), r.seq, 0)
    r.run_steps(22)
    got = r.lengths()
    with_fx = [(66, True)] + [(70, i % 2 == 1) for i in range(6)]
    want = pairs(66, 7) + with_fx + pairs(66, 7)
    check(fits(got, want), "song mode: each next pattern starts long at the amount", str(got))
    r.stop()

    # song mode, as on the device: a Swing FX on the first step of one pattern, none in the
    # other, on either track, at the default length; the song starts on the one without
    for track in (0, 1):
        r.clear(32)
        r.swing_fx(0, track, 0, 75)
        r.stop()
        r.starts = []
        r.next_pattern_index = 2
        r.m.call(r.m.addr("_ZN9Sequencer20playSongFromPositionEh"), r.seq, 0)
        r.run_steps(129)
        got = r.lengths()
        want = (pairs(66, 32) + pairs(75, 32)) * 2
        # 66 and 75 are 20 blocks apart; plain CE's steps at 75 come out up to 3 from the sum
        check(fits(got, want, 3.5), f"song mode, FX on track {track + 1}: the pattern with none swings at the amount",
              str(got[:4] + got[32:36] + got[64:68]))
        r.stop()

    # a looped selection of 3 steps: long first at its start and each pass
    r.clear()
    r.m.call(r.m.addr("_ZN9Sequencer12setSelectionEhhhh"), r.seq, 2, 0, 4, 15)
    r.starts = []
    r.m.call(r.m.addr("_ZN9Sequencer13playSelectionEb"), r.seq, 1)
    r.run_steps(31)
    got = r.lengths()
    check(fits(got, pairs(66, 3) * 10), "a looped selection: every pass starts long, in its own steps", str(got))
    r.stop()

    # performance mode: a take's start only
    r.clear(7)
    r.m.call(r.m.addr("_ZN9Sequencer20enterPerformanceModeEv"), r.seq)
    r.starts = []
    r.play()
    r.run_steps(15)
    got = r.lengths()
    check(fits(got, pairs(66, 14)), "performance mode: long first, and the pair runs on past the end", str(got))
    r.stop()


def test_live(mt: Path) -> None:
    r = Rig(mt)
    m = r.m
    m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
    sd = FakeSd(m)
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), sd.obj)
    m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: None)
    m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    edited = m.addr("_ZN4mods20projectSettingEditedEv")
    switched = m.addr("_ZN4mods25projectSwingSwitchChangedEv")
    other = m.addr("_ZN4mods20projectSwitchChangedEv")

    r.set_switch(True)
    r.set_amount(66)
    r.clear()
    r.swing_fx(0, 2, 3, 40)
    r.stop()
    r.starts = []
    r.play()
    r.run_steps(2)                  # on step 1, the short step of the first pair
    before = r.correction()
    check(abs(before - 84) < 0.01, "playing at 66, on the short step", str(before))

    r.set_amount(60)
    m.call(edited)
    check(abs(r.correction() - 90) < 0.01, "the amount edited: at once, the pair's position kept", str(r.correction()))

    r.set_switch(False)
    m.call(switched)
    check(abs(r.correction() - 100) < 0.01, "switched off: no swing", str(r.correction()))

    r.set_amount(70)
    m.call(edited)
    check(abs(r.correction() - 100) < 0.01, "the amount edited with the mod off: still none", str(r.correction()))

    r.set_switch(True)
    m.call(switched)
    check(abs(r.correction() - 80) < 0.01, "switched on: the amount, the pair's position kept", str(r.correction()))

    # step 3 has a Swing FX at 40
    r.run_steps(3)
    fx = r.correction()
    check(abs(fx - 90) < 0.01 or abs(fx - 110) < 0.01, "a Swing FX took over", str(fx))
    m.call(edited)
    check(abs(r.correction() - fx) < 0.01, "an unchanged amount leaves it alone", str(r.correction()))
    m.write(m.addr("_ZN4mods8switchesE") + HUMANIZE_ID, b"\x01")
    m.call(other)
    check(abs(r.correction() - fx) < 0.01, "another mod's switch leaves it alone", str(r.correction()))
    m.write(m.addr("_ZN4mods8switchesE") + HUMANIZE_ID, b"\x00")

    r.stop()
    r.set_amount(55)
    m.call(edited)
    r.set_switch(True)
    m.call(switched)
    check(abs(r.correction() - 100) < 0.01, "stopped: nothing changes until the next take", str(r.correction()))
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def menu_item(m: Machine, group: str, index: int) -> int:
    return m.r32(m.r32(m.addr(group) + 12) + 4 * index)


def test_config(mt: Path) -> None:
    r = Rig(mt)
    m = r.m
    switches = m.addr("_ZN4mods8switchesE")
    settings = m.addr("_ZN4mods5swing8settingsE")
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))

    item = menu_item_named(m, "menuMods", "Global Swing")
    setup = m.r32(item + 16)
    check(m.cstr(m.r32(item + 4)) == "Global Swing" and m.r32(setup) == m.addr("_ZN4mods8defaultsE") + SWING_ID,
          "Config -> Mods: Global Swing sets the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Global Swing")
    setup = m.r32(item + 16)
    check(m.cstr(m.r32(item + 4)) == "Global Swing" and m.r32(setup) == switches + SWING_ID,
          "Project Settings -> Mods: Global Swing is the project's switch")

    group = m.addr("menuProjectSwing")
    check(m.r8(group + 8) == 2 and m.r8(group + 29) == 2, "Project Settings -> Global Swing has 2 items")
    check(m.r32(group + 4) == m.addr("menuProject"), "inside Project Settings")
    expected_items = [("Global swing", MENU_LIST_TEXT_ACTION, switches + SWING_ID),
                      ("Swing amount", MENU_LIST_VALUES, settings)]
    for i, (name, kind, value) in enumerate(expected_items):
        item = menu_item(m, "menuProjectSwing", i)
        setup = m.r32(item + 16)
        check(m.cstr(m.r32(item + 4)) == name and m.r32(item + 12) == kind and m.r32(setup) == value,
              f"Project Settings -> Global Swing: {name}", f"{m.cstr(m.r32(item + 4))} {m.r32(item + 12)}")
    setup = m.r32(menu_item(m, "menuProjectSwing", 1) + 16)
    check(m.r8(setup + 4) == 0 and m.r8(setup + 5) == 50 and m.r8(setup + 7) == 25,
          "Swing amount: list rows 0..50, shown as 25..75")

    changes: list[str] = []
    m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: changes.append("changed"))
    m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
    sd = FakeSd(m)
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), sd.obj)
    m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    check(m.r8(settings) == 25, "a new project starts with the default, 50", str(m.r8(settings)))
    check(m.r8(switches + SWING_ID) == 0, "and the mod Off, the default's default")

    edited = m.addr("_ZN4mods20projectSettingEditedEv")
    changes.clear()
    m.call(edited)
    check(changes == [], "no edit: the project is not marked changed", str(changes))
    m.write(settings, bytes([41]))
    m.call(edited)
    check(changes == ["changed"], "an edit marks the project changed", str(changes))
    m.call(m.addr("_ZN4mods16onWorkspaceSavedEv"))
    ws = sd.files.get("Workspace/mods.bin", b"")
    check(bytes([RECORD_ID, 1, 1, 0, 41]) in ws, "it lands in Workspace/mods.bin", ws.hex())

    m.write(settings, bytes([25]))
    m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    check(m.r8(settings) == 41, "loading the Workspace brings it back", str(m.r8(settings)))
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def side_file(on: int, amount: int) -> bytes:
    """swing.bin as the 1.2.0 patch writes it."""
    data = bytes([on, amount - 25, 0, 0])
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return b"HSW1" + data + struct.pack("<I", h)


class Projects:
    """CE's project hooks on a fake SD card, in the order CE's file manager calls them."""

    def __init__(self, r: Rig):
        self.m = m = r.m
        m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
        self.sd = FakeSd(m)
        m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: None)
        m.call(m.addr("_ZN4mods4initEP10FileSystem"), self.sd.obj)
        self.switches = m.addr("_ZN4mods8switchesE")
        self.settings = m.addr("_ZN4mods5swing8settingsE")
        self.path_at = self._find_path()

    def _find_path(self) -> int:
        """cFileManager's path and name fields: wherever the hooks read them from."""
        m, sd = self.m, self.sd
        fm = m.sym("newFileManager")
        probe = "?//mods.bin"
        sd.files[probe] = b"not a real one"
        for at in range(fm.addr, fm.addr + fm.size):
            before = m.read(at, 2)
            m.write(at, b"?\0")
            sd.calls.clear()
            try:
                m.call(m.addr("_ZN4mods17onProjectOpenCopyEv"))
            except RuntimeError:
                pass
            found = any("readFile" in c for c in sd.calls)
            m.write(at, before)
            if found:
                del sd.files[probe]
                return at
        raise RuntimeError("no project path field in cFileManager")

    def set_project(self, folder: str, name: str) -> None:
        self.m.write(self.path_at, folder.encode() + b"\0")
        self.m.write(self.path_at + PATCH_SIZE, name.encode() + b"\0")

    def open(self, folder: str, name: str) -> None:
        self.set_project(folder, name)
        self.m.call(self.m.addr("_ZN4mods17onProjectOpenCopyEv"))
        self.m.call(self.m.addr("_ZN4mods17onWorkspaceLoadedEv"))

    def new_project(self) -> None:
        self.set_project("", "New Project")
        self.m.call(self.m.addr("_ZN4mods17onProjectOpenCopyEv"))
        self.m.call(self.m.addr("_ZN4mods17onWorkspaceLoadedEv"))

    def save(self, folder: str | None = None, name: str | None = None) -> None:
        """Save, or Save As with a folder and name: Workspace first, then the project folder."""
        if folder is not None:
            self.set_project(folder, name)
        self.m.call(self.m.addr("_ZN4mods16onWorkspaceSavedEv"))
        self.m.call(self.m.addr("_ZN4mods14onProjectSavedEv"))

    def edit(self, on: bool, amount: int) -> None:
        self.m.write(self.switches + SWING_ID, bytes([1 if on else 0]))
        self.m.call(self.m.addr("_ZN4mods20projectSwitchChangedEv"))
        self.m.write(self.settings, bytes([amount - 25]))
        self.m.call(self.m.addr("_ZN4mods20projectSettingEditedEv"))

    def state(self) -> tuple[int, int]:
        return self.m.r8(self.switches + SWING_ID), self.m.r8(self.settings) + 25


def test_projects(mt: Path) -> None:
    r = Rig(mt)
    p = Projects(r)
    sd = p.sd

    # the import, through the same hooks a project's Open goes through
    sd.files["Projects/Old/swing.bin"] = side_file(1, 66)
    p.open("Projects", "Old")
    check(p.state() == (1, 66), "a 1.2.0 project, no mods.bin: On with its amount", str(p.state()))
    check(sd.files["Projects/Old/swing.bin"] == side_file(1, 66), "its swing.bin stays as it was")

    sd.files["Projects/OldOff/swing.bin"] = side_file(0, 36)
    p.open("Projects", "OldOff")
    check(p.state() == (0, 36), "a 1.2.0 project with it Off: Off with its amount", str(p.state()))

    # the case that bit on hardware: a mods.bin saved before this build had Swing
    older_set = bytearray(15)
    older_set[HUMANIZE_ID] = 1
    sd.files["Projects/Saved/mods.bin"] = store_image(b"MTP1", [(0, 1, bytes(older_set))])
    sd.files["Projects/Saved/swing.bin"] = side_file(1, 70)
    p.open("Projects", "Saved")
    check(p.state() == (1, 70), "a mods.bin from an older build: swing.bin still imported", str(p.state()))
    check(p.m.r8(p.switches + HUMANIZE_ID) == 1, "and the project's Humanize switch kept")
    p.edit(False, 70)
    p.save()
    p.open("Projects", "OldOff")
    p.open("Projects", "Saved")
    check(p.state() == (0, 70), "imported once: turned Off and saved, it stays Off", str(p.state()))

    # Save
    p.open("Projects", "Old")
    p.edit(True, 60)
    p.save()
    p.new_project()
    check(p.state() == (0, 50), "New Project: Off, 50", str(p.state()))
    p.open("Projects", "Old")
    check(p.state() == (1, 60), "Save: reopened with its settings", str(p.state()))

    # Save As
    p.edit(True, 61)
    p.save("Projects", "Copy")
    check("Projects/Copy/mods.bin" in sd.files, "Save As: mods.bin in the new folder")
    p.open("Projects", "Old")
    check(p.state() == (1, 60), "Save As leaves the first project as it was", str(p.state()))
    p.open("Projects", "Copy")
    check(p.state() == (1, 61), "Save As: the new project has the settings", str(p.state()))

    # New Project, then Save As at once: the defaults, not the last project's
    p.new_project()
    p.save("Projects", "Fresh")
    p.open("Projects", "Copy")
    p.open("Projects", "Fresh")
    check(p.state() == (0, 50), "New Project then Save As: the defaults", str(p.state()))

    # New Project, edited, then Save As
    p.new_project()
    p.edit(True, 70)
    p.save("Projects", "Fresh2")
    p.open("Projects", "Copy")
    p.open("Projects", "Fresh2")
    check(p.state() == (1, 70), "New Project, edited, then Save As: its settings", str(p.state()))

    # Save As over another project
    p.open("Projects", "Copy")
    p.edit(True, 72)
    p.save("Projects", "Fresh")
    p.open("Projects", "Copy")
    p.open("Projects", "Fresh")
    check(p.state() == (1, 72), "Save As over another project: replaces its settings", str(p.state()))

    # an edit not saved is gone after opening another project
    p.edit(True, 30)
    p.open("Projects", "Copy")
    p.open("Projects", "Fresh")
    check(p.state() == (1, 72), "an unsaved edit doesn't stick", str(p.state()))
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def main() -> int:
    mt, ref = Path(sys.argv[1]), Path(sys.argv[2])
    test_projects(mt)
    test_off(mt, ref)
    test_on(mt)
    test_live(mt)
    test_config(mt)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
