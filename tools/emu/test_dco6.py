# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Level B for DCO6 (type id 66, core/mods/synths/psyn/dco6Synth.cpp), through CE's real synth host on
the full build (tools/emu/sound_rig.py), with CE's factory engines on the fake card beside it. Those
run as they are and nothing hooks inside them; DCO6 is ours and open, so the rig watches its render
(d6_render: r0 the voice's state, r1 its parameters) to see which voice plays what. The plan's
section 11 lists these (docs/mt/VA_SYNTH_PLAN.md).

1. Init: the values a voice renders with are the parameter table's defaults, with the macros as the
   model picker leaves them (Cutoff's bipolar macro leaves 2.4 kHz), Velocity on Volume, and the
   hook's place for a voice alone (16: slot 0 of 1).
2. Notes: a note sounds and stops after its release (no voice renders), the same twice.
3. Unison and polyphony, through the hook in CE's host (InstrumentImpl::mtUnisonNoteOn), by which
   voice holds which note at which place:
   - 8 voices, Unison Off: 8 notes, one voice each;
   - 8 voices, Unison 4: 2 notes, a stack of 4 each; a third note takes the oldest whole stack;
   - 7 voices, Unison 4: one stack; voices 4-6 never render;
   - 3 voices, Unison 8: a stack of 3;
   - 8 voices, Unison 2: a released stack is taken before a held one.
4. Unison changed while notes are held: the held stacks keep their places, the next note takes
   the new size.
5. Velocity reaches every member of a stack.
6. Stereo: Unison 4 at Width 100 % gives two sides; Unison Off, one.
7. The SDK's rules: no malloc and no createMetadata in the audio interrupt, nor in a note-on (which
   the firmware runs in the interrupt), the hook's included; the interrupt's mask balanced.
8. Engine switches: 30 rounds of DCO6, WAVE, CHIP with notes held: the heap settles.
9. The Synths switch Off: a part already on DCO6 plays on, sample for sample as On.
10. Patch files: a DCO6 patch saved and loaded back restores a changed value.
11. Own-code instructions per voice, 8 voices on Init: 18,915 measured on the step 1 build; a
    regression number (within 10 %), not a budget.
12. Stress: random notes, parameters (NaN included), Unison and the part's voices changed, over
    1,500 blocks: no assert, no malloc in the interrupt, the mask balanced.

CE's stock engines, mods build against the plain-CE build sample for sample, are
tools/emu/test_sound_rig.py's: the hook must change nothing for them.

Usage: python tools/emu/test_dco6.py build/emu/TrackerPlus_MT_full.axf [--only name,name]
"""
from __future__ import annotations

import math
import random
import struct
import sys
from pathlib import Path

from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sound_rig import peak  # noqa: E402
from test_synths import CHIP, PHASE, WAVE, Rig as SynthsRig, clip_run  # noqa: E402
from machine import SCRATCH  # noqa: E402

DCO6 = 66
RENDER = "_ZN7mtsynthL9d6_renderEPNS_8d6_voiceEPKfPfS4_ff"
META = "_ZN11psyn_bridge13PluginAdapterIN9polysynth12_GLOBAL__N_19Dco6VoiceEE14createMetadataEPKvPv"
N_PARAMS = 35
# d6_voice (core/mods/synths/d6Engine.h): 31 four-byte fields, then the bytes
OFF_STAGE, OFF_NOTE, OFF_SLOT, OFF_SIZE, OFF_INDEX = 124, 126, 127, 128, 129
P = dict(range=0, tune=1, fine=2, saw=3, pulse=4, sub=5, noise=6, pw=7, pwm=8, pwm_src=9, lfo_pitch=10, hpf=11,
         cutoff=12, res=13, env_amt=14, lfo_cut=15, key=16, attack=17, decay=18, sustain=19, release=20, vca=21,
         lfo_wave=22, lfo_rate=23, lfo_delay=24, lfo_sync=25, lfo_trig=26, unison=27, detune=28, width=29,
         vintage=30, glide=31, volume=32, place=33, boost=34)
VELOCITY = 100 / 127                # Synth::triggerTestNote's, through the Velocity macro to Volume
INIT = dict(range=1, tune=0, fine=0, saw=1, pulse=0, sub=0, noise=0, pw=0, pwm=0, pwm_src=0, lfo_pitch=0, hpf=20,
            cutoff=2400, res=0.15, env_amt=0.35, lfo_cut=0, key=0.5, attack=0.005, decay=0.6, sustain=0.6,
            release=0.35, vca=0, lfo_wave=0, lfo_rate=0.693, lfo_delay=0, lfo_sync=0, lfo_trig=0, unison=1,
            detune=0, width=0.5, vintage=0, glide=0, volume=VELOCITY, place=16, boost=0)
CHORD = (48, 52, 55, 59, 62, 65, 69, 72)
COST_INIT = 18_915                  # own-code instructions per voice per block, 8 voices, Init (step 1 build)

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        failures.append(what)


class Rig(SynthsRig):
    """test_synths' rig, with DCO6's render watched: each block, which voices rendered with what."""

    def __init__(self, axf):
        super().__init__(axf)
        self.seen = {}              # voice index -> (note, slot, size, stage, params) in the last block
        self.watch_own("DCO6", META, self._on_meta)
        self.watch_own("DCO6", RENDER, self._on_render)
        self.in_note = False
        self.malloc_in_note = 0

    def _on_render(self, mm):
        v, p = mm.uc.reg_read(UC_ARM_REG_R0), mm.uc.reg_read(UC_ARM_REG_R1)
        b = mm.read(v + OFF_STAGE, 8)
        params = struct.unpack(f"<{N_PARAMS}f", mm.read(p, 4 * N_PARAMS))
        self.seen[b[OFF_INDEX - OFF_STAGE]] = (b[OFF_NOTE - OFF_STAGE], b[OFF_SLOT - OFF_STAGE],
                                                b[OFF_SIZE - OFF_STAGE], b[0], params)

    def _on_malloc(self, mm):
        super()._on_malloc(mm)
        if self.in_note:
            self.malloc_in_note += 1

    def note(self, part, note, on):
        """A note through Synth's own call, which the firmware makes in the audio interrupt"""
        self.in_note = True
        try:
            super().note(part, note, on)
        finally:
            self.in_note = False

    def block(self):
        self.seen = {}
        return super().block()

    def part(self, voices, unison=None, params=(), others=0):
        """Part 0 on DCO6 with this many voices (the other parts' freed first: CE's total is 8)"""
        for p in (1, 2):
            self.set_voices(p, others if p == 1 else 0)
        self.set_instrument(0, DCO6)
        self.set_voices(0, voices)
        if unison is not None:
            self.set_param(0, P["unison"], float(unison))
        for k, v in params:
            self.set_param(0, P[k], v)

    def held(self):
        """voice index -> (note, slot, size) for the voices that rendered in the last block"""
        return {i: s[:3] for i, s in sorted(self.seen.items())}

    def rules_ok(self):
        return (not self.failures and self.malloc_in_block == 0 and self.malloc_in_note == 0
                and self.meta_calls["block"] == 0 and self.interrupts["disable"] == self.interrupts["enable"])


def expect_stacks(stacks):
    """{first voice: (note, size)} -> {voice: (note, slot, size)}"""
    out = {}
    for first, (note, size) in stacks.items():
        for m in range(size):
            out[first + m] = (note, m, size)
    return out


def init_values(axf):
    r = Rig(axf)
    r.part(8)
    r.note(0, 60, True)
    r.blocks(3)
    got = r.seen.get(0)
    if got is None:
        check(False, f"Init: voice 0 didn't render ({r.held()})")
        return
    params = got[4]
    wrong = [(k, round(params[P[k]], 4), v) for k, v in INIT.items()
             if abs(params[P[k]] - v) > 1e-4 * max(1.0, abs(v))]
    check(not wrong and got[1:3] == (0, 1),
          f"Init: a voice renders with the table's defaults, the macros as the picker leaves them, "
          f"Velocity on Volume and the place of a voice alone (wrong: {wrong[:4]}; slot, size {got[1:3]})")


def notes(axf):
    r = Rig(axf)
    r.part(8)
    outs = []
    for _ in range(2):
        r.note(0, 60, True)
        on = r.blocks(30)
        rendered = len(r.seen)
        r.note(0, 60, False)
        r.blocks(330)                   # Release 0.35 s: under 1e-4 by about 0.75 s, 260 blocks
        quiet = r.blocks(10)
        outs.append((peak(on[0]), rendered, len(r.seen), peak(quiet[0])))
    ok = all(p > 100 and n == 1 and after == 0 and q <= 2 for p, n, after, q in outs) and r.rules_ok()
    peaks = [o[0] for o in outs]
    same = abs(20 * math.log10(peaks[0] / max(peaks[1], 1))) < 1.0
    check(ok and same, f"a note sounds and stops after its release, twice (peaks {peaks}, voices rendering "
                       f"while held, after: {[(o[1], o[2]) for o in outs]}, after it quiet {[o[3] for o in outs]})")


def unison(axf):
    # 8 voices, Unison Off: 8 notes, one voice each, as CE's own allocator
    r = Rig(axf)
    r.part(8, 1)
    for n in CHORD:
        r.note(0, n, True)
    r.blocks(2)
    want = {i: (n, 0, 1) for i, n in enumerate(CHORD)}
    check(r.held() == want and r.rules_ok(), f"8 voices, Unison Off: 8 notes on 8 voices ({r.held()})")

    # 8 voices, Unison 4: two stacks; a third note takes the oldest stack whole
    r = Rig(axf)
    r.part(8, 4)
    r.note(0, 60, True)
    r.note(0, 64, True)
    r.blocks(2)
    two = r.held()
    r.note(0, 67, True)
    r.blocks(2)
    three = r.held()
    check(two == expect_stacks({0: (60, 4), 4: (64, 4)}) and three == expect_stacks({0: (67, 4), 4: (64, 4)})
          and r.rules_ok(),
          f"8 voices, Unison 4: 2 notes of 4 voices; a third takes the oldest stack whole ({two} then {three})")

    # 7 voices, Unison 4: one stack; voices 4-6 never render
    r = Rig(axf)
    r.part(7, 4)
    r.note(0, 60, True)
    r.blocks(2)
    one = r.held()
    r.note(0, 64, True)
    renders = set()
    for _ in range(20):
        r.blocks(1)
        renders |= set(r.seen)
    check(one == expect_stacks({0: (60, 4)}) and r.held() == expect_stacks({0: (64, 4)})
          and renders == {0, 1, 2, 3} and r.rules_ok(),
          f"7 voices, Unison 4: one stack of 4, the next note takes it; voices that rendered {sorted(renders)}")

    # 3 voices, Unison 8: a stack of 3
    r = Rig(axf)
    r.part(3, 8)
    r.note(0, 60, True)
    r.blocks(2)
    check(r.held() == expect_stacks({0: (60, 3)}) and r.rules_ok(),
          f"3 voices, Unison 8: a stack of 3 ({r.held()})")

    # 8 voices, Unison 2: a released stack before a held one
    r = Rig(axf)
    r.part(8, 2)
    for n in (60, 62, 64, 65):
        r.note(0, n, True)
    r.blocks(2)
    r.note(0, 62, False)                # the second stack released, still sounding
    r.blocks(2)
    r.note(0, 67, True)
    r.blocks(2)
    check(r.held() == expect_stacks({0: (60, 2), 2: (67, 2), 4: (64, 2), 6: (65, 2)}) and r.rules_ok(),
          f"8 voices, Unison 2: a new note takes the released stack, not a held one ({r.held()})")


def unison_changed(axf):
    r = Rig(axf)
    r.part(8, 4)
    r.note(0, 60, True)
    r.note(0, 64, True)
    r.blocks(2)
    r.set_param(0, P["unison"], 2.0)
    r.blocks(2)
    kept = r.held()
    r.note(0, 67, True)
    r.blocks(2)
    want = {0: (67, 0, 2), 1: (67, 1, 2), 2: (60, 2, 4), 3: (60, 3, 4), 4: (64, 0, 4), 5: (64, 1, 4),
            6: (64, 2, 4), 7: (64, 3, 4)}
    check(kept == expect_stacks({0: (60, 4), 4: (64, 4)}) and r.held() == want and r.rules_ok(),
          f"Unison 4 to 2 with notes held: they keep their places; the next note takes a stack of 2, the "
          f"oldest ({r.held()})")


def velocity(axf):
    r = Rig(axf)
    r.part(8, 4)
    r.note(0, 60, True)
    r.blocks(2)
    vols = [round(s[4][P["volume"]], 4) for _, s in sorted(r.seen.items())]
    check(len(vols) == 4 and all(abs(v - VELOCITY) < 1e-4 for v in vols),
          f"Velocity reaches every member of a stack (Volume {vols}, want {VELOCITY:.4f})")


def stereo(axf):
    sides = {}
    for u in (1, 4):
        r = Rig(axf)
        r.part(8, u, (("detune", 25.0), ("width", 1.0)))
        r.note(0, 60, True)
        left, right = r.blocks(40)
        sides[u] = (max(abs(a - b) for a, b in zip(left, right)), peak(left))
    check(sides[1][0] <= 16 and sides[4][0] > 1000 and sides[4][1] > 100,
          f"stereo: Unison 4 at Width 100 % gives two sides, Unison Off one (largest L-R "
          f"{sides[4][0]} and {sides[1][0]})")


def rules(axf):
    r = Rig(axf)
    r.part(8, 4, (("detune", 12.0),))
    heap0 = r.heap_brk
    for n in (60, 64, 67, 71, 74):
        r.note(0, n, True)
        r.blocks(3)
    for n in (60, 64, 67, 71, 74):
        r.note(0, n, False)
    r.blocks(10)
    check(r.rules_ok() and r.heap_brk == heap0,
          f"the SDK's rules with Unison 4, stacks stolen: malloc in a note-on {r.malloc_in_note}, in the "
          f"interrupt {r.malloc_in_block}, createMetadata in the interrupt {r.meta_calls['block']}, the mask "
          f"{r.interrupts}, heap +{r.heap_brk - heap0} {r.failures[:1]}")


def switches(axf):
    r = Rig(axf)
    r.set_instrument(0, CHIP)
    r.blocks(2)
    heap, in_use = [], []
    info = SCRATCH + 0x3F0000           # newlib's struct mallinfo, returned through r0
    for _ in range(30):
        for e in (DCO6, WAVE, CHIP):
            r.set_instrument(0, e)
            r.note(0, 60, True)
            r.note(0, 64, True)
            r.blocks(3)
        heap.append(r.heap_brk)
        r.call("mallinfo", info)
        in_use.append(struct.unpack("<10I", r.m.read(info, 40))[7])    # uordblks
    check(not r.failures and abs(in_use[-1] - in_use[1]) <= 64 and heap[-1] == heap[14]
          and r.malloc_in_block == 0,
          f"30 rounds of DCO6, WAVE, CHIP with notes held: {in_use[1]:,} bytes in use after round 2, "
          f"{in_use[-1]:,} after 30; the heap's top {heap[14] - r.heap_base:,} at round 15, "
          f"{heap[-1] - r.heap_base:,} at 30 {r.failures[:1]}")


def switch_off(axf):
    outs = []
    for on in (True, False):
        r = Rig(axf)
        r.part(8, 4, (("detune", 12.0), ("width", 1.0)))
        r.set_switch(on)
        r.note(0, 60, True)
        outs.append(r.blocks(30))
    check(outs[0] == outs[1] and peak(outs[1][0]) > 100,
          "with the Synths switch Off a part already on DCO6 plays on, Unison and all, as with it On")


def patch_files(axf):
    r = Rig(axf)
    # the rig never runs cFileManager::init (test_synths.patch_files): make the Patches folder here
    r.stub(r.m.addr("_ZN12cFileManager19createPatchesFolderEv"), lambda mm: r.sd.dirs.add("Patches"))
    r.part(8)
    r.set_param(0, P["cutoff"], 900.0)
    r.set_param(0, P["unison"], 3.0)
    name = r.m.put_str("MTDCO6")
    saved = r.m.call(r._save, r.synth, 0, name, max_insns=200_000_000) & 0xFF
    r.set_param(0, P["cutoff"], 5000.0)
    r.set_param(0, P["unison"], 1.0)
    loaded = r.m.call(r._load, r.synth, 0, name, max_insns=200_000_000) & 0xFF
    got = (r.get_param(0, P["cutoff"]), r.get_param(0, P["unison"]))
    check(saved == 1 and loaded == 1 and abs(got[0] - 900.0) < 1.0 and abs(got[1] - 3.0) < 1e-3,
          f"a DCO6 patch saved and loaded back (save {saved}, load {loaded}): Cutoff {got[0]:.0f} Hz, "
          f"Unison {got[1]:.0f} {r.failures[:1]}")


def cost(axf):
    r = Rig(axf)
    r.part(8)
    e = r.own["DCO6"]
    for n in CHORD:
        r.note(0, n, True)
    r.blocks(12)
    count = [0]
    h = r.hook_range(e.base, e.base + e.header["image_size"], lambda uc, a, s, u: count.__setitem__(0, count[0] + 1))
    r.m.uc.ctl_flush_tb()
    per = []
    for _ in range(20):
        count[0] = 0
        r.blocks(1)
        per.append(count[0] / max(len(r.seen), 1))
    r.m.uc.hook_del(h)
    mean = sum(per) / len(per)
    check(abs(mean - COST_INIT) <= 0.1 * COST_INIT,
          f"own-code instructions per voice, 8 voices on Init: {mean:,.0f} (step 1 build {COST_INIT:,}, within 10 %)")


def stress(axf):
    rnd = random.Random(1)
    r = Rig(axf)
    r.part(8, 2)
    heap0 = r.heap_brk
    held = set()
    nan = struct.unpack("<f", struct.pack("<I", 0x7FC00000))[0]
    worst = 0
    for b in range(1500):
        x = rnd.random()
        if x < 0.25:
            n = rnd.randrange(24, 109)
            r.note(0, n, True)
            held.add(n)
        elif x < 0.45 and held:
            n = rnd.choice(sorted(held))
            r.note(0, n, False)
            held.discard(n)
        elif x < 0.50:
            r.set_param(0, P["unison"], float(rnd.randrange(1, 9)))
        elif x < 0.53:
            k = rnd.choice(("cutoff", "res", "detune", "width", "vintage", "pwm", "lfo_pitch", "hpf", "glide"))
            r.set_param(0, P[k], nan if rnd.random() < 0.2 else rnd.uniform(0.0, 1.0) *
                        {"cutoff": 20000.0, "detune": 50.0, "hpf": 2000.0, "glide": 2.0}.get(k, 1.0))
        elif x < 0.54:
            for n in sorted(held):
                r.note(0, n, False)
            held.clear()
            r.set_voices(0, rnd.randrange(1, 9))
        left, _ = r.block()
        worst = max(worst, clip_run(left)[1])
        if r.failures:
            break
    check(r.rules_ok() and r.heap_brk == heap0,
          f"stress, 1,500 blocks of random notes, parameters (NaN too), Unison and voices: asserts "
          f"{r.failures[:1]}, malloc in a note-on {r.malloc_in_note}, in the interrupt {r.malloc_in_block}, "
          f"the mask {r.interrupts}, heap +{r.heap_brk - heap0}, longest clip run {worst}")


PARTS = {"init": init_values, "notes": notes, "unison": unison, "changed": unison_changed, "velocity": velocity,
         "stereo": stereo, "rules": rules, "switches": switches, "off": switch_off, "patch": patch_files,
         "cost": cost, "stress": stress}


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    axf = Path(args[0])
    only = args[args.index("--only") + 1].split(",") if "--only" in args else list(PARTS)
    for name in only:
        print(f"--- {name}", flush=True)
        PARTS[name](axf)
    print(f"{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
