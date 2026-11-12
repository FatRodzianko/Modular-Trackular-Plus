# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Synth FX's rows and the project record on full Tracker+ builds, with the synth libraries in: the
patch editor's two plugin slots (button 3) and record 16. The second build is plain CE (MT_NO_MODS).

CE's real cInstrumentEditor runs on the sound rig with its display calls caught, as
test_masterfx_rig.py's ui section does for the Master page; buttons and the encoder call what
the page itself registered. The mods start as at boot (mods::init on the fake card, then a
main-loop pass with the mod On writes the default pack to /SynthFX/); a slot's plugin is made
from the card when it is picked.

1. off: with the mod Off, every CE engine's patch editor makes the same display calls, and
   draws the same bytes, as the reference build, through a walk over rows, values, defaults
   and sections. With the mod On, WAVE and PHASE have button 3 too, and no FX tile of their own.
2. page: mod On. Button 3 reads "Synth FX" and shows FX1 and FX2 at None; a slot's list has
   None and the pack, without the other slot's plugin; a pick makes the plugin, its knobs
   become rows with its names and texts, and the pick lands in the record; the encoder moves
   a knob a step a tick, Delete puts it back and empties a slot; a swap shows Loading until
   the main loop has made the new plugin, then the page redraws itself; button 3 again, or a
   section, goes back; the grid keeps every tile (VAP and WTFM fill all 16); toggling and
   picking leak no heap. Placement: on WAVE a plugin that allows Pre (the pack's Overdrive,
   Distortion and Bit Crusher) has a Placement row under its slot, Post at first; Pre makes it
   again with an instance for each voice, Delete puts Post back; no Placement row for the Chorus
   on WAVE, or for the Distortion on ACD or PHASE.
3. bytes: the display list with the most rows (Phaser and Flanger; on WAVE, the Distortion with
   its Placement and the Flanger), on a slot's list and on a knob, on every engine, under the
   limit.
4. projects: the record through CE's project hooks on a fake SD card (as test_swing.py): Open
   without it is every slot empty; a version 1 project opens with its effects in the slots,
   and the main loop makes their plugins; Save, Save As, New Project, Save As over another
   project; an unsaved edit doesn't stick; the switch Off keeps the slots. Record version 3
   keeps each slot's Placement; a version 2 project opens with every slot Post.

The record's rules are in the host tests (core/mods/tests/test_synthfx.cpp).

Usage: python tools/emu/test_synthfx_page.py <mods.axf> <reference.axf> [--only SECTION]
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sound_rig import SoundRig  # noqa: E402
from test_masterfx_rig import LIMIT, MasterPage  # noqa: E402
from test_step0 import store_image  # noqa: E402
from test_swing import Projects  # noqa: E402
from test_synths import WAVE  # noqa: E402
from test_synthfx import (FILE_SIZE, INSTANCE_BYTES, INSTANCE_STATE, KNOBS, NAMES, PERC, PRE_BYTE,  # noqa: E402
                          SLOT_BYTES, SYNTHFX_MOD)
from test_synths import PHASE  # noqa: E402

SYNTH0 = 64                 # SYNTH_INSTRUMENTS_START_IDX
RECORD = SYNTHFX_MOD + 1
SLOTS = "_ZN4mods7synthfx5slotsE"
SWITCHES = "_ZN4mods8switchesE"
STORE = "_ZN4mods12_GLOBAL__N_112projectStoreE"
FX_BUTTON = 3
SLOT_RECORD = FILE_SIZE + 4 * KNOBS    # a slot in record 16: its file and knobs (version 2's 576 bytes)
RECORD_SIZE = 3 * 2 * SLOT_RECORD + 6   # version 3: then each slot's Placement, a byte (1: Pre)
# the pack as the lists show it (each plugin's own name)
PACK = ("Bit Crusher", "Chorus", "Distortion", "Flanger", "Overdrive", "Phaser")
NAN = float("nan")
NAME = {f"{n.replace(' ', '')}.ptsfx": n for n in PACK}
KNOB_ROWS = {"BitCrusher.ptsfx": ("Bits", "Rate", "Level"), "Chorus.ptsfx": ("Rate", "Depth", "Mix"),
             "Distortion.ptsfx": ("Gain", "Tone", "Level"), "Flanger.ptsfx": ("Rate", "Depth", "Feedback", "Mix"),
             "Overdrive.ptsfx": ("Gain", "Tone", "Level"), "Phaser.ptsfx": ("Rate", "Depth", "Feedback", "Mix")}

failures = 0


def check(ok: bool, what: str) -> None:
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def start_mods(r, on):
    """As at boot: mods::init on the rig's card, a main-loop pass with the mod On (the default pack
    onto the card, its list read), then the switch as wanted."""
    m = r.m
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), r.sd.obj, max_insns=50_000_000)
    m.write(m.addr(SWITCHES) + SYNTHFX_MOD, b"\1")
    m.call(m.addr("_ZN4mods6updateEv"), max_insns=200_000_000)
    m.write(m.addr(SWITCHES) + SYNTHFX_MOD, bytes([1 if on else 0]))
    m.uc.ctl_flush_tb()


def slot_bytes(m, base):
    """The slots as the firmware holds them (each SLOT_BYTES, padded)"""
    return bytes(m.read(base, 3 * 2 * SLOT_BYTES))


def slot_of(raw, part, slot):
    """(file, knobs) of one slot in the slots' bytes"""
    at = (part * 2 + slot) * SLOT_BYTES
    file = raw[at:at + FILE_SIZE].split(b"\0")[0].decode()
    return file, struct.unpack("<12f", raw[at + FILE_SIZE:at + PRE_BYTE])


def pre_of(raw, part, slot):
    return raw[(part * 2 + slot) * SLOT_BYTES + PRE_BYTE] != 0


def record_slot(raw, part, slot):
    """(file, knobs, Pre) of one slot in record 16's bytes (version 3)"""
    i = part * 2 + slot
    at = i * SLOT_RECORD
    file = raw[at:at + FILE_SIZE].split(b"\0")[0].decode()
    return file, struct.unpack("<12f", raw[at + FILE_SIZE:at + SLOT_RECORD]), raw[6 * SLOT_RECORD + i] != 0


class EditorPage(MasterPage):
    """CE's instrument editor on the patch edit screen of synth part 0."""
    PAGE = "instrumentEditor"
    INIT = "_ZN17cInstrumentEditor19initDisplayControlsEv"
    RIG = SoundRig

    def __init__(self, axf, engine, on=False):
        super().__init__(axf)
        r, m = self.r, self.m
        r.stub(m.addr("_ZN8TitleBar33showCurrentInstrumentOnRightLabelEv"), lambda mm: 0)  # its own synth pointer
        self.mods = SLOTS in r.syms
        if self.mods:
            m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))  # the project store
            r.stub(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: 0)
            start_mods(r, on)
            self.slots = m.addr(SLOTS)
            self.fx_table = next(s.addr for n, s in r.syms.items() if "synthfx" in n and "instanceTable" in n)
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
        self.m.uc.ctl_flush_tb()

    def turn(self, value):
        self.encoder(value)
        self.m.uc.ctl_flush_tb()            # a pick may load a plugin's code

    def section(self, index):
        """A pad or the grid's middle button: setSection."""
        self.m.call(self.m.addr("_ZN17cInstrumentEditor10setSectionEi"), self.page, index, max_insns=300_000_000)

    def main_loop(self, ms=100):
        """Time passes (a swapped plugin is let go after 50 ms when no audio runs), then a main-loop
        pass: the mods' update (the slots' plugins made and freed) and the editor's own update,
        which redraws the rows when a slot changed."""
        m = self.m
        self.r.now += ms
        m.call(m.addr("_ZN4mods6updateEv"), max_insns=200_000_000)
        m.uc.ctl_flush_tb()
        m.call(m.addr("_ZN17cInstrumentEditor6updateEv"), self.page, max_insns=200_000_000)

    def slot(self, part=0, slot=0):
        return slot_of(slot_bytes(self.m, self.slots), part, slot)

    def pre(self, part=0, slot=0):
        return pre_of(slot_bytes(self.m, self.slots), part, slot)

    def made(self):
        """Plugin instances made now"""
        return sum(1 for i in range(56) if self.m.r32(self.fx_table + i * INSTANCE_BYTES + INSTANCE_STATE) != 0)

    def instance(self, part=0, slot=0):
        v = self.m.call(self.m.addr("_ZN4mods7synthfx12slotInstanceEii"), part, slot)
        return v - (1 << 32) if v & 0x80000000 else v

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

    def pick(self, file, slot=0, part=0, most=10):
        """The encoder on a slot's row until the slot has file (the row must be selected), then a
        main-loop pass: a pick over a made plugin is Loading until the main loop has let it go"""
        for _ in range(most):
            if self.slot(part, slot)[0] == file:
                break
            self.turn(1)
        self.main_loop()
        return self.slot(part, slot)[0] == file and self.instance(part, slot) >= 0


def after(texts, name):
    """The text drawn after a row's name: its value"""
    i = texts.index(name) if name in texts else -1
    return texts[i + 1] if 0 <= i < len(texts) - 1 else None


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
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        p = EditorPage(mods, e, on=True)
        p.open()
        texts, _ = p.drawn()
        check(FX_BUTTON in p.buttons and "Synth FX" in p.texts_of("text") and "FX" not in texts and not p.r.failures,
              f"{name}, mod On: button 3 reads Synth FX, and no FX tile of its own ({texts[:10]})")
        p = EditorPage(mods, e, on=False)
        p.open()
        texts, _ = p.drawn()
        check(FX_BUTTON not in p.buttons and "FX" not in texts and not p.r.failures,
              f"{name}, mod Off: no button 3, no FX tile (dry, as CE's engines)")


def test_page(mods):
    print("-- page --")
    p = EditorPage(mods, 1, on=True)
    p.open()
    grid_on = p.drawn()[0]
    check(FX_BUTTON in p.buttons and "Synth FX" in p.texts_of("text"), "ACD, mod On: button 3 reads Synth FX")
    p.take_log()
    p.press_id(FX_BUTTON)
    texts, _ = p.drawn()
    check(after(texts, "FX1") == "None" and after(texts, "FX2") == "None" and "Synth FX" in p.texts_of("text"),
          f"button 3: FX1 and FX2 at None, the list titled Synth FX ({texts[:8]})")
    check(all(n in texts for n in PACK), f"FX1's list: None and the pack ({[n for n in PACK if n not in texts]} missing)")
    check(p.record() is None, "nothing picked: no record in the project")

    # FX1: the Overdrive, made from the card; its knobs become rows
    picked = p.pick("Overdrive.ptsfx")
    texts, _ = p.drawn()
    gain, tone, level = after(texts, "Gain"), after(texts, "Tone"), after(texts, "Level")
    check(picked and p.instance() >= 0 and after(texts, "FX1") == "Overdrive" and
          (gain or "").endswith("dB") and tone == "50 %" and level == "+0.0 dB",
          f"FX1 picks the Overdrive: made ({p.instance()}), its Gain, Tone and Level as rows ({gain}, {tone}, {level})")
    rec = p.record()
    want = record_slot(rec[1], 0, 0) if rec and len(rec[1]) == RECORD_SIZE else None
    check(rec is not None and rec[0] == 3 and want is not None and want[0] == "Overdrive.ptsfx" and
          want[1][:3] == (50.0, 50.0, 0.0) and all(math.isnan(k) for k in want[1][3:]) and not want[2] and
          "Placement" not in p.drawn()[0],
          f"the pick lands in the project's record 16, version 3 ({len(rec[1]) if rec else 0} bytes), its knobs on "
          f"the plugin's steps, Post; ACD has no Placement row ({rec[0] if rec else None}, {want})")

    # the knobs: a step a tick, Delete, the ends
    p.press("down")
    p.turn(7)
    check(p.slot()[1][0] == 57.0, f"Gain: 1 % a step ({p.slot()[1][0]})")
    p.press("delete")
    check(p.slot()[1][0] == 50.0, f"Delete: Gain back to 50 % ({p.slot()[1][0]})")
    p.press("down")
    p.press("down")
    p.turn(100)
    texts, _ = p.drawn()
    check(p.slot()[1][2] == 6.0 and "+6.0 dB" in texts, f"Level: up to +6 dB ({p.slot()[1][2]})")
    p.turn(-1)
    check(p.slot()[1][2] == 5.5, "in 0.5 dB")

    # FX2: every plugin but FX1's; the Chorus
    p.press("down")
    texts, _ = p.drawn()
    shown = [n for n in PACK if n in texts]
    check(texts.count("Overdrive") == 1 and len(shown) == len(PACK) and after(texts, "FX2") == "None",
          f"FX2's list: None and every plugin but FX1's (the Overdrive only as FX1's, {texts.count('Overdrive')})")
    picked = p.pick("Chorus.ptsfx", slot=1)
    texts, _ = p.drawn()
    knobs = [after(texts, k) for k in ("Rate", "Depth", "Mix")]
    check(picked and p.instance(0, 1) >= 0 and all(knobs) and knobs[1:] == ["50 %", "50 %"],
          f"FX2 picks the Chorus: made, Rate, Depth and Mix as rows ({knobs})")

    # a swap: FX1 to the next plugin, Loading until the main loop has let the Overdrive go and made it
    for _ in range(4):
        p.press("up")
    p.turn(1)
    file = p.slot()[0]
    texts, _ = p.drawn()
    loading = after(texts, "FX1")
    check(file not in ("", "Overdrive.ptsfx", "Chorus.ptsfx") and loading == "Loading" and "Gain" not in texts and
          p.instance() == -1, f"a swap: FX1 to {file}, the row Loading ({loading}) and the old knobs gone")
    p.main_loop()
    made = p.instance()
    count = sum(p.instance(0, s) >= 0 for s in range(2))
    texts, _ = p.drawn()
    now = after(texts, "FX1")
    check(made >= 0 and count == 2, f"then the main loop lets the Overdrive go and makes it ({made}, {count} made)")
    rows = KNOB_ROWS.get(file, ("?",))
    check(now == NAME.get(file) and all(after(texts, k) for k in rows),
          f"the page redraws itself once the plugin is made: FX1 {now}, then its knobs {rows}")

    # Delete on FX1: None, the knobs' rows gone
    p.press("delete")
    p.main_loop()
    texts, _ = p.drawn()
    check(p.slot()[0] == "" and after(texts, "FX1") == "None" and after(texts, "FX2") == "Chorus" and p.instance() == -1,
          f"Delete on FX1: None, its plugin let go, FX2 kept ({after(texts, 'FX1')}, {after(texts, 'FX2')})")
    others = slot_bytes(p.m, p.slots)[2 * SLOT_BYTES:]
    check(all(slot_of(others, part, s)[0] == "" for part in range(2) for s in range(2)),
          "the other parts' slots untouched")

    # back: button 3 again, or a section
    p.press_id(FX_BUTTON)
    texts, _ = p.drawn()
    check("FX1" not in texts and texts == grid_on and p.texts_of("text")[-1] != "Synth FX",
          "button 3 again: the section's rows back, as before")
    p.press_id(FX_BUTTON)
    p.section(0)
    texts, _ = p.drawn()
    check("FX1" not in texts and "Saw Mix" in texts, "a section, even the same one, leaves the rows")
    p.press_id(FX_BUTTON)
    p.section(9)
    texts, _ = p.drawn()
    check("FX1" not in texts and "Macro name" in texts, "and a macro's tile")

    # VAP and WTFM fill all 16 tiles: On or Off, the same grid
    for e in (4, 5, PERC):
        a, b = EditorPage(mods, e, on=True), EditorPage(mods, e, on=False)
        a.open()
        b.open()
        ga, gb = a.drawn()[0], b.drawn()[0]
        ga = ["" if t == "Synth FX" else t for t in ga]       # button 3's label, blank with the mod Off
        check(ga == gb and "M7" in ga, f"{NAMES[e]}: every tile and macro as with the mod Off (M7 there)")

    # Placement on WAVE: a Pre plugin's row, Post at first; Pre makes one instance for each voice
    p = EditorPage(mods, WAVE, on=True)
    p.open()
    p.press_id(FX_BUTTON)
    picked = p.pick("Distortion.ptsfx")
    texts, _ = p.drawn()
    first = (after(texts, "Placement"), p.pre(), p.made())
    p.press("down")
    p.turn(-1)
    p.main_loop()
    texts, _ = p.drawn()
    rec = p.record()
    want = record_slot(rec[1], 0, 0) if rec and len(rec[1]) == RECORD_SIZE else None
    now = (after(texts, "Placement"), p.pre(), p.made(), want[2] if want else None)
    check(picked and first == ("Post", False, 1) and now == ("Pre", True, 8, True) and after(texts, "Gain"),
          f"WAVE, FX1 Distortion: Placement {first[0]} at first ({first[2]} made), Pre then ({now[2]} made, one "
          f"for each voice; the record's byte {now[3]}), the knobs under it")
    p.press("delete")
    p.main_loop()
    texts, _ = p.drawn()
    back = (after(texts, "Placement"), p.pre(), p.made())
    check(back == ("Post", False, 1), f"Delete on Placement: Post again, one instance ({back})")
    p.press("down")
    start = p.slot()[1][0]
    p.turn(-5)                          # Gain, with the knob one instance's
    p.press("up")
    p.turn(-1)                          # Pre: made again, eight of it
    p.main_loop()
    gain = p.slot()[1][0]
    p.press("down")
    p.turn(-3)                          # on every voice's instance (knobEdited)
    texts, _ = p.drawn()
    check(p.pre() and p.made() == 8 and gain == start - 5 and p.slot()[1][0] == gain - 3,
          f"Pre kept the Gain set before it ({start} -> {gain}), then Gain moved on: {p.slot()[1][0]} "
          f"({after(texts, 'Gain')})")
    # FX2: the Chorus has no Placement row
    p.press("down")
    p.press("down")
    p.press("down")
    p.pick("Chorus.ptsfx", slot=1)
    texts, _ = p.drawn()
    check(texts.count("Placement") == 1 and p.instance(0, 1) >= 0,
          f"WAVE, FX2 Chorus: no Placement row of its own ({texts.count('Placement')} in all)")
    for e, name in ((PHASE, "PHASE"), (1, "ACD")):
        q = EditorPage(mods, e, on=True)
        q.open()
        q.press_id(FX_BUTTON)
        q.pick("Distortion.ptsfx")
        texts, _ = q.drawn()
        check("Placement" not in texts and after(texts, "Gain"), f"{name}, FX1 Distortion: no Placement row")

    # no heap left behind, no asserts
    p = EditorPage(mods, 7, on=True)
    p.open()
    p.press_id(FX_BUTTON)
    p.press_id(FX_BUTTON)
    heap0 = p.r.heap_brk
    for i in range(40):
        p.press_id(FX_BUTTON)
        p.turn(1 if i % 3 else -1)
        p.press("down" if i % 4 else "up")
        if i % 5 == 0:
            p.section(i % 4)
        if i % 3 == 0:
            p.main_loop()
    check(p.r.heap_brk == heap0 and not p.r.failures, f"40 toggles, picks and edits: heap held, no asserts {p.r.failures[:1]}")


def test_bytes(mods, ref):
    print("-- bytes --")
    for e, name in NAMES.items():
        a, b = EditorPage(mods, e, on=True), EditorPage(ref, e)
        a.open()
        b.open()
        ce = b.drawn()[1]
        _, closed = a.drawn()
        a.press_id(FX_BUTTON)
        _, empty = a.drawn()
        a.pick("Phaser.ptsfx")
        for _ in range(5):
            a.press("down")
        a.pick("Flanger.ptsfx", slot=1)
        _, lists = a.drawn()
        a.press("down")
        _, bar = a.drawn()
        rows = a.m.call(a.m.addr("_ZN4mods7synthfx8rowCountEi"), 0)
        worst = max(closed, empty, lists, bar)
        check(worst < LIMIT and rows == 10, f"{name}: {closed} bytes on opening, {empty} with the slots empty, "
                                            f"{lists} with {rows} rows on a slot's list, {bar} on a knob "
                                            f"(CE {ce}; limit {LIMIT})")
    for e, name, first in ((WAVE, "WAVE", "Distortion.ptsfx"), (PHASE, "PHASE", "Phaser.ptsfx")):
        a = EditorPage(mods, e, on=True)
        a.open()
        _, closed = a.drawn()
        a.press_id(FX_BUTTON)
        _, empty = a.drawn()
        a.pick(first)
        for _ in range(5):
            a.press("down")
        a.pick("Flanger.ptsfx", slot=1)
        _, lists = a.drawn()
        a.press("down")
        _, bar = a.drawn()
        rows = a.m.call(a.m.addr("_ZN4mods7synthfx8rowCountEi"), 0)
        worst = max(closed, empty, lists, bar)
        check(worst < LIMIT and rows == 10, f"{name}: {closed} bytes on opening, {empty} with the slots empty, "
                                            f"{lists} with {rows} rows on a slot's list, {bar} on a knob "
                                            f"(limit {LIMIT})")


SLOTS_A = ((("Phaser.ptsfx", (10, 20, -30, 40)), ("Overdrive.ptsfx", (70, 30, -6))),)
SLOTS_B = ((("Chorus.ptsfx", (1, 2, 3)), (None, ())),)


def slot_image(part_slots):
    """One part's two slots as the firmware holds them, each with its Placement (padded: the padding
    isn't part of the slot, so parts() leaves it out)"""
    out = b""
    for item in part_slots:
        file, knobs = item[0], item[1]
        pre = len(item) > 2 and item[2]
        name = (file or "").encode().ljust(FILE_SIZE, b"\0")
        vals = list(knobs) + [NAN] * (KNOBS - len(knobs))
        out += (name + struct.pack("<12f", *vals) + bytes([1 if pre else 0])).ljust(SLOT_BYTES, b"\0")
    return out


def meaning(image):
    """A part's slots without the padding"""
    return b"".join(image[s * SLOT_BYTES:s * SLOT_BYTES + PRE_BYTE + 1] for s in range(2))


EMPTY = meaning(slot_image(((None, ()), (None, ()))))
A = meaning(slot_image(SLOTS_A[0]))
B = meaning(slot_image(SLOTS_B[0]))
C_IMAGE = slot_image((("Distortion.ptsfx", (80, 40, 0), True), ("BitCrusher.ptsfx", (30, 40, 0), False)))
C = meaning(C_IMAGE)
IMAGES = {meaning(i): i for i in (slot_image(((None, ()), (None, ()))), slot_image(SLOTS_A[0]),
                                   slot_image(SLOTS_B[0]), C_IMAGE)}


class FxProjects(Projects):
    """CE's project hooks on the sound rig (its platform: the plugins' clock)"""

    def __init__(self, axf):
        r = SoundRig(axf)
        r.stub(r.m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: r.platform)
        super().__init__(r)
        self.slots = self.m.addr(SLOTS)

    def edit(self, part, image, on=True):
        self.m.write(self.switches + SYNTHFX_MOD, bytes([1 if on else 0]))
        self.m.call(self.m.addr("_ZN4mods20projectSwitchChangedEv"))
        full = IMAGES[image]
        self.m.write(self.slots + part * 2 * SLOT_BYTES, full)
        self.m.call(self.m.addr("_ZN4mods20projectSettingEditedEv"))

    def parts(self):
        raw = slot_bytes(self.m, self.slots)
        return tuple(meaning(raw[p * 2 * SLOT_BYTES:(p + 1) * 2 * SLOT_BYTES]) for p in range(3))

    def state(self):
        return self.m.r8(self.switches + SYNTHFX_MOD), self.parts()


def test_projects(mods):
    print("-- projects --")
    p = FxProjects(mods)
    sd = p.sd
    empty3 = (EMPTY, EMPTY, EMPTY)

    older = bytearray(SYNTHFX_MOD)
    sd.files["Projects/Old/mods.bin"] = store_image(b"MTP1", [(0, 1, bytes(older))])
    p.edit(1, A)
    p.open("Projects", "Old")
    check(p.parts() == empty3, "a project from a build without Synth FX: every slot empty")

    # version 1: the Distortion and the Phaser on every part, Feedback 90 %
    v1 = bytes([3, 50, 50, 50, 185, 2, 100, 50, 1, 60] * 3)
    switches = bytearray(SYNTHFX_MOD + 1)
    switches[SYNTHFX_MOD] = 1
    sd.files["Projects/V1/mods.bin"] = store_image(b"MTP1", [(0, 1, bytes(switches)), (RECORD, 1, v1)])
    p.open("Projects", "V1")
    raw = slot_bytes(p.m, p.slots)
    fx = [slot_of(raw, part, s) for part in range(3) for s in range(2)]
    check([f for f, _ in fx] == ["Distortion.ptsfx", "Phaser.ptsfx"] * 3 and fx[0][1][:3] == (100.0, 50.0, 0.0) and
          fx[1][1][:4] == (50.0, 50.0, 90.0, 50.0),
          f"a version 1 project: its effects in the slots, the Distortion in FX1 and the Phaser in FX2 ({fx[:2]})")
    passes = 0
    made = [False] * 6
    while not all(made) and passes < 4:     # the first pass starts the plugins, the next makes the slots'
        p.m.call(p.m.addr("_ZN4mods6updateEv"), max_insns=400_000_000)
        p.m.uc.ctl_flush_tb()
        passes += 1
        made = [p.m.call(p.m.addr("_ZN4mods7synthfx12slotInstanceEii"), i // 2, i % 2) < 0x80000000 for i in range(6)]
    check(all(made), f"Open: each slot's plugin made by the main loop ({made}, after {passes} passes)")

    p.new_project()
    p.edit(1, A)
    p.edit(2, B)
    p.save("Projects", "Old")
    p.new_project()
    check(p.parts() == empty3, "New Project: every slot empty")
    p.open("Projects", "Old")
    check(p.parts() == (EMPTY, A, B), "Save: reopened with its slots")

    p.edit(0, B)
    p.save("Projects", "Copy")
    check("Projects/Copy/mods.bin" in sd.files, "Save As: mods.bin in the new folder")
    p.open("Projects", "Old")
    check(p.parts() == (EMPTY, A, B), "Save As leaves the first project as it was")
    p.open("Projects", "Copy")
    check(p.parts() == (B, A, B), "Save As: the new project has the slots")

    p.new_project()
    p.save("Projects", "Fresh")
    p.open("Projects", "Copy")
    p.open("Projects", "Fresh")
    check(p.parts() == empty3, "New Project then Save As: every slot empty")

    p.open("Projects", "Copy")
    p.edit(2, A)
    p.save("Projects", "Fresh")
    p.open("Projects", "Old")
    p.open("Projects", "Fresh")
    check(p.parts() == (B, A, A), "Save As over another project: replaces its slots")

    p.edit(0, A)
    p.open("Projects", "Old")
    p.open("Projects", "Fresh")
    check(p.parts() == (B, A, A), "an unsaved edit doesn't stick")

    p.edit(0, B, on=False)
    p.save()
    p.open("Projects", "Old")
    p.open("Projects", "Fresh")
    check(p.state() == (0, (B, A, A)), "Off: the switch saved Off, the slots kept")

    # version 3 keeps each slot's Placement; version 2 opens as Post
    p.edit(1, C)
    p.save("Projects", "Placed")
    p.new_project()
    p.open("Projects", "Placed")
    check(p.parts()[1] == C and pre_of(slot_bytes(p.m, p.slots), 1, 0) and
          not pre_of(slot_bytes(p.m, p.slots), 1, 1), "version 3: FX1's Pre and FX2's Post kept through Save and Open")
    v2 = b""
    for part in range(3):
        for file, knobs in (("Distortion.ptsfx", (80, 40, 0)), ("Chorus.ptsfx", (50, 50, 50))):
            vals = list(knobs) + [NAN] * (KNOBS - len(knobs))
            v2 += file.encode().ljust(FILE_SIZE, b"\0") + struct.pack("<12f", *vals)
    sd.files["Projects/V2/mods.bin"] = store_image(b"MTP1", [(0, 1, bytes(switches)), (RECORD, 2, v2)])
    p.open("Projects", "V2")
    raw = slot_bytes(p.m, p.slots)
    got = [(slot_of(raw, part, s)[0], pre_of(raw, part, s)) for part in range(3) for s in range(2)]
    check(got == [("Distortion.ptsfx", False), ("Chorus.ptsfx", False)] * 3,
          f"a version 2 project ({len(v2)} bytes): its slots, every one Post ({got[:2]})")
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
