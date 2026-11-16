# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Master FX's cost against today's release, in instructions a block, on the emulated Cortex-M7: CE's
own reverb, delay and limiter objects' update() and the panning amplifiers' (where ducking runs),
each counted whole per block (the slot's hook, the runtime and the plugin inside it), on the sound
rig's graph with a synth part playing 4 voices into both sends.

    python tools/masterfx/compare_cost.py <plugin firmware .axf> <masterfx-1.x firmware .axf>
        [--reference <plain CE .axf>] [--blocks N]

The masterfx-1.x firmware is today's release, d9a6795 built with `make plus EDITION_NAME=MT`: its
reverbs and delays are built in. Each choice runs on both firmwares where both have it; the dynamics
slot's plugins only on the plugin firmware, against CE's limiter on today's. Code hooks cover only
ITCM and flash, never the .psyn pool where the closed engines run (the rig's guard checks), and
count nothing outside the four update functions.

Stock is measured against the plain CE build of the same CE commit (--reference, MT_NO_MODS) when
one is given: since CE's cc53303 its delay and limiter cost about 27 % less than on the CE that
masterfx-1.x is built on, so Stock can no longer match today's release both ways.

Limits: Stock on both the same to within 1 %; each pack reverb or delay within 2 % of the built-in
effect it replaces, or PACK_SLACK more, whichever allows more; Glue's whole limiter update no more than 1.5 times CE's limiter's, without a key
and with one; the amplifiers with nothing ducked no more than AMPLIFIER_SLACK over today's (all of
them together, a block).
Instructions aren't cycles: the unit's caches and SDRAM add to all of them, and the Profiler is the
measure there.
"""
from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "emu"))
from masterfx_runtime import has_plugins  # noqa: E402
from test_masterfx_rig import Graph, limiter_knobs, set_dynamics, set_mod  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR  # noqa: E402

UPDATES = {
    "reverb": "_ZN19AudioEffectPolyverb6updateEv",
    "delay": "_ZN21AudioEffectShortDelay6updateEv",
    "limiter": "_ZN18AudioEffectLimiter6updateEv",
    "amplifiers": "_ZN14AudioAmplifier6updateEv",
}
OPEN_CODE = ((0x00000000, 0x0007FFFF), (0x60000000, 0x61FFFFFF))   # ITCM and flash
HOT = 6000                          # CE's Threshold knob, under the notes' peak: the limiter works
KEY = 1                             # CE's Sidechain list: sample track 1 (silent here, but a key)
AMPLIFIER_SLACK = 500               # instructions a block: the ducking hook's test in every amplifier
# instructions a block, 0.06 % of one: the pack's DSP is masterfx-1.x's source, built as a plugin
# (inside a struct, its state through the Env's pointer). BBD measured 746 over today's (+2.1 %).
PACK_SLACK = 1000

REVERBS = {1: "Freeverb", 2: "Plate", 3: "Spring", 4: "Shimmer", 5: '"Gated"', 6: "Reverse"}
DELAYS = {1: "Tape", 2: "BBD"}

failures: list[str] = []


def check(ok: bool, what: str) -> None:
    print(("  ok    " if ok else "  FAIL  ") + what, flush=True)
    if not ok:
        failures.append(what)


class Counted(Graph):
    """The graph with the four update functions counted, block by block"""

    def __init__(self, axf):
        super().__init__(axf, 60, 60)
        m = self.m
        self.inside = None
        self.ret = None
        self.counts = defaultdict(int)

        def entry(name):
            def f(mm):
                if self.inside is None:
                    self.inside = name
                    self.ret = mm.uc.reg_read(UC_ARM_REG_LR) & ~1
            return f
        for name, sym in UPDATES.items():
            self.watch(m.addr(sym), entry(name))

        def count(uc, address, size, user):
            if self.inside is None:
                return
            if address == self.ret:
                self.inside = None
                return
            self.counts[self.inside] += 1
        for lo, hi in OPEN_CODE:
            self.hook_range(lo, hi, count)
        m.uc.ctl_flush_tb()

    def measure(self, blocks: int) -> dict[str, float]:
        """4 voices playing into both sends: a warm-up, then the mean per block of each update"""
        self.set_voices(0, 4)
        for note in (48, 55, 60, 64):
            self.note(0, note, True)
        self.blocks(20)
        self.counts.clear()
        self.blocks(blocks)
        return {k: self.counts[k] / blocks for k in UPDATES}


def run(axf: Path, reverb=0, delay=0, dynamics=None, threshold=32767, key=0, blocks=40, plain=False) -> dict[str, float]:
    g = Counted(axf)
    if not plain:                   # plain CE has no mod settings: its Stock is all it has
        set_mod(g, True, reverb, delay)
    if dynamics is not None:
        set_dynamics(g, dynamics)
    limiter_knobs(g, threshold, key)
    return g.measure(blocks)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("plugins", type=Path)
    ap.add_argument("today", type=Path)
    ap.add_argument("--reference", type=Path, help="plain CE built from the same commit (MT_NO_MODS), for Stock")
    ap.add_argument("--blocks", type=int, default=40)
    a = ap.parse_args()
    probe = Counted(a.plugins)
    if not has_plugins(probe.m):
        raise SystemExit(f"{a.plugins}: not the plugin firmware")
    if has_plugins(Counted(a.today).m):
        raise SystemExit(f"{a.today}: not masterfx-1.x")
    n = a.blocks
    rows = []

    def row(what, mine, theirs, slot, limit, slack=0, floor=0):
        ratio = mine[slot] / theirs[slot] if theirs[slot] else float("inf")
        rows.append((what, slot, theirs[slot], mine[slot]))
        allowed = (f"limit {100 * (limit - 1):+.1f} %" + (f" and {slack} more" if slack else "")
                   + (f" or {floor} more" if floor else ""))
        check(mine[slot] <= max(theirs[slot] * limit + slack, theirs[slot] + floor),
              f"{what}: {slot} {mine[slot]:,.0f} against today's {theirs[slot]:,.0f} ({100 * (ratio - 1):+.1f} %, {allowed})")

    def stock(**k):
        """Stock to compare with: plain CE from the same commit if given, else today's release"""
        return run(a.reference, plain=True, **k) if a.reference else run(a.today, **k)
    base = "plain CE" if a.reference else "today's"

    print(f"-- Stock, against {base} --", flush=True)
    stock_mine, stock_today = run(a.plugins, blocks=n), stock(blocks=n)
    for slot in UPDATES:
        if slot == "amplifiers":    # the ducking hook's test, with nothing ducked: a few hundred
            row("Stock", stock_mine, stock_today, slot, 1.0, AMPLIFIER_SLACK)
            continue
        row("Stock", stock_mine, stock_today, slot, 1.01)
        check(stock_today[slot] / max(stock_mine[slot], 1) <= 1.01, f"Stock: {slot} no cheaper than {base} either")
    check(min(stock_mine.values()) > 1000, "each update runs (the counts aren't empty)")

    print("-- the reverbs and delays --", flush=True)
    for i, name in REVERBS.items():
        row(name, run(a.plugins, reverb=i, blocks=n), run(a.today, reverb=i, blocks=n), "reverb", 1.02, floor=PACK_SLACK)
    for i, name in DELAYS.items():
        row(name, run(a.plugins, delay=i, blocks=n), run(a.today, delay=i, blocks=n), "delay", 1.02, floor=PACK_SLACK)

    print("-- the dynamics slot --", flush=True)
    for key, label in ((0, "no key"), (KEY, "a key")):
        today = run(a.today, threshold=HOT, key=key, blocks=n)
        mine_stock = run(a.plugins, dynamics="", threshold=HOT, key=key, blocks=n)
        glue = run(a.plugins, dynamics="Glue.ptmfx", threshold=HOT, key=key, blocks=n)
        row(f"Stock limiter, {label}", mine_stock, stock(threshold=HOT, key=key, blocks=n), "limiter", 1.01)
        row(f"Glue, {label}", glue, today, "limiter", 1.5)
        row(f"Glue, {label}", glue, today, "amplifiers", 1.0, AMPLIFIER_SLACK)

    print("\n| Choice | Update | Today | Plugin firmware | Change |")
    print("| :--- | :--- | ---: | ---: | ---: |")
    for what, slot, theirs, mine in rows:
        print(f"| {what} | {slot} | {theirs:,.0f} | {mine:,.0f} | {100 * (mine / theirs - 1) if theirs else 0:+.1f} % |")
    print(f"\n{len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
