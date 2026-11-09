# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Song Loop checks on full Tracker+ builds, with the synth libraries in. The second build is the
same source with MT_NO_MODS, as plain CE. Song Loop is P-N-E-M's design, from their 1.2.0 patch.

The sequencer runs in time, as the audio interrupt runs it: Sequencer::tick, with CE's own
getNextSongPattern and switchNextPatternInSong on mtProject's real song. Pattern loads are recorded,
not done; notes go to a stub. The Song screen's handlers are CE's own static functions, with the
display calls caught.

1. Config: the project's switch and the default; the project item calls the mod's switch action.
2. Song order: Play Selection from the stopped song plays the block over and over, each pattern
   preloaded the one that plays next, also with patterns used more than once in the song (where
   CE's switchRamPatternsNow alone would pick a later slot); Continue Song plays the block out,
   then the song goes on;
   Play Selection while playing waits for the block, or jumps to it at the pattern end when it is
   behind. A Break FX cuts the pattern short and the order holds.
3. Off = CE: the switch Off, and On with no block, play note for note, load for load, as the
   reference build. The reference build has none of the mod.
4. Performance: the loop keeps Performance on at the pattern end; Continue Song turns it off at
   the next one, as CE's Continue Song. With track 1 Bwd the main loop asks for the next pattern
   once per pattern, on track 16's last two rows, with the audio interrupt held off; not with Fwd.
5. Stop: CE's stop ends the loop, from the main loop and from the interrupt's context.
6. The Song screen: Shift + Down / Up select, Up / Down / the jog move the block during the loop,
   Undo, Add, Delete and << Pattern >> are locked with a block and stock without, the softkeys
   greyed through refreshEditMode, Play Selection / Continue Song through functPlayPattern, the
   labels through the mod's update.
7. The status bar: cSongTimer::refresh's icon, plain, armed and looping.
8. Fonts: on a fake display driver, the glyphs and widths of the status font and the two list
   icons uploaded, the old glyphs read back first and written back when the switch goes Off;
   nothing at all with it Off. The list icons sit between CE's last bitmap and the controls.
9. Frames: the block's frame and the grey one, and the display list they add.

The logic on plain data is also in the host tests (core/mods/tests/test_songloop.cpp). The
screen as drawn, the jog and the buttons are for the device.

Usage: python tools/emu/test_songloop.py build/emu/TrackerPlus_MT_full.axf build/emu/TrackerPlus_ref_full.axf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_S0

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import menu_item_named  # noqa: E402
from test_swing import QUIET, f32  # noqa: E402
from test_tracklen import Rig as TracklenRig  # noqa: E402

SONGLOOP_ID = 13
SWITCHES = "_ZN4mods8switchesE"
DEFAULTS = "_ZN4mods8defaultsE"
STATE = "_ZN4mods8songloop5stateE"
CE_SIDE = "_ZN4mods8songloop12_GLOBAL__N_16CeSide"
FIELDS = ("sel", "anchor", "first", "last", "loop", "wrapPending", "snap", "end", "entered", "dimmed",
          "preloaded", "statusDirty")
FM = "newFileManager"
TRACK0, TRACK_STRIDE = 28, 1025
DT = 0.0005
TEMPO = 400.0
STEP_BLOCKS = 312500.0 / TEMPO * 48 / 1e6 / DT
LENGTH = 8                                       # steps per pattern
FX_BREAK = 13
PLAYMODE_BACKWARD = 1
FONT_SIZE = 172                                  # strFont
STATUS_FONT = 3
LIST_ICON, LIST_ICON_ARMED = 140000, 140200
CONTROLS_RAM_START = 150000
GLYPHS_APPLIED = "_ZN4mods8songloop12_GLOBAL__N_113glyphsAppliedE"

failures = 0


def file_locals(m: Machine, source: str) -> dict[str, int]:
    """One source file's local symbols: CE has a static functDown in several files."""
    data = m.elf.data
    shoff, shentsize, shnum = struct.unpack_from("<I", data, 32)[0], *struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    out: dict[str, int] = {}
    for sh in sections:
        if sh[1] != 2:                               # SHT_SYMTAB
            continue
        off, size, link, entsize = sh[4], sh[5], sh[6], sh[9]
        stroff = sections[link][4]
        current = ""
        for j in range(size // entsize):
            st_name, value, _size, info, _other, _shndx = struct.unpack_from("<IIIBBH", data, off + j * entsize)
            name = data[stroff + st_name:data.index(b"\0", stroff + st_name)].decode("latin-1")
            if info & 0xF == 4:                      # STT_FILE
                current = name
            elif current == source and info >> 4 == 0 and info & 0xF == 2:
                out[name] = value
    return out


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


class SongRig:
    """The sequencer playing mtProject's song, and the Song screen's state, on one machine."""

    pattern = TracklenRig.pattern               # the two RAM patterns, as Track Length's rig has them
    step_at = TracklenRig.step_at

    def __init__(self, axf: Path, on: bool = True, slots: int = 8, real_fonts: bool = False):
        self.m = m = Machine(axf)
        self.mods = STATE in m.elf.symbols
        self.now = 1000
        self._fake_timer()
        m.call(m.addr("_GLOBAL__sub_I_sequencer"))
        self.seq = m.addr("sequencer")
        self.patterns = m.addr("seq")
        self.pattern_size = m.sym("seq").size // 2
        m.call(m.addr("_ZN9Sequencer4initEv"), self.seq)
        m.call(m.addr("_GLOBAL__sub_I_songEditor"))
        self.fm = m.addr(FM)
        self.project = m.addr("mtProject")

        self.loads: list[tuple] = []     # (block, pattern, from the main loop)
        self.notes: list[tuple] = []
        self.calls: list[tuple] = []
        self.block = 0
        self.in_main = False
        self.interrupts_off = False
        self.held: set[str] = set()
        self.last_loaded = 1
        keep = ("loadWorkspacePattern", "setSongPos", "loadWorkspacePatternNow")
        for name in QUIET:
            if not any(k + "E" in name for k in keep):
                m.stub_function(m.addr(name), lambda mm: 0)
        m.stub_function(m.addr("_ZN12cFileManager20loadWorkspacePatternEh"), self._load)
        m.stub_function(m.addr("_ZN12cFileManager23loadWorkspacePatternNowEh"), self._load_now)
        m.stub_function(m.addr("_ZN12cFileManager21getLoadedPatternIndexEv"), lambda mm: self.last_loaded)
        m.stub_function(m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"),
                        lambda mm: self.notes.append((self.block, mm.arg(1))))
        m.stub_function(m.addr("_ZN9Sequencer18isTrackEngineMutedEh"), lambda mm: 0)
        m.stub_function(m.addr("_Z21disableAudioInterruptv"), lambda mm: self._interrupts(True))
        m.stub_function(m.addr("_Z20enableAudioInterruptv"), lambda mm: self._interrupts(False))
        m.stub_function(m.addr("_ZN7Buttons15isButtonPressedEi"), lambda mm: 1 if "shift" in self.held else 0)
        m.w32(m.addr("_ZN10ModuleBase7buttonsE"), m.alloc(64))
        # The Song screen's drawing and CE's own stock moves: recorded, not run.
        for name, sym in m.elf.symbols.items():
            if real_fonts and ("Font" in name or "decompress" in name):
                continue                             # CE's own font loading, for the font checks
            if name.startswith("_ZN8cDisplay") and sym.is_func and "getTheme" not in name:
                m.stub_function(sym.addr, lambda mm, n=name: self.calls.append((n, mm.arg(1), mm.arg(2))) and 0)
        for short in ("27changeSongPositionSelectionEs", "29refreshSongPlayerSelectedSlotEv", "8showIconE6icon_th",
                      "8hideIconEv", "15startSongPlayerEv", "20refreshButtonsLabelsEv", "19refreshPatternsListEv"):
            name = "_ZN11cSongEditor" + short
            if name in m.elf.symbols:
                m.stub_function(m.addr(name), lambda mm, n=short: self.calls.append((n, mm.arg(1), mm.arg(2))) and 0)
        TracklenRig._set_tempo(self, TEMPO)
        TracklenRig.clear(self, LENGTH)
        for index in range(2):
            for track in (0, 5, 15):
                TracklenRig.put(self, index, track, 0, 60)
                TracklenRig.put(self, index, track, 4, 62)
        self._find_song()
        self.set_playlist(list(range(1, slots + 1)))
        if self.mods:
            m.call(m.addr("_GLOBAL__sub_I__ZN4mods8songloop5stateE"))
            m.call(m.addr("_ZN4mods8songloop4initEv"))
            self.ce = m.call(m.addr("_ZN4mods8songloop6ceSideEv"))
            self.set_switch(on)
            self._find_editor_fields()
            self.set_editor(focus=1, rec=1, slots=slots, cursor=0)

    # -- what's stubbed -----------------------------------------------------------------
    def _fake_timer(self) -> None:
        m = self.m
        vtable = m.alloc(4 * 16, 4)
        stub = m.add_stub(lambda mm: self.now)
        for i in range(16):
            m.w32(vtable + 4 * i, stub)
        obj = m.alloc(16, 4)
        m.w32(obj, vtable)
        m.w32(m.addr("_ZN13elapsedMillis5timerE"), obj)

    def _load(self, m: Machine) -> int:
        self.loads.append((self.block, m.arg(1), self.in_main, self.interrupts_off))
        self.last_loaded = m.arg(1)
        return 1

    def _load_now(self, m: Machine) -> int:
        self.calls.append(("loadNow", m.arg(1), 0))
        self.last_loaded = m.arg(1)
        return 1

    def _interrupts(self, off: bool) -> int:
        self.interrupts_off = off
        return 0

    # -- CE's fields, found through CE's own functions -----------------------------------
    def _find_song(self) -> None:
        m = self.m
        before = m.read(self.project, m.sym("mtProject").size)
        m.call(m.addr("_ZN12cFileManager10setSongPosEh"), self.fm, 0xA7)
        after = m.read(self.project, len(before))
        moved = [i for i in range(len(before)) if before[i] != after[i]]
        check(len(moved) == 1, "the rig finds mtProject.song.playlistPos", str(moved))
        self.pos_at = self.project + moved[0]
        self.playlist_at = self.pos_at - 255
        m.write(self.pos_at, b"\0")
        m.write(self.playlist_at + 7, b"\x2B")
        got = m.call(m.addr("_ZN12cFileManager14getSongPatternEh"), self.fm, 7) & 0xFF
        check(got == 0x2B, "the rig finds mtProject.song.playlist")

    def _ce(self, method: str, *args: int) -> int:
        return self.m.call(self.m.addr(CE_SIDE + method), self.ce, *args)

    def _find_editor_fields(self) -> None:
        m = self.m
        se = m.sym("songEditor")
        saved = m.read(se.addr, se.size)

        def find(method: str, width: int, value: int) -> int:
            hits = []
            for offset in range(0, se.size - width + 1):
                m.write(se.addr, b"\0" * se.size)
                m.write(se.addr + offset, value.to_bytes(width, "little"))
                if self._ce(method) & ((1 << (8 * width)) - 1) == value:
                    hits.append(offset)
            check(len(hits) == 1, f"the rig finds songEditor's field for CeSide::{method}", str(hits))
            return se.addr + hits[0]
        self.f_focus = find("5focusEv", 1, 1)
        self.f_slots = find("5slotsEv", 2, 0x0102)
        self.f_cursor = find("6cursorEv", 1, 0x5A)
        self.f_label = find("10labelStateEv", 1, 0x5A)
        m.write(se.addr, b"\0" * se.size)
        self.f_rec = None
        for offset in range(se.size):
            m.write(se.addr + offset, b"\x01")
            if self._ce("3recEv") & 0xFF:
                self.f_rec = se.addr + offset
                break
            m.write(se.addr + offset, b"\0")
        check(self.f_rec is not None, "the rig finds songEditor.editMode")
        m.write(se.addr, saved)

    # -- settings -------------------------------------------------------------------------
    def set_switch(self, on: bool) -> None:
        self.m.write(self.m.addr(SWITCHES) + SONGLOOP_ID, bytes([1 if on else 0]))

    def set_playlist(self, slots: list[int]) -> None:
        self.m.write(self.playlist_at, bytes(slots) + b"\0" * (255 - len(slots)))
        self.slots = len(slots)

    def set_editor(self, focus=None, rec=None, slots=None, cursor=None) -> None:
        m = self.m
        if focus is not None:
            m.write(self.f_focus, bytes([focus]))
        if rec is not None:
            m.write(self.f_rec, bytes([rec]))
        if slots is not None:
            m.write(self.f_slots, struct.pack("<H", slots))
        if cursor is not None:
            m.write(self.f_cursor, bytes([cursor]))

    def cursor(self) -> int:
        return self.m.r8(self.f_cursor)

    def state(self) -> dict:
        raw = self.m.read(self.m.addr(STATE), len(FIELDS))
        return dict(zip(FIELDS, raw))

    def set_state(self, **kw) -> None:
        self.m.call(self.m.addr("_ZN4mods8songloop5resetEv"))
        for k, v in kw.items():
            self.m.write(self.m.addr(STATE) + FIELDS.index(k), bytes([v]))

    def pos(self) -> int:
        return self.m.r8(self.pos_at)

    def performance(self) -> bool:
        return self._ce("11performanceEv") & 0xFF != 0

    # -- the Song screen ------------------------------------------------------------------
    def handler(self, name: str, *args: int) -> None:
        if not hasattr(self, "song_locals"):
            self.song_locals = file_locals(self.m, "fSongEdit.cpp")
        self.m.call(self.song_locals[name], *args, max_insns=2_000_000)

    def up(self) -> None:
        self.handler("_ZL7functUpv")

    def down(self) -> None:
        self.handler("_ZL9functDownv")

    def jog(self, value: int) -> None:
        self.handler("_ZL12functEncoders", value & 0xFFFFFFFF)   # an int16_t, extended by the caller

    def play_pattern(self) -> None:
        self.handler("_ZL16functPlayPatternv")

    def stock_moves(self) -> list[int]:
        return [struct.unpack("<h", struct.pack("<H", c[1] & 0xFFFF))[0] for c in self.calls
                if c[0] == "27changeSongPositionSelectionEs"]

    # -- playing --------------------------------------------------------------------------
    def play_song(self, from_pos: int) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer20playSongFromPositionEh"), self.seq, from_pos)

    def stop(self) -> None:
        self.m.call(self.m.addr("_ZN9Sequencer4stopEv"), self.seq)

    def run_blocks(self, count: int, main_tick: bool = False) -> list[int]:
        """Blocks of the interrupt's tick; the play positions, each once as it starts."""
        positions = []
        tick_fn = self.m.addr("_ZN4mods8songloop4tickERNS0_2CeE") if main_tick else 0
        for _ in range(int(count)):
            self.m.uc.reg_write(UC_ARM_REG_S0, f32(DT))
            self.m.call(self.m.addr("_ZN9Sequencer4tickEf"), self.seq, max_insns=20_000_000)
            self.block += 1
            if tick_fn:
                self.in_main = True
                self.m.call(tick_fn, self.ce)
                self.in_main = False
            p = self.pos()
            if not positions or positions[-1] != p:
                positions.append(p)
        return positions

    def run_patterns(self, count: int, **kw) -> list[int]:
        return self.run_blocks(count * LENGTH * STEP_BLOCKS, **kw)


def ones(positions: list[int]) -> list[int]:
    return [p + 1 for p in positions]


def preloaded(r: SongRig, since: int = 0) -> list[int]:
    return [p for _, p, main, _ in r.loads[since:] if not main]


# -- 1 --------------------------------------------------------------------------------------
def test_config(mt: Path) -> None:
    m = Machine(mt)
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    item = menu_item_named(m, "menuMods", "Song Loop")
    check(m.r32(m.r32(item + 16)) == m.addr(DEFAULTS) + SONGLOOP_ID,
          "Config -> Mods -> Song Loop sets the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Song Loop")
    setup = m.r32(item + 16)
    check(m.r32(setup) == m.addr(SWITCHES) + SONGLOOP_ID, "Project Settings -> Mods -> Song Loop is the project's switch")
    action = m.addr("_ZN4mods28projectSongloopSwitchChangedEv")
    words = [m.r32(setup + 4 * i) & ~1 for i in range(1, 5)]
    check(action & ~1 in words, "the project item calls projectSongloopSwitchChanged (the loop reset, the glyphs)")
    for group in ("menuMods", "menuProjectMods"):
        a = m.addr(group)
        names = [m.cstr(m.r32(m.r32(m.r32(a + 12) + 4 * i) + 4)) for i in range(m.r8(a + 8))]
        mods = [n for n in names if n in ("Color Song", "Copy to New", "Global Swing", "Humanize", "Master FX",
                                          "Song Loop", "Track Length")]
        check("Song Loop" in mods and mods == sorted(mods), f"{group}: the mods stay A-Z", str(names))


# -- 2 --------------------------------------------------------------------------------------
def select_block(r: SongRig, first: int, last: int) -> None:
    """Shift + Down from `first` in the pattern column, as on the unit."""
    r.set_editor(focus=1, rec=1, cursor=first)
    r.held = {"shift"}
    for _ in range(last - first):
        r.down()
    r.held = set()


def test_song_order(mt: Path) -> None:
    r = SongRig(mt)
    select_block(r, 2, 4)
    st = r.state()
    check((st["sel"], st["first"], st["last"], r.cursor()) == (1, 2, 4, 4), "Shift + Down x2 from 3: block 3-5", str(st))
    r.play_pattern()
    st = r.state()
    check(st["loop"] == 1 and r.pos() == 2 and ("loadNow", 3, 0) in r.calls and r.cursor() == 4,
          "Play Selection, stopped: the song from position 3 (pattern 3), loop on, the cursor back on 5")
    order = r.run_patterns(10)
    check(ones(order) == [3, 4, 5, 3, 4, 5, 3, 4, 5, 3, 4], "the block over and over", str(ones(order)))
    loads = preloaded(r)
    check(loads[:9] == [4, 5, 3, 4, 5, 3, 4, 5, 3], "each preload is the pattern that plays next", str(loads))

    r.play_pattern()                                # Continue Song
    st = r.state()
    check(st["loop"] == 0 and st["sel"] == 0 and r.cursor() == r.pos(), "Continue Song: loop and block off, the cursor on the play position")
    before = len(r.loads)
    tail = r.run_patterns(5)
    check(ones(tail) == [4, 5, 6, 7, 8, 1], "Continue Song during 4: the block plays out, then the song to its end and round",
          str(ones(tail)))
    check(preloaded(r, before) == [5, 6, 7, 8, 1], "and the preloads follow the song", str(preloaded(r, before)))

    # Patterns used more than once: CE's switchRamPatternsNow looks for the loaded pattern from the
    # play position on, so only the mod's pattern end keeps the loop on the block's own slots.
    r = SongRig(mt)
    r.set_playlist([1, 2, 1, 2, 3, 4, 1, 2])
    select_block(r, 2, 3)
    r.play_pattern()
    order = r.run_patterns(7)
    check(ones(order) == [3, 4, 3, 4, 3, 4, 3, 4], "patterns 1 2 in the song three times: the block 3-4 itself repeats",
          str(ones(order)))
    check(preloaded(r)[:6] == [2, 1, 2, 1, 2, 1], "the preloads", str(preloaded(r)))

    # Play Selection while playing, the block ahead: the song plays on up to it, then repeats it.
    r = SongRig(mt)
    r.play_song(0)
    select_block(r, 4, 5)
    r.play_pattern()
    st = r.state()
    check(st["loop"] == 1 and st["entered"] == 0, "Play Selection while playing, block ahead: armed")
    order = r.run_patterns(8)
    check(ones(order) == [1, 2, 3, 4, 5, 6, 5, 6, 5], "plays on to 5-6, then repeats it", str(ones(order)))

    # The block behind the play position: to its start at the pattern end.
    r = SongRig(mt)
    r.play_song(6)
    select_block(r, 1, 2)
    r.play_pattern()
    st = r.state()
    check(st["loop"] == 1 and st["entered"] == 1, "Play Selection while playing, block behind: looping at once")
    order = r.run_patterns(5)
    check(ones(order) == [7, 2, 3, 2, 3, 2], "to the block's start at the pattern end", str(ones(order)))

    # Break: the pattern ends early; the order holds.
    r = SongRig(mt)
    for index in range(2):
        TracklenRig.put(r, index, 3, 2, 60, fx=(FX_BREAK, 0))
    select_block(r, 2, 4)
    r.play_pattern()
    order = r.run_patterns(8)
    check(ones(order)[:9] == [3, 4, 5, 3, 4, 5, 3, 4, 5] and len(order) > 10,
          "Break FX: shorter patterns, the same block order", str(ones(order)))
    loads = preloaded(r)
    check(all(a == b for a, b in zip(loads, [4, 5, 3] * 8)), "Break FX: the preloads too", str(loads[:12]))

    # The block moved during the loop: the next preload goes to the new block.
    r = SongRig(mt)
    select_block(r, 1, 2)
    r.play_pattern()
    r.run_patterns(1.5)
    r.set_editor(cursor=r.state()["end"])
    for _ in range(3):
        r.down()                                    # the whole block, without Shift
    st = r.state()
    check((st["first"], st["last"]) == (4, 5), "Down x3 during the loop: the block moves to 5-6")
    order = r.run_patterns(6)
    check(ones(order)[-4:] in ([5, 6, 5, 6], [6, 5, 6, 5]), "the loop follows the block", str(ones(order)))


# -- 3 --------------------------------------------------------------------------------------
def take(axf: Path, on: bool, breaks: bool, block: bool = False, playlist: list[int] | None = None) -> tuple:
    r = SongRig(axf, on=on)
    if playlist:
        r.set_playlist(playlist)
    if breaks:
        for index in range(2):
            TracklenRig.put(r, index, 3, 5, 60, fx=(FX_BREAK, 0))
    if block and r.mods:
        select_block(r, 2, 4)                        # a block, not played
    r.play_song(1)
    order = r.run_patterns(12)
    r.stop()
    return order, r.notes, [(b, p) for b, p, _, _ in r.loads], r.calls.count(("loadNow", 2, 0))


def test_off(mt: Path, ref: Path) -> None:
    for breaks, playlist in ((False, None), (True, None), (False, [1, 2, 1, 2, 3, 4, 1, 2])):
        want = take(ref, False, breaks, playlist=playlist)
        what = (" with Break" if breaks else "") + (" with patterns used twice" if playlist else "")
        check(len(want[0]) > 10 and len(want[1]) > 30, f"reference{what}: the song plays", str(want[0]))
        check(take(mt, False, breaks, block=True, playlist=playlist) == want,
              f"Off, with a block selected{what}: note for note, load for load as plain CE")
        check(take(mt, True, breaks, playlist=playlist) == want, f"On, no block{what}: as plain CE")
        check(take(mt, True, breaks, block=True, playlist=playlist) == want, f"On, a block not played{what}: as plain CE")

    m = Machine(ref)
    left = [n for n in m.elf.symbols if "songloop" in n.lower() or "Songloop" in n]
    check(left == [], "the reference build has none of the mod", str(left[:5]))

    r = SongRig(mt, on=False)
    r.held = {"shift"}
    r.set_editor(cursor=3)
    r.down()
    r.up()
    r.held = set()
    r.jog(2)
    check(r.stock_moves() == [1, -1, 2] and r.state()["sel"] == 0, "Off: Shift + Up/Down and the jog are CE's", str(r.stock_moves()))
    r.set_state(sel=1, first=2, last=4, end=4)      # as a stale state would be
    before = len(r.calls)
    r.play_pattern()
    check(r.state()["loop"] == 0 and ("loadNow", 2, 0) not in r.calls[before:], "Off: softkey 0 is CE's")


# -- 4 --------------------------------------------------------------------------------------
def test_performance(mt: Path) -> None:
    m_turn_off = "_ZN16cPerformanceMode21turnOffAtEndOfPatternEv"
    for loop, want in ((True, True), (False, False)):
        r = SongRig(mt)
        r.play_song(2)
        r.m.call(r.m.addr("_ZN9Sequencer20enterPerformanceModeEv"), r.seq)
        if loop:
            r.set_state(sel=1, anchor=2, first=2, last=4, end=4, loop=1, entered=1)
        r.m.call(r.m.addr(m_turn_off), r.m.addr("performanceMode"))  # CE's Continue Song, Performance part
        r.run_patterns(1.2)
        check(r.performance() == want,
              f"pattern end with Performance off scheduled, loop {'on' if loop else 'off'}: Performance "
              f"{'stays on' if want else 'off, as CE'}")

    r = SongRig(mt)
    select_block(r, 2, 4)
    r.play_pattern()
    r.m.call(r.m.addr("_ZN9Sequencer20enterPerformanceModeEv"), r.seq)
    r.run_patterns(2.3)
    check(r.performance(), "the loop keeps Performance on over pattern ends")
    r.play_pattern()                                 # Continue Song
    check(r.state()["loop"] == 0 and r.performance(), "Continue Song: Performance on to the pattern end")
    r.run_patterns(1.0)
    check(not r.performance(), "and off at the pattern end, as CE's Continue Song")

    def bwd(mode: int, perform: bool = True) -> SongRig:
        r = SongRig(mt)
        r.play_song(0)
        if perform:
            r.m.call(r.m.addr("_ZN9Sequencer20enterPerformanceModeEv"), r.seq)
            r.m.call(r.m.addr("_ZN9Sequencer22setPerformancePlayModeEha"), r.seq, 0, mode)
        r.run_patterns(4, main_tick=True)
        return r
    r = bwd(PLAYMODE_BACKWARD)
    own = [(b, p, off) for b, p, main, off in r.loads if main]
    check(len(own) in (3, 4) and all(off for _, _, off in own), "track 1 Bwd: one own request per pattern, the interrupt held off",
          str(own))
    pattern_blocks = LENGTH * STEP_BLOCKS
    late = [b % pattern_blocks for b, _, _ in own]
    check(all(pattern_blocks - 2.2 * STEP_BLOCKS <= x < pattern_blocks for x in late),
          "on track 16's last two rows", str([round(x / STEP_BLOCKS, 2) for x in late]))
    check([p for _, p, _ in own][:3] == [2, 3, 4], "each the song's next pattern", str(own))
    check(not r.interrupts_off, "the audio interrupt on again after")
    for what, kw in (("Fwd", dict(mode=0)), ("no Performance", dict(mode=PLAYMODE_BACKWARD, perform=False))):
        r = bwd(**kw)
        check(not [x for x in r.loads if x[2]], f"{what}: no own request")


# -- 5 --------------------------------------------------------------------------------------
def test_stop(mt: Path) -> None:
    r = SongRig(mt)
    select_block(r, 2, 4)
    r.play_pattern()
    r.run_patterns(0.5)
    r.stop()
    st = r.state()
    check(st["loop"] == 0 and st["sel"] == 1, "Stop: the loop ends, the block stays")
    r = SongRig(mt)
    select_block(r, 2, 4)
    r.play_pattern()
    r.m.call(r.m.addr("_ZN9Sequencer4stopENS_11StopContextE"), r.seq, 1)
    check(r.state()["loop"] == 0, "Stop from the interrupt's context (MIDI Stop): the loop ends")
    order = []
    r.play_song(2)
    order = r.run_patterns(4)
    check(ones(order) == [3, 4, 5, 6, 7], "then Play Song plays the song on", str(ones(order)))


# -- 6 --------------------------------------------------------------------------------------
def editor_reads(r: SongRig, call) -> set[int]:
    se = r.m.sym("songEditor")
    seen: set[int] = set()

    def hook(uc, access, address, size, value, user):
        for a in range(address, address + size):
            seen.add(a - se.addr)
    h = r.m.uc.hook_add(UC_HOOK_MEM_READ, hook, begin=se.addr, end=se.addr + se.size - 1)
    call()
    r.m.uc.hook_del(h)
    return seen


def test_screen(mt: Path) -> None:
    r = SongRig(mt, slots=10)
    r.set_editor(cursor=4)
    r.held = {"shift"}
    for _ in range(3):
        r.down()
    st = r.state()
    check((st["sel"], st["first"], st["last"], r.cursor()) == (1, 4, 7, 7), "Shift + Down x3 from 5: block 5-8")
    r.up()
    check((r.state()["last"], r.cursor()) == (6, 6), "Shift + Up: block 5-7, the cursor on the moved edge")
    for _ in range(4):
        r.up()
    st = r.state()
    check((st["first"], st["last"], st["sel"]) == (4, 4, 0), "Shift + Up x4: never above the anchor")
    r.down()
    r.down()
    r.held = set()
    r.calls.clear()
    r.down()
    check(r.state()["sel"] == 0 and r.stock_moves() == [1], "Down without Shift: the block goes, CE moves the cursor")
    r.set_editor(rec=0)
    r.held = {"shift"}
    r.calls.clear()
    r.down()
    check(r.state()["sel"] == 0 and r.stock_moves() == [1], "Rec off: Shift + Down is CE's")
    r.set_editor(rec=1, focus=0)
    r.calls.clear()
    r.down()
    check(r.state()["sel"] == 0 and r.stock_moves() == [], "the grid: not the pattern column's")

    se = r.m.sym("songEditor")
    allowed = set(range(r.f_slots - se.addr, r.f_slots - se.addr + 2))
    for name in ("_ZL9functUndov", "_ZL12functAddSlotv", "_ZL15functDeleteSlotv"):
        r.set_state(sel=1, first=2, last=4, end=4)
        reads = editor_reads(r, lambda: r.handler(name))
        locked = reads <= allowed
        r.set_state()
        reads = editor_reads(r, lambda: r.handler(name))
        check(locked and not reads <= allowed, f"{name}: locked with a block, CE's without")
    change = "_ZN11cSongEditor29changeSelectedPlayListPatternEi"
    for kw, what in ((dict(sel=1, first=2, last=4, end=4), "a block"), (dict(loop=1), "the loop")):
        r.set_state(**kw)
        reads = editor_reads(r, lambda: r.m.call(r.m.addr(change), se.addr, 1))
        check(reads <= allowed, f"<< Pattern >>: locked with {what}")
    r.set_state()
    reads = editor_reads(r, lambda: r.m.call(r.m.addr(change), se.addr, 1))
    check(not reads <= allowed, "<< Pattern >>: CE's without a block")
    r.held = {"shift"}
    r.set_editor(focus=1, rec=1, cursor=3)
    playlist = r.m.read(r.playlist_at, 16)
    r.set_state(sel=1, first=2, last=4, end=4)
    r.jog(1)
    locked = r.m.read(r.playlist_at, 16) == playlist
    r.set_state()
    r.jog(1)
    check(locked and r.m.read(r.playlist_at, 16) != playlist, "Shift + jog: the pattern locked with a block, CE's change without")
    r.held = set()

    # refreshEditMode: the four softkeys greyed while locked
    def colors() -> list[int]:
        r.calls.clear()
        r.m.call(r.m.addr("_ZN11cSongEditor15refreshEditModeEv"), se.addr)
        return [c[2] for c in r.calls if "setControlColors" in c[0]]
    r.set_editor(rec=0)
    grey = colors()
    r.set_editor(rec=1)
    r.set_state()
    lit = colors()
    r.set_state(sel=1, first=2, last=4, end=4)
    locked = colors()
    check(len(grey) == 4 and len(set(grey)) == 1 and grey != lit and locked == grey,
          "refreshEditMode: the softkeys greyed with a block, as with Rec off", f"{grey} {lit} {locked}")

    # during the loop: Up / Down / jog move the whole block
    r = SongRig(mt, slots=10)
    r.set_state(sel=1, anchor=2, first=2, last=4, loop=1, end=4, entered=1)
    r.set_editor(cursor=4)
    moves = []
    for how, d in (("down", 0), ("up", 0), ("jog", 9), ("down", 0), ("jog", -20)):
        if how == "jog":
            r.jog(d)
        else:
            getattr(r, how)()
        st = r.state()
        moves.append((st["first"] + 1, st["last"] + 1, r.cursor() + 1))
    check(r.stock_moves() == [] and moves == [(4, 6, 6), (3, 5, 5), (8, 10, 10), (8, 10, 10), (1, 3, 3)],
          "during the loop, Up / Down / jog move the whole block within the song", str(moves))
    r.set_state()
    r.calls.clear()
    r.jog(2)
    check(r.stock_moves() == [2], "the jog without a loop: CE's cursor move")

    # labels, from the Song screen's update
    def labels() -> list[str]:
        r.calls.clear()
        r.now += 200
        try:
            r.m.call(r.m.addr("_ZN11cSongEditor6updateEv"), se.addr, max_insns=5_000_000)
        except RuntimeError:
            pass
        return [r.m.cstr(c[2]) for c in r.calls
                if c[0] in ("_ZN8cDisplay14setControlTextEaPKc", "_ZN8cDisplay15setControlText2EaPKc")][:4]
    r = SongRig(mt)
    r.set_state(sel=1, anchor=2, first=2, last=4, end=4)
    got = labels()
    check(got == ["Play", "Selection", "Play", "Song"], "update, stopped with a block: Play Selection / Play Song", str(got))
    stock = [c for c in r.calls if c[0] == "20refreshButtonsLabelsEv"]
    check(stock == [], "and not CE's labels")
    r.play_pattern()
    got = labels()
    check(r.m.r8(r.f_label) == 0x21, "update, looping: Continue Song", str(got))
    r.set_state()
    r.m.write(r.f_label, b"\0")
    labels()
    check([c for c in r.calls if c[0] == "20refreshButtonsLabelsEv"] != [], "no block: CE's labels")


# -- 7 --------------------------------------------------------------------------------------
def test_status(mt: Path) -> None:
    r = SongRig(mt)
    seen = []
    r.m.stub_function(r.m.addr("sprintf"), lambda mm: seen.append(mm.cstr(mm.arg(4))) or 0)
    timer = r.m.addr("songTimer")

    def icon() -> str:
        r.m.call(r.m.addr("_ZN10cSongTimer7refreshEv.part.0"), timer)
        return seen[-1] if seen else ""
    r.m.write(r.m.addr(GLYPHS_APPLIED), b"\x01")    # as after the fonts' load
    r.play_song(0)
    plain = icon()
    r.set_state(sel=1, anchor=5, first=5, last=7, end=7, loop=1)
    armed = icon()
    r.run_patterns(5)
    looping = icon()
    r.set_switch(False)                              # the loop's state left as it was
    r.run_patterns(0.3)                              # the step moves on: the bar redraws
    off = icon()
    check((plain, armed, looping, off) == ("\x1a\x1b", "{}", "`^", "\x1a\x1b"),
          "the status bar: the play icon, armed {}, looping `^, Off the play icon", repr((plain, armed, looping, off)))
    r.set_switch(True)
    r.m.write(r.m.addr(GLYPHS_APPLIED), b"\x00")    # a font the glyphs didn't fit
    r.run_patterns(0.3)
    check(icon() == "\x1a\x1b", "the glyphs not in the font: CE's play icon, not ` ^ as text")


# -- 8 --------------------------------------------------------------------------------------
class FakeDriver:
    def __init__(self, m: Machine):
        self.m = m
        self.log: list[tuple] = []
        real = m.addr("_ZTV17DisplayDriverImpl") + 8
        names = {s.addr & ~1: n for n, s in m.elf.symbols.items() if n.startswith("_ZN17DisplayDriverImpl")}
        vtable = m.alloc(4 * 32, 4)
        self.slots = []
        for i in range(21):
            target = m.r32(real + 4 * i) & ~1
            name = names.get(target, "?")
            self.slots.append(name)
            m.w32(vtable + 4 * i, m.add_stub(lambda mm, n=name: self._call(n)))
        self.obj = m.alloc(16, 4)
        m.w32(self.obj, vtable)

    def _call(self, name: str) -> int:
        m = self.m
        if "RAMG" in name:
            data, size, addr = m.arg(1), m.arg(2), m.arg(3)
            if "read" in name:
                m.write(data, bytes((addr + i) & 0xFF for i in range(size)))
                self.log.append(("read", addr, size, b""))
            else:
                self.log.append(("write", addr, size, m.read(data, size)))
        return 0


def font_rig(mt: Path, on: bool, real_fonts: bool = False) -> tuple:
    r = SongRig(mt, on=on, real_fonts=real_fonts)
    m = r.m
    if real_fonts:                                   # loadFont's display list: not needed here
        for name, sym in m.elf.symbols.items():
            if name.startswith("_Z") and len(name.split("API_")) == 2 and sym.is_func and "API_LIB" not in name:
                m.stub_function(sym.addr, lambda mm: 0)
    d = FakeDriver(m)
    display = m.sym("display")
    for o in range(0, display.size, 4):
        m.w32(display.addr + o, d.obj)               # wherever cDisplay keeps its driver
    f = m.addr("fonts") + STATUS_FONT * FONT_SIZE
    widths = bytearray([0] * 0x18 + [16] * (128 - 0x18))
    widths[0x1A], widths[0x1B] = 21, 22
    m.write(f, bytes(widths))
    m.w32(f + 156, 0x10000)                           # address
    m.w32(f + 160, 0x10000 + 148)                     # source
    m.write(f + 164, struct.pack("<HHBxH", 20, 24, 2, 10))   # width, height, format, linestride
    return r, d, f


def test_fonts(mt: Path) -> None:
    glyph = 10 * 24
    source = 0x10000 + 148

    def at(c: int) -> int:
        return source + (c - 0x18) * glyph
    r, d, f = font_rig(mt, on=False)
    r.m.call(r.m.addr("_ZN4mods8songloop11fontsLoadedEv"))
    r.m.call(r.m.addr("_ZN4mods8songloop5applyEv"))
    check(d.log == [], "Off: the fonts and RAM_G untouched")

    r, d, f = font_rig(mt, on=True)
    r.m.call(r.m.addr("_ZN4mods8songloop11fontsLoadedEv"))
    reads = sorted(a for k, a, _, _ in d.log if k == "read")
    writes = {a: data for k, a, _, data in d.log if k == "write"}
    w = r.m.read(f, 128)
    check(reads == sorted(at(c) for c in (0x60, 0x5E, 0x7B, 0x7D)), "On: the four glyphs read back first", str(reads))
    check(all(at(c) in writes for c in (0x60, 0x5E, 0x7B, 0x7D)) and 0x10000 + 0x5C in writes and 0x10000 + 0x78 in writes,
          "the glyphs and the widths uploaded")
    check((w[0x60], w[0x5E], w[0x7B], w[0x7D]) == (21, 22, 21, 22), "the widths are the play icon's")
    check(any(writes[at(0x60)]) and any(writes[at(0x5E)]) and any(writes[at(0x7B)]) and not any(writes[at(0x7D)]),
          "|: :| in ` ^, |: in { (} empty)")
    icon, armed = writes.get(LIST_ICON, b""), writes.get(LIST_ICON_ARMED, b"")
    check(len(icon) == 200 and len(armed) == 200 and any(icon) and icon != armed, "the two list icons uploaded")
    d.log.clear()
    r.m.call(r.m.addr("_ZN4mods8songloop5applyEv"))
    check(d.log == [], "applied once: not again on the next Workspace load")

    d.log.clear()
    r.set_switch(False)
    r.m.call(r.m.addr("_ZN4mods28projectSongloopSwitchChangedEv"))
    back = {a: data for k, a, _, data in d.log if k == "write"}
    w = r.m.read(f, 128)
    check(all(back.get(at(c)) == bytes((at(c) + i) & 0xFF for i in range(glyph)) for c in (0x60, 0x5E, 0x7B, 0x7D)),
          "switched Off: CE's own glyphs written back")
    check((w[0x60], w[0x5E], w[0x7B], w[0x7D]) == (16, 16, 16, 16) and 0x10000 + 0x5C in back, "and CE's widths")

    # CE's own two fonts (Config -> General -> Display font), through cDisplay::setFont and loadFont
    for index, name in ((0, "Original"), (1, "New")):
        r, d, f = font_rig(mt, on=True, real_fonts=True)
        r.m.call(r.m.addr("_ZN8cDisplay7setFontEh"), r.m.addr("display"), index, max_insns=200_000_000)
        ready = r.m.call(r.m.addr("_ZN4mods8songloop11glyphsReadyEv")) & 0xFF
        w = r.m.read(f, 128)
        writes = {a for k, a, _, _ in d.log if k == "write"}
        width, height, stride = struct.unpack("<HH", r.m.read(f + 164, 4)) + struct.unpack("<H", r.m.read(f + 170, 2))
        print(f"   {name}: status font {width} px wide, {height} high, {stride} bytes a row")
        check(ready and LIST_ICON in writes and (w[0x60], w[0x5E]) == (w[0x1A], w[0x1B]),
              f"the {name} font loaded: the glyphs and the list icons in again")

    m = r.m
    bmp = m.sym("bitmaps")
    end = max(m.r32(bmp.addr + 20 * i + 4) + m.r32(bmp.addr + 20 * i + 8) for i in range(bmp.size // 20))
    check(end <= LIST_ICON and LIST_ICON_ARMED + 200 <= CONTROLS_RAM_START,
          f"the list icons in RAM_G after CE's last bitmap ({end}) and before the controls ({CONTROLS_RAM_START})")


# -- 9 --------------------------------------------------------------------------------------
def test_frames(mt: Path) -> None:
    r = SongRig(mt, slots=20)
    m = r.m
    se = m.sym("songEditor")
    commands: list[tuple] = []
    for name, sym in m.elf.symbols.items():
        base = name.split("API_")
        if name.startswith("_Z") and len(base) == 2 and sym.is_func and "API_LIB" not in name:
            m.stub_function(sym.addr, lambda mm, n=base[1]: commands.append((n, mm.arg(0), mm.arg(1))) and 0)
    # songEditor.patternsList.linesCount, found by what the frame does with it
    lines_at = None
    for offset in range(0, se.size - 1, 2):
        if se.addr + offset in (r.f_focus, r.f_slots, r.f_cursor, r.f_rec) or se.addr + offset + 1 in (r.f_focus, r.f_rec, r.f_cursor):
            continue
        saved = m.read(se.addr + offset, 2)
        m.write(se.addr + offset, struct.pack("<H", 1))
        r.set_state(sel=1, first=0, last=3, end=3)
        r.set_editor(focus=1, slots=20)
        out = m.alloc(8, 4)
        m.w32(out, 0)
        m.w32(out + 4, 0)
        m.call(m.addr("_ZN4mods8songloop10drawFramesEiiiiiiRiS1_"), 0, 0, 0, 0, 0, 100, out, out + 4)
        m.write(se.addr + offset, saved)
        if m.r32(out + 4) == 27:
            lines_at = se.addr + offset
            break
    check(lines_at is not None, "the rig finds songEditor.patternsList.linesCount")
    if lines_at is None:
        return
    m.write(lines_at, struct.pack("<H", 12))

    def frames(focus: int, **kw) -> tuple:
        r.set_state(**kw)
        r.set_editor(focus=focus, cursor=5)
        out = m.alloc(8, 4)
        y = 91 + 3 * 27
        m.w32(out, y)
        m.w32(out + 4, y + 27)
        commands.clear()
        m.call(m.addr("_ZN4mods8songloop10drawFramesEiiiiiiRiS1_"), 91, 2, 3, 0, 10, 200, out, out + 4)
        return m.r32(out), m.r32(out + 4), list(commands)
    top, bottom, cmds = frames(1, sel=1, anchor=5, first=5, last=8, end=8)
    check((top, bottom, cmds) == (91 + 3 * 27, 91 + 7 * 27, []), "the cursor's frame over the block 6-9, nothing drawn by the mod",
          str((top, bottom, cmds)))
    top, bottom, cmds = frames(1)
    check((top, bottom) == (91 + 3 * 27, 91 + 4 * 27), "no block: CE's frame")
    top, bottom, cmds = frames(0, sel=1, anchor=5, first=5, last=8, end=8, loop=1, entered=1)
    names = [c[0] for c in cmds]
    check((top, bottom) == (91 + 3 * 27, 91 + 4 * 27) and len(cmds) == 9 and names[0].startswith("COLOR")
          and any(n.startswith("LINE_WIDTH") for n in names), "the grid, looping: the grey frame, 9 commands", str(names))
    print(f"   the grey frame adds {4 * len(cmds)} bytes of display list, only with the cursor in the grid during a loop")
    top, bottom, cmds = frames(0, sel=1, anchor=5, first=5, last=8, end=8)
    check(cmds == [], "the grid, no loop: nothing drawn")

    # The list icon: the same six commands as CE's play icon, at the same place.
    r.play_song(1)
    r.set_state(sel=1, anchor=1, first=1, last=2, end=2, loop=1, entered=1)
    commands.clear()
    check(m.call(m.addr("_ZN4mods8songloop12drawListIconEii"), 30, 200) & 0xFF == 0 and commands == [],
          "the icons not in RAM_G: CE's play icon")
    m.write(m.addr(GLYPHS_APPLIED), b"\x01")
    drew = m.call(m.addr("_ZN4mods8songloop12drawListIconEii"), 30, 200) & 0xFF
    names = [c[0] for c in commands]
    check(drew and len(names) == 6 and names[0].startswith("BITMAP_SOURCE") and commands[0][1] == LIST_ICON,
          "the list icon, looping: |: :| in six commands, as CE's icon", str(names))
    r.set_state(sel=1, anchor=5, first=5, last=6, end=6, loop=1)
    commands.clear()
    m.call(m.addr("_ZN4mods8songloop12drawListIconEii"), 30, 200)
    check(commands and commands[0][1] == LIST_ICON_ARMED, "armed: |:")
    r.set_state()
    commands.clear()
    check(m.call(m.addr("_ZN4mods8songloop12drawListIconEii"), 30, 200) & 0xFF == 0 and commands == [], "no loop: CE's icon")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mt, ref = Path(sys.argv[1]), Path(sys.argv[2])
    tests = {"config": lambda: test_config(mt), "order": lambda: test_song_order(mt), "off": lambda: test_off(mt, ref),
             "performance": lambda: test_performance(mt), "stop": lambda: test_stop(mt),
             "screen": lambda: test_screen(mt), "status": lambda: test_status(mt), "fonts": lambda: test_fonts(mt),
             "frames": lambda: test_frames(mt)}
    for name in sys.argv[3:] or list(tests):
        print(f"-- {name}")
        tests[name]()
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
