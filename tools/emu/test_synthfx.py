# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Level B for Synth FX (core/mods/synthfx/synthfxMod.h): two plugin slots on each of CE's own engines,
through CE's real synth host (tools/emu/sound_rig.py). The closed engines run as they are and only what
comes out of the firmware is checked: ours with and without the slots, never an engine's insides.

The rig starts the mods as the unit does (mods::init on the fake card, then mods::update with the mod
On writes the default pack to /SynthFX/ and lists it); a slot is set where the page sets it
(mods::synthfx::slots) and made with mods::synthfx::remake, as the page's pick does. The plugins' code
is read from the card into the code pool and run from there.

Each part plays note 60 (PERC: 48, which differs left and right, so a lost right side shows).

0. Config: the two Synth FX items are the mod's default and the project's switch.
1. Off = CE: with the switch Off and plugins in both slots, or On with both slots empty, every engine
   sounds as the reference build does, sample for sample, and On with nothing chosen makes no plugin.
2. Mix 0: a modulation plugin with nothing of it mixed in leaves every engine's sound as it was, sample
   for sample, so the slot's path (its fade, and a mono part sent as stereo) is clean.
3. As before (--before, the phase 1 build): every engine, each effect a version 1 project could have,
   through the plugins, sounds as through the built-in effects did, picked mid-note: sample for sample
   within 60 blocks of the pick (the old Distortion ramped in, the plugin starts at once, and the
   difference dies away with the effect's filters), and within the last bit with a Phaser at high
   Feedback after a Distortion, while the note still sounds.
4. Each engine with each pack plugin: the sound changes from the first block it can (LATENCY blocks
   after the pick) without a click (no sample-to-sample jump bigger than the steady sound's own); no
   clipping run longer than the dry sound's; a mono part goes stereo only with a widening plugin.
5. Order and swap: FX1 then FX2 (Overdrive into Chorus is not Chorus into Overdrive); a slot's plugin
   swapped mid-note fades out, is let go, and the new one fades in, without a click.
6. Cleared: the slots fade out, the plugins are let go, and a new note sounds as it would have without
   them, sample for sample.
7. WAVE and PHASE parts are skipped; three parts with two plugins each all run.
8. Garbage knobs in the slots (NaN, infinities, out of range): each plugin is made with its knobs on its
   steps and within its range, and the sound stays bounded; cleared, the part sounds as without them.
9. The rules: no malloc in the audio interrupt, the heap held, interrupts turned back on, the block pool
   no fuller than with CE's own stereo engine (PERC) on all three parts, and the slots' cost per block
   in instructions, the plugins' own code included (the closed engines' own aren't counted).

Usage: python tools/emu/test_synthfx.py <mods.axf> <reference.axf> [--before <phase 1 axf>] [--only SECTION]
       [--engines 1,6]
SECTION is one of the functions below; --engines limits sections 1 to 4 to those engine ids
(tools/emu/plant_synthfx.py).
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unicorn.arm_const import UC_ARM_REG_R0  # noqa: E402

from machine import Machine  # noqa: E402
from sound_rig import BLOCK, SoundRig, peak  # noqa: E402
from test_step0 import menu_item_named  # noqa: E402
from test_synths import BLOCK_BUDGET, PHASE, WAVE, clip_run  # noqa: E402
from test_synths import Rig as SynthsRig  # noqa: E402

NAMES = {1: "ACD", 2: "FAT", 4: "VAP", 5: "WTFM", 6: "PERC", 7: "DIRT", 8: "CHIP"}
ENGINES = dict(NAMES)               # sections 1 to 4 (--engines)
PERC = 6
SYNTHFX_MOD = 15                    # Mod::synthfx
FILE_SIZE, KNOBS = 48, 12           # synthfxMod.h: a slot's file name and knobs
SLOT_BYTES = FILE_SIZE + 4 * KNOBS
INSTANCE_BYTES, INSTANCE_STATE = 76, 72   # synthfxPlugins.cpp's Instance, and its state pointer
NAN = float("nan")

# The pack's plugins and their knobs at the defaults; widening ones make a mono part stereo
PACK = {"Overdrive": (100, 50, 0), "Distortion": (100, 50, 0), "BitCrusher": (100, 50, 0),
        "Chorus": (50, 50, 50), "Flanger": (50, 50, 50, 50), "Phaser": (50, 50, 50, 50)}
WIDENS = {"Chorus", "Flanger", "Phaser"}

# A version 1 project's settings (the phase 1 build's ten, synthFx.h's order) and the slots it
# converts to (synthfxSettings.cpp); Gain, Rate, Depth, Mix are percent steps, Feedback step - 95
EFFECT, RATE, DEPTH, MIX, FEEDBACK, DIST, GAIN, TONE, PLACEMENT, LEVEL = range(10)
INIT = (0.0, 0.5, 0.5, 0.5, 0.5, 0.0, 0.5, 0.5, 1.0, 0.0)
AS_BEFORE = (
    ("Overdrive, Gain 100 %", {DIST: 1, GAIN: 1.0}, [(0, "Overdrive", (100, 50, 0))]),
    ("Distortion, Gain 100 %, Tone 20 %, -6 dB", {DIST: 2, GAIN: 1.0, TONE: 0.2, LEVEL: -6.0},
     [(0, "Distortion", (100, 20, -6))]),
    ("Bit Crusher, Bits 30 %, Rate 40 %", {DIST: 3, GAIN: 0.3, TONE: 0.4}, [(0, "BitCrusher", (30, 40, 0))]),
    ("Chorus", {EFFECT: 1}, [(1, "Chorus", (50, 50, 50))]),
    ("Flanger, Rate 80 %, Feedback -60 %", {EFFECT: 2, RATE: 0.8, FEEDBACK: -0.6}, [(1, "Flanger", (80, 50, -60, 50))]),
    ("Phaser, Mix 100 %", {EFFECT: 3, MIX: 1.0}, [(1, "Phaser", (50, 50, 50, 100))]),
    ("Distortion and Phaser, Feedback 90 %", {DIST: 2, GAIN: 1.0, EFFECT: 3, FEEDBACK: 0.9},
     [(0, "Distortion", (100, 50, 0)), (1, "Phaser", (50, 50, 90, 50))]),
)

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def sym_containing(syms, *parts):
    hits = [n for n in syms if all(p in n for p in parts) and not n.endswith("_veneer")]
    assert len(hits) == 1, (parts, hits[:6])
    return syms[hits[0]]


class Rig(SynthsRig):
    """The mods started as at boot, the plugins on the fake card, the slots set as the page sets them."""

    def __init__(self, axf, on=True):
        super().__init__(axf)
        m = self.m
        self.stub(m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: self.platform)
        m.call(m.addr("_ZN4mods4initEP10FileSystem"), self.sd.obj, max_insns=50_000_000)
        self.fx_switch(True)
        self.update()                   # the pack onto the card, the list
        self.fx_switch(on)
        self.slots = m.addr("_ZN4mods7synthfx5slotsE")
        self._remake = m.addr("_ZN4mods7synthfx6remakeEiib")
        self._instance = m.addr("_ZN4mods7synthfx12slotInstanceEii")
        table = sym_containing(self.syms, "synthfx", "plugins", "instanceTable")
        self.instance_table, self.instance_count = table.addr, table.size // INSTANCE_BYTES
        # the pool's memory (synthfxPool), not the plugins' bookkeeping of it, which has the same name
        pool = max((s for n, s in self.syms.items() if "synthfx" in n and "codePool" in n), key=lambda s: s.size)
        self.code_pool = (pool.addr, pool.addr + pool.size - 1)
        for part in range(3):           # the sends off, so nothing lingers in the master's effects
            self.reverb_send(part, 0.0)
            self.delay_send(part, 0.0)

    def open_code(self):
        """The firmware's open code and the plugins' code pool, where the pack's code runs"""
        return super().open_code() + [self.code_pool]

    def fx_switch(self, on: bool):
        self.m.write(self.switches + SYNTHFX_MOD, bytes([1 if on else 0]))

    def update(self):
        """One main-loop pass of the mods (mods::update): a project's slots made, old plugins freed"""
        self.m.call(self.m.addr("_ZN4mods6updateEv"), max_insns=200_000_000)
        self.m.uc.ctl_flush_tb()        # new code in the pool

    def set_slot(self, part, slot, name=None, knobs=()):
        """The slot's setting, as the page's pick sets it, without making it"""
        at = self.slots + (part * 2 + slot) * SLOT_BYTES
        file = f"{name}.ptsfx".encode() if name else b""
        self.m.write(at, file.ljust(FILE_SIZE, b"\0"))
        vals = list(knobs) + [NAN] * (KNOBS - len(knobs))
        self.m.write(at + FILE_SIZE, struct.pack("<12f", *vals))

    def fx(self, part, slot, name=None, knobs=()):
        """A slot set and made again, as the page's pick: an empty slot takes its plugin now; one
        that had a plugin lets it fade out over the next block and takes the new one at the next
        update"""
        self.set_slot(part, slot, name, knobs)
        self.m.call(self._remake, part, slot, 0, max_insns=50_000_000)
        self.m.uc.ctl_flush_tb()

    def knobs(self, part, slot):
        at = self.slots + (part * 2 + slot) * SLOT_BYTES + FILE_SIZE
        return struct.unpack("<12f", self.m.read(at, 48))

    def instance(self, part, slot) -> int:
        return struct.unpack("<i", struct.pack("<I", self.m.call(self._instance, part, slot)))[0]

    def instances(self) -> int:
        """Plugin instances made now (each holds state)"""
        return sum(1 for i in range(self.instance_count)
                   if self.m.r32(self.instance_table + i * INSTANCE_BYTES + INSTANCE_STATE) != 0)


class OldRig(SynthsRig):
    """The phase 1 build: the built-in effects, set through their ten settings"""

    def __init__(self, axf):
        super().__init__(axf)
        self.settings = self.m.addr("_ZN4mods7synthfx8settingsE")
        for part in range(3):
            self.reverb_send(part, 0.0)
            self.delay_send(part, 0.0)

    def fx_switch(self, on: bool):
        self.m.write(self.switches + SYNTHFX_MOD, bytes([1 if on else 0]))

    def fx(self, part, values):
        vals = list(INIT)
        for k, v in values.items():
            vals[k] = v
        self.m.write(self.settings + 40 * part, struct.pack("<10f", *vals))


def note_for(engine):
    """Note 60, but PERC's note 60 is the same on both sides and its note 48 isn't, so PERC plays 48:
    a mistake that loses the right side can show."""
    return 48 if engine == PERC else 60


def play(axf, engine, on=True, blocks=30, at=0, slots=(), rig=None, voices=None):
    """One part on engine, its note held; slots [(slot, name, knobs)] picked before block `at`."""
    r = rig or Rig(axf, on=on)
    r.set_instrument(0, engine)
    if voices:
        r.set_voices(0, voices)
    r.note(0, note_for(engine), True)
    out_l, out_r = r.blocks(at) if at else ([], [])
    for slot, name, knobs in slots:
        r.fx(0, slot, name, knobs)
    left, right = r.blocks(blocks - at)
    return r, (out_l + left, out_r + right)


def rules_ok(r, heap0=None):
    return (not r.failures and r.malloc_in_block == 0 and r.interrupts["disable"] == r.interrupts["enable"]
            and (heap0 is None or r.heap_brk == heap0))


def max_diff(a, b):
    return max((abs(x - y) for x, y in zip(a, b)), default=0)


def stereo(left, right):
    """The sides differ by more than the master's rounding (a stereo part that lost its right side
    still differs by 1 in a few samples; PERC's note 48 differs by over 1,000)"""
    return max_diff(left, right) > 16


def first_change(x, dry, at):
    """The first block from `at` where x leaves the dry sound"""
    for b in range(at, len(dry) // BLOCK):
        if x[b * BLOCK:(b + 1) * BLOCK] != dry[b * BLOCK:(b + 1) * BLOCK]:
            return b
    return None


def onset(x, ref, b):
    """The first 4 samples from where x leaves ref in block b, as their largest difference, and that
    sample's place in the block. The output path delays the part by a few samples (5 on ACD,
    2026-11-11), so a block's first samples are still the one before it."""
    if b is None:
        return 0, None
    s = next((i for i in range(b * BLOCK, (b + 1) * BLOCK) if x[i] != ref[i]), b * BLOCK)
    return max_diff(x[s:s + 4], ref[s:s + 4]), s - b * BLOCK


def jump(x):
    """The biggest step from one sample to the next"""
    return max((abs(x[i + 1] - x[i]) for i in range(len(x) - 1)), default=0)


def blk(x, b, n=1):
    return x[b * BLOCK:(b + n) * BLOCK]


# A pick made before block `at` first shows in the master output at block at + LATENCY: the rig's
# path from the synth to the output, the same for every engine and effect (2026-11-09).
LATENCY = 3


def config(mods):
    """Config -> Mods sets the default for new projects, Project Settings -> Mods the project's switch"""
    m = Machine(mods)
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    for group, array in (("menuMods", "_ZN4mods8defaultsE"), ("menuProjectMods", "_ZN4mods8switchesE")):
        item = menu_item_named(m, group, "Synth FX")
        check(m.r32(m.r32(item + 16)) == m.addr(array) + SYNTHFX_MOD, f"{group}: Synth FX is mod {SYNTHFX_MOD}'s "
                                                                  f"{'default' if 'Project' not in group else 'switch'}")


def off_is_ce(mods, ref):
    for e, name in ENGINES.items():
        rr = SoundRig(ref)
        rr.set_instrument(0, e)
        for part in range(3):
            rr.reverb_send(part, 0.0)
            rr.delay_send(part, 0.0)
        rr.note(0, note_for(e), True)
        want = rr.blocks(25)
        r, off = play(mods, e, on=False, blocks=25,
                      slots=[(0, "Distortion", (100, 50, 0)), (1, "Phaser", (50, 50, 50, 50))])
        check(off == want and peak(want[0]) > 100 and rules_ok(r) and r.instances() == 2,
              f"{name}: Synth FX Off with Distortion and Phaser in the slots sounds as the reference build, "
              f"sample for sample (the plugins made, {r.instances()}, but bypassed)")
        r, none = play(mods, e, on=True, blocks=25)
        check(none == want and r.instances() == 0,
              f"{name}: Synth FX On with both slots empty sounds as the reference build (no plugin made)")


def mix_zero(mods):
    for e, name in ENGINES.items():
        _, dry = play(mods, e, on=False, blocks=25)
        for fname in ("Chorus", "Flanger", "Phaser"):
            knobs = PACK[fname][:-1] + (0,)
            r, got = play(mods, e, blocks=25, at=5, slots=[(1, fname, knobs)])
            check(got == dry and r.instance(0, 1) >= 0 and rules_ok(r),
                  f"{name}, {fname} at Mix 0: the sound as without it, sample for sample ("
                  f"{'stereo' if stereo(*dry) else 'mono'} dry)")


def as_before(mods, before):
    """A version 1 project's effects through the plugins, against the phase 1 build's built-in ones"""
    if before is None:
        check(False, "as_before needs --before <the phase 1 build>")
        return
    # The built-in Distortion ramped in over about 8 blocks from its pick; the plugin is on from its
    # first sample (the slot fades it in over one block), so they differ at first and the difference
    # dies away with the effect's filters (2026-11-11): the Overdrive's within about 55 blocks, CHIP's
    # Bit Crusher about 72. Behind a Phaser at Feedback 90 % it rings on: within the last bit 105 to
    # 162 blocks after the pick, and a last-bit difference stays as long as the note; PERC's drum
    # has decayed to about 125 by then. The largest difference in the transient can be up to the sum
    # of the two (opposite signs), so it is held to twice the sound's peak, not more.
    at = 10
    for e, name in ENGINES.items():
        for what, old, new in AS_BEFORE:
            feedback = "Phaser" in what and "Feedback" in what
            n, settle, heard = (200, 170, 100) if feedback else (110, 80, 500)
            o = OldRig(before)
            o.fx_switch(True)
            o.set_instrument(0, e)
            o.note(0, note_for(e), True)
            o.blocks(at)
            o.fx(0, old)
            want = o.blocks(n - at)
            r, got = play(mods, e, blocks=n, at=at, slots=new)
            got = (got[0][at * BLOCK:], got[1][at * BLOCK:])
            diffs = [max(max_diff(blk(got[0], b), blk(want[0], b)), max_diff(blk(got[1], b), blk(want[1], b)))
                     for b in range(n - at)]
            # the first block from which they agree to the end: within the last bit, and exactly
            close_from = same_from = n - at
            while close_from > 0 and diffs[close_from - 1] <= 1:
                close_from -= 1
            while same_from > 0 and diffs[same_from - 1] == 0:
                same_from -= 1
            sounding = peak(blk(want[0], min(close_from, n - at - 1), n - at - close_from))
            ok = (close_from <= settle and (same_from <= settle or feedback) and sounding > heard and
                  max(diffs) <= 2 * max(peak(want[0]), peak(want[1]), 1) and rules_ok(r))
            check(ok, f"{name}, {what}: as the built-in effect, within the last bit from block {close_from} after the "
                      f"pick, sample for sample from block {same_from} (plan: by {settle}); the sound there "
                      f"{sounding}, the largest difference before {max(diffs)}")


def each_effect(mods):
    at, n = 10, 40
    for e, name in ENGINES.items():
        _, dry = play(mods, e, on=False, blocks=n)
        dry_stereo = stereo(*dry)
        dry_run = clip_run(dry[0] + dry[1])[1]
        cases = [(f"{p} in FX1", [(0, p, PACK[p])]) for p in PACK]
        cases += [("Distortion in FX1, Phaser in FX2", [(0, "Distortion", PACK["Distortion"]),
                                                       (1, "Phaser", (50, 50, 90, 50))])]
        for what, slots in cases:
            r, got = play(mods, e, blocks=n, at=at, slots=slots)
            heap0 = r.heap_brk
            left, right = got
            b = first_change(left, dry[0], at)
            # no click: the block it starts in jumps no more than the dry sound before and the wet
            # sound after do, with a little room
            steady = max(jump(blk(dry[0], at, LATENCY)), jump(blk(left, b + 1, 6)) if b else 0)
            click = jump(left[b * BLOCK - 1:(b + 1) * BLOCK + 1]) if b else 0
            # faded in: its first samples hold little of the change the next blocks have (a 128th of
            # it at the first; without the fade, all of it)
            start, where = onset(left, dry[0], b)
            later = max_diff(blk(left, b + 1, 4), blk(dry[0], b + 1, 4)) if b else 0
            run = clip_run(left + right)[1]
            tail = (at + LATENCY + 4) * BLOCK
            wet = stereo(left[tail:], right[tail:])
            widens = any(p in WIDENS for _, p, _ in slots)
            image_ok = wet if widens else wet == dry_stereo
            ok = (b == at + LATENCY and click <= 1.25 * steady + 64 and start <= 0.05 * later + 16 and
                  run <= max(dry_run, 7) and image_ok and rules_ok(r, heap0))
            check(ok, f"{name}, {what}: changes from block {b}, fading in (its first samples, from {where}, {start} from dry, "
                      f"{later} later), its largest step there {click} (steady {steady}); "
                      f"{'stereo' if wet else 'mono'} (dry {'stereo' if dry_stereo else 'mono'}); longest clip run "
                      f"{run} (dry {dry_run}) {r.failures[:1]}")


def order_and_swap(mods):
    at, n = 8, 36
    for e in (1, PERC):
        name = NAMES[e]
        _, a = play(mods, e, blocks=n, at=at, slots=[(0, "Overdrive", (100, 50, 0)), (1, "Chorus", PACK["Chorus"])])
        _, b = play(mods, e, blocks=n, at=at, slots=[(0, "Chorus", PACK["Chorus"]), (1, "Overdrive", (100, 50, 0))])
        tail = (at + LATENCY + 4) * BLOCK
        # which runs first: the plugins' process calls in a block, by the slot each plugin is in
        r = Rig(mods)
        calls = []
        r.watch(sym_containing(r.syms, "plugins", "7process").addr, lambda mm: calls.append(mm.uc.reg_read(UC_ARM_REG_R0)))
        r.m.uc.ctl_flush_tb()
        r.set_instrument(0, e)
        r.note(0, note_for(e), True)
        r.fx(0, 0, "Overdrive", (100, 50, 0))
        r.fx(0, 1, "Chorus", PACK["Chorus"])
        fx1, fx2 = r.instance(0, 0), r.instance(0, 1)
        r.blocks(3)
        order = calls[-2:]
        check(a[0][tail:] != b[0][tail:] and stereo(a[0][tail:], a[1][tail:]) and stereo(b[0][tail:], b[1][tail:])
              and order == [fx1, fx2] and fx1 != fx2,
              f"{name}: FX1 runs before FX2 (a block runs plugin {order}, FX1's {fx1}, FX2's {fx2}; Overdrive into "
              f"Chorus differs from Chorus into Overdrive), both stereo")

        # a swap mid-note: Overdrive -> Distortion in FX1, then Distortion -> None
        r = Rig(mods)
        r.set_instrument(0, e)
        r.note(0, note_for(e), True)
        r.blocks(4)
        r.fx(0, 0, "Overdrive", (100, 50, 0))
        x = r.blocks(16)
        first = r.instance(0, 0)
        r.fx(0, 0, "Distortion", (100, 50, 0))
        loading = r.instance(0, 0)
        y = r.blocks(1)                     # the Overdrive fades out
        r.update()                          # freed, the Distortion made
        second, count = r.instance(0, 0), r.instances()
        z = r.blocks(16)
        r.fx(0, 0)                          # cleared
        w = r.blocks(1)
        r.update()
        gone, after = r.instance(0, 0), r.instances()
        v = r.blocks(10)
        left = x[0] + y[0] + z[0] + w[0] + v[0]
        steady = max(jump(left[8 * BLOCK:16 * BLOCK]), jump(left[24 * BLOCK:33 * BLOCK]),
                     jump(left[(16 + 1 + 16 + 1 + 4) * BLOCK:]))
        swap = jump(left[14 * BLOCK:(16 + 1 + 6) * BLOCK])
        clear = jump(left[(16 + 1 + 14) * BLOCK:(16 + 1 + 16 + 1 + 4) * BLOCK])
        ok = (first >= 0 and loading == -1 and second >= 0 and count == 1 and gone == -1 and after == 0 and
              swap <= 1.25 * steady + 64 and clear <= 1.25 * steady + 64 and rules_ok(r))
        check(ok, f"{name}: Overdrive swapped for Distortion mid-note: let go ({first} -> {loading} -> {second}, "
                  f"{count} made), largest step {swap}; cleared: let go ({gone}, {after} made), largest step {clear} "
                  f"(steady {steady})")


def schedule(axf, engine, steps):
    """One part on engine, Synth FX On: ("fx", slot, name, knobs) a pick (name None: cleared),
    ("blocks", n), ("update",) a main-loop pass, ("made",) to note the part's plugins made, ("note",) a
    new note (the Init notes die away within about 180 blocks), ("keep", n): n blocks kept. Returns the
    rig, the counts and the blocks kept."""
    r = Rig(axf)
    r.set_instrument(0, engine)
    n = note_for(engine)
    r.note(0, n, True)
    made, kept = [], ([], [])
    for step in steps:
        if step[0] == "fx":
            r.fx(0, step[1], step[2], step[3])
        elif step[0] == "blocks":
            r.blocks(step[1]) if step[1] else None
        elif step[0] == "update":
            r.update()
        elif step[0] == "made":
            made.append(r.instances())
        elif step[0] == "note":
            r.note(0, n, False)
            r.blocks(1)
            r.note(0, n, True)
        else:
            a, b = r.blocks(step[1])
            kept[0].extend(a)
            kept[1].extend(b)
    return r, made, kept


def release(mods):
    """Distortion and Phaser for 20 blocks, then cleared: the part lets them go, and a new note sounds
    as in a run that never had them, sample for sample."""
    for e in (1, PERC):
        name = NAMES[e]
        tail = [("blocks", 40), ("note",), ("keep", 40)]
        _, _, dry = schedule(mods, e, [("blocks", 34)] + tail)
        r, made, got = schedule(mods, e, [("blocks", 10), ("fx", 0, "Distortion", (100, 50, 0)),
                                          ("fx", 1, "Phaser", (50, 50, 90, 50)), ("blocks", 20), ("made",),
                                          ("fx", 0, None, ()), ("fx", 1, None, ()), ("blocks", 1), ("update",),
                                          ("made",), ("blocks", 3)] + tail)
        same = got == dry
        check(made == [2, 0] and same and peak(dry[0]) > 1000 and rules_ok(r),
              f"{name}: cleared, the plugins fade out and are let go ({made[0]} -> {made[1]} made); a new note then "
              f"sounds as without them, sample for sample ({same}, peak {peak(dry[0])})")
        # faded out: where the cleared run leaves one that kept them, its first samples hardly do
        picks = [("blocks", 10), ("fx", 0, "Distortion", (100, 50, 0)), ("fx", 1, "Phaser", (50, 50, 90, 50)),
                 ("blocks", 20)]
        _, _, kept = schedule(mods, e, picks + [("keep", 10)])
        _, _, cut = schedule(mods, e, picks + [("fx", 0, None, ()), ("fx", 1, None, ()), ("keep", 10)])
        b = first_change(cut[0], kept[0], 0)
        start, where = onset(cut[0], kept[0], b)
        later = max_diff(blk(cut[0], b + 1, 4), blk(kept[0], b + 1, 4)) if b is not None else 0
        check(b == LATENCY and start <= 0.05 * later + 16 and later > 500,
              f"{name}: cleared, the slots fade out from block {b} (its first samples, from {where}, {start} from the kept "
              f"sound, {later} later)")


def skipped_and_shared(mods):
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        slots = [(0, "Distortion", (100, 50, 0)), (1, "Phaser", (50, 50, 50, 50))]
        _, off = play(mods, e, on=False, blocks=25, at=5, slots=slots)
        r, on = play(mods, e, on=True, blocks=25, at=5, slots=slots)
        check(on == off, f"{name}: skipped, with Synth FX On and plugins in both slots it sounds as with Synth FX Off")
    # three parts, two plugins each
    for layout in (((1, 3), (7, 3), (8, 2)), ((WAVE, 3), (1, 3), (PERC, 2))):
        r = Rig(mods)
        for part in range(3):
            r.set_voices(part, 0)
        picks = (("Overdrive", "Chorus"), ("Distortion", "Flanger"), ("BitCrusher", "Phaser"))
        for part, (e, v) in enumerate(layout):
            r.set_instrument(part, e)
            r.set_voices(part, v)
            r.fx(part, 0, picks[part][0], PACK[picks[part][0]])
            r.fx(part, 1, picks[part][1], PACK[picks[part][1]])
            r.note(part, note_for(e) + 4 * part, True)
        left, _ = r.blocks(20)
        made = [int(r.instance(p, 0) >= 0) + int(r.instance(p, 1) >= 0) for p in range(3)]
        names = "+".join(NAMES.get(e, "WAVE") for e, _ in layout)
        check(made == [2, 2, 2] and peak(left) > 100 and rules_ok(r),
              f"{names}: every part's two plugins are made ({made}), the sound {peak(left)}")


def garbage(mods):
    bad = (NAN, float("inf"), -float("inf"), 1e30, -1e30, 7.5, -3.0)
    ranges = {"Distortion": ((0, 100), (0, 100), (-30, 6)), "Phaser": ((0, 100), (0, 100), (-95, 95), (0, 100))}
    for e in (1, PERC):
        name = NAMES[e]
        r = Rig(mods)
        r.set_instrument(0, e)
        r.note(0, note_for(e), True)
        r.blocks(5)
        kept, knobs_ok = ([], []), True
        for i, v in enumerate(bad):
            for slot, plugin in ((0, "Distortion"), (1, "Phaser")):
                knobs = [v if (k + i) % 2 == 0 else 50 for k in range(KNOBS)]
                r.fx(0, slot, plugin, knobs)
                r.blocks(1)
                r.update()
                got = r.knobs(0, slot)[:len(ranges[plugin])]
                knobs_ok = knobs_ok and all(math.isfinite(g) and lo <= g <= hi for g, (lo, hi) in zip(got, ranges[plugin]))
            a, b = r.blocks(8)
            kept[0].extend(a)
            kept[1].extend(b)
        worst = clip_run(kept[0] + kept[1], run=1)[1]
        r.fx(0, 0)
        r.fx(0, 1)
        r.blocks(1)
        r.update()
        made = r.instances()
        check(knobs_ok and made == 0 and worst < 8 * BLOCK and rules_ok(r),
              f"{name}: garbage knobs for {len(bad)} picks: each plugin made with its knobs on its steps and in its "
              f"range ({knobs_ok}), longest run at the rails {worst}; cleared, let go ({made} made)")


def rules_and_cost(mods):
    """The worst case the plan names: 3 parts sharing the 8 voices, each with the Distortion in FX1 and
    the Phaser in FX2, on a mono engine and on PERC; against the same with Synth FX Off."""
    res = {}
    for e in (1, PERC):
        for on in (False, True):
            r = Rig(mods, on=on)
            for part in range(3):
                r.set_voices(part, 0)
            for part, v in enumerate((3, 3, 2)):
                r.set_instrument(part, e)
                r.set_voices(part, v)
                r.fx(part, 0, "Distortion", (100, 50, 0))
                r.fx(part, 1, "Phaser", (50, 50, 90, 50))
                for k in range(v):
                    r.note(part, 48 + 12 * part + 4 * k, True)
            r.blocks(30)
            heap0 = r.heap_brk
            cost = r.count_open(lambda: r.blocks(4)) // 4
            left, right = r.blocks(20)
            res[e, on] = (cost, r.pool_peak(), rules_ok(r, heap0), peak(left), clip_run(left + right)[1])
    perc = res[PERC, False][1]
    for e in (1, PERC):
        name = NAMES[e]
        on, off = res[e, True], res[e, False]
        extra = on[0] - off[0]
        check(on[2] and off[2] and on[3] > 100,
              f"{name} x 3 parts, Distortion and Phaser: no malloc in the interrupt, heap held, interrupts "
              f"back on (peak {on[3]}, longest clip run {on[4]}, without the slots {off[4]})")
        check(on[1] <= perc,
              f"{name} x 3 parts: block pool peak {on[1]} with the slots, {off[1]} without; PERC's own, "
              f"without them, {perc}")
        check(extra <= BLOCK_BUDGET // 5,
              f"{name} x 3 parts: the slots cost {extra:,} instructions a block, the plugins' own included "
              f"({100 * extra / BLOCK_BUDGET:.1f} % of a block's {BLOCK_BUDGET:,}; plan: under 20 %)")


SECTIONS = ("config", "off_is_ce", "mix_zero", "as_before", "each_effect", "order_and_swap", "release",
            "skipped_and_shared", "garbage", "rules_and_cost")


def main() -> int:
    global ENGINES
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mods", type=Path)
    ap.add_argument("ref", type=Path)
    ap.add_argument("--before", type=Path, help="the phase 1 build (TrackerPlus_MT_p1.axf), for as_before")
    ap.add_argument("--only", choices=SECTIONS)
    ap.add_argument("--engines", help="engine ids for sections 1 to 4, e.g. 1,6")
    a = ap.parse_args()
    if a.engines:
        ENGINES = {int(e): NAMES[int(e)] for e in a.engines.split(",")}
    for name in SECTIONS:
        if a.only in (None, name):
            fn = globals()[name]
            if name == "off_is_ce":
                fn(a.mods, a.ref)
            elif name == "as_before":
                fn(a.mods, a.before)
            else:
                fn(a.mods)
    print(f"{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
