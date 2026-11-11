# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Level B for Synth FX (core/mods/synthfx/synthfxMod.h): the Synths effects on CE's own engines,
through CE's real synth host (tools/emu/sound_rig.py). The closed engines run as they are and only
what comes out of the firmware is checked: ours with and without the insert, never an engine's
insides. Settings are written where the page will write them (mods::synthfx::settings).

Each part plays note 60 (PERC: 48, which differs left and right, so a lost right side shows).

0. Config: the two Synth FX items are the mod's default and the project's switch.
1. Off = CE: with the switch Off and effects chosen, or On with nothing chosen, every engine sounds
   as the reference build does, sample for sample.
2. Mix 0: a modulation effect with nothing of it mixed in leaves every engine's sound as it was,
   sample for sample, so the dry path through the insert (and a mono part sent as stereo) is clean.
3. Each engine with each modulation effect and each Distortion type: the sound changes from the
   first block it can (LATENCY blocks after the setting) and only a little in that block (no
   click); no clipping run longer than the dry sound's own; a mono part goes stereo only with a
   modulation effect.
4. Cleared: everything fades out, the part lets its state go, and a new note sounds as it would
   have without the effects, sample for sample.
5. WAVE and PHASE parts are skipped; three parts with effects each get a state.
6. Garbage settings (NaN, infinities, out of range) for 70 blocks; once cleared the part lets go
   and a new note sounds as before, and a Chorus chosen again sounds.
7. The rules: no malloc in the audio interrupt, the heap held, interrupts turned back on, the block
   pool no fuller than with CE's own stereo engine (PERC) on all three parts, and the insert's cost
   per block in open-code instructions (the closed engines' own aren't counted).

Usage: python tools/emu/test_synthfx.py <mods.axf> <reference.axf> [--only SECTION] [--engines 1,6]
SECTION is one of the functions below (config, off_is_ce, mix_zero, each_effect, release, skipped_and_shared,
garbage, rules_and_cost); --engines limits sections 1 to 3 to those engine ids (tools/emu/plant_synthfx.py).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from sound_rig import BLOCK, SoundRig, peak  # noqa: E402
from test_step0 import menu_item_named  # noqa: E402
from test_synths import BLOCK_BUDGET, PHASE, WAVE, clip_run  # noqa: E402
from test_synths import Rig as SynthsRig  # noqa: E402

NAMES = {1: "ACD", 2: "FAT", 4: "VAP", 5: "WTFM", 6: "PERC", 7: "DIRT", 8: "CHIP"}
ENGINES = dict(NAMES)               # sections 1 to 3 (--engines)
PERC = 6
SYNTHFX_MOD = 15                    # Mod::synthfx
# synthFx.h's ten, and their values as each part starts
EFFECT, RATE, DEPTH, MIX, FEEDBACK, DIST, GAIN, TONE, PLACEMENT, LEVEL = range(10)
INIT = (0.0, 0.5, 0.5, 0.5, 0.5, 0.0, 0.5, 0.5, 1.0, 0.0)
EFFECTS = {1: "Chorus", 2: "Flanger", 3: "Phaser"}
DISTS = {1: "Overdrive", 2: "Distortion", 3: "Bit Crusher"}

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


class Rig(SynthsRig):
    def __init__(self, axf):
        super().__init__(axf)
        self.settings = self.m.addr("_ZN4mods7synthfx8settingsE")
        held = [n for n in self.syms if "synthfx" in n and "held" in n]
        assert len(held) == 1, held
        self.held_at = self.syms[held[0]].addr
        for part in range(3):           # the sends off, so nothing lingers in the master's effects
            self.reverb_send(part, 0.0)
            self.delay_send(part, 0.0)

    def fx_switch(self, on: bool):
        self.m.write(self.switches + SYNTHFX_MOD, bytes([1 if on else 0]))

    def fx(self, part, values=None, **at):
        """A part's ten settings: values (all ten), then single ones by index (at: {"i3": v})."""
        vals = list(INIT if values is None else values)
        for k, v in at.items():
            vals[int(k[1:])] = v
        self.m.write(self.settings + 40 * part, struct.pack("<10f", *vals))

    def held(self, part) -> int:
        return self.m.read(self.held_at + part, 1)[0]


def setting(**kw):
    """{"effect": 3, "dist": 2} -> fx()'s keyword form"""
    names = {"effect": EFFECT, "rate": RATE, "depth": DEPTH, "mix": MIX, "feedback": FEEDBACK,
             "dist": DIST, "gain": GAIN, "tone": TONE, "level": LEVEL}
    return {f"i{names[k]}": v for k, v in kw.items()}


def note_for(engine):
    """Note 60, but PERC's note 60 is the same on both sides and its note 48 isn't, so PERC plays 48:
    a mistake that loses the right side can show."""
    return 48 if engine == PERC else 60


def play(axf, engine, on=True, blocks=30, at=0, part_fx=None, rig=None, voices=None):
    """One part on engine, its note held. part_fx (settings keywords) from block `at`."""
    r = rig or Rig(axf)
    r.set_instrument(0, engine)
    if voices:
        r.set_voices(0, voices)
    r.fx_switch(on)
    r.note(0, note_for(engine), True)
    out_l, out_r = r.blocks(at) if at else ([], [])
    if part_fx is not None:
        r.fx(0, **setting(**part_fx))
    left, right = r.blocks(blocks - at)
    return r, (out_l + left, out_r + right)


def rules_ok(r, heap0=None):
    return (not r.failures and r.malloc_in_block == 0 and r.interrupts["disable"] == r.interrupts["enable"]
            and (heap0 is None or r.heap_brk == heap0))


def off_is_ce(mods, ref):
    for e, name in ENGINES.items():
        rr = SoundRig(ref)
        rr.set_instrument(0, e)
        for part in range(3):
            rr.reverb_send(part, 0.0)
            rr.delay_send(part, 0.0)
        rr.note(0, note_for(e), True)
        want = rr.blocks(25)
        r, off = play(mods, e, on=False, blocks=25, part_fx={"effect": 3, "dist": 2, "gain": 1.0})
        check(off == want and peak(want[0]) > 100 and rules_ok(r),
              f"{name}: Synth FX Off with Phaser and Distortion chosen sounds as the reference build, sample for sample")
        r, none = play(mods, e, on=True, blocks=25)
        check(none == want and r.held(0) == 0,
              f"{name}: Synth FX On with nothing chosen sounds as the reference build (the part holds no state)")


def mix_zero(mods):
    for e, name in ENGINES.items():
        _, dry = play(mods, e, on=False, blocks=25)
        for fx, fname in EFFECTS.items():
            r, got = play(mods, e, blocks=25, at=5, part_fx={"effect": fx, "mix": 0.0})
            check(got == dry and r.held(0) == 1 and rules_ok(r),
                  f"{name}, {fname} at Mix 0: the sound as without it, sample for sample (held {r.held(0)}, "
                  f"{'stereo' if stereo(*dry) else 'mono'} dry)")


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


# A setting made before block `at` first shows in the master output at block at + LATENCY: the
# rig's path from the synth to the output, the same for every engine and effect (2026-11-09: all 49
# cases of each_effect).
LATENCY = 3


def each_effect(mods):
    at = 10
    for e, name in ENGINES.items():
        _, dry = play(mods, e, on=False, blocks=40)
        dry_stereo = stereo(*dry)
        dry_run = clip_run(dry[0] + dry[1])[1]
        cases = [(f"{fname}", {"effect": fx}) for fx, fname in EFFECTS.items()]
        cases += [(f"{dname}, Gain 100 %", {"dist": d, "gain": 1.0}) for d, dname in DISTS.items()]
        cases += [("Distortion and Phaser", {"dist": 2, "gain": 1.0, "effect": 3, "feedback": 0.9})]
        for what, kw in cases:
            r, got = play(mods, e, blocks=40, at=at, part_fx=kw)
            heap0 = r.heap_brk
            left, right = got
            b = first_change(left, dry[0], at)
            # its first block: the Mix (or the Distortion's fade) is 5 % of the way, so close to the dry
            first = max_diff(left[b * BLOCK:(b + 1) * BLOCK], dry[0][b * BLOCK:(b + 1) * BLOCK]) if b else 0
            ref_peak = max(peak(dry[0][b * BLOCK:(b + 1) * BLOCK]), 1) if b else 1
            run = clip_run(left + right)[1]
            # the image once the effect is fully in (the Distortion's fade mixes the dry sound in for
            # about 8 blocks)
            tail = (at + LATENCY + 10) * BLOCK
            wet = stereo(left[tail:], right[tail:])
            image = "stereo" if wet else "mono"
            image_ok = wet if "effect" in kw else wet == dry_stereo
            ok = (b == at + LATENCY and first <= 0.25 * ref_peak and run <= max(dry_run, 7)
                  and image_ok and rules_ok(r, heap0))
            check(ok, f"{name}, {what}: changes from block {b}, by {first} against its peak {ref_peak} there; "
                      f"{image} (dry {'stereo' if dry_stereo else 'mono'}); longest clip run {run} (dry {dry_run}) "
                      f"{r.failures[:1]}")


def schedule(axf, engine, steps):
    """One part on engine, Synth FX On: ("fx", settings keywords, a list of ten, or None for Init),
    ("blocks", n), ("held",) to note the part's held flag, ("note",) a new note (the Init notes die
    away within about 180 blocks), ("keep", n): n blocks kept. Returns the rig, the held flags and
    the blocks kept."""
    r = Rig(axf)
    r.set_instrument(0, engine)
    r.fx_switch(True)
    n = note_for(engine)
    r.note(0, n, True)
    held, kept = [], ([], [])
    for step in steps:
        if step[0] == "fx":
            if isinstance(step[1], dict):
                r.fx(0, **setting(**step[1]))
            else:
                r.fx(0, step[1])
        elif step[0] == "blocks":
            r.blocks(step[1]) if step[1] else None
        elif step[0] == "held":
            held.append(r.held(0))
        elif step[0] == "note":
            r.note(0, n, False)
            r.blocks(1)
            r.note(0, n, True)
        else:
            a, b = r.blocks(step[1])
            kept[0].extend(a)
            kept[1].extend(b)
    return r, held, kept


def release(mods):
    """Phaser and Distortion for 20 blocks, then cleared: within 30 blocks the part has let go, and
    a new note sounds as in a run that never had them, sample for sample."""
    for e in (1, PERC):
        name = NAMES[e]
        tail = [("blocks", 40), ("note",), ("keep", 40)]
        _, _, dry = schedule(mods, e, [("blocks", 60)] + tail)
        r, held, got = schedule(mods, e, [("blocks", 10), ("fx", {"effect": 3, "dist": 2, "gain": 1.0}),
                                          ("blocks", 20), ("held",), ("fx", None), ("blocks", 30), ("held",)] + tail)
        same = got == dry
        check(held == [1, 0] and same and peak(dry[0]) > 1000 and rules_ok(r),
              f"{name}: cleared, the effects fade out and the part lets its state go (held {held[0]} -> "
              f"{held[1]}); a new note then sounds as without them, sample for sample ({same}, peak {peak(dry[0])})")


def skipped_and_shared(mods):
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        _, off = play(mods, e, on=False, blocks=25, part_fx={"effect": 3, "dist": 2})
        r, on = play(mods, e, on=True, blocks=25, part_fx={"effect": 3, "dist": 2})
        check(on == off and r.held(0) == 0, f"{name}: skipped, with Synth FX On and effects chosen it sounds "
                                            f"as with Synth FX Off (held {r.held(0)})")
    # three parts, each with effects: a state for each part on a CE engine
    for layout in (((1, 3), (7, 3), (8, 2)), ((WAVE, 3), (1, 3), (PERC, 2))):
        r = Rig(mods)
        r.fx_switch(True)
        for part in range(3):
            r.set_voices(part, 0)
        for part, (e, v) in enumerate(layout):
            r.set_instrument(part, e)
            r.set_voices(part, v)
            r.fx(part, **setting(effect=1 + part, dist=1 + part))
            r.note(part, note_for(e) + 4 * part, True)
        left, _ = r.blocks(20)
        held = [r.held(p) for p in range(3)]
        want = [0 if e in (WAVE, PHASE) else 1 for e, _ in layout]
        names = "+".join(NAMES.get(e, "WAVE") for e, _ in layout)
        check(held == want and peak(left) > 100 and rules_ok(r),
              f"{names}: every CE part with effects holds a state ({held}), the sound {peak(left)}")


def garbage(mods):
    bad = (float("nan"), float("inf"), -float("inf"), 1e30, -1e30, 7.5, -3.0)
    for e in (1, PERC):
        name = NAMES[e]
        steps = [("blocks", 5)]
        for i, v in enumerate(bad):
            steps += [("fx", [v if (k + i) % 2 == 0 else INIT[k] for k in range(10)]), ("keep", 5),
                      ("fx", [v] * 10), ("keep", 5)]
            if i % 2:
                steps.append(("note",))
        steps += [("fx", None), ("blocks", 60), ("held",), ("note",), ("keep", 40), ("fx", {"effect": 1}), ("keep", 20)]
        r, held, got = schedule(mods, e, steps)
        # the same notes with nothing chosen
        clean = [s for s in steps if s[0] != "fx"]
        _, _, dry = schedule(mods, e, clean)
        n = len(bad) * 10 * BLOCK                   # the garbage blocks kept, then 40 after the clear, 20 with a Chorus
        worst = clip_run(got[0][:n] + got[1][:n], run=1)[1]
        after = (got[0][n:n + 40 * BLOCK], got[1][n:n + 40 * BLOCK])
        same = after == (dry[0][n:n + 40 * BLOCK], dry[1][n:n + 40 * BLOCK])
        chorus, plain = got[0][n + 40 * BLOCK:], dry[0][n + 40 * BLOCK:]
        check(held == [0] and same and peak(chorus) > 1000 and chorus != plain and rules_ok(r),
              f"{name}: garbage settings for {10 * len(bad)} blocks (longest run at the rails {worst}); cleared, "
              f"the part lets go (held {held}) and a new note sounds as without them ({same}); a Chorus then "
              f"sounds and changes it (peak {peak(chorus)})")


def rules_and_cost(mods):
    """The worst case the plan names: 3 parts sharing the 8 voices, each with Distortion and a
    Phaser, on a mono engine and on PERC; against the same with Synth FX Off."""
    res = {}
    for e in (1, PERC):
        for on in (False, True):
            r = Rig(mods)
            r.fx_switch(on)
            for part in range(3):
                r.set_voices(part, 0)
            for part, v in enumerate((3, 3, 2)):
                r.set_instrument(part, e)
                r.set_voices(part, v)
                r.fx(part, **setting(effect=3, dist=2, gain=1.0, feedback=0.9))
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
              f"back on (peak {on[3]}, longest clip run {on[4]}, without the effects {off[4]})")
        # a mono part sent as stereo takes blocks as a stereo part does: no more than CE's own
        # stereo engine, PERC, on all three parts
        check(on[1] <= perc,
              f"{name} x 3 parts: block pool peak {on[1]} with the effects, {off[1]} without; PERC's own, "
              f"without them, {perc}")
        check(extra <= BLOCK_BUDGET // 5,
              f"{name} x 3 parts: the insert costs {extra:,} open-code instructions a block "
              f"({100 * extra / BLOCK_BUDGET:.1f} % of a block's {BLOCK_BUDGET:,}; plan: under 20 %)")


def config(mods):
    """Config -> Mods sets the default for new projects, Project Settings -> Mods the project's switch"""
    m = Machine(mods)
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    for group, array in (("menuMods", "_ZN4mods8defaultsE"), ("menuProjectMods", "_ZN4mods8switchesE")):
        item = menu_item_named(m, group, "Synth FX")
        check(m.r32(m.r32(item + 16)) == m.addr(array) + SYNTHFX_MOD, f"{group}: Synth FX is mod {SYNTHFX_MOD}'s "
                                                                  f"{'default' if 'Project' not in group else 'switch'}")


SECTIONS = ("config", "off_is_ce", "mix_zero", "each_effect", "release", "skipped_and_shared", "garbage", "rules_and_cost")


def main() -> int:
    global ENGINES
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mods", type=Path)
    ap.add_argument("ref", type=Path)
    ap.add_argument("--only", choices=SECTIONS)
    ap.add_argument("--engines", help="engine ids for sections 1 to 3, e.g. 1,6")
    a = ap.parse_args()
    if a.engines:
        ENGINES = {int(e): NAMES[int(e)] for e in a.engines.split(",")}
    for name in SECTIONS:
        if a.only in (None, name):
            fn = globals()[name]
            fn(a.mods, a.ref) if name == "off_is_ce" else fn(a.mods)
    print(f"{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
