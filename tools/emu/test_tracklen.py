# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Variable Track Length checks on full Tracker+ builds, with the synth libraries in. The
second build is the same source with MT_NO_MODS, as plain CE.

The sequencer runs in time, as the audio interrupt runs it: Sequencer::tick, with real
patterns. Notes go to stubs (sendNoteOn, sendNoteOff): nothing reaches the audio engine or
the synth libraries.

1. Off, and On with no length: every note as plain CE, block for block, in every kind of
   take, with lengths stored or a length the pattern has since been shortened below.
2. On: a short track plays exactly what the track with its loop written out plays, on all
   16 tracks, block for block, with Micro-move, Roll, Break, song mode, Play from a
   position, a looped selection and performance mode.
3. CE's conditions: a short track's pass ends at its own length. Play/Skip and Skip/Play,
   GL and !L. A full-length track and the mod Off count as CE. Play starts again, a new
   pattern starts again, the same pattern repeating goes on counting.
4. Live recording: early and late hits, a note-off, length 1, the Micro-move a hit writes.
   Over several loops a note may replace a note-off, never a note. Off and full length
   write what plain CE writes.
5. The grid: refreshPattern's rows and the rows it marks; CE's four drawing functions, both
   arrangements: past the length grey, blank where empty, and no more display list than
   CE's own worst case.
6. The button: its place, its second line, the encoder while it's held and after, and when
   it's held with something else on the screen.
7. Config and files: the switches, mods.bin through Save, Save As and New Project, a 1.2.0
   project's tracklen.bin, pattern files untouched. Where the mod's memory is.

The file rules are also in the host tests (core/mods/tests/test_tracklen.cpp).

Usage: python tools/emu/test_tracklen.py build/emu/TrackerPlus_MT_full.axf build/emu/TrackerPlus_ref_full.axf
"""
from __future__ import annotations

import random
import struct
import sys
from pathlib import Path

from unicorn.arm_const import UC_ARM_REG_S0

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import FakeSd, menu_item_named  # noqa: E402
from test_swing import Projects, QUIET, f32  # noqa: E402

TRACKLEN_ID = 12
RECORD_ID = TRACKLEN_ID + 1
HUMANIZE_ID = 1
TRACK0, TRACK_STRIDE, STEP_SIZE = 28, 1025, 8
TRACKS = 16
STEPS = 128
DT = 0.0005
TEMPO = 400.0
PLAIN = 312500.0 / TEMPO * 48 / 1e6 / DT        # a plain step, in blocks
FX_MICROMOVE, FX_ROLL, FX_CHANCE, FX_BREAK, FX_VOLUME = 2, 3, 4, 13, 18
PLAY_SKIP, SKIP_PLAY, GROUP_LAST, NOT_LAST = 103, 139, 101, 102
NOTE_OFF, EMPTY = -4, -1
GRID_OUTSIDE_PATTERN = 20
PLAYMODE_BACKWARD = 1
MENU_LIST_TEXT_ACTION = 4
DISPLAY_LIMIT = 8172
FONT_SIZE = 172
VIEWS = (0, 1, 2, 4, 8, 3, 5, 9, 6, 10, 12)     # patternViewMode: 4 tracks, or 8 with one or two columns

failures = 0
actual_pattern_offset: int | None = None
rec_options_found: tuple[int, int] | None = None


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


class Rig:
    def __init__(self, axf: Path):
        self.m = m = Machine(axf)
        m.call(m.addr("_GLOBAL__sub_I_sequencer"))
        self.seq = m.addr("sequencer")
        self.patterns = m.addr("seq")
        self.pattern_size = m.sym("seq").size // 2
        m.call(m.addr("_ZN9Sequencer4initEv"), self.seq)

        self.block = 0
        self.log: list[tuple] = []
        self.next_pattern = [1]         # what the next pattern's number is, in song mode
        self.mods = "_ZN4mods8switchesE" in m.elf.symbols
        self.project = m.addr("mtProject")

        for name in QUIET:
            if "sendNoteOff" not in name:
                m.stub_function(m.addr(name), lambda mm: 0)
        m.stub_function(m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"), self._on)
        m.stub_function(m.addr("_ZN9Sequencer11sendNoteOffEhPNS_9strPlayer14strPlayerTrack11strSendStepEa"), self._off)
        m.stub_function(m.addr("_ZN9Sequencer18isTrackEngineMutedEh"), lambda mm: 0)
        m.stub_function(m.addr("_ZN12cFileManager23switchNextPatternInSongEv"), lambda mm: 1)
        m.stub_function(m.addr("_ZN12cFileManager21getLoadedPatternIndexEv"), self._loaded)
        m.stub_function(m.addr("_ZN12cFileManager14getSongPatternEh"), lambda mm: 1)

        self.tick_addr = m.addr("_ZN9Sequencer4tickEf")
        self._set_tempo(TEMPO)
        self.clear()
        if self.mods:
            self.m.write(m.addr("_ZN4mods8switchesE") + TRACKLEN_ID, b"\x01")
            m.call(m.addr("_ZN4mods8tracklen4initEv"))
            self._find_actual_pattern()
            self.set_pattern(1)
            self.set_switch(True)
        elif actual_pattern_offset is not None:
            self.set_pattern(1)

    # -- what's stubbed ---------------------------------------------------------------
    def _on(self, m: Machine) -> None:
        self.log.append((self.block, "on", m.arg(1), bytes(m.read(m.arg(2), 8))))

    def _off(self, m: Machine) -> None:
        self.log.append((self.block, "off", m.arg(1)))

    def _loaded(self, m: Machine) -> int:
        self.next_pattern.append(self.next_pattern.pop(0))
        return self.next_pattern[-1]

    def _set_tempo(self, tempo: float) -> None:
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

    def _find_actual_pattern(self) -> None:
        """mtProject.values.actualPattern, from what Roll's look-ahead reads."""
        global actual_pattern_offset
        m = self.m
        self.clear(16)
        self.put(0, 0, 2, 60)
        m.call(m.addr("_ZN4mods8tracklen4editEhhii"), 7, 0, 3 - 16, 16)
        calc = m.addr("_ZN9Sequencer14calcStepLengthEhh")
        size = m.sym("mtProject").size
        if actual_pattern_offset is None:
            for offset in range(size):
                before = m.read(self.project + offset, 1)
                m.write(self.project + offset, b"\x07")
                got = m.call(calc, self.seq, 0, 3)
                m.write(self.project + offset, before)
                if got == 2:
                    actual_pattern_offset = offset
                    break
            else:
                raise RuntimeError("no actualPattern in mtProject")
        m.call(m.addr("_ZN4mods8tracklen4initEv"))
        self.clear()

    # -- the settings -----------------------------------------------------------------
    def set_switch(self, on: bool) -> None:
        self.m.write(self.m.addr("_ZN4mods8switchesE") + TRACKLEN_ID, bytes([1 if on else 0]))

    def set_pattern(self, number: int) -> None:
        self.m.write(self.project + actual_pattern_offset, bytes([number]))

    def pattern_number(self) -> int:
        return self.m.r8(self.project + actual_pattern_offset)

    def set_length(self, track: int, length: int, pattern_length: int, pattern: int = 1) -> bool:
        """Through Trk Len's own edit: from none or any length to `length`."""
        edit = self.m.addr("_ZN4mods8tracklen4editEhhii")
        self.m.call(edit, pattern, track, -300 & 0xFFFFFFFF, pattern_length)
        return self.m.call(edit, pattern, track, length - 1, pattern_length) != 0

    def stored(self, track: int, pattern: int = 1) -> int:
        return self.m.call(self.m.addr("_ZN4mods8tracklen6storedEhh"), pattern, track) & 0xFF

    # -- the pattern ------------------------------------------------------------------
    def pattern(self, index: int) -> int:
        return self.patterns + index * self.pattern_size

    def clear(self, length: int = 16) -> None:
        for index in range(2):
            self.m.call(self.m.addr("_ZN9Sequencer12clearPatternEPNS_10strPatternE"),
                        self.seq, self.pattern(index))
            for track in range(TRACKS):
                self.m.write(self.pattern(index) + TRACK0 + track * TRACK_STRIDE, bytes([length - 1]))

    def step_at(self, index: int, track: int, step: int) -> int:
        return self.pattern(index) + TRACK0 + track * TRACK_STRIDE + 1 + step * STEP_SIZE

    def put(self, index, track, step, note=EMPTY, fx=(0, 0), fx2=(0, 0), volume=-1, micromove=-1) -> None:
        self.m.write(self.step_at(index, track, step),
                     struct.pack("<bBBBBBbb", note, 0, fx[0], fx[1], fx2[0], fx2[1], volume, micromove))

    def put_raw(self, index, track, step, raw: bytes) -> None:
        self.m.write(self.step_at(index, track, step), raw)

    def get(self, index, track, step) -> tuple:
        return struct.unpack("<bBBBBBbb", self.m.read(self.step_at(index, track, step), STEP_SIZE))

    def image(self, index: int = 0) -> bytes:
        return bytes(self.m.read(self.pattern(index), self.pattern_size))

    # -- playing ----------------------------------------------------------------------
    def run_blocks(self, count: int) -> None:
        for _ in range(int(count)):
            self.m.uc.reg_write(UC_ARM_REG_S0, f32(DT))
            self.m.call(self.tick_addr, self.seq, max_insns=20_000_000)
            self.block += 1

    def run_to(self, step: float) -> None:
        """To this far into the take, in steps from its start."""
        self.run_blocks(max(0, round(step * PLAIN) - self.block))

    def play(self, from_pos: int = 0) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer11playPatternEh"), self.seq, from_pos)

    def stop(self) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer4stopEv"), self.seq)

    def begin(self) -> None:
        self.stop()
        self.log = []
        self.block = 0

    def take(self, kind: str, steps: int) -> list[tuple]:
        """Every note of a take of `steps` plain steps."""
        m = self.m
        self.begin()
        if kind == "play":
            self.play()
        elif kind == "from 5":
            self.play(5)
        elif kind == "song":
            m.call(m.addr("_ZN9Sequencer20playSongFromPositionEh"), self.seq, 0)
        elif kind == "selection":
            m.call(m.addr("_ZN9Sequencer12setSelectionEhhhh"), self.seq, 2, 0, 9, 15)
            m.call(m.addr("_ZN9Sequencer13playSelectionEb"), self.seq, 1)
        elif kind == "performance":
            m.call(m.addr("_ZN9Sequencer20enterPerformanceModeEv"), self.seq)
            for track in range(0, TRACKS, 3):
                m.call(m.addr("_ZN9Sequencer22setPerformancePlayModeEha"), self.seq, track, PLAYMODE_BACKWARD)
            self.play()
        else:
            raise ValueError(kind)
        self.run_blocks(steps * PLAIN)
        log = self.log
        self.stop()
        return log      # performance mode stays on: it's the last of a rig's takes


# ---- 1, 2: what plays ----------------------------------------------------------------

KINDS = ["play", "from 5", "song", "selection", "performance"]


def random_step(rng: random.Random, fx_kinds: bool) -> bytes:
    """A note or not, with what Roll, Micro-move and Break read. No chance, no randomness."""
    if rng.random() < 0.35:
        return struct.pack("<bBBBBBbb", EMPTY, 0, 0, 0, 0, 0, -1, -1)
    fx, value = 0, 0
    if fx_kinds:
        fx, value = rng.choice([(0, 0), (0, 0), (FX_MICROMOVE, 40), (FX_ROLL, 3), (FX_ROLL, 10), (FX_VOLUME, 50)])
    return struct.pack("<bBBBBBbb", rng.randint(36, 84), 0, fx, value, 0, 0, -1, -1)


def lengths_for(rng: random.Random, pattern_length: int) -> list[int]:
    return [rng.choice([1, 2, 3, 4, 5, 6, 7, pattern_length - 1, pattern_length, pattern_length])
            for _ in range(TRACKS)]


def fill_short(r: Rig, rng_seed: int, length: int, lengths: list[int], breaks: bool = False) -> None:
    """Each track's own steps, with other notes past its length that must never sound."""
    rng = random.Random(rng_seed)
    r.clear(length)
    for track in range(TRACKS):
        for step in range(length):
            raw = random_step(rng, True)
            for index in range(2):
                r.put_raw(index, track, step, raw)
    if breaks:
        for index in range(2):
            r.put(index, 5, min(2, lengths[5] - 1), 50, (FX_BREAK, 0))


def write_out(r: Rig, length: int, lengths: list[int]) -> None:
    """The same tracks with no length: each step holds what the loop plays there."""
    for track in range(TRACKS):
        e = lengths[track]
        for index in range(2):
            own = [r.m.read(r.step_at(index, track, s), STEP_SIZE) for s in range(e)]
            for step in range(length):
                r.put_raw(index, track, step, bytes(own[step % e]))


def test_off(mt: Path, ref: Path) -> None:
    rng = random.Random(1)
    length = 16
    lengths = lengths_for(rng, length)
    a, b = Rig(mt), Rig(ref)
    for rig in (a, b):
        fill_short(rig, 5, length, lengths)

    for kind in KINDS:
        want = b.take(kind, 40)
        check(len(want) > 40, f"the rig plays: {kind}", str(len(want)))

        a.set_switch(True)
        a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
        check(a.take(kind, 40) == want, f"On, no lengths: every note as plain CE: {kind}")

        for track in range(TRACKS):
            a.set_length(track, lengths[track], length)
        a.set_switch(False)
        check(a.take(kind, 40) == want, f"Off, with lengths stored: every note as plain CE: {kind}")
        a.set_switch(True)

    # a pattern shortened below a track's length plays the track whole
    a, b = Rig(mt), Rig(ref)
    for rig in (a, b):
        fill_short(rig, 5, length, lengths)
    for track in range(TRACKS):
        a.set_length(track, 20, 32)
    check(a.take("play", 40) == b.take("play", 40), "a length past the pattern's: as plain CE")

    # a length of the pattern's whole length is none
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    a.set_length(3, 16, 16)
    check(a.stored(3) == 0, "the pattern's length stores as none", str(a.stored(3)))


def test_written_out(mt: Path) -> None:
    for seed, length, breaks in ((11, 16, False), (12, 16, True), (13, 7, False), (14, 128, False), (15, 64, False)):
        rng = random.Random(seed)
        lengths = lengths_for(rng, length)
        a = Rig(mt)
        fill_short(a, seed, length, lengths, breaks)
        for track in range(TRACKS):
            a.set_length(track, lengths[track], length)

        b = Rig(mt)
        b.set_switch(False)
        fill_short(b, seed, length, lengths, breaks)
        write_out(b, length, lengths)

        steps = min(2 * length + 12, 140)
        for kind in KINDS if length <= 16 else ["play"]:
            got, want = a.take(kind, steps), b.take(kind, steps)
            ok = got == want and len(want) > steps // 2
            first = next((i for i, (x, y) in enumerate(zip(got, want)) if x != y), None)
            check(ok, f"length {length}{' with Break' if breaks else ''}: a short track plays its loop written out: {kind}",
                  f"{len(got)} vs {len(want)} notes, first difference {first}: "
                  f"{got[first] if first is not None else ''} vs {want[first] if first is not None else ''}")

    # the notes past the length really are other notes: the check above can fail
    a = Rig(mt)
    a.clear(16)
    for step in range(16):
        a.put(0, 1, step, 40 + step)
    a.set_length(1, 3, 16)
    notes = [e[3][0] for e in a.take("play", 16) if e[1] == "on" and e[2] == 1]
    check(notes[:16] == [40, 41, 42] * 5 + [40], "a track of 3: steps 1-3 over and over, to the pattern's end", str(notes))
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    notes = [e[3][0] for e in a.take("play", 16) if e[1] == "on" and e[2] == 1]
    check(notes[:16] == [40 + s for s in range(16)], "and whole again with its length gone", str(notes))


def test_roll_look_ahead(mt: Path) -> None:
    """calcStepLength against the loop written out, for every step of every length."""
    a = Rig(mt)
    calc = a.m.addr("_ZN9Sequencer14calcStepLengthEhh")
    rng = random.Random(3)
    ok = True
    for trial in range(40):
        length = rng.choice([4, 7, 16, 32])
        e = rng.randint(1, length - 1)
        a.clear(length)
        own = [rng.random() < 0.3 for _ in range(e)]
        for s in range(e):
            a.put(0, 2, s, 60 if own[s] else EMPTY)
        for s in range(e, length):
            a.put(0, 2, s, 77 if rng.random() < 0.5 else EMPTY)
        a.set_length(2, e, length)
        for step in range(length):
            want = length - step
            for d in range(1, length - step):
                if own[(step + d) % e]:
                    want = d
                    break
            got = a.m.call(calc, a.seq, 2, step)
            if got != want:
                ok = False
                print(f"  length {length} own {e} {own} step {step}: {got} vs {want}")
    check(ok, "Roll's look-ahead: to the next note of the loop, or the pattern's end")


# ---- 3: CE's conditions ----------------------------------------------------------------

def cond_value(play: int, skip: int, base: int = PLAY_SKIP) -> int:
    return base + (play - 1) * 6 + (skip - 1)


def cond_passes(positions: int, pattern_length: int, length: int) -> list[int]:
    """CE's count at each position of a take, with a short track's own loops."""
    out, n = [], 0
    for i in range(positions):
        pos = i % pattern_length
        if i > 0 and (pos == 0 or (length < pattern_length and pos % length == 0)):
            n += 1
        out.append(n)
    return out


def fired_positions(log: list[tuple], track: int) -> list[int]:
    return [round(e[0] / PLAIN) for e in log if e[1] == "on" and e[2] == track]


def test_conditions(mt: Path, ref: Path) -> None:
    pattern_length, positions = 16, 48
    a = Rig(mt)

    for length in (6, 4, 8, 1, 5, 15):
        for play, skip, base, name in ((1, 1, PLAY_SKIP, "Play 1/Skip 1"), (2, 1, PLAY_SKIP, "Play 2/Skip 1"),
                                       (1, 3, PLAY_SKIP, "Play 1/Skip 3"), (1, 1, SKIP_PLAY, "Skip 1/Play 1"),
                                       (2, 2, SKIP_PLAY, "Skip 2/Play 2")):
            a.clear(pattern_length)
            a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
            for s in range(length):
                a.put(0, 1, s, 60, (FX_CHANCE, cond_value(play, skip, base)))
            # a full-length track beside it, counting whole passes
            for s in range(pattern_length):
                a.put(0, 2, s, 62, (FX_CHANCE, cond_value(play, skip, base)))
            a.set_length(1, length, pattern_length)

            def want(own: int) -> list[int]:
                cycle = play + skip
                out = []
                for i, n in enumerate(cond_passes(positions, pattern_length, own)):
                    plays = n % cycle < play
                    if (base == PLAY_SKIP) == plays:
                        out.append(i)
                return out

            log = a.take("play", positions - 0.5)
            check(fired_positions(log, 1) == want(length), f"length {length}, {name}: counts the track's own loops",
                  f"{fired_positions(log, 1)} vs {want(length)}")
            check(fired_positions(log, 2) == want(pattern_length), f"length {length}, {name}: a full-length track counts passes",
                  f"{fired_positions(log, 2)} vs {want(pattern_length)}")

    # the example in the plan, spelled out
    a.clear(16)
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    for s in range(6):
        a.put(0, 1, s, 60, (FX_CHANCE, cond_value(1, 1)))
    a.set_length(1, 6, 16)
    got = fired_positions(a.take("play", 31.5), 1)
    check(got == [0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 22, 23, 24, 25, 26, 27],
          "length 6, Play 1/Skip 1: steps 0-5 play, 6-11 skip, 12-15 play, next pass 0-5 skip", str(got))

    # GL and !L follow the condition before them
    a.clear(16)
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    a.put(0, 1, 0, 60, (FX_CHANCE, cond_value(1, 1)))
    a.put(0, 1, 1, 61, (FX_CHANCE, GROUP_LAST))
    a.put(0, 1, 2, 62, (FX_CHANCE, NOT_LAST))
    a.set_length(1, 3, 16)
    got = [(round(e[0] / PLAIN), e[3][0]) for e in a.take("play", 11.5) if e[1] == "on" and e[2] == 1]
    check(got == [(0, 60), (1, 61), (5, 62), (6, 60), (7, 61), (11, 62)],
          "GL and !L: with the condition of the loop they're in", str(got))

    # Play starts counting again
    a.clear(16)
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    for s in range(4):
        a.put(0, 1, s, 60, (FX_CHANCE, cond_value(1, 1)))
    a.set_length(1, 4, 16)
    first = fired_positions(a.take("play", 10.5), 1)
    second = fired_positions(a.take("play", 10.5), 1)
    check(first == second == [0, 1, 2, 3, 8, 9, 10], "Play counts from the start again", f"{first} {second}")

    # A short track in a song: a new pattern number starts again, the same one goes on.
    def song(numbers: list[int]) -> list[int]:
        a.next_pattern[:] = numbers
        a.set_pattern(numbers[0])
        return fired_positions(a.take("song", 39.5), 1)

    a.clear(16)
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    for index in range(2):
        for s in range(6):
            a.put(index, 1, s, 60, (FX_CHANCE, cond_value(1, 1)))
    for number in (1, 2):
        a.set_length(1, 6, 16, pattern=number)

    got = song([1])
    want = [i for i, n in enumerate(cond_passes(40, 16, 6)) if n % 2 == 0]
    check(got == want, "song mode, the same pattern repeating: goes on counting", f"{got} vs {want}")

    got = song([2, 1])
    def restarting() -> list[int]:
        out = []
        for i in range(40):
            pos = i % 16
            n = pos // 6                # each pattern starts again at 0
            if n % 2 == 0:
                out.append(i)
        return out
    check(got == restarting(), "song mode, another pattern each time: starts again", f"{got} vs {restarting()}")
    a.next_pattern[:] = [1]
    a.set_pattern(1)

    # a pattern with no length in a song: the other pattern's lengths don't follow it
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    a.set_length(1, 6, 16, pattern=2)
    for index in range(2):
        for s in range(16):
            a.put(index, 1, s, 60 + s)
    a.next_pattern[:] = [2, 1]
    a.set_pattern(2)
    notes = [e[3][0] for e in a.take("song", 32) if e[1] == "on" and e[2] == 1]
    check(notes[16:32] == [60 + s for s in range(16)], "song mode: each pattern number has its own lengths", str(notes))
    a.next_pattern[:] = [1]
    a.set_pattern(1)

    # Off: the conditions count as plain CE
    b = Rig(ref)
    for rig in (a, b):
        rig.clear(16)
        for s in range(16):
            rig.put(0, 1, s, 60, (FX_CHANCE, cond_value(1, 1)))
            rig.put(0, 2, s, 61, (FX_CHANCE, cond_value(1, 2, SKIP_PLAY)))
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    a.set_length(1, 6, 16)
    a.set_length(2, 3, 16)
    a.set_switch(False)
    want = b.take("play", 48)
    check(a.take("play", 48) == want, "Off, with lengths stored: conditions as plain CE")
    a.set_switch(True)
    a.m.call(a.m.addr("_ZN4mods8tracklen4initEv"))
    check(a.take("play", 48) == want, "On, no lengths: conditions as plain CE")


# ---- 4: live recording -----------------------------------------------------------------

class Recording:
    """Live recording on one track, by hits at given times."""

    def __init__(self, r: Rig, track: int, one_armed: bool = True):
        self.r = r
        self.track = track
        m = r.m
        for name in ("_ZN12playerEngine6noteOnEhaahhhh", "_ZN12playerEngine7noteOffEa",
                     "_ZN12cFileManager24storePatternUndoRevisionEv",
                     "_ZN12cFileManager29setActualPatternStructChangedEv"):
            m.stub_function(m.addr(name), lambda mm: 0)
        m.call(m.addr("_ZN9Sequencer12setSelectionEhhhh"), r.seq, 0, track, 0, track)
        if one_armed:           # every track starts armed
            for other in range(TRACKS):
                if other != track:
                    m.call(m.addr("_ZN9Sequencer21toogleRecArmedOnTrackEh"), r.seq, other)
        self.on = m.addr("_ZN9Sequencer12handleNoteOnEhhas")
        self.off = m.addr("_ZN9Sequencer13handleNoteOffEhhhs")
        self.rec_enable = m.addr("_ZN9Sequencer9recEnableEb")
    def _writes_micromove(self, r: Rig) -> bool:
        """Whether a hit, with mtConfig as it stands, writes a Micro-move."""
        m = r.m
        r.put(0, self.track, 0)
        m.call(self.on, r.seq, GRID_OUTSIDE_PATTERN, 60, 100, 160)
        m.call(self.off, r.seq, GRID_OUTSIDE_PATTERN, 60, 0, 160)
        got = r.get(0, self.track, 0)[7] != -1
        for s in range(2):
            r.put(0, self.track, s)
        return got

    def set_microtiming(self, on: bool) -> None:
        """mtConfig's recording option, found from what a hit writes."""
        global rec_options_found
        r, m = self.r, self.r.m
        config = m.sym("mtConfig")
        if rec_options_found is None:
            r.begin()
            m.call(self.rec_enable, r.seq, 1)
            r.play()
            r.run_blocks(1)
            for offset in range(config.size):
                before = m.read(config.addr + offset, 1)
                for value in (1, 2, 3):
                    m.write(config.addr + offset, bytes([value]))
                    try:
                        hit = self._writes_micromove(r)
                    except RuntimeError:
                        hit = False
                    if hit:
                        rec_options_found = (offset, value)
                        break
                m.write(config.addr + offset, before)
                if rec_options_found is not None:
                    break
            else:
                raise RuntimeError("no recOptions in mtConfig")
            m.call(self.rec_enable, r.seq, 0)
            r.stop()
        offset, value = rec_options_found
        m.write(config.addr + offset, bytes([value if on else 0]))

    def start(self) -> None:
        r = self.r
        r.begin()
        r.m.call(self.rec_enable, r.seq, 1)
        r.play()

    def finish(self) -> None:
        r = self.r
        r.m.call(self.rec_enable, r.seq, 0)
        r.stop()

    def note_on(self, at: float, note: int = 60) -> None:
        self.r.run_to(at)
        self.r.m.call(self.on, self.r.seq, GRID_OUTSIDE_PATTERN, note, 100, note + 100)

    def note_off(self, at: float, note: int = 60) -> None:
        self.r.run_to(at)
        self.r.m.call(self.off, self.r.seq, GRID_OUTSIDE_PATTERN, note, 0, note + 100)

    def hit(self, at: float, end: float, note: int = 60) -> None:
        self.note_on(at, note)
        self.note_off(end, note)

    def notes(self, count: int) -> list[int]:
        return [self.r.get(0, self.track, s)[0] for s in range(count)]


def fresh_recording(axf: Path, length: int, own: int | None) -> tuple[Rig, Recording]:
    r = Rig(axf)
    r.clear(16)
    if own is not None and r.mods:
        r.set_length(1, own, 16)
    return r, Recording(r, 1)


def test_recording(mt: Path, ref: Path) -> None:
    # where a hit goes
    r, rec = fresh_recording(mt, 16, 4)
    rec.start()
    rec.note_on(6.1, 60)                    # early, at position 6: step 2 of 4
    rec.note_off(6.3, 60)                   # the note-off goes to the next step of the loop
    rec.finish()
    check(rec.notes(6) == [EMPTY, EMPTY, 60, NOTE_OFF, EMPTY, EMPTY],
          "an early hit: onto the step the track is playing, its note-off the next", str(rec.notes(6)))

    r, rec = fresh_recording(mt, 16, 4)
    rec.start()
    rec.note_on(6.7, 60)                    # late, at position 6: step 3
    rec.note_off(6.9, 60)                   # the note-off comes round to step 0
    rec.finish()
    check(rec.notes(6) == [NOTE_OFF, EMPTY, EMPTY, 60, EMPTY, EMPTY],
          "a late hit: the step played next, a note-off past the end comes round", str(rec.notes(6)))

    r, rec = fresh_recording(mt, 16, 4)
    rec.start()
    rec.hit(7.7, 7.9)                       # late at position 7: the loop's start
    rec.finish()
    check(rec.notes(1) == [60], "a late hit on the loop's last step: its first", str(rec.notes(4)))

    r, rec = fresh_recording(mt, 16, 4)
    rec.start()
    rec.hit(15.7, 15.9)                     # late at the pattern's last position: step 0
    rec.finish()
    check(rec.notes(1) == [60], "a late hit on the pattern's last step: the first", str(rec.notes(4)))

    r, rec = fresh_recording(mt, 16, 1)
    rec.start()
    rec.hit(5.1, 5.3)
    rec.finish()
    check(rec.notes(2) == [60, EMPTY], "length 1: all hits on step 1, and its note-off has nowhere to go", str(rec.notes(3)))

    r, rec = fresh_recording(mt, 16, 4)
    rec.set_microtiming(True)
    rec.start()
    rec.note_on(6.5, 60)
    rec.note_off(7.5, 60)
    rec.finish()
    on, off = r.get(0, 1, 2), r.get(0, 1, 3)
    check(on[0] == 60 and 40 <= on[7] <= 60, "Micro-move: a hit keeps its timing on a short track", str(on))
    check(off[0] == NOTE_OFF and 40 <= off[7] <= 60,
          "Micro-move: a note-off on the step being played has its timing, past the length too", str(off))

    # every track armed: the hit goes to the cursor's track, or the next that's free
    r = Rig(mt)
    r.clear(16)
    r.set_length(0, 4, 16)
    rec = Recording(r, 0, one_armed=False)
    rec.start()
    rec.hit(6.1, 6.3)
    check(rec.notes(4) == [EMPTY, EMPTY, 60, NOTE_OFF], "every track armed: a hit on the cursor's short track", str(rec.notes(4)))
    rec.hit(11.1, 11.3, 62)                 # position 11 = step 3: replaces the off
    check(rec.notes(4) == [NOTE_OFF, EMPTY, 60, 62], "and it replaces a note-off there too", str(rec.notes(4)))
    rec.finish()

    # several loops: tl7's fix
    r, rec = fresh_recording(mt, 16, 4)
    rec.start()
    rec.hit(0.1, 0.3, 60)                   # note on 0, off on 1
    rec.hit(5.1, 5.3, 62)                   # position 5 = step 1: replaces the off, off on 2
    rec.hit(10.1, 10.3, 64)                 # position 10 = step 2: replaces the off, off on 3
    check(rec.notes(4) == [60, 62, 64, NOTE_OFF], "a new note replaces a note-off from an earlier loop", str(rec.notes(4)))
    rec.hit(12.1, 12.3, 65)                 # position 12 = step 0: a note, stays
    check(rec.notes(4) == [60, 62, 64, NOTE_OFF], "a note is never replaced", str(rec.notes(4)))
    rec.finish()

    # a note-off with an FX or a volume isn't only a note-off
    r, rec = fresh_recording(mt, 16, 4)
    r.put(0, 1, 2, NOTE_OFF, (FX_VOLUME, 10))
    r.put(0, 1, 3, NOTE_OFF, volume=20)
    rec.start()
    rec.hit(2.1, 2.3)
    rec.hit(3.1, 3.3)
    rec.finish()
    check(rec.notes(2 + 2)[2:] == [NOTE_OFF, NOTE_OFF], "a note-off with an FX or a volume stays", str(rec.notes(4)))

    # the note-off's own timing doesn't go on the note
    r, rec = fresh_recording(mt, 16, 4)
    r.put(0, 1, 1, NOTE_OFF, micromove=77)
    rec.start()
    rec.hit(1.1, 1.3)
    rec.finish()
    check(r.get(0, 1, 1)[0] == 60 and r.get(0, 1, 1)[7] == -1, "a note in place of a note-off has none of its timing", str(r.get(0, 1, 1)))

    # a whole-length track doesn't replace note-offs: CE's rule
    r, rec = fresh_recording(mt, 16, None)
    r.put(0, 1, 2, NOTE_OFF)
    rec.start()
    rec.hit(2.1, 2.3)
    rec.finish()
    check(rec.notes(3)[2] == NOTE_OFF, "a whole-length track keeps CE's rule on note-offs", str(rec.notes(4)))

    # the same takes as plain CE: Off, and On at full length
    def session(rig: Rig, rec: Recording, microtiming: bool) -> bytes:
        rec.set_microtiming(microtiming)
        rec.start()
        for at, end, note in ((0.1, 0.3, 60), (2.7, 3.2, 62), (5.1, 7.7, 64), (15.8, 16.3, 65), (17.1, 17.3, 66),
                              (20.5, 20.9, 67), (33.1, 33.6, 68)):
            rec.hit(at, end, note)
        rec.finish()
        return rig.image()

    for microtiming in (False, True):
        b, rec_b = fresh_recording(ref, 16, None)
        want = session(b, rec_b, microtiming)
        a, rec_a = fresh_recording(mt, 16, None)
        check(session(a, rec_a, microtiming) == want, f"On, full length: recording as plain CE, Micro-move {microtiming}")
        a, rec_a = fresh_recording(mt, 16, 5)
        a.set_switch(False)
        check(session(a, rec_a, microtiming) == want, f"Off, with a length: recording as plain CE, Micro-move {microtiming}")


# ---- 5, 6: the Pattern screen ---------------------------------------------------------

TP_ROW = 19
TP_TRACK = 15 * TP_ROW
TP_ACTUAL_TRACK = 16 * TP_TRACK
TP_ACTUAL_STEP = TP_ACTUAL_TRACK + 2
TP_FIRST_VISIBLE = TP_ACTUAL_TRACK + 8
TP_POPUP_MODE = TP_ACTUAL_TRACK + 27
TP_INACTIVE = TP_ACTUAL_TRACK + 28
TP_PAST_LENGTH = TP_ACTUAL_TRACK + 112
TP_SIZE = TP_PAST_LENGTH + 32


class Screen:
    """cPatternEditor's own functions, with the display and input calls caught."""

    def __init__(self, r: Rig):
        self.r = r
        self.m = m = r.m
        m.call(m.addr("_GLOBAL__sub_I__ZN14cPatternEditor14trackerPatternE"))
        self.tp = m.addr("_ZN14cPatternEditor14trackerPatternE")
        self.editor = m.addr("patternEditor")
        self.held: set[int] = set()
        self.text2: list[tuple] = []
        self.values: dict[int, int] = {}
        self.binds: list[tuple] = []
        self.changed = 0
        self.refreshes = 0

        m.stub_function(m.addr("_ZN7Buttons15isButtonPressedEi"), lambda mm: 1 if mm.arg(1) in self.held else 0)
        m.stub_function(m.addr("_ZN8cDisplay15setControlText2EaPKc"),
                        lambda mm: self.text2.append((self._signed(mm.arg(1)), mm.cstr(mm.arg(2)))) and None)
        m.stub_function(m.addr("_ZN8cDisplay15setControlValueEai"),
                        lambda mm: self.values.__setitem__(self._signed(mm.arg(1)), mm.arg(2)))
        for name in m.elf.symbols:
            if name.startswith("_ZN8cDisplay") and ("Control" in name) and not name.startswith(("_ZN8cDisplay15setControlText2", "_ZN8cDisplay15setControlValue")):
                if m.elf.symbols[name].is_func:
                    m.stub_function(m.addr(name), lambda mm: 0)
        m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), self._changed)
        for name, bind in (("_ZN14InputProcessor12setButtonObjEhPFvhE", "hold"),
                           ("_ZN14InputProcessor12setButtonObjEhhPFvvE", "press"),):
            m.stub_function(m.addr(name), lambda mm, b=bind: self.binds.append((b, mm.arg(1))))
        m.stub_function(m.addr("_ZN14InputProcessor17clearButtonsRangeEhh"), lambda mm: 0)
        check(m.sym("_ZN14cPatternEditor14trackerPatternE").size == TP_SIZE, "the rig knows strTrackerPattern's layout",
              str(m.sym("_ZN14cPatternEditor14trackerPatternE").size))

    @staticmethod
    def _signed(v: int) -> int:
        v &= 0xFF
        return v - 256 if v >= 128 else v

    def _changed(self, m: Machine) -> None:
        self.changed += 1

    def set_track(self, track: int) -> None:
        self.m.write(self.tp + TP_ACTUAL_TRACK, bytes([track]))

    def set_step(self, step: int) -> None:
        self.m.write(self.tp + TP_ACTUAL_STEP, struct.pack("<h", step))

    def row(self, track: int, row: int) -> tuple[str, str, str, str]:
        a = self.tp + track * TP_TRACK + row * TP_ROW
        return (self.m.cstr(a, 5), self.m.cstr(a + 5, 4), self.m.cstr(a + 9, 5), self.m.cstr(a + 14, 5))

    def past(self, track: int) -> int:
        return struct.unpack("<H", self.m.read(self.tp + TP_PAST_LENGTH + 2 * track, 2))[0]


def test_grid(mt: Path) -> None:
    r = Rig(mt)
    s = Screen(r)
    m = r.m
    refresh = m.addr("_ZN14cPatternEditor14refreshPatternEv")
    pte = s.editor

    r.clear(16)
    for step in range(16):
        r.put(0, 1, step, 40 + step, (FX_VOLUME, step))
    r.put(0, 2, 0, 60)
    r.set_length(1, 3, 16)
    r.set_length(2, 4, 16)
    s.set_step(7)                           # rows 0..14 show steps 0..14
    m.call(refresh, pte, max_insns=20_000_000)

    r3 = [s.row(1, row) for row in range(15)]
    check([x[0] for x in r3[:3]] == [x[0] for x in r3[3:6]] == [x[0] for x in r3[12:15]],
          "the grid: past the length, the loop", str([x[0] for x in r3]))
    check(r3[0][2] == r3[3][2] != "", "and its FX", str([x[2] for x in r3[:6]]))
    check(r3[0][0] != "---" and s.past(1) == 0b111111111111000, "the rows past the length are marked", bin(s.past(1)))
    check(s.past(2) == 0b111111111110000, "each track its own", bin(s.past(2)))
    check(s.past(3) == 0, "a track with no length has none", bin(s.past(3)))
    check(s.row(2, 4)[0] == s.row(2, 0)[0] and s.row(2, 5)[0] == s.row(2, 1)[0], "a ghost of an empty step reads as empty")

    # moving the length moves the rows at once
    r.set_length(1, 5, 16)
    m.call(refresh, pte, max_insns=20_000_000)
    check(s.past(1) == 0b111111111100000, "a change: the next refresh has it", bin(s.past(1)))
    # Off is CE's grid
    r.set_switch(False)
    m.call(refresh, pte, max_insns=20_000_000)
    check(all(s.past(t) == 0 for t in range(TRACKS)), "Off: nothing marked")
    check(s.row(1, 3)[0] != s.row(1, 0)[0], "Off: the track's own steps", str(s.row(1, 3)))
    r.set_switch(True)

    # The arrangement the rig starts in, the revolver: rows wrap round the pattern first,
    # then round the track's loop.
    r.clear(8)
    for step in range(8):
        r.put(0, 1, step, 40 + step)
    r.set_length(1, 3, 8)
    s.set_step(0)
    m.call(refresh, pte, max_insns=20_000_000)
    steps = [(row - 7) % 8 for row in range(15)]        # rows show steps 1..7, 0..7
    want_mask = sum(1 << row for row, step in enumerate(steps) if step >= 3)
    check(s.past(1) == want_mask, "revolver: the rows past the length are marked", f"{bin(s.past(1))} vs {bin(want_mask)}")
    shown = [s.row(1, row)[0] for row in range(15)]
    check(shown == [shown[7 + (step % 3)] for step in steps], "revolver: each row shows the step it plays", str(shown))

    # The arrangement with nothing above the first row: those rows stay empty and unmarked.
    config = m.sym("mtConfig")
    found = False
    for offset in range(config.size):
        before = m.read(config.addr + offset, 1)
        for value in (1, 2, 3):
            m.write(config.addr + offset, bytes([value]))
            try:
                m.call(refresh, pte, max_insns=20_000_000)
                found = s.row(1, 0)[0] == ""
            except RuntimeError:
                found = False
            if found:
                break
        if found:
            break
        m.write(config.addr + offset, before)
    check(found, "the rig finds the arrangement without wrapping")
    want_mask = sum(1 << row for row in range(7 + 3, 7 + 8))
    check(s.past(1) == want_mask, "no wrapping: only the steps past the length are marked", f"{bin(s.past(1))} vs {bin(want_mask)}")
    check(s.row(1, 6)[0] == "" and s.row(1, 7 + 3)[0] == s.row(1, 7)[0], "and they show the loop", str(s.row(1, 6)))


def draw_commands(m: Machine, cls: str, obj: int, log: list) -> list[tuple]:
    log.clear()
    m.call(m.addr(f"_ZN{len(cls)}{cls}6updateEv"), obj, max_insns=20_000_000)
    for n in (2, 3, 4, 5):
        m.call(m.addr(f"_ZN{len(cls)}{cls}7update{n}Ev"), obj, max_insns=20_000_000)
    return list(log)


class Drawing:
    """CE's tracker controls drawing a made-up strTrackerPattern."""

    def __init__(self, m: Machine, cls: str):
        self.m = m
        self.cls = cls
        self.log: list[tuple] = []
        self.view = 0
        for name, sym in m.elf.symbols.items():
            if name.startswith("_Z") and "API_" in name and sym.is_func:
                m.stub_function(sym.addr, lambda mm, n=name: self._command(n, mm))
        self.obj = m.alloc(1024, 8)
        m.call(m.addr(f"_ZN{len(cls)}{cls}C1EP20strControlProperties"), self.obj, 0)
        self.tp = m.alloc(TP_SIZE, 8)
        fonts = m.sym("fonts")          # as the display's init sets them, from its font files
        for i in range(fonts.size // FONT_SIZE):
            a = fonts.addr + i * FONT_SIZE
            m.write(a, bytes([0]) + bytes([10] * 127))
            m.write(a + 152, struct.pack("<h", i + 1))
            m.write(a + 164, struct.pack("<HH", 10, 20))
        self.colors = m.alloc(4 * 32, 4)
        for i in range(32):
            m.w32(self.colors + 4 * i, 0x100000 + i)
        vt = m.r32(self.obj)
        check(vt != 0, f"{cls}: built")
        m.call(m.addr(f"_ZN{len(cls)}{cls}9setColorsEPKm"), self.obj, self.colors)
        m.call(m.addr(f"_ZN{len(cls)}{cls}7setDataEPv"), self.obj, self.tp)

    def _command(self, name: str, m: Machine) -> None:
        short = name.split("API_")[1]
        if short.startswith("COLOR"):
            self.log.append(("COLOR", m.arg(0) & 0xFFFFFF))
        elif short.startswith("VERTEX"):
            self.log.append(("VERTEX", m.arg(0) & 0xFFFF, m.arg(1) & 0xFFFF, m.arg(3) & 0xFF))
        else:
            self.log.append((short.split("E")[0],))

    def fill(self, cell, past=0) -> None:
        """Every row of every track: cell(track, row) gives (note, instrument, fx1, fx2)."""
        m = self.m
        m.write(self.tp, b"\0" * TP_SIZE)
        for t in range(TRACKS):
            for row in range(15):
                note, instr, fx1, fx2 = cell(t, row)
                a = self.tp + t * TP_TRACK + row * TP_ROW
                m.write(a, note.encode().ljust(5, b"\0"))
                m.write(a + 5, instr.encode().ljust(4, b"\0"))
                m.write(a + 9, fx1.encode().ljust(5, b"\0"))
                m.write(a + 14, fx2.encode().ljust(5, b"\0"))
            m.write(self.tp + TP_PAST_LENGTH + 2 * t, struct.pack("<H", past(t) if callable(past) else past))
        m.write(self.tp + TP_ACTUAL_TRACK, b"\0")
        m.write(self.tp + TP_ACTUAL_STEP, struct.pack("<h", 7))
        m.write(self.tp + TP_ACTUAL_STEP + 2, struct.pack("<h", 7))
        m.write(self.tp + TP_ACTUAL_STEP + 4, struct.pack("<h", 16))
        m.write(self.tp + TP_FIRST_VISIBLE + 1, b"\x04")

    def draw(self) -> list[tuple]:
        m = self.m
        m.call(m.addr(f"_ZN{len(self.cls)}{self.cls}8setValueEi"), self.obj, self.view)
        return draw_commands(m, self.cls, self.obj, self.log)


def test_drawing(mt: Path) -> None:
    for cls in ("cTracker", "cHorizontalTracker"):
        r = Rig(mt)
        d = Drawing(r.m, cls)
        grey = 0x100000 + 12

        full = ("C-4", "01", "V50", "V50")
        empty = ("---", "--", "---", "---")

        d.fill(lambda t, row: empty)
        all_empty = d.draw()
        d.fill(lambda t, row: empty, past=0x7FFF)
        ghost_empty = d.draw()
        check(len(ghost_empty) < len(all_empty), f"{cls}: an empty cell past the length draws nothing",
              f"{len(ghost_empty)} vs {len(all_empty)}")

        d.fill(lambda t, row: full)
        all_full = d.draw()
        d.fill(lambda t, row: full, past=0x7FFF)
        ghost_full = d.draw()
        check(("COLOR", grey) in ghost_full, f"{cls}: a cell past the length is grey")
        check(sum(1 for c in ghost_full if c[0] == "VERTEX") == sum(1 for c in all_full if c[0] == "VERTEX"),
              f"{cls}: it draws the same characters")

        # The display list, in each view CE has: 4 tracks with all four columns, 8 tracks
        # with one or two. Past the length a cell has the same characters as CE's, and a
        # colour change at most where CE's own cells have one.
        overall = [0, 0]
        for view in VIEWS:
            d.view = view
            d.fill(lambda t, row: full if (t + row) % 2 else empty)
            ce = len(d.draw())
            d.fill(lambda t, row: full)
            ce = max(ce, len(d.draw()))
            ours = 0
            for mask in (lambda t: 0x7FFF, lambda t: 0x7FF8, lambda t: 0x7F00 >> (t % 4),
                         lambda t: 0x5555 if t % 2 else 0x2AAA):  # the last: a 2-step pattern in the revolver view
                d.fill(lambda t, row: full, past=mask)
                ours = max(ours, len(d.draw()))
            real = 0
            for mask in (lambda t: 0x7FFF, lambda t: 0x7FF8, lambda t: 0x7F00 >> (t % 4)):
                d.fill(lambda t, row: full, past=mask)
                real = max(real, len(d.draw()))
            overall[0] = max(overall[0], ce)
            overall[1] = max(overall[1], ours)
            check(real <= ce, f"{cls}, view {view}: a track's length costs no more than CE's own worst, "
                  f"{real * 4} B against {ce * 4} B", f"{real} vs {ce}")
            print(f"  view {view}: CE's worst {ce * 4} B, short tracks {real * 4} B, a 2-step pattern alternating {ours * 4} B")
        check(overall[1] * 4 < DISPLAY_LIMIT, f"{cls}: the grid's worst is under the limit: {overall[1] * 4} B of {DISPLAY_LIMIT} B",
              str(overall))
        d.view = 0

        # a track with nothing marked draws what it drew
        d.fill(lambda t, row: full if row % 3 else empty)
        first = d.draw()
        d.fill(lambda t, row: full if row % 3 else empty, past=0)
        check(first == d.draw(), f"{cls}: nothing marked, nothing changes")


def find_encoder(s: Screen) -> int:
    """The Pattern screen's encoder function, as its own screen setup binds it."""
    m = s.m
    bound: list[int] = []
    for name in m.elf.symbols:
        if name.startswith("_ZN14InputProcessor9setPotObj") and m.elf.symbols[name].is_func:
            m.stub_function(m.addr(name), lambda mm: bound.append(mm.arg(2)) and None)
    for name in m.elf.symbols:
        if name.startswith("_ZN14InputProcessor") and m.elf.symbols[name].is_func \
                and "setButtonObj" not in name and "clearButtonsRange" not in name and "setPotObj" not in name:
            m.stub_function(m.addr(name), lambda mm: 0)
    m.call(m.addr("_ZN14cPatternEditor21setDefaultScreenFunctEv"), s.editor, max_insns=5_000_000)
    candidates = [b for b in bound if b]
    if not candidates:
        raise RuntimeError("no encoder bound")
    return candidates[0]


def test_button(mt: Path) -> None:
    r = Rig(mt)
    s = Screen(r)
    m = r.m
    m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), FakeSd(m).obj)
    switches = m.addr("_ZN4mods8switchesE")
    get = m.addr("_ZN4mods19patternEditorButtonEj")
    for name in ("_ZN14cPatternEditor14refreshPatternEv",):
        m.stub_function(m.addr(name), lambda mm: setattr(s, "refreshes", s.refreshes + 1))

    # its place
    m.write(switches + HUMANIZE_ID, b"\x00")
    r.set_switch(False)
    check(m.call(get, 0) == 0, "Off: no button")
    r.set_switch(True)
    button = m.call(get, 0)
    check(button != 0 and m.cstr(m.r32(button + 4)) == "Trk Len", "On: a Trk Len button on the first free slot")
    check(m.r32(button + 8) == 0 and m.r32(button + 12) != 0 and m.r32(button + 16) != 0,
          "a held button with a second line")
    m.write(switches + HUMANIZE_ID, b"\x01")
    check(m.cstr(m.r32(m.call(get, 0) + 4)) == "Humanize" and m.cstr(m.r32(m.call(get, 1) + 4)) == "Trk Len",
          "With Humanize on: the next slot")
    check(m.call(get, 2) == 0, "and no more")
    m.write(switches + HUMANIZE_ID, b"\x00")

    # the page
    page_fn = m.addr("_ZL25functNextActionButtonPagev")
    m.stub_function(m.addr("_ZN14cPatternEditor29refreshActualActionButtonPageEv"), lambda mm: 0)
    before = bytes(m.read(s.editor, m.sym("patternEditor").size))
    m.call(page_fn)
    after = bytes(m.read(s.editor, m.sym("patternEditor").size))
    moved = [i for i in range(len(before)) if before[i] != after[i]]
    check(len(moved) == 1, "the rig finds the button page", str(moved))
    page_at = s.editor + moved[0]

    def set_page(n: int) -> None:
        m.write(page_at, bytes([n]))

    set_page(2)
    held_slot = 2                           # the first free slot: interfaceButton2

    text2 = m.r32(button + 16)

    def second_line() -> str:
        return m.cstr(m.call(text2))

    r.clear(16)
    s.set_track(3)
    check(second_line() == "--", "a track with no length: --", second_line())
    r.set_length(3, 6, 16)
    check(second_line() == "6", "a short track: its length", second_line())
    s.set_track(4)
    check(second_line() == "--", "it follows the cursor", second_line())
    s.set_track(3)

    encoder = find_encoder(s)
    def turn(value: int) -> None:
        m.call(encoder, value)

    # CE's encoder is stubbed past the point where it matters: the pattern's image shows it
    pattern_before = r.image()
    s.held = {held_slot}
    s.refreshes = 0
    s.changed = 0
    turn(1)
    check(r.stored(3) == 7, "held: the encoder sets the length", str(r.stored(3)))
    check(s.refreshes == 1, "the grid is drawn again at once", str(s.refreshes))
    check(s.changed == 1, "and the project is marked changed", str(s.changed))
    turn(-100)
    check(r.stored(3) == 1, "not below 1", str(r.stored(3)))
    turn(-1)
    check(r.stored(3) == 1 and s.changed == 2, "and a turn that changes nothing marks nothing", str(s.changed))
    turn(100)
    check(r.stored(3) == 0, "up to the pattern's length, which is none", str(r.stored(3)))
    check(second_line() == "--", "shown as --", second_line())
    check(r.image() == pattern_before, "a length isn't in the pattern")

    # the label, every frame
    update = m.addr("_ZL23updateTrackLengthButtonv")
    r.set_length(3, 6, 16)
    s.text2.clear()
    m.call(update)
    check(len(s.text2) == 1 and s.text2[0][1] == "6", "the label's second line follows a change", str(s.text2))
    s.text2.clear()
    m.call(update)
    check(s.text2 == [], "and nothing's sent when nothing changed: no display list per frame", str(s.text2))
    s.set_track(4)
    m.call(update)
    check(len(s.text2) == 1 and s.text2[0][1] == "--", "moving the cursor changes it", str(s.text2))
    s.set_track(3)

    # held with something else going on: not the length
    s.held = {held_slot}
    set_page(0)
    length = r.stored(3)
    guard(m, turn)
    check(r.stored(3) == length, "another button page: the encoder isn't the length's")
    s.text2.clear()
    m.call(update)
    check(s.text2 == [], "and the label isn't touched", str(s.text2))
    set_page(2)
    s.held = set()
    guard(m, turn)
    check(r.stored(3) == length, "released: the encoder is CE's")
    r.set_switch(False)
    s.held = {held_slot}
    guard(m, turn)
    check(r.stored(3) == length, "Off: the encoder is CE's")
    r.set_switch(True)
    s.held = {held_slot + 1}
    guard(m, turn)
    check(r.stored(3) == length, "another button held: the encoder is CE's")

    # bound in every mode, unlike the buttons that edit: the rig's screen isn't in edit mode
    s.binds.clear()
    m.call(m.addr("_ZN14cPatternEditor25setActionButtonPage3FunctEv"), s.editor)
    check(("hold", 2) in s.binds, "bound outside edit mode too", str(s.binds))

    # the labels
    s.text2.clear()
    s.values.clear()
    r.set_length(3, 6, 16)
    m.call(m.addr("_ZN14cPatternEditor27showActionButtonPage3LabelsEv"), s.editor, max_insns=5_000_000)
    check(any(t[1] == "6" for t in s.text2), "page 3's labels: the second line", str(s.text2))


def guard(m: Machine, turn) -> None:
    """CE's own encoder path past the mod's check; a stubbed screen may stop it short."""
    try:
        turn(1)
    except RuntimeError as e:
        print(f"  (CE's encoder path stopped in the rig: {e})")


# ---- 7: Config and files ---------------------------------------------------------------

def side_file(entries: list[tuple[int, dict[int, int]]]) -> bytes:
    """tracklen.bin as the 1.2.0 patch writes it."""
    body = struct.pack("<I", len(entries))
    for pattern, lengths in entries:
        body += bytes([pattern]) + bytes(lengths.get(t, 0) for t in range(TRACKS))
    h = 2166136261
    for b in body:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return b"TLN1" + body + struct.pack("<I", h)


def test_config(mt: Path) -> None:
    r = Rig(mt)
    m = r.m
    switches = m.addr("_ZN4mods8switchesE")
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    item = menu_item_named(m, "menuMods", "Track Length")
    setup = m.r32(item + 16)
    check(m.r32(item + 12) == MENU_LIST_TEXT_ACTION and m.r32(setup) == m.addr("_ZN4mods8defaultsE") + TRACKLEN_ID,
          "Config -> Mods: Track Length sets the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Track Length")
    setup = m.r32(item + 16)
    check(m.r32(item + 12) == MENU_LIST_TEXT_ACTION and m.r32(setup) == switches + TRACKLEN_ID,
          "Project Settings -> Mods: Track Length is the project's switch")

    # where it lives
    active = m.addr("_ZN4mods8tracklen13activeLengthsE")
    check(0x20000000 <= active < 0x20040000, "the interrupt's copy is in DTCM", hex(active))
    table = next(s.addr for n, s in m.elf.symbols.items() if n.endswith("tracklen12_GLOBAL__N_15tableE"))
    check(table >= 0x80000000, "the table is in SDRAM", hex(table))


def test_files(mt: Path) -> None:
    r = Rig(mt)
    p = Projects(r)
    m, sd = r.m, p.sd
    edited = m.addr("_ZN4mods21projectTracklenEditedEv")
    r.clear(16)

    def edit(track: int, length: int, pattern: int = 1) -> None:
        r.set_length(track, length, 16, pattern)
        m.call(edited)

    p.new_project()
    check(r.stored(1) == 0, "a new project has no lengths")
    r.set_switch(True)
    m.call(m.addr("_ZN4mods20projectSwitchChangedEv"))
    p.save("Projects", "NoLengths")
    data = sd.files.get("Projects/NoLengths/mods.bin", b"")
    check(bytes([RECORD_ID, 1]) not in data[8:], "no lengths: no record", data.hex())

    # the round trip
    pattern_image = r.image()
    edit(1, 6)
    edit(2, 3)
    edit(15, 1, pattern=255)
    check(r.image() == pattern_image, "lengths don't touch the pattern")
    p.save("Projects", "Short")
    data = sd.files.get("Projects/Short/mods.bin", b"")
    entry = bytes([1, 0, 6, 3] + [0] * 13)
    check(entry in data, "Save: they land in mods.bin", data.hex())
    check(not any(k.endswith("tracklen.bin") for k in sd.files), "and nothing writes a tracklen.bin")

    p.new_project()
    check(r.stored(1) == 0, "New Project: none", str(r.stored(1)))
    p.open("Projects", "Short")
    check((r.stored(1), r.stored(2), r.stored(15, 255)) == (6, 3, 1), "Open brings them back",
          str((r.stored(1), r.stored(2), r.stored(15, 255))))

    # Save As, and an edit that's discarded
    edit(1, 7)
    p.save("Projects", "Copy")
    p.open("Projects", "Short")
    check(r.stored(1) == 6, "Save As leaves the first project as it was", str(r.stored(1)))
    p.open("Projects", "Copy")
    check(r.stored(1) == 7, "Save As: the new project has them", str(r.stored(1)))
    edit(1, 2)
    p.open("Projects", "Short")
    p.open("Projects", "Copy")
    check(r.stored(1) == 7, "an unsaved edit doesn't stick", str(r.stored(1)))

    # Off keeps them
    r.set_switch(False)
    m.call(m.addr("_ZN4mods20projectSwitchChangedEv"))
    p.save()
    p.open("Projects", "Short")
    p.open("Projects", "Copy")
    check(r.stored(1) == 7 and m.r8(p.switches + TRACKLEN_ID) == 0, "Off saved: Off, with the lengths kept")
    r.set_switch(True)

    # the last length gone: the record goes
    p.open("Projects", "Short")
    edit(1, 16)
    edit(2, 16)
    edit(15, 16, pattern=255)
    p.save("Projects", "Gone")
    data = sd.files.get("Projects/Gone/mods.bin", b"")
    check(entry not in data and bytes([1, 0, 6, 3]) not in data, "the last length gone: no entry", data.hex())

    # a full table beside the other records: every pattern, written and read
    p.new_project()
    for pattern in range(1, 256):
        r.set_length(0, 5, 16, pattern)
    m.call(edited)
    p.save("Projects", "Full")
    p.new_project()
    p.open("Projects", "Full")
    check(all(r.stored(0, x) == 5 for x in (1, 100, 255)), "all 255 patterns fit")

    # 1.2.0's file
    sd.files["Projects/Old/tracklen.bin"] = side_file([(1, {1: 6, 2: 3}), (9, {15: 2})])
    original = sd.files["Projects/Old/tracklen.bin"]
    p.new_project()
    p.open("Projects", "Old")
    check((r.stored(1), r.stored(2), r.stored(15, 9)) == (6, 3, 2), "a 1.2.0 project: its lengths",
          str((r.stored(1), r.stored(2), r.stored(15, 9))))
    check(m.r8(p.switches + TRACKLEN_ID) == 1, "and the mod On, since it holds a length")
    check(sd.files["Projects/Old/tracklen.bin"] == original, "its tracklen.bin stays as it was")

    sd.files["Projects/Empty/tracklen.bin"] = side_file([])
    p.new_project()
    r.set_switch(False)
    p.open("Projects", "Empty")
    check(m.r8(p.switches + TRACKLEN_ID) == 0, "a file with no lengths doesn't turn it on")

    damaged = bytearray(side_file([(1, {1: 6})]))
    damaged[12] ^= 0x40
    sd.files["Projects/Bad/tracklen.bin"] = bytes(damaged)
    p.new_project()
    p.open("Projects", "Bad")
    check(r.stored(1) == 0 and m.r8(p.switches + TRACKLEN_ID) == 0, "a damaged file gives none")

    # imported once: turned Off and saved, it stays Off
    p.open("Projects", "Old")
    r.set_switch(False)
    m.call(m.addr("_ZN4mods20projectSwitchChangedEv"))
    p.save()
    p.new_project()
    p.open("Projects", "Old")
    check(m.r8(p.switches + TRACKLEN_ID) == 0, "imported once: turned Off and saved, it stays Off")
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mt, ref = Path(sys.argv[1]), Path(sys.argv[2])
    sections = sys.argv[3:] or ["config", "off", "written", "roll", "conditions", "recording", "grid", "drawing", "button", "files"]
    tests = {"config": lambda: test_config(mt), "off": lambda: test_off(mt, ref), "written": lambda: test_written_out(mt),
             "roll": lambda: test_roll_look_ahead(mt), "conditions": lambda: test_conditions(mt, ref),
             "recording": lambda: test_recording(mt, ref), "grid": lambda: test_grid(mt),
             "drawing": lambda: test_drawing(mt), "button": lambda: test_button(mt), "files": lambda: test_files(mt)}
    for name in sections:
        print(f"-- {name}")
        tests[name]()
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
