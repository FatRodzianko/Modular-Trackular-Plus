# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Checks a built Synth FX plugin (.ptsfx) on the emulated Cortex-M7, before it goes on a card:
loaded by the firmware's own runtime (the rig, tools/synthfx/rig/build.sh), then called the
ways the firmware calls it.

    python tools/synthfx/check_ptsfx.py My.ptsfx [more.ptsfx ...] [--rig build/emu/synthfx_rig.axf]

1. The header, as the firmware reads it: refused files say why. The knobs and their texts.
2. Blocks on noise, a loud sine and silence, at the default knobs, at every knob's ends, every
   knob swept, random settings, mono, stereo and (for WIDENS) a mono part widened:
   - the output is finite and within +-4 (beyond, the firmware would change it);
   - the plugin writes only its state, its blocks and the stack (no globals);
   - reset() at any time gives what a fresh instance gives;
   - its cost: instructions per block, the most and the mean, against its share of a block
     (a quarter: over it 8 blocks in a row, the firmware bypasses the slot). Instructions
     are not cycles: SDRAM and the caches add to them on the unit, where the Profiler
     measures it with all 8 voices sounding.
Exit 0 if every plugin passed.
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_runtime as rt  # noqa: E402
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_S0  # noqa: E402

B = rt.B
CLOCK_HZ = 600e6
SHARE = CLOCK_HZ * B / 44100.0 / 4.0          # a quarter of a block at 600 MHz: the time guard's
STACK = (0x20000000, 0x20058000)
FLAG_WIDENS, FLAG_PRE = 1, 2


class Checker:
    def __init__(self, rig: rt.Rig, path: Path):
        self.rig, self.path = rig, path
        self.data = path.read_bytes()
        self.problems: list[str] = []
        self.notes: list[str] = []

    def problem(self, text: str) -> None:
        self.problems.append(text)

    # ---- 1. the header, through the firmware's own check ----------------------------------
    def header(self) -> bool:
        r = self.rig
        r.fresh({f"/SynthFX/{self.path.name}": self.data})
        r.start()
        cat = [c for c in r.catalog() if c[0] == self.path.name]
        if not cat or cat[0][2] != "ok":
            self.problem(f"the firmware won't list it: {cat[0][2] if cat else 'not a .ptsfx name'}"
                         " (ptsfx_builder.py says why)")
            return False
        self.p = rt.Plugin(self.data)
        name = self.data[8:40].split(b"\0")[0].decode(errors="replace")
        author = self.data[40:56].split(b"\0")[0].decode(errors="replace")
        flags = [f for f, bit in (("WIDENS", FLAG_WIDENS), ("PRE", FLAG_PRE)) if self.p.flags & bit]
        self.notes.append(f"'{name}' by '{author}': {self.p.code_size:,} B of code, {self.p.state_size:,} B of state"
                          f", flags {' '.join(flags) or 'none'}")
        i, s = r.create(self.path.name)
        if i < 0:
            self.problem(f"the firmware can't make an instance: {s}")
            return False
        for k, (lo, hi, de, st) in enumerate(self.p.knobs):
            o = 92 + 32 * k
            label = self.data[o:o + 11].split(b"\0")[0].decode(errors="replace")
            texts = []
            for v in (lo, de, hi):
                r.set_knob(i, k, v)
                texts.append(r.knob_text(i, k))
            self.notes.append(f"knob {k}: {label:10} {lo:g} to {hi:g} step {st:g}, default {de:g}: "
                              f"texts {' / '.join(repr(t) for t in texts)}")
            if any(t == "" for t in texts):
                self.problem(f"knob {k} ({label}) shows no text at some value")
        r.call("rig_destroy", i)
        return True

    # ---- 2. blocks, called directly ---------------------------------------------------------
    def load(self) -> None:
        r, m = self.rig, self.rig.m
        m.write(rt.DIRECT_AT, self.p.code)
        m.uc.ctl_flush_tb()
        self.e = [(rt.DIRECT_AT + o) | 1 for o in self.p.entries[:4]]
        self.st = rt.DIRECT_AT + 0x10000
        self.L, self.R = rt.DIRECT_AT + 0x20000, rt.DIRECT_AT + 0x21000

    def new(self, knobs: list[float]) -> None:
        m = self.rig.m
        m.write(self.st, b"\0" * self.p.state_size)
        m.call(self.e[0], self.st, self.rig.host)
        self.set(knobs)

    def set(self, knobs: list[float]) -> None:
        m = self.rig.m
        for k, v in enumerate(knobs):
            m.uc.reg_write(UC_ARM_REG_S0, rt.f32(v))
            m.call(self.e[1], self.st, k)

    def block(self, x: np.ndarray, xr: np.ndarray | None, in_ch: int, with_r: bool, counted: list[int] | None = None):
        m = self.rig.m
        m.write(self.L, np.asarray(x, dtype="<f4").tobytes())
        if xr is not None:
            m.write(self.R, np.asarray(xr, dtype="<f4").tobytes())
        n = [0]
        hk = None
        if counted is not None:
            hk = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1))
            m.uc.ctl_flush_tb()
        try:
            m.call(self.e[2], self.st, self.L, self.R if with_r else 0, B, in_ch, max_insns=50_000_000)
        except RuntimeError as err:
            self.problem(f"process() didn't return: {err}")
            raise
        finally:
            if hk is not None:
                m.uc.hook_del(hk)
                m.uc.ctl_flush_tb()
        if counted is not None:
            counted.append(n[0])
        lo = np.frombuffer(m.read(self.L, 4 * B), dtype="<f4").astype(np.float64)
        ro = np.frombuffer(m.read(self.R, 4 * B), dtype="<f4").astype(np.float64) if with_r else None
        return lo, ro

    def layouts(self) -> list[tuple[str, int, bool, bool]]:
        """(name, in_channels, R given, R holds input) as the firmware will call it"""
        if self.p.flags & FLAG_PRE:
            return [("in a voice (mono, R null)", 1, False, False), ("on a part, mono", 1, False, False),
                    ("on a part, stereo", 2, True, True)]
        if self.p.flags & FLAG_WIDENS:
            return [("a mono part, widened", 1, True, False), ("a stereo part", 2, True, True)]
        return [("a mono part", 1, False, False), ("a stereo part", 2, True, True)]

    def settings(self) -> list[tuple[str, list[float]]]:
        knobs = self.p.knobs
        out = [("defaults", [k[2] for k in knobs]), ("all at min", [k[0] for k in knobs]),
               ("all at max", [k[1] for k in knobs])]
        rng = np.random.default_rng(7)
        for k in range(len(knobs)):
            for end in (0, 1):
                v = [kk[2] for kk in knobs]
                v[k] = knobs[k][end]
                out.append((f"knob {k} at its {'min' if end == 0 else 'max'}", v))
        for n in range(4):
            v = []
            for lo, hi, de, st in knobs:
                steps = int(round((hi - lo) / st))
                v.append(rt.from32(rt.f32(lo + int(rng.integers(0, steps + 1)) * st)))
            out.append((f"random {n + 1}", v))
        return out

    def run(self) -> None:
        self.load()
        m = self.rig.m
        inputs = [("noise", rt.rnd(11, 6 * B, 0.9)), ("a loud sine", np.sin(np.arange(6 * B) * 0.07) * 1.0),
                  ("silence", np.zeros(6 * B))]
        allowed = [(self.st, self.st + self.p.state_size), (self.L, self.L + 4 * B), (self.R, self.R + 4 * B), STACK]
        stray: list[int] = []

        def on_write(uc, access, addr, size, value, user):
            if not any(a <= addr < b for a, b in allowed) and len(stray) < 4:
                stray.append(addr)
        wh = m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
        costs: list[int] = []
        bad_out: list[str] = []
        try:
            for lname, in_ch, with_r, r_in in self.layouts():
                for sname, knobs in self.settings():
                    self.new(knobs)
                    for iname, x in inputs:
                        xr = (x[::-1] * 0.8) if r_in else None
                        for b in range(6):
                            seg = slice(b * B, (b + 1) * B)
                            lo, ro = self.block(x[seg], None if xr is None else xr[seg], in_ch, with_r,
                                                costs if b >= 2 else None)
                            for side, y in (("L", lo), ("R", ro)):
                                if y is None:
                                    continue
                                if not np.all(np.isfinite(y)):
                                    bad_out.append(f"{lname}, {sname}, {iname}: {side} not finite")
                                elif np.max(np.abs(y)) > 4.0:
                                    bad_out.append(f"{lname}, {sname}, {iname}: {side} reaches {np.max(np.abs(y)):.1f}")
                # every knob swept while it plays
                self.new([k[2] for k in self.p.knobs])
                x = rt.rnd(12, B, 0.7)
                for k, (lo_, hi_, de_, st_) in enumerate(self.p.knobs):
                    steps = int(round((hi_ - lo_) / st_))
                    for j in np.linspace(0, steps, min(steps + 1, 12)).astype(int):
                        v = [kk[2] for kk in self.p.knobs]
                        v[k] = rt.from32(rt.f32(lo_ + int(j) * st_))
                        self.set(v)
                        lo, ro = self.block(x, x if r_in else None, in_ch, with_r)
                        if not np.all(np.isfinite(lo)) or np.max(np.abs(lo)) > 4.0:
                            bad_out.append(f"{lname}, knob {k} swept: L {'not finite' if not np.all(np.isfinite(lo)) else 'over 4'}")
                            break
                # reset at any time: then as a fresh instance
                knobs = self.settings()[-1][1]
                x = rt.rnd(13, 4 * B, 0.8)
                self.new(knobs)
                fresh = [self.block(x[b * B:(b + 1) * B], x[b * B:(b + 1) * B] if r_in else None, in_ch, with_r)
                         for b in range(4)]
                self.new(knobs)
                for b in range(3):
                    self.block(rt.rnd(20 + b, B, 0.9), rt.rnd(30 + b, B, 0.9) if r_in else None, in_ch, with_r)
                m.call(self.e[3], self.st)
                again = [self.block(x[b * B:(b + 1) * B], x[b * B:(b + 1) * B] if r_in else None, in_ch, with_r)
                         for b in range(4)]
                same = all(np.array_equal(a[0], f[0]) and (a[1] is None or np.array_equal(a[1], f[1]))
                           for a, f in zip(again, fresh))
                if not same:
                    self.problem(f"{lname}: after reset() the output isn't a fresh instance's (state left over)")
        finally:
            m.uc.hook_del(wh)
        if stray:
            self.problem(f"writes outside its state and blocks (a global, or past its state), e.g. at "
                         f"{', '.join(hex(a) for a in stray)}")
        for b in bad_out[:4]:
            self.problem(f"output the firmware would change: {b}")
        if len(bad_out) > 4:
            self.problem(f"... and {len(bad_out) - 4} more blocks like it")
        most, mean = max(costs), sum(costs) / len(costs)
        self.notes.append(f"cost: {most:,} instructions per block at most, {mean:,.0f} on average; its share is "
                          f"about {SHARE:,.0f} cycles ({100 * most / SHARE:.1f} %), a whole block {4 * SHARE:,.0f} "
                          f"({100 * most / (4 * SHARE):.1f} %)")
        if most > SHARE:
            self.problem("over its share of a block: the firmware would bypass it")
        elif most > SHARE / 4:
            self.notes.append("warning: over a sixteenth of a block. Six of these, on three parts, would take a "
                              "quarter of the CPU before the synths' own work")


def real_math(rig: rt.Rig) -> None:
    """The host table's functions as the firmware's libm gives them (single precision)"""
    from unicorn.arm_const import UC_ARM_REG_S1
    m = rig.m
    rng = np.random.default_rng(1)

    def f1(fn):
        def stub(mm):
            x = rt.from32(mm.uc.reg_read(UC_ARM_REG_S0))
            try:
                y = fn(x)
            except (ValueError, OverflowError):
                y = math.nan
            mm.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", np.float32(y).tobytes())[0])
        return stub

    def f2(fn):
        def stub(mm):
            x, y = rt.from32(mm.uc.reg_read(UC_ARM_REG_S0)), rt.from32(mm.uc.reg_read(UC_ARM_REG_S1))
            try:
                z = fn(x, y)
            except (ValueError, OverflowError, ZeroDivisionError):
                z = math.nan
            mm.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", np.float32(z).tobytes())[0])
        return stub
    fns = [f1(math.sin), f1(math.cos), f1(math.tan), f1(math.tanh),
           f1(lambda x: math.exp(x) if x < 89 else math.inf), f1(lambda x: math.log(x) if x > 0 else -math.inf if x == 0 else math.nan),
           f2(math.pow), f1(lambda x: math.sqrt(x) if x >= 0 else math.nan), f1(abs),
           f2(lambda a, b: b if math.isnan(a) else a if math.isnan(b) else min(a, b)),
           f2(lambda a, b: b if math.isnan(a) else a if math.isnan(b) else max(a, b))]
    addrs = [m.add_stub(f) for f in fns] + [m.add_stub(lambda mm: int(rng.integers(0, 1 << 32)))]
    m.write(rig.host + 12, struct.pack(f"<{len(addrs)}I", *addrs))   # print stays the rig's


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("plugins", nargs="+", type=Path)
    ap.add_argument("--rig", type=Path, default=rt.ROOT / "build" / "emu" / "synthfx_rig.axf")
    a = ap.parse_args()
    rig = rt.Rig(a.rig)
    real_math(rig)
    failed = 0
    for path in a.plugins:
        c = Checker(rig, path)
        print(f"{path.name}:")
        try:
            if c.header():
                c.run()
        except RuntimeError:
            pass
        for n in c.notes:
            print(f"  {n}")
        for p in c.problems:
            print(f"  FAIL {p}")
        print(f"  {'passed' if not c.problems else 'FAILED'}")
        failed += 1 if c.problems else 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
