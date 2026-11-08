# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Copy to New checks on a full Tracker+ build, with the synth libraries in. The second build is the
same source with MT_NO_MODS, as plain CE.

1. Button: not there with the switch Off; with it On, its place among Humanize and Trk Len in every
   combination, its two lines, and not bound outside edit mode. Page 3's label text against page 2's.
2. Config: the switch in Config -> Mods and in Project Settings -> Mods.
3. The copy, stopped: through CE's own file manager on a fake card. The file is what CE's own pattern
   writer writes for the RAM pattern, unsaved edits included; CE's reader reads it back the same; the
   Song-screen block is set, the project marked changed, the target's own changed flag left clear, and
   the RAM patterns untouched. Nothing on the press; one pattern file read per update.
4. The search: each reason to skip a pattern, an empty file and a missing one, the song, none left.
5. Failures: switched pattern, the interrupt switching patterns inside the snapshot, a write error,
   another project opened, the switch turned Off. A write while the RAM patterns change (as MIDI
   Start in Song mode would load one) still writes the snapshot.
6. Playing: pattern mode and song mode, with the sequencer ticking between updates.
7. Track lengths: On, the target gets the source's, and they reach mods.bin; Off, mods.bin is the same.
8. Color Song is told about the target.
9. Off = CE: the switch Off writes nothing; the reference build has none of the mod.

The rules on plain data and the job's timing (the busy limit, the playing gap, performance mode) are
in the host tests (core/mods/tests/test_copypattern.cpp). The popup as drawn, the card's real timing
and two card operations at once are for the device.

Usage: python tools/emu/test_copypattern.py build/emu/TrackerPlus_MT_full.axf build/emu/TrackerPlus_ref_full.axf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import menu_item_named  # noqa: E402
from test_swing import Projects  # noqa: E402
from test_tracklen import (PLAIN, STEP_SIZE, TRACK0, TRACK_STRIDE, TRACKS, Rig, Screen,  # noqa: E402
                           SongPaste)
from unicorn import UC_HOOK_CODE  # noqa: E402

COPYPATTERN_ID = 9
HUMANIZE_ID = 1
TRACKLEN_ID = 12
COLORSONG_ID = 0
TRACKLEN_RECORD = TRACKLEN_ID + 1
MENU_LIST_TEXT_ACTION = 4
NOTE_OFF = -4

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


def path(pattern: int) -> str:
    return f"Workspace/patterns/pattern_{pattern:02d}.mtp"


class CopyRig:
    """The Pattern screen's Copy to New on CE's file manager and sequencer, with a fake card."""

    def __init__(self, axf: Path):
        self.r = r = Rig(axf)
        self.p = p = Projects(r)
        self.s = SongPaste(r, p)
        self.m = m = r.m
        self.sd = p.sd
        self.fm = m.addr("newFileManager")
        m.call(m.addr("_GLOBAL__sub_I__ZN4mods11copypattern12formatCopiedEPcjh"))
        self.switches = m.addr("_ZN4mods8switchesE")
        self.job = m.addr("_ZN4mods11copypattern12_GLOBAL__N_13jobE")
        self.press_fn = m.addr("_ZN4mods18copypatternPressedEv")
        self.update_fn = m.addr("_ZN4mods17copypatternUpdateEv")
        self.messages: list[str] = []
        self.changed = 0
        self.now = 1000
        m.stub_function(m.addr("_ZN15InterfacePopups4showEhPKch"),
                        lambda mm: self.messages.append(mm.cstr(mm.arg(2))))
        # The time: Tracker::getInstance()->getPlatform()->getTimer()->millis(), each a fake object
        # whose every virtual returns the next one, so it holds wherever the compiler inlined the call.
        timer = self._fake_object(lambda mm: self.now)
        platform = self._fake_object(lambda mm: timer)
        tracker = self._fake_object(lambda mm: platform)
        m.stub_function(m.addr("_ZN7Tracker11getInstanceEv"), lambda mm: tracker)
        m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), self._changed)
        self.scratch = m.alloc(r.pattern_size, 8)
        self._find_fields()
        self.new_project()

    def _fake_object(self, fn) -> int:
        m = self.m
        vtable = m.alloc(4 * 32, 4)
        stub = m.add_stub(fn)
        for i in range(32):
            m.w32(vtable + 4 * i, stub)
        obj = m.alloc(16, 4)
        m.w32(obj, vtable)
        return obj

    def _changed(self, m: Machine) -> None:
        self.changed += 1

    def _find_fields(self) -> None:
        """Where CE keeps what the mod reads: found through CE's own functions, not by layout."""
        m, r = self.m, self.r
        seq = m.sym("sequencer")
        words = [o for o in range(0, seq.size, 4) if m.r32(seq.addr + o) == r.pattern(0)]
        check(len(words) == 1, "the rig finds the sequencer's RAM pattern pointer", str(words))
        self.current_at = seq.addr + words[0]
        others = [o for o in range(0, seq.size, 4) if m.r32(seq.addr + o) == r.pattern(1)]
        self.buffer_at = seq.addr + others[0]

        # the Song-screen blocks: what CE's updatePatternBitmask writes
        project = m.sym("mtProject")
        self.make_pattern(self.scratch, {(0, 0): (60, 0)}, 16)
        before = m.read(project.addr, project.size)
        m.call(m.addr("_ZN12cFileManager20updatePatternBitmaskEhPN9Sequencer10strPatternE"), self.fm, 200, self.scratch)
        after = m.read(project.addr, project.size)
        moved = [i for i in range(project.size) if before[i] != after[i]]
        check(len(moved) == 1, "the rig finds the Song-screen blocks", str(moved))
        self.blocks = project.addr + moved[0] - 199
        m.write(project.addr + moved[0], b"\0")
        self.make_pattern(self.scratch, {(8, 0): (60, 48)}, 16)
        before = m.read(project.addr, project.size)
        m.call(m.addr("_ZN12cFileManager20updatePatternBitmaskEhPN9Sequencer10strPatternE"), self.fm, 200, self.scratch)
        after = m.read(project.addr, project.size)
        moved = [i for i in range(project.size) if before[i] != after[i]]
        self.blocks_midi = project.addr + moved[0] - 199
        m.write(project.addr + moved[0], b"\0")

        # the song: the one other place where a pattern number makes the mod's own RAM check say used
        ce = m.call(m.addr("_ZN4mods11copypattern6ceSideEv"))
        used = m.addr("_ZN4mods11copypattern12_GLOBAL__N_16CeSide9usedInRamEh")
        self.playlist = None
        for offset in range(project.size):
            a = project.addr + offset
            if a in (self.blocks + 199, self.blocks_midi + 199):
                continue
            b = m.read(a, 1)
            m.write(a, bytes([200]))
            hit = m.call(used, ce, 200) & 0xFF
            m.write(a, b)
            if hit:
                self.playlist = a
                break
        check(self.playlist is not None, "the rig finds the song's playlist")

    # -- set-up ---------------------------------------------------------------------------------
    def new_project(self) -> None:
        self.p.new_project()
        self.sd.files = {k: v for k, v in self.sd.files.items() if not k.startswith("Workspace/patterns/")}
        self.m.write(self.blocks, bytes(255))
        self.m.write(self.blocks_midi, bytes(255))
        self.m.write(self.playlist, bytes([1]) + bytes(254))
        self.set_switch(COPYPATTERN_ID, True)
        self.set_switch(TRACKLEN_ID, True)
        self.r.set_pattern(1)
        self.messages.clear()

    def set_switch(self, mod: int, on: bool) -> None:
        self.m.write(self.switches + mod, bytes([1 if on else 0]))

    def make_pattern(self, at: int, steps: dict, length: int) -> None:
        """A cleared pattern with `steps`: (track, step) -> (note, instrument) or the 8 raw bytes."""
        m = self.m
        m.call(m.addr("_ZN9Sequencer12clearPatternEPNS_10strPatternE"), self.r.seq, at)
        for t in range(TRACKS):
            m.write(at + TRACK0 + t * TRACK_STRIDE, bytes([length - 1]))
        for (t, s), v in steps.items():
            raw = v if isinstance(v, bytes) else struct.pack("<bBBBBBbb", v[0], v[1], 0, 0, 0, 0, -1, -1)
            m.write(at + TRACK0 + t * TRACK_STRIDE + 1 + s * STEP_SIZE, raw)

    def write_file(self, pattern: int, steps: dict, length: int = 16, block: bool = False) -> None:
        """A pattern file through CE's own writer, and its Song-screen block if `block`."""
        m = self.m
        self.make_pattern(self.scratch, steps, length)
        ok = m.call(self.s.write_fn, self.fm, m.put_str(path(pattern)), self.scratch, max_insns=30_000_000)
        check(ok & 0xFF == 1, f"pattern {pattern} written")
        if block:
            m.call(m.addr("_ZN12cFileManager20updatePatternBitmaskEhPN9Sequencer10strPatternE"), self.fm, pattern,
                   self.scratch)

    def current(self) -> int:
        return self.m.r32(self.current_at)

    def ram_edit(self, steps: dict, length: int = 64) -> None:
        self.make_pattern(self.current(), steps, length)

    # -- the mod --------------------------------------------------------------------------------
    def press(self) -> None:
        self.m.call(self.press_fn)

    def running(self) -> bool:
        return self.m.r8(self.job) != 0

    def update(self) -> None:
        self.m.call(self.update_fn, max_insns=50_000_000)

    def run(self, limit: int = 600) -> int:
        """Updates until the job ends."""
        n = 0
        while self.running() and n < limit:
            self.update()
            n += 1
        return n

    def expected(self) -> bytes:
        """What CE's own writer writes for the RAM pattern."""
        m = self.m
        m.call(self.s.write_fn, self.fm, m.put_str("Expected/pattern.mtp"), self.current(), max_insns=30_000_000)
        return self.sd.files.pop("Expected/pattern.mtp")

    def read_back(self, pattern: int) -> bytes:
        m = self.m
        ok = m.call(m.addr("_ZN12cFileManager15readPatternFileEPKcPN9Sequencer10strPatternE"), self.fm,
                    m.put_str(path(pattern)), self.scratch, max_insns=30_000_000)
        return m.read(self.scratch, self.r.pattern_size) if ok & 0xFF else b""

    def pattern_changed_flag(self, pattern: int) -> bool:
        return self.m.call(self.m.addr("_ZN12cFileManager22isPatternStructChangedEh"), self.fm, pattern) & 0xFF != 0

    def file_reads(self) -> int:
        return sum(1 for c in self.sd.calls if "readFile" in c)

    def writefile_slot(self) -> int:
        m = self.m
        impl_vt = m.addr("_ZTV14FileSystemImpl") + 8
        target = m.addr("_ZN14FileSystemImpl9writeFileEPKvjPv") & ~1
        slots = m.sym("_ZTV14FileSystemImpl").size // 4 - 2
        return next(i for i in range(slots) if m.r32(impl_vt + 4 * i) & ~1 == target)


# ---- 1, 2: the button and Config -----------------------------------------------------------

def test_button(mt: Path) -> None:
    c = CopyRig(mt)
    m = c.m
    get = m.addr("_ZN4mods19patternEditorButtonEj")

    def names() -> list[str]:
        out = []
        for i in range(5):
            b = m.call(get, i)
            if b == 0:
                break
            out.append(m.cstr(m.r32(b + 4)))
        return out

    for mod in (HUMANIZE_ID, TRACKLEN_ID, COPYPATTERN_ID):
        c.set_switch(mod, False)
    check(names() == [], "all Off: no buttons", str(names()))
    c.set_switch(COPYPATTERN_ID, True)
    for humanize in (False, True):
        for tracklen in (False, True):
            c.set_switch(HUMANIZE_ID, humanize)
            c.set_switch(TRACKLEN_ID, tracklen)
            want = (["Humanize"] if humanize else []) + ["Copy to"] + (["Trk Len"] if tracklen else [])
            check(names() == want, f"Humanize {humanize}, Trk Len {tracklen}: {want}", str(names()))
    c.set_switch(COPYPATTERN_ID, False)
    check(names() == ["Humanize", "Trk Len"], "Copy to New Off: the others close up", str(names()))
    c.set_switch(COPYPATTERN_ID, True)
    c.set_switch(HUMANIZE_ID, False)
    c.set_switch(TRACKLEN_ID, False)

    button = m.call(get, 0)
    check(m.r32(button + 8) == m.addr("_ZL14functCopyToNewv") and m.r32(button + 12) == 0,
          "a press button, not a held one")
    check(m.cstr(m.call(m.r32(button + 16))) == "New", "its second line: New")

    s = Screen(c.r)
    page3 = m.addr("_ZN14cPatternEditor25setActionButtonPage3FunctEv")
    s.binds.clear()
    m.call(page3, s.editor)
    check(("press", 2) not in s.binds, "not bound outside edit mode, like Humanize", str(s.binds))
    # edit mode: the one byte of the editor that binds it
    size = m.sym("patternEditor").size
    found = []
    for offset in range(size):
        before = m.read(s.editor + offset, 1)
        if before != b"\0":
            continue
        m.write(s.editor + offset, b"\x01")
        s.binds.clear()
        try:
            m.call(page3, s.editor)
        except RuntimeError:
            pass
        m.write(s.editor + offset, before)
        if ("press", 2) in s.binds:
            found.append(offset)
    check(len(found) == 1, "bound in edit mode, through one flag", str(found))

    # the labels: page 3 with all three mods, against page 2, which CE already draws
    texts: list[str] = []
    m.stub_function(m.addr("_ZN8cDisplay14setControlTextEaPKc"), lambda mm: texts.append(mm.cstr(mm.arg(2))))
    for mod in (HUMANIZE_ID, TRACKLEN_ID, COPYPATTERN_ID):
        c.set_switch(mod, True)

    def page(name: str) -> int:
        texts.clear()
        s.text2.clear()
        m.call(m.addr(name), s.editor, max_insns=5_000_000)
        return sum(len(t) for t in texts) + sum(len(t) for _, t in s.text2)

    page2 = page("_ZN14cPatternEditor27showActionButtonPage2LabelsEv")
    page3 = page("_ZN14cPatternEditor27showActionButtonPage3LabelsEv")
    check("Copy to" in texts and any(t == "New" for _, t in s.text2), "page 3 shows Copy to / New", str(texts))
    check(0 < page3 < page2, "page 3's label text is less than page 2's", f"page 3 {page3}, page 2 {page2}")


def test_config(mt: Path) -> None:
    c = CopyRig(mt)
    m = c.m
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    item = menu_item_named(m, "menuMods", "Copy to New")
    check(m.r32(item + 12) == MENU_LIST_TEXT_ACTION and m.r32(m.r32(item + 16)) == m.addr("_ZN4mods8defaultsE") + COPYPATTERN_ID,
          "Config -> Mods: the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Copy to New")
    check(m.r32(item + 12) == MENU_LIST_TEXT_ACTION and m.r32(m.r32(item + 16)) == c.switches + COPYPATTERN_ID,
          "Project Settings -> Mods: the project's switch")
    mods = m.addr("menuMods")
    order = [m.cstr(m.r32(m.r32(m.r32(mods + 12) + 4 * i) + 4)) for i in range(1, m.r8(mods + 8))]
    check(order == sorted(order), "Config -> Mods stays A-Z", str(order))


# ---- 3, 4: the copy and the search ----------------------------------------------------------

def test_copy(mt: Path) -> None:
    c = CopyRig(mt)
    m, sd = c.m, c.sd
    c.write_file(1, {(0, 0): (40, 1)}, 16, block=True)          # the old saved version of pattern 1
    c.ram_edit({(0, 0): (60, 1), (3, 100): (62, 2), (9, 5): (70, 48), (15, 127): (NOTE_OFF, 0)}, 64)
    for t, length in ((1, 31), (2, 127), (14, 0)):
        m.write(c.current() + TRACK0 + t * TRACK_STRIDE, bytes([length]))
    ram_before = m.read(c.r.pattern(0), 2 * c.r.pattern_size)
    files_before = dict(sd.files)
    reads = c.file_reads()

    c.press()
    check(c.running() and sd.files == files_before and c.file_reads() == reads and not c.messages,
          "the press touches no files")
    c.changed = 0
    c.update()
    check(not c.running() and c.messages == ["Copied to pattern 2"], "pattern 2 has no file: copied", str(c.messages))
    got = sd.files.get(path(2), b"")
    want = c.expected()
    check(got == want and len(got) == c.r.pattern_size, "the file is CE's own writer's, unsaved edits included",
          f"{len(got)} B")
    check(got != sd.files[path(1)], "not the saved file")
    check(c.read_back(2) == m.read(c.current(), c.r.pattern_size), "CE's reader reads it back as the RAM pattern")
    lengths = [c.read_back(2)[TRACK0 + t * TRACK_STRIDE] for t in (0, 1, 2, 14)]
    check(lengths == [63, 31, 127, 0], "every track's length kept (64, 32, 128, 1)", str(lengths))
    check(m.r8(c.blocks + 1) != 0 and m.r8(c.blocks_midi + 1) != 0, "the Song-screen block is set")
    check(c.changed >= 1, "the project is marked changed", str(c.changed))
    check(not c.pattern_changed_flag(2), "the target's own changed flag stays clear")
    check(c.r.pattern_number() == 1, "you stay on pattern 1")
    ram_now = m.read(c.r.pattern(0), 2 * c.r.pattern_size)
    # CE's writer sets the header and CRC fields of the pattern it's given, as its own save does
    check(ram_now[TRACK0:c.r.pattern_size] == ram_before[TRACK0:c.r.pattern_size]
          and ram_now[c.r.pattern_size:] == ram_before[c.r.pattern_size:], "both RAM patterns' steps untouched")

    # the next copy skips pattern 2, now used
    c.press()
    c.run()
    check(c.messages[-1] == "Copied to pattern 3", "the next copy: pattern 3", str(c.messages))
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


def raw_step(note=-1, instrument=0, fx1=0, fx2=0, volume=-1, micromove=-1, value=0) -> bytes:
    return struct.pack("<bBBBBBbb", note, instrument, fx1, value, fx2, value, volume, micromove)


def test_search(mt: Path) -> None:
    c = CopyRig(mt)
    m, sd = c.m, c.sd
    c.ram_edit({(0, 0): (60, 1)}, 16)
    c.r.set_pattern(10)
    m.write(c.blocks + 10, b"\x01")                                  # 11: a block
    m.write(c.playlist + 1, bytes([12]))                             # 12: in the song
    c.write_file(13, {(0, 60): (60, 1)}, 16)                         # a note past the end
    c.write_file(14, {(5, 3): raw_step(micromove=20)})               # only an m
    c.write_file(15, {(5, 3): raw_step(volume=0)})                   # only a V
    c.write_file(16, {(5, 3): raw_step(note=NOTE_OFF)})              # a lone note-off
    c.write_file(17, {(5, 3): raw_step(instrument=4)})               # an instrument, no note
    c.write_file(18, {(5, 3): raw_step(fx2=7)})                      # an FX type
    sd.files[path(19)] = b"short"                                    # unreadable
    c.write_file(20, {(5, 3): raw_step(value=50)}, 32)               # FX values only: empty
    reads = []
    before_files = dict(sd.files)
    c.press()
    n = 0
    while c.running() and n < 50:
        before = c.file_reads()
        c.update()
        reads.append(c.file_reads() - before)
        n += 1
    check(c.messages == ["Copied to pattern 20"], "the first empty pattern: 20, an empty file", str(c.messages))
    check(max(reads) <= 1, "at most one pattern file read per update", str(reads))
    check(len(reads) == 8, "one update per file checked, none for 11 and 12", str(reads))
    check(all(sd.files[path(p)] == before_files[path(p)] for p in range(13, 20)), "the files it skipped are left as they were")

    # a missing file is a target too, and the search wraps: 255 has a block, 1 is in the song
    c.new_project()
    c.r.set_pattern(254)
    c.ram_edit({(0, 0): (60, 1)}, 16)
    m.write(c.blocks + 254, b"\x01")
    c.press()
    c.run()
    check(c.messages == ["Copied to pattern 2"], "from 254: past 255 and 1 to 2, which has no file", str(c.messages))

    # none left
    c.new_project()
    c.r.set_pattern(7)
    m.write(c.blocks, bytes([1]) * 255)
    files = dict(sd.files)
    c.press()
    c.run()
    check(c.messages == ["No empty pattern left"] and sd.files == files, "none left: nothing written", str(c.messages))


# ---- 5: failures -----------------------------------------------------------------------------

def test_failures(mt: Path) -> None:
    c = CopyRig(mt)
    m, sd = c.m, c.sd
    c.ram_edit({(0, 0): (60, 1)}, 16)
    c.write_file(2, {(0, 0): (60, 1)})

    # you switch pattern between the press and the copy
    c.press()
    c.update()                                    # pattern 2: a file with a note
    c.r.set_pattern(5)
    c.update()
    check(c.messages == ["Pattern changed - try again"] and path(3) not in sd.files, "switched before the copy",
          str(c.messages))
    c.r.set_pattern(1)

    # the interrupt switches the RAM patterns inside the snapshot
    def switch(uc, address, size, user):
        a, b = m.r32(c.current_at), m.r32(c.buffer_at)
        m.w32(c.current_at, b)
        m.w32(c.buffer_at, a)
        c.r.set_pattern(4)
    h = m.uc.hook_add(UC_HOOK_CODE, switch, begin=m.addr("_ZN9Sequencer24copyCurrentPatternToBuffEPh") & ~1,
                      end=m.addr("_ZN9Sequencer24copyCurrentPatternToBuffEPh") & ~1)
    c.messages.clear()
    c.press()
    c.run()
    m.uc.hook_del(h)
    check(c.messages == ["Pattern changed - try again"] and path(3) not in sd.files,
          "a switch during the snapshot: nothing written", str(c.messages))
    m.w32(c.current_at, c.r.pattern(0))
    m.w32(c.buffer_at, c.r.pattern(1))
    c.r.set_pattern(1)

    # the RAM patterns change during the write: the file is still the snapshot
    want = c.expected()
    slot = c.writefile_slot()
    original = m.r32(sd.vtable + 4 * slot)

    def scribble(uc, address, size, user):
        for i in range(2):
            m.write(c.r.pattern(i) + TRACK0 + 1, raw_step(note=99, instrument=9))
    h = m.uc.hook_add(UC_HOOK_CODE, scribble, begin=original & ~1, end=original & ~1)
    c.messages.clear()
    c.press()
    c.run()
    m.uc.hook_del(h)
    check(c.messages == ["Copied to pattern 3"] and sd.files.get(path(3)) == want,
          "RAM patterns changed during the write: the file is the snapshot", str(c.messages))
    c.ram_edit({(0, 0): (60, 1)}, 16)

    # a write error: the file is removed again, nothing else changes
    m.w32(sd.vtable + 4 * slot, m.add_stub(lambda mm: 0))
    c.changed = 0
    c.messages.clear()
    c.r.set_length(0, 5, 16, 1)
    c.press()
    c.run()
    m.w32(sd.vtable + 4 * slot, original)
    check(c.messages == ["Copy failed - SD card"] and path(4) not in sd.files, "a write error: no file left",
          str(c.messages))
    check(m.r8(c.blocks + 3) == 0 and c.r.stored(0, 4) == 0, "and no block, no lengths")

    # another project, or the switch Off: dropped without a message
    c.write_file(4, {(0, 0): (60, 1)})
    c.messages.clear()
    c.press()
    c.update()
    c.p.open("Projects", "Other")
    check(not c.running() and not c.messages, "another project opened: dropped")
    c.set_switch(COPYPATTERN_ID, True)
    c.r.set_pattern(1)
    c.press()
    c.update()
    c.set_switch(COPYPATTERN_ID, False)
    c.update()
    check(not c.running() and not c.messages, "switched Off: dropped")
    files = dict(sd.files)
    c.press()
    c.run()
    check(not c.running() and sd.files == files and not c.messages, "Off: the press does nothing")
    check(not sd.unexpected and not sd.handles, "only expected file calls, every file closed",
          f"{sd.unexpected} {sd.handles}")


# ---- 6: playing --------------------------------------------------------------------------------

def test_playing(mt: Path) -> None:
    c = CopyRig(mt)
    r, m, sd = c.r, c.m, c.sd
    c.ram_edit({(0, 0): (60, 1), (0, 8): (62, 1)}, 16)
    c.write_file(2, {(0, 0): (60, 1)})
    c.write_file(3, {(0, 0): (60, 1)})

    # pattern mode: the sequencer ticks between updates
    r.begin()
    r.play()
    c.press()
    n = 0
    while c.running() and n < 100:
        r.run_blocks(PLAIN)
        c.now += 50
        c.update()
        n += 1
    check(c.messages == ["Copied to pattern 4"], "playing: copied", str(c.messages))
    check(sd.files.get(path(4)) == c.expected(), "the file is the RAM pattern's")
    check(r.pattern_number() == 1, "still on pattern 1")
    r.stop()

    # song mode: the song moves on between the press and the copy
    m.write(c.playlist, bytes([1, 9]))
    r.next_pattern = [9, 1]
    r.begin()
    m.call(m.addr("_ZN9Sequencer20playSongFromPositionEh"), r.seq, 0)
    c.messages.clear()
    c.press()
    c.update()                        # one file checked: 2, used
    r.run_blocks(17 * PLAIN)          # past the pattern's end: the next pattern
    c.update()
    check(r.pattern_number() == 9 and c.messages == ["Pattern changed - try again"],
          "song mode moved on: Pattern changed", f"{r.pattern_number()} {c.messages}")
    c.messages.clear()
    c.press()
    n = 0
    while c.running() and n < 100:
        r.run_blocks(PLAIN)
        c.now += 50
        c.update()
        n += 1
    check(r.pattern_number() == 9 and c.messages == ["Copied to pattern 10"] and sd.files.get(path(10)) == c.expected(),
          "song mode, pressed again on pattern 9: copied to 10, the next one", f"{r.pattern_number()} {c.messages}")
    r.stop()


# ---- 7, 8: track lengths and Color Song -----------------------------------------------------------

def test_lengths(mt: Path) -> None:
    c = CopyRig(mt)
    r, m, sd = c.r, c.m, c.sd
    c.ram_edit({(0, 0): (60, 1)}, 16)
    r.set_length(0, 5, 16, 1)
    r.set_length(1, 7, 16, 1)
    r.set_length(3, 4, 16, 2)                     # the target's own, from before
    m.call(m.addr("_ZN4mods21projectTracklenEditedEv"))
    c.press()
    c.run()
    check([r.stored(t, 2) for t in range(4)] == [5, 7, 0, 0], "On: the target gets the source's lengths, in place of its own",
          str([r.stored(t, 2) for t in range(4)]))
    c.p.save()
    data = sd.files.get("Workspace/mods.bin", b"")
    check(bytes([2, 5, 7] + [0] * 14) in data, "and they reach Workspace/mods.bin", data.hex())

    c.set_switch(TRACKLEN_ID, False)
    m.call(m.addr("_ZN4mods20projectSwitchChangedEv"))
    c.p.save()
    before = sd.files.get("Workspace/mods.bin")
    c.press()
    c.run()
    c.p.save()
    check(c.messages[-1] == "Copied to pattern 3" and r.stored(0, 3) == 0, "Off: no lengths copied")
    check(sd.files.get("Workspace/mods.bin") == before, "and mods.bin is the same")

    # Color Song hears about the target
    told: list[int] = []
    m.stub_function(m.addr("_ZN4mods9colorsong14patternChangedEh"), lambda mm: told.append(mm.arg(0) & 0xFF))
    c.set_switch(COLORSONG_ID, True)
    c.press()
    c.run()
    check(told == [4], "Color Song: told about the target", str(told))


# ---- 9: Off = CE ---------------------------------------------------------------------------------

def test_reference(ref: Path) -> None:
    m = Machine(ref)
    names = [n for n in m.elf.symbols if "4mods11copypattern" in n or "mods18copypattern" in n
             or "mods17copypattern" in n or "CopyToNew" in n]
    check(names == [], "the reference build has none of the mod", str(names[:5]))


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mt, ref = Path(sys.argv[1]), Path(sys.argv[2])
    sections = sys.argv[3:] or ["button", "config", "copy", "search", "failures", "playing", "lengths", "reference"]
    tests = {"button": lambda: test_button(mt), "config": lambda: test_config(mt), "copy": lambda: test_copy(mt),
             "search": lambda: test_search(mt), "failures": lambda: test_failures(mt),
             "playing": lambda: test_playing(mt), "lengths": lambda: test_lengths(mt),
             "reference": lambda: test_reference(ref)}
    for name in sections:
        print(f"-- {name}")
        tests[name]()
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
