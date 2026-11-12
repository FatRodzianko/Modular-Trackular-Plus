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
7. Three parts with two plugins each all run, a WAVE part among them.
8. Garbage knobs in the slots (NaN, infinities, out of range): each plugin is made with its knobs on its
   steps and within its range, and the sound stays bounded; cleared, the part sounds as without them.
9. The rules: no malloc in the audio interrupt, the heap held, interrupts turned back on, the block pool
   no fuller than with CE's own stereo engine (PERC) on all three parts, and the slots' cost per block
   in instructions, the plugins' own code included (the closed engines' own aren't counted).
10. WAVE and PHASE on the slots: with Synth FX Off they are dry, sample for sample, whatever the slots
   hold; On, the slots play. Their parts go in mono (the sides are alike): FX1 runs on one channel
   where a stereo engine's runs on two.
11. Old patches (--before, the phase 1 build, where WAVE and PHASE had the effects inside): a patch's
   old Effect and Distortion (parameters 62-71) are moved into the part's slots by the main loop,
   once, with Synth FX On or Off, and the patch's own set to None and Off; not while the file manager
   is busy. Then the part sounds as the phase 1 build did, within the last bit (Placement Pre on WAVE
   included: FX1 Pre; on PHASE, Post).
12. Placement Pre on WAVE: one instance for each voice, each voice its own, reset when a voice takes
   its seat and not otherwise, the slot not run on the part as well; Pre goes through the voice's
   filter and Post doesn't; on another engine a Pre slot runs as Post, sample for sample; a knob edit
   reaches every voice's instance; all 8 voices with two Pre slots, and 3 WAVE parts 3 + 3 + 2, under
   the rules, and their cost.
13. The guards and a user's plugin, with the device test's files (build/device/synthfx_card, from
   tools/synthfx/device/build.sh) on the card and a clock that counts the plugins' instructions as
   cycles: GuardSlow at Light keeps running, at Heavy is bypassed after 8 blocks over a quarter block,
   the sound untouched either way; the SDK's Delay echoes, under the rules; with the marker on the
   card at start a project's plugins aren't made, a user's pick is, and safe mode ends 5 s after it;
   the marker is on the card before a new plugin's first block (GuardHang's, which never returns).

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
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0  # noqa: E402

from machine import Machine  # noqa: E402
from sound_rig import BLOCK, BLOCK_MS, SoundRig, peak  # noqa: E402
from test_step0 import menu_item_named  # noqa: E402
from test_synths import BLOCK_BUDGET, CHORD, PHASE, WAVE, clip_run  # noqa: E402
from test_synths import Rig as SynthsRig  # noqa: E402

NAMES = {1: "ACD", 2: "FAT", 4: "VAP", 5: "WTFM", 6: "PERC", 7: "DIRT", 8: "CHIP"}
ENGINES = dict(NAMES)               # sections 1 to 4 (--engines)
PERC = 6
SYNTHFX_MOD = 15                    # Mod::synthfx
FILE_SIZE, KNOBS = 48, 12           # synthfxMod.h: a slot's file name and knobs
PRE_BYTE = FILE_SIZE + 4 * KNOBS    # the slot's Placement: 1 Pre (synthfxMod.h's Slot)
SLOT_BYTES = 100                    # the Slot, padded
OLD = 62                            # a WAVE or PHASE patch's old effects: parameters 62 to 71, EFFECT.. order
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

    def set_slot(self, part, slot, name=None, knobs=(), pre=False):
        """The slot's setting, as the page's pick sets it, without making it"""
        at = self.slots + (part * 2 + slot) * SLOT_BYTES
        file = f"{name}.ptsfx".encode() if name else b""
        self.m.write(at, file.ljust(FILE_SIZE, b"\0"))
        vals = list(knobs) + [NAN] * (KNOBS - len(knobs))
        self.m.write(at + FILE_SIZE, struct.pack("<12f", *vals))
        self.m.write(at + PRE_BYTE, bytes([1 if pre else 0]))

    def slot(self, part, slot):
        """The slot's setting: its plugin's name (None: empty), knobs, Placement Pre"""
        at = self.slots + (part * 2 + slot) * SLOT_BYTES
        file = self.m.read(at, FILE_SIZE).split(b"\0")[0].decode("latin-1")
        return (file[:-6] if file.endswith(".ptsfx") else (file or None)), self.knobs(part, slot), \
            self.m.read(at + PRE_BYTE, 1)[0] != 0

    def fx(self, part, slot, name=None, knobs=(), pre=False):
        """A slot set and made again, as the page's pick: an empty slot takes its plugin now; one
        that had a plugin lets it fade out over the next block and takes the new one at the next
        update"""
        self.set_slot(part, slot, name, knobs, pre)
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


def shared(mods):
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


def calls_of(r):
    """Every plugins::process call from now, as (instance, channels), and every plugins::reset's instance"""
    calls, resets = [], []
    r.watch(sym_containing(r.syms, "plugins", "7process").addr,
            lambda mm: calls.append((mm.arg(0), mm.arg(3))))
    r.watch(sym_containing(r.syms, "plugins", "5reset").addr, lambda mm: resets.append(mm.arg(0)))
    r.m.uc.ctl_flush_tb()
    return calls, resets


def wave_off_and_mono(mods):
    """WAVE and PHASE: Off is dry, On the slots play, and the part goes in mono"""
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        slots = [(0, "Distortion", (100, 50, 0)), (1, "Phaser", (50, 50, 50, 50))]
        _, dry = play(mods, e, on=False, blocks=25)
        r, off = play(mods, e, on=False, blocks=25, at=5, slots=slots)
        made_off = r.instances()
        r = Rig(mods, on=False)                     # Pre in FX1, as an old WAVE patch converts to
        r.set_instrument(0, e)
        r.fx(0, 0, "Distortion", (100, 50, 0), pre=True)
        r.note(0, note_for(e), True)
        off_pre = r.blocks(25)
        check(off == dry and off_pre == dry and peak(dry[0]) > 100 and made_off == 2 and rules_ok(r),
              f"{name}: Synth FX Off with Distortion and Phaser in the slots (FX1 Post, then Pre) sounds dry, "
              f"sample for sample, as CE's engines do (the plugins made, {made_off}, but bypassed)")
        r, on = play(mods, e, blocks=25, at=5, slots=slots)
        tail = (5 + LATENCY + 4) * BLOCK
        check(on[0][tail:] != dry[0][tail:] and stereo(on[0][tail:], on[1][tail:]) and not stereo(*dry)
              and rules_ok(r), f"{name}: Synth FX On, the slots play (the Phaser makes the part stereo; dry, "
                               f"its sides are alike)")
    # in mono: FX1's process calls take one channel on WAVE and PHASE, two on PERC
    for e, name, want in ((WAVE, "WAVE", 1), (PHASE, "PHASE", 1), (PERC, "PERC", 2)):
        r = Rig(mods)
        calls, _ = calls_of(r)
        r.set_instrument(0, e)
        r.note(0, note_for(e), True)
        r.fx(0, 0, "Distortion", (100, 50, 0))
        r.fx(0, 1, "Chorus", PACK["Chorus"])
        r.blocks(2)
        del calls[:]
        r.blocks(4)
        fx1, fx2 = r.instance(0, 0), r.instance(0, 1)
        ch1 = sorted({c for i, c in calls if i == fx1})
        ch2 = sorted({c for i, c in calls if i == fx2})
        check(ch1 == [want] and ch2 == [want], f"{name}: FX1 runs on {ch1} channels, FX2 (Chorus, after a mono "
                                               f"FX1) on {ch2} (want {want} for both)")


def old_patch(r, part, values):
    """A WAVE or PHASE patch's old effects (parameters 62 to 71), as loading an old patch sets them"""
    for k, v in values.items():
        r.set_param(part, OLD + k, v)


def busy_byte(r):
    """cFileManager's status (its getStatus is inline): the first byte of newFileManager that, set to 1,
    makes convertOldPatches stop at once (found by what it does, as sound_rig finds the project path)"""
    fm = r.m.sym("newFileManager")
    conv = r.m.addr("_ZN4mods7synthfx17convertOldPatchesEv")
    idle = r.count_open(lambda: r.m.call(conv, max_insns=10_000_000))
    for at in range(fm.addr, fm.addr + fm.size):
        was = r.m.read(at, 1)
        r.m.write(at, b"\x01")
        n = r.count_open(lambda: r.m.call(conv, max_insns=10_000_000))
        r.m.write(at, was)
        if n * 4 < idle:
            return at
    return None                         # it never waits (a fault: the check below fails)


# The old effects a WAVE or PHASE patch could have (AS_BEFORE's, and Placement Pre), and the slots each
# becomes: [(slot, name, knobs, Pre on WAVE)]
OLD_PATCHES = tuple((what, old, [(s, n, k, False) for s, n, k in new]) for what, old, new in AS_BEFORE) + (
    ("Distortion Pre, Gain 100 %, Tone 30 %", {DIST: 2, GAIN: 1.0, TONE: 0.3, PLACEMENT: 0.0},
     [(0, "Distortion", (100, 30, 0), True)]),
    ("Bit Crusher Pre and Chorus", {DIST: 3, GAIN: 0.5, PLACEMENT: 0.0, EFFECT: 1},
     [(0, "BitCrusher", (50, 50, 0), True), (1, "Chorus", (50, 50, 50), False)]),
)


def old_patches(mods, before):
    """An old WAVE or PHASE patch's effects, moved into the slots, against the phase 1 build's own"""
    conv = None
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        # once, with Synth FX On or Off; the patch's own set to None and Off; not while the file manager is busy
        for on in (True, False):
            r = Rig(mods, on=on)
            busy = busy_byte(r)
            r.set_instrument(0, e)
            conv = []
            r.watch(r.m.addr("_ZN4mods7synthfx12fromOldPatchEiPKfb"), lambda mm: conv.append(mm.arg(0)))
            r.m.uc.ctl_flush_tb()
            old_patch(r, 0, {DIST: 2, GAIN: 1.0, EFFECT: 3, FEEDBACK: 0.9})
            if busy is not None:
                r.m.write(busy, b"\x01")
            r.update()
            waited = (len(conv), r.slot(0, 0)[0])
            if busy is not None:
                r.m.write(busy, b"\x00")
            r.update()
            r.update()
            r.update()
            got = [r.slot(0, s)[0] for s in (0, 1)]
            cleared = (r.get_param(0, OLD + EFFECT), r.get_param(0, OLD + DIST))
            made = r.instances()
            check(waited == (0, None) and conv == [0] and got == ["Distortion", "Phaser"] and cleared == (0.0, 0.0)
                  and made == 2 and rules_ok(r),
                  f"{name}, Synth FX {'On' if on else 'Off'}: an old patch's Distortion and Phaser move into FX1 and "
                  f"FX2 ({got}), once ({len(conv)} conversions in three passes), the patch's own set to None and Off "
                  f"({cleared}); not while the file manager is busy ({waited[0]} then); {made} plugins made")
        if before is None:
            check(False, "old_patches needs --before <the phase 1 build>")
            continue
        for what, old, new in OLD_PATCHES:
            # as as_before: a Phaser at Feedback 90 % after a Distortion rings on for longer
            feedback = "Phaser" in what and "Feedback" in what
            n, settle, heard = (200, 170, 100) if feedback else (110, 80, 500)
            o = OldRig(before)
            o.set_instrument(0, e)
            old_patch(o, 0, old)
            o.note(0, note_for(e), True)
            want = o.blocks(n)
            r = Rig(mods)
            r.set_instrument(0, e)
            old_patch(r, 0, old)
            r.update()                              # converted
            r.update()                              # the slots made
            slots = [r.slot(0, s) for s in (0, 1)]
            expect = [(None, False)] * 2
            for s, pname, _, pre in new:
                expect[s] = (pname, pre and e == WAVE)
            knobs_ok = all(all(abs(a - b) < 0.5 for a, b in zip(slots[s][1], k)) for s, _, k, _ in new)
            r.note(0, note_for(e), True)
            got = r.blocks(n)
            diffs = [max(max_diff(blk(got[0], b), blk(want[0], b)), max_diff(blk(got[1], b), blk(want[1], b)))
                     for b in range(n)]
            close_from = n
            while close_from > 0 and diffs[close_from - 1] <= 1:
                close_from -= 1
            sounding = peak(blk(want[0], min(close_from, n - 1), n - close_from))
            placed = [(sl[0], sl[2]) for sl in slots]
            ok = (placed == expect and knobs_ok and close_from <= settle and sounding > heard and
                  max(diffs) <= 2 * max(peak(want[0]), peak(want[1]), 1) and rules_ok(r))
            check(ok, f"{name}, {what}: converted to {placed} (knobs {'as' if knobs_ok else 'NOT as'} the patch's), "
                      f"sounds as the phase 1 build within the last bit from block {close_from} (plan: by {settle}); the "
                      f"sound there {sounding}, the largest difference before {max(diffs)}")


# WAVE with a sine on both oscillators into a low Cutoff (test_synths_dsp.py's Pre against Post): a
# Distortion before the filter is filtered, one after it isn't
WAVE_LOW = {12: 500.0, 13: 0.0, 14: 0.0, 2: 0.0, 0: 0.0, 1: 0.0, 3: 0.0}


def brightness(x):
    """The sound's slope against its level: more of it high up, more slope"""
    d = [x[i + 1] - x[i] for i in range(len(x) - 1)]
    return math.sqrt(sum(v * v for v in d) / len(d)) / max(math.sqrt(sum(v * v for v in x) / len(x)), 1e-9)


def pre(mods):
    """Placement Pre on WAVE: a Distortion in each voice, before its filter"""
    # each voice its own instance, reset when it takes its seat, the slot not run on the part as well
    r = Rig(mods)
    calls, resets = calls_of(r)
    for part in (1, 2):                 # all 8 voices to part 0, so the notes don't take each other's
        r.set_voices(part, 0)
    r.set_instrument(0, WAVE)
    r.set_voices(0, 8)
    r.fx(0, 0, "Distortion", (100, 50, 0), pre=True)
    r.fx(0, 1, "Phaser", (50, 50, 50, 50))
    made = r.instances()
    phaser = r.instance(0, 1)
    del resets[:]
    for n in (60, 64, 67):
        r.note(0, n, True)
    per_block = []
    for b in range(6):
        del calls[:]
        r.blocks(1)
        per_block.append(([i for i, c in calls if i != phaser], sum(1 for i, _ in calls if i == phaser)))
    first_resets = len(resets)
    r.note(0, 71, True)
    del calls[:]
    r.blocks(3)
    four = sorted({i for i, _ in calls if i != phaser})
    seats = [sorted(ids) for ids, _ in per_block[1:]]
    ok = (made == 9 and all(len(s) == 3 and len(set(s)) == 3 for s in seats) and all(s == seats[0] for s in seats)
          and all(p == 1 for _, p in per_block[1:]) and first_resets == 3 and len(resets) == 4 and len(four) == 4
          and set(seats[0]) < set(four) and rules_ok(r))
    check(ok, f"WAVE, FX1 Distortion Pre: {made} instances made (8 and the Phaser's 1); 3 voices run 3 of their own "
              f"each block ({seats[0]}, the same each block: {all(s == seats[0] for s in seats)}), the Phaser once "
              f"({[p for _, p in per_block]}); reset {first_resets} times for 3 notes over 6 blocks, "
              f"{len(resets) - first_resets} more for a 4th ({four})")
    # a knob edit (as the page's) reaches every voice's instance
    k_step = sym_containing(r.syms, "plugins", "11setKnobStep").addr
    k_get = sym_containing(r.syms, "plugins", "8knobStep").addr
    r.m.call(k_step, r.instance(0, 0), 0, 20)
    r.m.call(r.m.addr("_ZN4mods7synthfx10knobEditedEiim"), 0, 0, 0)
    steps = [r.m.call(k_get, i, 0) for i in four]
    check(steps == [20] * 4, f"WAVE, Pre: a knob edit on FX1 reaches every voice's instance (Gain steps {steps})")

    # Pre goes through the voice's filter, Post doesn't
    outs = {}
    for place in ("dry", "Pre", "Post"):
        r = Rig(mods)
        r.set_instrument(0, WAVE)
        for pid, v in WAVE_LOW.items():
            r.set_param(0, pid, v)
        if place != "dry":
            r.fx(0, 0, "Distortion", (100, 100, 0), pre=place == "Pre")
        r.note(0, 48, True)
        outs[place] = r.blocks(40)[0][20 * BLOCK:]
    # 0.106 and 0.152 on 2026-11-11 (dry 0.035): run on the part as well as in the voices, Pre is Post
    bp, bq, bd = brightness(outs["Pre"]), brightness(outs["Post"]), brightness(outs["dry"])
    check(bq > 1.25 * bp and bp > 1.5 * bd and peak(outs["Pre"]) > 100,
          f"WAVE: Pre goes through the filter (brightness {bp:.3f}), Post doesn't ({bq:.3f}); dry {bd:.3f}")

    # on another engine a Pre slot runs as Post
    for e in (1, PERC):
        got = {}
        for p in (False, True):
            r = Rig(mods)
            r.set_instrument(0, e)
            r.note(0, note_for(e), True)
            r.blocks(4)
            r.fx(0, 0, "Distortion", (100, 50, 0), pre=p)
            got[p] = (r.blocks(30), r.instances())
        check(got[True][0] == got[False][0] and got[True][1] == 8 and got[False][1] == 1,
              f"{NAMES[e]}: a Pre slot runs on the part as Post, sample for sample ({got[True][1]} instances made, "
              f"{got[False][1]} for Post)")

    # all 8 voices, and 3 WAVE parts 3 + 3 + 2: the rules, and the cost against Synth FX Off
    cases = (("8 voices, Distortion Pre and Bit Crusher Pre", (8, 0, 0),
              (("Distortion", (100, 50, 0), True), ("BitCrusher", (50, 50, 0), True))),
             ("3 + 3 + 2, each Distortion Pre and Phaser", (3, 3, 2),
              (("Distortion", (100, 50, 0), True), ("Phaser", (50, 50, 90, 50), False))))
    for what, layout, picks in cases:
        res = {}
        for on in (False, True):
            r = Rig(mods, on=on)
            used = [p for p in range(3) if layout[p]]
            for part in range(3):
                if not layout[part]:
                    r.set_voices(part, 0)
            for part in used:
                r.set_instrument(part, WAVE)
                r.set_voices(part, layout[part])
                for slot, (pname, knobs, p) in enumerate(picks):
                    r.fx(part, slot, pname, knobs, pre=p)
                for n in (CHORD if len(used) == 1 else CHORD[:layout[part]]):
                    r.note(part, n + 12 * part - 12, True)
            r.blocks(10)
            heap0 = r.heap_brk
            cost = r.count_open(lambda: r.blocks(4)) // 4
            left, right = r.blocks(20)
            res[on] = (cost, r.pool_peak(), rules_ok(r, heap0), peak(left), clip_run(left + right)[1], r.instances())
        on, off = res[True], res[False]
        extra = on[0] - off[0]
        check(on[2] and on[3] > 100 and on[1] <= max(off[1], 74) and on[4] <= max(off[4], 7),
              f"WAVE, {what}: {on[5]} instances; no malloc in the interrupt, heap held, interrupts back on (peak "
              f"{on[3]}, longest clip run {on[4]}, Off {off[4]}; block pool peak {on[1]}, Off {off[1]})")
        check(extra <= BLOCK_BUDGET // 4,
              f"WAVE, {what}: the slots cost {extra:,} instructions a block, the plugins' own included "
              f"({100 * extra / BLOCK_BUDGET:.1f} % of a block's {BLOCK_BUDGET:,}; plan: under 25 %)")


# ---- 13. the guards and a user's plugin, before the unit (DEVICE_TEST_LADDER.md's plugin set) ----
GUARD_CARD = Path(__file__).resolve().parents[2] / "build" / "device" / "synthfx_card" / "SynthFX"
ACD = 1
MARKER = "/SynthFX/.starting"
CYCLES_PER_US = 600                 # the unit's 600 MHz core
BUDGET = CYCLES_PER_US * (2902 // 4)  # synthfxPluginsCe.cpp: a quarter of a block
EMPTY, LOADING, RUNNING, BYPASSED, FAILED = range(5)   # synthfxMod.h's SlotState
SKIPPED = 7                         # plugins::Status::skipped


class GuardRig(Rig):
    """The Rig with files on the card before the mods start (the marker among them, if asked) and a
    cycle clock that counts the plugins' own instructions, an instruction as a cycle, so the time
    guard reads what it reads on the unit; the firmware's code adds nothing to it."""

    def __init__(self, axf, files=None, marker=False):
        self.pool_insns = 0
        self._card = dict(files or {})
        if marker:
            self._card[MARKER] = b"1"
        super().__init__(axf)
        m = self.m
        lo, hi = self.code_pool
        self.hook_range(lo, hi, lambda uc, x, sz, u: setattr(self, "pool_insns", self.pool_insns + 1))
        m.uc.ctl_flush_tb()
        self._state = m.addr("_ZN4mods7synthfx9slotStateEii")
        self._status = m.addr("_ZN4mods7synthfx10slotStatusEii")
        self._safe = sym_containing(self.syms, "synthfx", "inSafeMode").addr   # safeMode() is not linked

    def _fake_timer(self):
        super()._fake_timer()
        m = self.m
        vt = m.r32(self.timer)          # Timer's slots: millis, micros, delay, clockCycles, clockCyclesPerUs
        m.w32(vt + 4 * 3, m.add_stub(lambda mm: (int(self.now * 1000 * CYCLES_PER_US) + self.pool_insns)
                                     & 0xFFFFFFFF))
        m.w32(vt + 4 * 4, m.add_stub(lambda mm: CYCLES_PER_US))

    def _fake_platform(self):
        super()._fake_platform()
        self.sd.files.update(self._card)

    def from_project(self, part, slot, name, knobs=()):
        """A project's slot, as a load sets it: safe mode may skip it"""
        self.set_slot(part, slot, name, knobs)
        self.m.call(self._remake, part, slot, 1, max_insns=50_000_000)
        self.m.uc.ctl_flush_tb()

    def state(self, part, slot) -> int:
        return self.m.call(self._state, part, slot) & 0xFF

    def status(self, part, slot) -> int:
        return self.m.call(self._status, part, slot) & 0xFF

    def safe_mode(self) -> bool:
        return self.m.r8(self._safe) != 0


def card_files(*names):
    return {f"/SynthFX/{n}.ptsfx": (GUARD_CARD / f"{n}.ptsfx").read_bytes() for n in names}


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def guards(mods):
    """13. The time guard, the start marker and safe mode, and the SDK's Delay as a user's plugin, on
    the real firmware, with the files the device test puts on the card"""
    if not GUARD_CARD.is_dir():
        check(False, f"guards: {GUARD_CARD} (tools/synthfx/device/build.sh) is there")
        return
    files = card_files("GuardSlow", "GuardHang", "Delay")

    # The time guard: GuardSlow passes the sound through, so a part with it sounds as without it,
    # running or bypassed; Light stays under the budget, Heavy is bypassed after 8 blocks over it
    def slow(load):
        r = GuardRig(mods, files)
        r.set_instrument(0, ACD)
        r.note(0, 60, True)
        out = list(r.blocks(4)[0])
        if load is not None:
            r.fx(0, 0, "GuardSlow", (load,))
        costs, states = [], []
        for _ in range(24):
            n0 = r.pool_insns
            out += r.block()[0]
            r.update()
            costs.append(r.pool_insns - n0)
            states.append(r.state(0, 0))
        return r, out, costs, states

    _, dry, _, _ = slow(None)
    r, light, costs, states = slow(0)
    turn = max(costs) / 50_000
    check(light == dry and all(s == RUNNING for s in states[1:]) and 0 < max(costs) < BUDGET,
          f"GuardSlow Light: keeps running and passes the part through, sample for sample; "
          f"{max(costs):,} instructions a block of the budget's {BUDGET:,} ({turn:.1f} a turn), states {states[:4]}")
    r, heavy, costs, states = slow(1)
    over = [c > BUDGET for c in costs]
    first = over.index(True) if any(over) else None
    ran = sum(over)
    check(heavy == dry and first is not None and ran == 8 and not any(over[first + 8:])
          and all(s == BYPASSED for s in states[first + 8:]) and r.instances() == 1,
          f"GuardSlow Heavy: over the budget for 8 blocks in a row ({ran}, from block {first}), then bypassed "
          f"(states {states[first + 7:first + 10] if first is not None else states}) and not run, the sound "
          f"untouched (sample for sample as without it: {heavy == dry}), the plugin kept ({r.instances()})")

    # The SDK's Delay, from the card as a user's file: echoes after the note, the part made stereo
    def delay(on):
        r = GuardRig(mods, files)
        r.set_instrument(0, ACD)
        r.note(0, 60, True)
        r.blocks(2)
        if on:
            r.fx(0, 1, "Delay", (180, 60, 50))      # Time 180 ms, Feedback 60 %, Mix 50 %
        left, right = r.blocks(8)
        r.note(0, 60, False)
        heap0 = r.heap_brk
        cost = r.count_open(lambda: r.blocks(4)) // 4
        tail_l, tail_r = r.blocks(150)
        return r, left + tail_l, right + tail_r, cost, heap0

    r0, dl, dr, dcost, _ = delay(False)
    r1, wl, wr, wcost, heap0 = delay(True)
    late = slice(60 * BLOCK, 150 * BLOCK)           # 0.2 s on: the note's own release has gone
    check(rms(wl[late]) > 4 * rms(dl[late]) + 20 and stereo(wl, wr) and not stereo(dl, dr)
          and rules_ok(r1, heap0) and peak(wl + wr) < 32767 and r1.status(0, 1) == 0,
          f"Delay (synthfx-sdk/examples) from the card: echoes after the note (rms {rms(wl[late]):.0f} "
          f"against {rms(dl[late]):.0f} without it), the part stereo, under the rules, no clipping")
    check(wcost - dcost <= BLOCK_BUDGET // 4,
          f"Delay: {wcost - dcost:,} instructions a block, its own included ({100 * (wcost - dcost) / BLOCK_BUDGET:.1f} %)")

    # Safe mode: the marker left by a stop. A project's plugins aren't made, a user's pick is, and
    # 5 s after it the marker goes and safe mode ends; the skipped slots stay as they are
    r = GuardRig(mods, files, marker=True)
    was = r.safe_mode()
    r.set_instrument(0, ACD)
    r.note(0, 60, True)
    r.from_project(0, 0, "GuardHang")
    r.from_project(0, 1, "Delay")
    skipped = [r.status(0, 0), r.status(0, 1)], [r.state(0, 0), r.state(0, 1)]
    try:
        out = []
        for _ in range(10):             # r.block(), with a short limit so a hang stops soon
            r.m.call(r._update, r.engine, r.left, r.right, BLOCK, max_insns=20_000_000)
            r.now += BLOCK_MS
            out += struct.unpack(f"<{BLOCK}h", r.m.read(r.left, 2 * BLOCK))
    except RuntimeError as e:          # GuardHang made after all, and run
        print("     ", e)
        check(False, f"safe mode: the marker found at start, a project's GuardHang and Delay not made "
                     f"(statuses {skipped[0]}, states {skipped[1]}): it ran and never returned")
        return
    made0 = r.instances()
    r.fx(1, 0, "Chorus", PACK["Chorus"])
    made1, kept, still = r.instances(), MARKER in r.sd.files, r.safe_mode()
    r.now += 5001
    r.update()
    gone, after = MARKER not in r.sd.files, r.safe_mode()
    r.blocks(4)
    check(was and skipped == ([SKIPPED, SKIPPED], [FAILED, FAILED]) and made0 == 0 and peak(out) > 100,
          f"safe mode: the marker found at start, a project's GuardHang and Delay not made (statuses "
          f"{skipped[0]}, states {skipped[1]}, {made0} made), and the part plays dry (peak {peak(out)})")
    check(made1 == 1 and kept and still and gone and not after and r.status(0, 0) == SKIPPED and r.instances() == 1,
          f"safe mode: a user's pick is made ({made1}), the marker kept for 5 s ({kept}, {still}) then removed and "
          f"safe mode over ({gone}, {not after}); the project's slots still not made ({r.status(0, 0)})")

    # The marker before the plugin's code first runs: a project's GuardHang is made, and the
    # marker is on the card before its first block, which never returns
    r = GuardRig(mods, files)
    r.set_instrument(0, ACD)
    r.note(0, 60, True)
    r.blocks(2)
    r.from_project(0, 0, "GuardHang")
    written = MARKER in r.sd.files
    lo, hi = r.code_pool
    pc = None
    try:
        r.m.call(r._update, r.engine, r.left, r.right, BLOCK, max_insns=2_000_000)
    except RuntimeError as e:
        pc = r.m.uc.reg_read(UC_ARM_REG_PC)
        print("     ", e)
    check(written and pc is not None and lo <= pc <= hi,
          f"GuardHang: the marker on the card when it is made ({written}), before its first block, which "
          f"never returns (stopped at {pc if pc is None else hex(pc)}, inside the plugins' code: "
          f"{pc is not None and lo <= pc <= hi})")


SECTIONS = ("config", "off_is_ce", "mix_zero", "as_before", "each_effect", "order_and_swap", "release",
            "shared", "garbage", "rules_and_cost", "wave_off_and_mono", "old_patches", "pre", "guards")


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
            elif name in ("as_before", "old_patches"):
                fn(a.mods, a.before)
            else:
                fn(a.mods)
    print(f"{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
