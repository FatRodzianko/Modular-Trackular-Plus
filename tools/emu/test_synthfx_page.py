# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Synth FX phase 3 on full Tracker+ builds, with the synth libraries in: the patch editor's
Synth FX rows (button 3) and the project record. The second build is plain CE (MT_NO_MODS).

CE's real cInstrumentEditor runs on the sound rig with its display calls caught, as
test_masterfx_rig.py's ui section does for the Master page; buttons and the encoder call what
the page itself registered. The engines' parameters come through Patch's public metadata, as
the editor reads them.

1. off: with the mod Off, every CE engine's patch editor makes the same display calls, and
   draws the same bytes, as the reference build, through a walk over rows, values, defaults
   and sections. On WAVE (mod On) there is no button 3 either.
2. page: mod On. Button 3 reads "Synth FX" and shows the nine rows with WAVE's names and
   texts; the encoder and Delete set the part's settings and the project's record; the
   Distortion renames Gain and Tone; the lists and bars; button 3 again, or a section, goes
   back; the grid keeps every tile (VAP and WTFM fill all 16); toggling leaks no heap.
3. bytes: the display list with the rows shown, on every engine, under the limit.
4. projects: the record through CE's project hooks on a fake SD card (as test_swing.py): Open
   without it is all Off; Save, Save As, New Project, Save As over another project; an
   unsaved edit doesn't stick; the switch Off keeps the settings.

The record's rules are in the host tests (core/mods/tests/test_synthfx.cpp).

Usage: python tools/emu/test_synthfx_page.py <mods.axf> <reference.axf> [--only SECTION]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from sound_rig import SoundRig  # noqa: E402
from test_masterfx_rig import LIMIT, MasterPage  # noqa: E402
from test_step0 import store_image  # noqa: E402
from test_swing import Projects  # noqa: E402
from test_synths import WAVE  # noqa: E402
from test_synthfx import INIT, NAMES, PERC, SYNTHFX_MOD  # noqa: E402

SYNTH0 = 64                 # SYNTH_INSTRUMENTS_START_IDX
RECORD = SYNTHFX_MOD + 1
SETTINGS = "_ZN4mods7synthfx8settingsE"
SWITCHES = "_ZN4mods8switchesE"
STORE = "_ZN4mods12_GLOBAL__N_112projectStoreE"
FX_BUTTON = 3
# the rows, as WAVE's FX section has them without Placement, and their settings' index
ROWS = (("Distortion", 5), ("Gain", 6), ("Tone", 7), ("Level", 9), ("Effect", 0), ("Rate", 1), ("Depth", 2),
        ("Feedback", 4), ("FX Mix", 3))
INIT_TEXTS = ("Off", "50 %", "50 %", "+0.0 dB", "None", None, "50 %", "+50 %", "50 %")

failures = 0


def f32s(values):
    """As the firmware holds them: single precision."""
    return struct.unpack(f"<{len(values)}f", struct.pack(f"<{len(values)}f", *values))


def check(ok: bool, what: str) -> None:
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


class EditorPage(MasterPage):
    """CE's instrument editor on the patch edit screen of synth part 0."""
    PAGE = "instrumentEditor"
    INIT = "_ZN17cInstrumentEditor19initDisplayControlsEv"
    RIG = SoundRig

    def __init__(self, axf, engine, on=False):
        super().__init__(axf)
        r, m = self.r, self.m
        r.stub(m.addr("_ZN8TitleBar33showCurrentInstrumentOnRightLabelEv"), lambda mm: 0)  # its own synth pointer
        self.mods = SETTINGS in r.syms
        if self.mods:
            m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))  # the project store
            r.stub(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: 0)
            m.write(m.addr(SWITCHES) + SYNTHFX_MOD, bytes([1 if on else 0]))
        r.set_instrument(0, engine)
        settings = m.alloc(512, 4)
        r.call("_ZN5Synth21initSynthPartSettingsEiR17SynthPartSettings", r.synth, 0, settings)
        r.call("_ZN11audioEngine28refreshSynthInstrumentParamsER17SynthPartSettingsi", r.engine, settings, 0)
        m.write(m.addr("mtProject") + self._last_used(), bytes([SYNTH0]))
        self.take_log()

    def _last_used(self):
        """mtProject.values.lastUsedInstrument: the first byte of mtProject the patch editor reads."""
        m = self.m
        base, size = m.addr("mtProject"), m.sym("mtProject").size
        seen = []

        def on_read(uc, access, address, sz, value, user):
            if not seen:
                seen.append(address - base)
                uc.emu_stop()
        h = m.uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=base, end=base + size)
        try:
            m.call(m.addr("_ZN17cInstrumentEditor26showSynthPatchEditorScreenEv"), self.page, max_insns=1_000_000)
        except RuntimeError:
            pass
        m.uc.hook_del(h)
        return seen[0]

    def open(self):
        """The patch editor's Edit Patch button (SynthEditor.cpp): mode, pads, screen, buttons."""
        self.buttons.clear()
        self.m.call(self.m.addr("_ZL19functSynthEditPatchv"), max_insns=300_000_000)

    def press_id(self, button):
        mode, fn = self.buttons[button]
        self.m.call(fn, button, max_insns=100_000_000) if mode in (2, 4) else self.m.call(fn, max_insns=100_000_000)

    def section(self, index):
        """A pad or the grid's middle button: setSection."""
        self.m.call(self.m.addr("_ZN17cInstrumentEditor10setSectionEi"), self.page, index, max_insns=300_000_000)

    def settings(self, part=0):
        return struct.unpack("<10f", self.m.read(self.m.addr(SETTINGS) + 40 * part, 40))

    def record(self):
        """Record 16's version and bytes in the project's store, or None."""
        m = self.m
        out = m.alloc(8, 4)
        data = m.call(m.addr("_ZNK4mods5Store4findEhPhPt"), m.addr(STORE), RECORD, out, out + 4)
        if data == 0:
            return None
        return m.r8(out), bytes(m.read(data, m.r32(out + 4) & 0xFFFF))

    def drawn(self):
        """The texts one full redraw of the shown controls draws, and its bytes."""
        self.texts.clear()
        total = sum(self.page_bytes().values())
        return list(self.texts), total

    def texts_of(self, kind):
        return [e[2] for e in self.log if e[0] == kind]


def walk(p):
    """The patch editor as a user goes over it: rows, values, defaults, sections, macros."""
    p.open()
    for name in ("down", "down", "up"):
        p.press(name)
    p.encoder(1)
    p.encoder(-1)
    p.encoder(3)
    p.press("delete")
    for index in (1, 2, 0, 9):          # sections, and a macro's tile
        p.section(index)
        p.press("down")
        p.encoder(1)
        p.press("delete")
    for button in (4, 6, 5):            # the encoder's buttons
        if button in p.buttons:
            p.press_id(button)


def test_off(mods, ref):
    print("-- off --")
    for e, name in NAMES.items():
        a, b = EditorPage(mods, e, on=False), EditorPage(ref, e)
        walk(a)
        walk(b)
        keep = lambda log: [x for x in log if x[0] != "data"]  # noqa: E731
        x, y = keep(a.take_log()), keep(b.take_log())
        same = len(y) > 150 and x == y   # a walk makes 240
        check(same, f"{name}, mod Off: the same {len(y)} display calls as the reference build")
        if not same:
            for i, (u, v) in enumerate(zip(x, y)):
                if u != v:
                    print(f"  first difference at {i}: {u} / {v}")
                    break
        check(a.page_bytes() == b.page_bytes() and FX_BUTTON not in a.buttons and not a.r.failures and not b.r.failures,
              f"{name}, mod Off: the same display bytes, no button 3, no asserts")
    p = EditorPage(mods, WAVE, on=True)
    p.open()
    check(FX_BUTTON not in p.buttons and "Synth FX" not in p.texts_of("text"),
          "WAVE, mod On: no button 3 (its own FX section has the effects)")


def test_page(mods):
    print("-- page --")
    p = EditorPage(mods, 1, on=True)
    p.open()
    grid_on = p.drawn()[0]
    check(FX_BUTTON in p.buttons and "Synth FX" in p.texts_of("text"), "ACD, mod On: button 3 reads Synth FX")
    p.take_log()
    p.press_id(FX_BUTTON)
    texts, _ = p.drawn()
    names_ok = all(n in texts for n, _ in ROWS)
    values = []
    for (n, _), want in zip(ROWS, INIT_TEXTS):
        i = texts.index(n) if n in texts else -1
        values.append(texts[i + 1] if 0 <= i < len(texts) - 1 else None)
    texts_ok = all(w is None or v == w for v, w in zip(values, INIT_TEXTS)) and values[5].endswith("Hz")
    check(names_ok and texts_ok and "Synth FX" in p.texts_of("text"),
          f"button 3: the nine rows with WAVE's names and Init texts, the list titled Synth FX ({values})")
    check(p.record() is None, "nothing edited: no record in the project")

    # the Distortion's list: the encoder moves it, Delete puts it back
    p.encoder(1)
    s = p.settings()
    texts, _ = p.drawn()
    check(s[5] == 1.0 and "Overdrive" in texts and any(t.startswith("+") and t.endswith("dB") and t != "+0.0 dB"
                                                         for t in texts),
          f"encoder on Distortion: Overdrive, and Gain shows its dB ({s[5]})")
    rec = p.record()
    check(rec is not None and rec[0] == 1 and len(rec[1]) == 30 and rec[1][5] == 1,
          f"the edit lands in the project's record 16 ({rec})")
    p.encoder(2)
    texts, _ = p.drawn()
    check(p.settings()[5] == 3.0 and "Bits" in texts and "Rate" in texts and "Gain" not in texts
          and any(t.endswith("bit") for t in texts), "Bit Crusher: Gain and Tone become Bits and Rate")
    p.encoder(5)
    check(p.settings()[5] == 3.0, "the list stops at its end")
    p.press("delete")
    texts, _ = p.drawn()
    check(p.settings()[5] == 0.0 and "Gain" in texts, "Delete: Distortion back to Off, Gain again")

    # values: down to Gain, Level, Feedback
    p.press("down")
    p.encoder(7)
    check(abs(p.settings()[6] - 0.57) < 1e-6, f"Gain: 1 % a step ({p.settings()[6]})")
    p.press("down")
    p.press("down")
    p.encoder(100)
    texts, _ = p.drawn()
    check(p.settings()[9] == 6.0 and "+6.0 dB" in texts, f"Level: up to +6 dB ({p.settings()[9]})")
    p.encoder(-1)
    check(p.settings()[9] == 5.5, "in 0.5 dB")
    for _ in range(4):
        p.press("down")
    p.encoder(-300)
    texts, _ = p.drawn()
    check(abs(p.settings()[4] + 0.95) < 1e-6 and "-95 %" in texts, f"Feedback: down to -95 % ({p.settings()[4]})")
    p.press("delete")
    check(p.settings()[4] == 0.5, "Delete: Feedback back to +50 %")
    p.press("up")
    p.press("up")
    p.press("up")
    p.encoder(1)
    texts, _ = p.drawn()
    check(p.settings()[0] == 1.0 and "Chorus" in texts, "Effect: Chorus")
    rec = p.record()
    want = [round(INIT[k] * 100) if k in (1, 2, 3, 6, 7) else 0 for k in range(10)]
    want[0], want[4], want[6], want[8], want[9] = 1, 145, 57, 1, 71
    check(rec is not None and list(rec[1][:10]) == want and list(rec[1][10:]) == [0, 50, 50, 50, 145, 0, 50, 50, 1, 60] * 2,
          f"the record holds part 1's steps, the others at Init ({list(rec[1][:10]) if rec else None})")
    others = p.settings(1), p.settings(2)
    check(others == (INIT, INIT), "the other parts' settings untouched")

    # back: button 3 again, or a section
    p.press_id(FX_BUTTON)
    texts, _ = p.drawn()
    check("Distortion" not in texts and texts == grid_on and p.texts_of("text")[-1] != "Synth FX",
          "button 3 again: the section's rows back, as before")
    p.press_id(FX_BUTTON)
    p.section(0)
    texts, _ = p.drawn()
    check("Distortion" not in texts and "Saw Mix" in texts, "a section, even the same one, leaves the rows")
    p.press_id(FX_BUTTON)
    p.section(9)
    texts, _ = p.drawn()
    check("Distortion" not in texts and "Macro name" in texts, "and a macro's tile")

    # VAP and WTFM fill all 16 tiles: On or Off, the same grid
    for e in (4, 5, PERC):
        a, b = EditorPage(mods, e, on=True), EditorPage(mods, e, on=False)
        a.open()
        b.open()
        ga, gb = a.drawn()[0], b.drawn()[0]
        ga = ["" if t == "Synth FX" else t for t in ga]       # button 3's label, blank with the mod Off
        check(ga == gb and "M7" in ga, f"{NAMES[e]}: every tile and macro as with the mod Off (M7 there)")

    # no heap left behind, no asserts
    p = EditorPage(mods, 7, on=True)
    p.open()
    p.press_id(FX_BUTTON)
    p.press_id(FX_BUTTON)
    heap0 = p.r.heap_brk
    for i in range(40):
        p.press_id(FX_BUTTON)
        p.encoder(1 if i % 2 else -1)
        p.press("down")
        if i % 5 == 0:
            p.section(i % 4)
    check(p.r.heap_brk == heap0 and not p.r.failures, f"40 toggles and edits: heap held, no asserts {p.r.failures[:1]}")


def test_bytes(mods, ref):
    print("-- bytes --")
    for e, name in NAMES.items():
        a, b = EditorPage(mods, e, on=True), EditorPage(ref, e)
        a.open()
        b.open()
        ce = b.drawn()[1]
        _, closed = a.drawn()
        a.press_id(FX_BUTTON)
        _, rows = a.drawn()
        a.press("down")
        a.press("down")
        a.press("down")
        _, bar = a.drawn()
        worst = max(closed, rows, bar)
        check(worst < LIMIT, f"{name}: {closed} bytes on opening, {rows} with the rows, {bar} on a bar "
                             f"(CE {ce}; limit {LIMIT})")


class FxProjects(Projects):
    def __init__(self, axf):
        class R:
            pass
        r = R()
        r.m = Machine(axf)
        super().__init__(r)
        self.fx = self.m.addr(SETTINGS)

    def edit(self, part, values, on=True):
        self.m.write(self.switches + SYNTHFX_MOD, bytes([1 if on else 0]))
        self.m.call(self.m.addr("_ZN4mods20projectSwitchChangedEv"))
        self.m.write(self.fx + 40 * part, struct.pack("<10f", *values))
        self.m.call(self.m.addr("_ZN4mods20projectSettingEditedEv"))

    def state(self):
        return (self.m.r8(self.switches + SYNTHFX_MOD),
                tuple(struct.unpack("<10f", self.m.read(self.fx + 40 * p, 40)) for p in range(3)))


def test_projects(mods):
    print("-- projects --")
    p = FxProjects(mods)
    sd = p.sd
    a = f32s((3.0, 0.25, 0.75, 0.4, -0.5, 2.0, 0.8, 0.3, 1.0, -6.0))   # each on its setting's steps
    b = f32s((1.0, 0.5, 0.5, 0.9, 0.5, 3.0, 0.1, 0.9, 1.0, 2.5))
    init3 = (INIT, INIT, INIT)

    older = bytearray(SYNTHFX_MOD)
    sd.files["Projects/Old/mods.bin"] = store_image(b"MTP1", [(0, 1, bytes(older))])
    p.edit(1, a)
    p.open("Projects", "Old")
    check(p.state()[1] == init3, "a project from a build without Synth FX: every effect Off")

    p.edit(1, a)
    p.edit(2, b)
    p.save()
    p.new_project()
    check(p.state()[1] == init3, "New Project: every effect Off")
    p.open("Projects", "Old")
    check(p.state()[1] == (INIT, a, b), "Save: reopened with its settings")

    p.edit(0, b)
    p.save("Projects", "Copy")
    check("Projects/Copy/mods.bin" in sd.files, "Save As: mods.bin in the new folder")
    p.open("Projects", "Old")
    check(p.state()[1] == (INIT, a, b), "Save As leaves the first project as it was")
    p.open("Projects", "Copy")
    check(p.state()[1] == (b, a, b), "Save As: the new project has the settings")

    p.new_project()
    p.save("Projects", "Fresh")
    p.open("Projects", "Copy")
    p.open("Projects", "Fresh")
    check(p.state()[1] == init3, "New Project then Save As: every effect Off")

    p.open("Projects", "Copy")
    p.edit(2, a)
    p.save("Projects", "Fresh")
    p.open("Projects", "Old")
    p.open("Projects", "Fresh")
    check(p.state()[1] == (b, a, a), "Save As over another project: replaces its settings")

    p.edit(0, a)
    p.open("Projects", "Old")
    p.open("Projects", "Fresh")
    check(p.state()[1] == (b, a, a), "an unsaved edit doesn't stick")

    p.edit(0, b, on=False)
    p.save()
    p.open("Projects", "Old")
    p.open("Projects", "Fresh")
    check(p.state() == (0, (b, a, a)), "Off: the switch saved Off, the settings kept")
    check(not sd.unexpected and not sd.handles, f"only expected file calls, every file closed {sd.unexpected}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mods")
    ap.add_argument("ref")
    ap.add_argument("--only")
    a = ap.parse_args()
    sections = {"off": lambda: test_off(a.mods, a.ref), "page": lambda: test_page(a.mods),
                "bytes": lambda: test_bytes(a.mods, a.ref), "projects": lambda: test_projects(a.mods)}
    for name, fn in sections.items():
        if a.only in (None, name):
            fn()
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
