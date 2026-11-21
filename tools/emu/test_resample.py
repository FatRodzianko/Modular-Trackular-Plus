# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Resample checks on a full Tracker+ build, with the synth libraries in. The second build, if
given, is the same source with MT_NO_MODS, as plain CE.

Steps 1 and 2 of docs/mt/RESAMPLE_PORT_PLAN.md:

1. Config: Resample in Config -> Mods (the default for new projects) and in Project
   Settings -> Mods (the project's switch).
2. Projects, through the hooks CE's file manager calls: a 1.2.0 project's resample.bin is
   imported once, its record 12 goes out with Save and Save As and comes back with Open,
   and New Project starts with both buttons Off.
3. The Sample Recorder's setup screen: CE's real cSampleRecorder, started as the interface starts
   it, with its display calls caught (cDisplay::setControlText and the API_* functions) and its
   keys pressed through what it registered. With the mod Off, the same display calls as plain CE
   for every source. On: Trk Audio and Trk Record for "Line In", Trk Audio alone for the mono Line
   Ins and the mics, nothing for Radio and USB; presses toggle with the coupling, store record 12
   and mark the project changed; the switch takes effect at the next visit; the screen's display
   bytes stay under kFrameControlBytes.

The record's rules, the importer's refusals, the record through mods.bin and the buttons' rules are
also in the host tests (core/mods/tests/test_resample.cpp). What doesn't run here: the screen
itself, the real keys' timing, a count-in or a take (steps 3 to 6).

Usage: python tools/emu/test_resample.py build/emu/TrackerPlus_MT_resample_step2.axf [plain_CE.axf]
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import after_hooked_store  # noqa: E402
from sound_rig import SoundRig  # noqa: E402
from test_masterfx_rig import LIMIT, MasterPage, last  # noqa: E402
from test_step0 import menu_item_named, store_image  # noqa: E402
from test_swing import Projects, Rig  # noqa: E402
from unicorn import UC_HOOK_MEM_WRITE  # noqa: E402

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


# -- the Sample Recorder's setup screen ----------------------------------------------------------
# cSampleRecorder::sourceType and ptrSourcesNamesLabels on the Tracker+ (CE_20261119 on)
SOURCES = ("Line In (L)", "Line In (R)", "Line In (M)", "Line In", "Radio", "Mic LG", "Mic HG",
           "USB (M)", "USB")
LINE_IN, RADIO, MIC_LG, USB = 3, 4, 5, 8
TRK_AUDIO_ONLY = (0, 1, 2, 5, 6)           # the mono Line Ins and the mics
KEY_TRK_AUDIO, KEY_TRK_RECORD = 1, 2       # interfaceButton1, interfaceButton2


def expected(source: int, audio: str = "Off", record: str = "Off") -> tuple[str, str, str, str]:
    """label[1] and label[2] with Resample On: title, value, title, value."""
    if source == LINE_IN:
        return "Trk Audio", audio, "Trk Record", record
    if source in TRK_AUDIO_ONLY:
        return "Trk Audio", audio, "", ""
    if source == RADIO:
        return "Radio Freq", "(CE's frequency)", "", ""
    return "", "", "", ""


class RecorderPage(MasterPage):
    """CE's cSampleRecorder on the sound rig, its setup screen started as the interface starts it."""
    PAGE = "sampleRecorder"
    INIT = "_ZN15cSampleRecorder19initDisplayControlsEv"
    RIG = SoundRig

    def __init__(self, axf: Path):
        super().__init__(axf)
        r, m = self.r, self.m
        self.changed = 0
        self.label0 = None
        self.mods = "_ZN4mods8switchesE" in r.syms

        def project_changed(mm):
            self.changed += 1
            return 0
        r.stub(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), project_changed)
        if self.mods:
            m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))   # the project store
            m.call(m.addr("_ZN4mods4initEP10FileSystem"), r.sd.obj, max_insns=50_000_000)
            m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"), max_insns=50_000_000)
            self.settings = m.addr("_ZN4mods8resample8settingsE")
        # start() takes the codec and the radio from the platform; setDefaultSource needs them
        m.call(m.addr("_ZN15cSampleRecorder5startEm"), self.page, 0, max_insns=200_000_000)
        self.source_at = self._source_offset()

    def _source_offset(self) -> int:
        """recorderConfig.source: the first byte setDefaultSource writes, with sourceTypeLineInS."""
        m = self.m
        seen = []

        def on_write(uc, access, address, sz, value, user):
            after_hooked_store(uc)
            if not seen and sz == 1 and value == LINE_IN:
                seen.append(address - self.page)
        h = m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=self.page, end=self.page + 0x8000)
        m.call(m.addr("_ZN15cSampleRecorder16setDefaultSourceEv"), self.page, max_insns=50_000_000)
        m.uc.hook_del(h)
        self.take_log()
        return seen[0]

    def switch(self, on: bool) -> None:
        self.m.write(self.m.addr("_ZN4mods8switchesE") + RESAMPLE_ID, bytes([1 if on else 0]))

    def visit(self, source: int) -> list:
        """Leave the recorder and come back with this source, as the interface does: stop, start(0)."""
        m = self.m
        m.call(m.addr("_ZN15cSampleRecorder4stopEv"), self.page, max_insns=50_000_000)
        m.write(self.page + self.source_at, bytes([source]))
        self.take_log()
        self.buttons.clear()
        m.call(m.addr("_ZN15cSampleRecorder5startEm"), self.page, 0, max_insns=200_000_000)
        log = self.take_log()
        self.label0 = next(h for h, t in last(log, "text").items() if t == "Source")
        return log

    def key(self, button: int) -> list:
        mode, fn = self.buttons[button]
        if mode == 1:
            self.m.call(fn, max_insns=30_000_000)
        else:
            self.m.call(fn, 1, max_insns=30_000_000)
        return self.take_log()

    def columns(self, log: list) -> tuple[str, str, str, str]:
        """label[1] and label[2] as this log left them: title, value, title, value."""
        text, text2 = last(log, "text"), last(log, "text2")
        a, b = self.label0 + 1, self.label0 + 2
        return text.get(a) or "", text2.get(a) or "", text.get(b) or "", text2.get(b) or ""

    def state(self) -> tuple[int, int]:
        return self.m.r8(self.settings), self.m.r8(self.settings + 1)

    def record(self) -> bytes | None:
        """Record 12 in the project's store, through Store::find."""
        m = self.m
        length = m.alloc(4, 4)
        m.w32(length, 0)
        data = m.call(m.addr("_ZNK4mods5Store4findEhPhPt"), m.addr("_ZN4mods12_GLOBAL__N_112projectStoreE"), RECORD_ID, 0,
                      length)
        return bytes(m.read(data, m.r32(length) & 0xFFFF)) if data else None


def test_recorder(mods_axf: Path, ref_axf: Path | None) -> None:
    page = RecorderPage(mods_axf)
    ref = RecorderPage(ref_axf) if ref_axf else None

    # Off: the setup screen as plain CE's, for every source
    page.switch(False)
    for s, name in enumerate(SOURCES):
        log = page.visit(s)
        shown = page.columns(log)
        check(shown[0] in ("", "Radio Freq") and shown[2] == "", f"Off, {name}: no Resample button", str(shown))
        if ref is not None:
            check(log == ref.visit(s), f"Off, {name}: the same display calls as plain CE")
    page.visit(LINE_IN)
    before = page.state(), page.changed
    log = page.key(KEY_TRK_AUDIO) + page.key(KEY_TRK_RECORD)
    check((page.state(), page.changed) == before and not [e for e in log if e[0] in ("text", "text2")],
          "Off: the keys under columns 2 and 3 do what CE's do", f"{page.state()} {page.changed}")

    # On: which source shows which button, each Off
    page.switch(True)
    for s, name in enumerate(SOURCES):
        shown = page.columns(page.visit(s))
        if s == RADIO:                                  # its second line is the tuned frequency
            shown = (shown[0], "(CE's frequency)" if shown[1].endswith("MHz") else shown[1], *shown[2:])
        check(shown == expected(s), f"On, {name}: {shown[0] or '-'} / {shown[2] or '-'}", str(shown))

    # presses on Line In, with the coupling
    page.visit(LINE_IN)
    changed = page.changed
    shown = page.columns(page.key(KEY_TRK_AUDIO))
    check(shown == expected(LINE_IN, "On", "Off") and page.state() == (1, 0), "Trk Audio pressed: On",
          f"{shown} {page.state()}")
    check(page.record() == b"\x01" and page.changed == changed + 1,
          "the press stores record 12 and marks the project changed", f"{page.record()} {page.changed}")
    shown = page.columns(page.key(KEY_TRK_RECORD))
    check(shown == expected(LINE_IN, "On", "On") and page.state() == (1, 1) and page.record() == b"\x03",
          "Trk Record pressed: both On", f"{shown} {page.state()}")
    shown = page.columns(page.key(KEY_TRK_AUDIO))
    check(shown == expected(LINE_IN, "Off", "Off") and page.state() == (0, 0) and page.record() == b"\x00",
          "Trk Audio pressed again: both Off", f"{shown} {page.state()}")
    shown = page.columns(page.key(KEY_TRK_RECORD))
    check(shown == expected(LINE_IN, "On", "On") and page.state() == (1, 1),
          "Trk Record pressed from Off: Trk Audio comes On with it", f"{shown} {page.state()}")
    check(page.changed == changed + 4, "every press marks the project changed", f"{page.changed - changed}")

    # the settings come back with the next visit; the keys without a button are CE's
    shown = page.columns(page.visit(MIC_LG))
    check(shown == expected(MIC_LG, "On"), "Mic LG: Trk Audio On, as stored", str(shown))
    page.key(KEY_TRK_RECORD)
    check(page.state() == (1, 1), "Mic LG: the key under column 3 leaves the settings alone", str(page.state()))
    page.visit(USB)
    page.key(KEY_TRK_AUDIO)
    check(page.state() == (1, 1), "USB: the key under column 2 leaves the settings alone", str(page.state()))
    page.visit(RADIO)
    page.key(KEY_TRK_AUDIO)
    page.key(KEY_TRK_RECORD)
    check(page.state() == (1, 1), "Radio: its keys stay the radio's", str(page.state()))

    # the switch is latched per visit
    page.visit(LINE_IN)
    page.switch(False)
    page.key(KEY_TRK_AUDIO)
    check(page.state() == (0, 0), "switched Off during the visit: the buttons work until it ends",
          str(page.state()))
    shown = page.columns(page.visit(LINE_IN))
    check(shown[0] == "" and shown[2] == "", "the next visit: no buttons", str(shown))
    page.switch(True)

    # display bytes: the whole setup screen and the two labels, with both buttons On
    page.visit(LINE_IN)
    page.key(KEY_TRK_RECORD)
    on_bytes = page.page_bytes()
    total = sum(on_bytes.values())
    check(total <= LIMIT, f"the setup screen with both buttons: {total} bytes, under {LIMIT}")
    page.switch(False)
    page.visit(LINE_IN)
    off_bytes = page.page_bytes()
    page.switch(True)
    added = sum(on_bytes.get(page.label0 + c, 0) - off_bytes.get(page.label0 + c, 0) for c in (1, 2))
    # the rig counts each text at its upper bound (24 + 12 a character, as test_masterfx_rig.py does)
    check(added > 0 and sum(off_bytes.values()) + added <= LIMIT,
          f"the two labels add {added} bytes, {LIMIT - total} to spare", str(added))
    print(f"   setup screen {total} bytes with both buttons, {sum(off_bytes.values())} without; "
          f"label[1] {on_bytes.get(page.label0 + 1)}, label[2] {on_bytes.get(page.label0 + 2)}")
    page.visit(LINE_IN)
    page.key(KEY_TRK_AUDIO)                            # both Off again


def main() -> int:
    r = Rig(Path(sys.argv[1]))
    test_config(r)
    test_projects(r)
    test_recorder(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) > 2 else None)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
