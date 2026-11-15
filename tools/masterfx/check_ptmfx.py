# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Checks a built Master FX plugin (.ptmfx) on the emulated Cortex-M7, before it goes on a card:
put on a fake card, listed, read and installed by the firmware's own runtime (a Tracker+ MT
firmware image, built with `make plus`), then run through the runtime's process() as the slot's
hook runs it.

    python tools/masterfx/check_ptmfx.py My.ptmfx [more.ptmfx or a folder of them ...] [--firmware TrackerPlus.axf]

1. The header, as the firmware's checkHeader reads it: a refused file says why. The slot, the
   labels, the own knobs and their texts at min, default and max.
2. Blocks on noise, a loud sine and silence, at several of CE's knob settings and at the own knobs'
   defaults, ends and random steps, then every own knob swept while it plays. For the dynamics
   slot: without a key, with one, and (PTMFX_FLAG_DUCK) ducking tracks.
   - float output finite and within +-4, before the firmware's guard would change it; past +-1 it
     clips when it goes back to 16 bits (a note, not a failure);
   - a ducking gain finite and within 0 to 4;
   - the meter's value (dynamics) finite and within 0 to 1;
   - the plugin's code writes only its state, the slot's memory, its output blocks and its own
     stack (a write that lands inside one of those by mistake can't be told apart);
   - after CE's clear (the memory cleared, then reset()), the same output as a fresh instance;
   - its cost: instructions per block in the plugin, the most and the mean, against the time
     guard's eighth of a block. Instructions are not cycles: the caches and SDRAM add to them on
     the unit, where the Profiler measures it.
A plugin's run stops at its first block over the time guard, or after 20 blocks of bad output.
Exit 0 if every plugin passed.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "emu"))
from machine import Machine  # noqa: E402
from masterfx_runtime import STATUS, Runtime, has_plugins  # noqa: E402
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_S0, UC_ARM_REG_SP  # noqa: E402

B = 128
HEADER = 240
CLOCK_HZ = 600e6
SHARE = CLOCK_HZ * B / 44100.0 / 8.0          # the time guard: an eighth of a block at 600 MHz
ENOUGH = 20                                   # blocks of bad output before a plugin's run stops


class Enough(Exception):
    """Enough seen of a plugin to fail it"""
FLAG_FLOAT, FLAG_DUCK = 1, 2
KINDS = {1: "reverb", 2: "delay", 3: "dynamics"}
SLOT = {"reverb": 0, "delay": 1, "dynamics": 2}
CE_LABELS = {"reverb": ["Size", "Damping", "Predelay", "Diffusion"],
             "delay": ["PingPong", "Sync", "Rate", "Time", "Feedback", "Filter"],
             "dynamics": ["Attack", "Release", "Threshold"]}

P = "_ZN4mods8masterfx7plugins"
CHECK_HEADER = P + "11checkHeaderERK11PtmfxHeadermPPKc"
PROCESS = P + "7processENS1_4SlotER10PtmfxBlockPf"
CLEARED = P + "7clearedENS1_4SlotE"
SET_KNOB = P + "7setKnobENS1_4SlotEmf"
KNOB_TEXT = P + "8knobTextENS1_4SlotEmPcm"

# CE's knobs as its pages give them to the slot (ptmfx.h's Ptmfx*Knobs)
REVERB_KNOBS = [  # amount, input gain, size, diffusion, 1 - damping, predelay (samples)
    (0.8, 0.1, 0.5, 0.625, 0.5, 0), (0.8, 0.1, 0.9, 0.85, 0.9, 1500), (0.8, 0.1, 0.0, 0.0, 0.0, 5000),
    (1.0, 0.1, 1.0, 0.85, 1.0, 250)]
DELAY_KNOBS = [  # time (samples), filter pole, feedback, pingpong
    (5000, -500, 120, 0), (300, -100, 252, 1), (44100, -1000, 60, 0), (130000, -800, 200, 1), (1, 0, 0, 0)]
DYNAMICS_KNOBS = [  # attack ms, release ms, threshold 0..1
    (1.0, 50.0, 1.0), (10.0, 300.0, 0.2), (100.0, 1000.0, 0.05), (0.5, 20.0, 0.01)]


def f32(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", v))[0]


def floats(m, a: int, n: int = B) -> list[float]:
    return list(struct.unpack(f"<{n}f", m.read(a, 4 * n)))


def q15(m, a: int) -> list[int]:
    return list(struct.unpack(f"<{B}h", m.read(a, 2 * B)))


class Header:
    def __init__(self, d: bytes):
        self.data = d
        self.flags, = struct.unpack_from("<H", d, 6)
        self.slot, self.nparams = d[8], d[9]
        self.name = d[12:44].split(b"\0")[0].decode(errors="replace")
        self.author = d[44:60].split(b"\0")[0].decode(errors="replace")
        self.labels = [d[60 + 12 * i:72 + 12 * i].split(b"\0")[0].decode(errors="replace") for i in range(6)]
        (self.state_size, self.code_offset, self.code_size, *self.entries) = struct.unpack_from("<8I", d, 132)
        self.knobs = []
        for k in range(min(self.nparams, 2)):
            o = 176 + 32 * k
            label = d[o:o + 11].split(b"\0")[0].decode(errors="replace")
            lo, hi, de, st = struct.unpack_from("<4f", d, o + 12)
            self.knobs.append((label, lo, hi, de, st))


class Checker:
    def __init__(self, firmware: Path, path: Path):
        self.firmware, self.path = firmware, path
        self.data = path.read_bytes()
        self.problems: list[str] = []
        self.notes: list[str] = []
        self.clips: list[str] = []

    def problem(self, text: str) -> None:
        self.problems.append(text)

    # ---- 1. the header and the install, through the firmware ---------------------------------
    def header(self) -> bool:
        if len(self.data) < HEADER:
            self.problem(f"{len(self.data)} bytes: not a .ptmfx (the header alone is {HEADER})")
            return False
        m = self.m = Machine(self.firmware)
        if not has_plugins(m):
            raise SystemExit(f"{self.firmware}: a firmware without the Master FX plugins (masterfx-1.x or CE)")
        h = self.h = Header(self.data)
        at = m.alloc(HEADER, 4)
        m.write(at, self.data[:HEADER])
        why = m.alloc(4, 4)
        m.w32(why, 0)
        ok = m.call(m.addr(CHECK_HEADER), at, len(self.data), why, max_insns=1_000_000) & 0xFF
        if not ok:
            reason = m.cstr(m.r32(why)) if m.r32(why) else "no reason given"
            if reason == "too big":
                reason += (f" (state {h.state_size:,} B, at most 1,024 for the reverb and 512 for the delay "
                           f"and dynamics, and at least 1; code {h.code_size:,} B, at most 8,192)")
            self.problem(f"the firmware refuses it: {reason} (ptmfx_builder.py says more)")
            return False
        self.kind = KINDS[h.slot]
        self.slot = SLOT[self.kind]
        if h.flags & FLAG_DUCK and "_ZN4mods8masterfx8duckMaskE" not in m.elf.symbols:
            self.notes.append("warning: this firmware can't duck tracks; the flag is for a later one")
        flags = [n for n, bit in (("FLOAT", FLAG_FLOAT), ("DUCK", FLAG_DUCK)) if h.flags & bit]
        self.notes.append(f"'{h.name}' by '{h.author}', the {self.kind} slot: {h.code_size:,} B of code, "
                          f"{h.state_size:,} B of state, flags {' '.join(flags) or 'none (16-bit blocks)'}")
        named = [f"{ce} as '{mine}'" for ce, mine in zip(CE_LABELS[self.kind], h.labels) if mine]
        self.notes.append("CE's knobs: " + (", ".join(named) if named else "all with CE's labels"))
        self.rt = Runtime(m, {f"/MasterFX/{self.path.name}": self.data})
        if not self.install():
            return False
        text = m.alloc(64, 4)
        for k, (label, lo, hi, de, st) in enumerate(h.knobs):
            texts = []
            for v in (lo, de, hi):
                self.set_knob(k, v)
                m.call(m.addr(KNOB_TEXT), self.slot, k, text, 64, max_insns=5_000_000)
                texts.append(m.cstr(text, 64))
            self.set_knob(k, de)
            self.notes.append(f"own knob {k}: {label:10} {lo:g} to {hi:g} step {st:g}, default {de:g}: "
                              f"texts {' / '.join(repr(t) for t in texts)}")
            if any(t == "" for t in texts):
                self.problem(f"own knob {k} ({label}) shows no text at some value")
        return True

    def install(self) -> bool:
        """A fresh instance, as masterfx::apply makes one: read, install (init on zeroed state), the
        slot's memory cleared, cleared()"""
        status = self.rt.install(self.kind, self.path.name)
        if status != "ok":
            self.problem(f"the firmware won't install it: {status}")
            return False
        self.clear()
        return True

    def clear(self) -> None:
        m = self.m
        areas, sizes = self.rt.memories[self.slot]
        for a, n in zip(areas, sizes):
            if a and n:
                m.write(a, bytes(n))
        m.call(m.addr(CLEARED), self.slot)

    def set_knob(self, k: int, v: float) -> None:
        self.m.uc.reg_write(UC_ARM_REG_S0, f32(v))
        self.m.call(self.m.addr(SET_KNOB), self.slot, k, max_insns=5_000_000)

    # ---- 2. blocks through the runtime's process() ---------------------------------------------
    def prepare(self) -> None:
        m = self.m
        self.left, self.right, self.inb = m.alloc(2 * B, 4), m.alloc(2 * B, 4), m.alloc(2 * B, 4)
        self.keyl, self.keyr = m.alloc(2 * B, 4), m.alloc(2 * B, 4)
        self.knobs_at = m.alloc(32, 4)
        self.gain_at = m.alloc(4 * B, 8)
        self.meter = m.alloc(4, 4)
        self.block_at = m.alloc(13 * 4, 4)
        code = self.rt.code[self.slot]
        self.entry = code + self.h.entries[2]
        self.code_range = (code, code + 8192)
        self.inside = False
        self.ret = None
        self.out_floats: tuple | None = None
        self.raw_meter = 1.0
        self.count = [0]

        def on_entry(uc, a, s, u):
            self.inside = True
            self.ret = uc.reg_read(UC_ARM_REG_LR) & ~1
            self.sp = uc.reg_read(UC_ARM_REG_SP)
        m.uc.hook_add(UC_HOOK_CODE, on_entry, begin=self.entry, end=self.entry)
        # a first block, to learn where the plugin's process() returns to in the runtime
        self.run_block([0] * B, [0] * B, None, [0] * B, self.ce_knobs()[0], count=False)
        if self.ret is None:
            raise RuntimeError("the runtime never called the plugin's process()")

        def on_return(uc, a, s, u):
            if not self.inside:
                return
            self.inside = False
            self.raw_meter = struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF))[0]
            if self.h.flags & FLAG_FLOAT:
                f = floats(self.m, self.rt.scratch, 2 * B)
                g = floats(self.m, self.gain_at) if self.ducking else None
                self.out_floats = (f[:B], f[B:], g)
        m.uc.hook_add(UC_HOOK_CODE, on_return, begin=self.ret, end=self.ret)
        # the float blocks' left and right (the keys and the delay's input are the host's)
        allowed = [(self.rt.states[self.slot], self.rt.states[self.slot] + self.h.state_size),
                   (self.rt.scratch, self.rt.scratch + 2 * 4 * B),
                   (self.left, self.left + 2 * B), (self.right, self.right + 2 * B),
                   (self.gain_at, self.gain_at + 4 * B)]
        areas, sizes = self.rt.memories[self.slot]
        allowed += [(a, a + n) for a, n in zip(areas, sizes) if a and n]
        self.stray: list[int] = []

        def on_write(uc, access, addr, size, value, user):
            pc = uc.reg_read(UC_ARM_REG_PC)
            if not self.code_range[0] <= pc < self.code_range[1]:
                return
            sp = uc.reg_read(UC_ARM_REG_SP)
            if sp - 0x200 <= addr < self.sp:  # its own stack (a push is seen before SP moves)
                return
            if not any(a <= addr < b for a, b in allowed) and len(self.stray) < 4:
                self.stray.append(addr)
        m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write)

    def ce_knobs(self) -> list[bytes]:
        if self.kind == "reverb":
            return [struct.pack("<5fi", *k) for k in REVERB_KNOBS]
        if self.kind == "delay":
            return [struct.pack("<IiBBxx", *k) for k in DELAY_KNOBS]
        return [struct.pack("<3fI", *k, 0) for k in DYNAMICS_KNOBS]

    def run_block(self, x, other, key, mono, knobs: bytes, count=True, duck=False):
        """One block: x and other are the left and right (reverb: the send; dynamics: the master),
        mono the delay's send, key the dynamics key (left, right) or None. The output: left, right
        and (ducking) the gain."""
        m = self.m
        self.ducking = duck
        if self.kind == "delay":
            m.write(self.inb, struct.pack(f"<{B}h", *mono))
            m.write(self.left, bytes(2 * B))
            m.write(self.right, bytes(2 * B))
        else:
            m.write(self.left, struct.pack(f"<{B}h", *x))
            m.write(self.right, struct.pack(f"<{B}h", *other))
        kl = kr = 0
        if key is not None:
            m.write(self.keyl, struct.pack(f"<{B}h", *key[0]))
            m.write(self.keyr, struct.pack(f"<{B}h", *key[1]))
            kl, kr = self.keyl, self.keyr
            knobs = knobs[:12] + struct.pack("<I", 1)
        m.write(self.knobs_at, knobs)
        if duck:
            m.write(self.gain_at, struct.pack(f"<{B}f", *([-7.0] * B)))   # not a gain: the plugin must fill it
        m.write(self.block_at, struct.pack("<13I", self.left, self.right, self.inb if self.kind == "delay" else 0,
                                           kl, kr, 0, 0, 0, 0, 0, self.knobs_at, B, self.gain_at if duck else 0))
        hk = None
        n = [0]
        if count:
            def counter(uc, a, s, u):
                if self.inside:
                    n[0] += 1
            hk = m.uc.hook_add(UC_HOOK_CODE, counter)
            m.uc.ctl_flush_tb()
        self.out_floats = None
        try:
            ran = m.call(m.addr(PROCESS), self.slot, self.block_at, self.meter, max_insns=50_000_000) & 0xFF
        finally:
            if hk is not None:
                m.uc.hook_del(hk)
                m.uc.ctl_flush_tb()
        if ran != 1:
            raise RuntimeError(f"the runtime didn't run it (Ran {ran})")
        self.instructions = n[0]
        out = (q15(m, self.left), q15(m, self.right), floats(m, self.gain_at) if duck else None)
        return out

    def judge(self, where: str, bad: list[str]) -> None:
        """The plugin's own output for the last block, before the firmware's guard"""
        if self.kind == "dynamics" and not (math.isfinite(self.raw_meter) and 0.0 <= self.raw_meter <= 1.0):
            bad.append(f"{where}: the meter's value {self.raw_meter} (the firmware holds it within 0 to 1)")
        if self.out_floats is None:
            return
        fl, fr, g = self.out_floats
        if g is not None:
            if not all(math.isfinite(v) for v in g):
                bad.append(f"{where}: the ducking gain isn't finite")
            elif min(g) < 0.0 or max(g) > 4.0:
                bad.append(f"{where}: the ducking gain {min(g):.2f} to {max(g):.2f}, not within 0 to 4"
                           + (" (left unfilled?)" if min(g) == -7.0 else ""))
            return
        for side, y in (("left", fl), ("right", fr)):
            if not all(math.isfinite(v) for v in y):
                bad.append(f"{where}: {side} not finite")
            elif max(abs(v) for v in y) > 4.0:
                bad.append(f"{where}: {side} reaches {max(abs(v) for v in y):.1f}")
            elif (max(abs(v) for v in y) > 1.0 and len(self.clips) < 3
                  and not any(c.startswith(f"{where}, {side} ") for c in self.clips)):
                self.clips.append(f"{where}, {side} {max(abs(v) for v in y):.2f}")

    def layouts(self) -> list[tuple[str, bool, bool]]:
        """(name, with a key, ducking)"""
        if self.kind != "dynamics":
            return [(f"the {self.kind} send", False, False)]
        out = [("the master, no key", False, False), ("the master with a key", True, False)]
        if self.h.flags & FLAG_DUCK:
            out.append(("ducking tracks by the key", True, True))
        return out

    def own_settings(self) -> list[tuple[str, list[float]]]:
        knobs = self.h.knobs
        out = [("own knobs at their defaults", [k[3] for k in knobs])]
        rnd = random.Random(7)
        for k in range(len(knobs)):
            for end in (1, 2):
                v = [kk[3] for kk in knobs]
                v[k] = knobs[k][end]
                out.append((f"own knob {k} at its {'min' if end == 1 else 'max'}", v))
        for n in range(2 if knobs else 0):
            v = []
            for _, lo, hi, de, st in knobs:
                steps = int(round((hi - lo) / st))
                v.append(lo + rnd.randint(0, steps) * st)
            out.append((f"own knobs random {n + 1}", v))
        return out

    def run(self) -> None:
        self.prepare()
        self.costs: list[int] = []
        self.bad: list[str] = []
        try:
            self.blocks()
        except Enough:
            self.notes.append("its run stopped early: " + ("a block over the time guard" if self.costs and
                              max(self.costs) > SHARE else f"{ENOUGH} blocks of bad output"))
        self.report()

    def blocks(self) -> None:
        rnd = random.Random(11)
        noise = [[rnd.randint(-29000, 29000) for _ in range(B)] for _ in range(6)]
        sine = [[int(32000 * math.sin(2 * math.pi * 220 * (b * B + i) / 44100)) for i in range(B)] for b in range(6)]
        silence = [[0] * B for _ in range(6)]
        kick = [[int(30000 * math.sin(2 * math.pi * 60 * (b * B + i) / 44100) * (1.0 if b < 3 else 0.1))
                 for i in range(B)] for b in range(6)]
        signals = [("noise", noise), ("a loud sine", sine), ("silence", silence)]
        ce = self.ce_knobs()
        costs, bad = self.costs, self.bad
        for lname, keyed, duck in self.layouts():
            for oname, own in self.own_settings():
                for ci, knobs in enumerate(ce if oname.endswith("defaults") else ce[:2]):
                    for sname, sig in signals:
                        if not self.install():
                            return
                        for k, v in enumerate(own):
                            self.set_knob(k, v)
                        for b, x in enumerate(sig):
                            other = [v // 2 for v in x]
                            key = (kick[b], kick[b]) if keyed else None
                            self.run_block(x, other, key, x, knobs, count=b >= 2, duck=duck)
                            if b >= 2:
                                costs.append(self.instructions)
                                if self.instructions > SHARE:
                                    raise Enough
                            self.judge(f"{lname}, {oname}, CE's knobs {ci}, {sname}", bad)
                            if len(bad) >= ENOUGH:
                                raise Enough
            # every own knob swept while it plays
            if not self.install():
                return
            for k, (label, lo, hi, de, st) in enumerate(self.h.knobs):
                steps = int(round((hi - lo) / st))
                for j in sorted({round(steps * t / 11) for t in range(12)}):
                    self.set_knob(k, lo + j * st)
                    key = (kick[0], kick[0]) if keyed else None
                    self.run_block(noise[0], noise[1], key, noise[0], ce[0], count=False, duck=duck)
                    self.judge(f"{lname}, own knob {k} swept", bad)
                    if len(bad) >= ENOUGH:
                        raise Enough
            # CE's clear at any time: then as a fresh instance
            script = [noise[b] for b in range(4)]
            key = (kick[0], kick[1]) if keyed else None
            if not self.install():
                return
            fresh = [self.run_block(x, x[::-1], key, x, ce[1], count=False, duck=duck) for x in script]
            if not self.install():
                return
            for b in range(3):
                self.run_block(sine[b], noise[b], key, sine[b], ce[1], count=False, duck=duck)
            self.clear()
            again = [self.run_block(x, x[::-1], key, x, ce[1], count=False, duck=duck) for x in script]
            if again != fresh:
                self.problem(f"{lname}: after CE's clear (memory cleared, reset()) the output isn't a fresh "
                             "instance's: something kept in the state isn't started over in reset()")

    def report(self) -> None:
        costs, bad = self.costs, self.bad
        if self.stray:
            self.problem("the plugin writes outside its state, the slot's memory and its blocks (a global, or "
                         f"past its state), e.g. at {', '.join(hex(a) for a in self.stray)}")
        for b in bad[:4]:
            self.problem(f"output the firmware would change: {b}")
        if len(bad) > 4:
            self.problem(f"... and {len(bad) - 4} more blocks like it" + (" (then it stopped)" if len(bad) >= ENOUGH
                                                                           else ""))
        if self.clips:
            self.notes.append("note: float output past full scale, which clips going back to 16 bits: "
                              + "; ".join(self.clips))
        if not costs:
            return
        most, mean = max(costs), sum(costs) / len(costs)
        self.notes.append(f"cost: {most:,} instructions per block at most, {mean:,.0f} on average; the time "
                          f"guard's eighth of a block is about {SHARE:,.0f} cycles ({100 * most / SHARE:.1f} %), "
                          f"a whole block {8 * SHARE:,.0f} ({100 * most / (8 * SHARE):.1f} %)")
        if most > SHARE:
            self.problem("over the time guard's eighth of a block: the firmware would bypass it")
        elif most > SHARE / 2:
            self.notes.append("warning: over half the time guard's share; the unit's caches and SDRAM add to "
                              "this, and the synths need most of the CPU")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("plugins", nargs="+", type=Path)
    ap.add_argument("--firmware", type=Path, default=ROOT / "platforms" / "imx" / "build-PLUS" / "TrackerPlus.axf",
                    help="a Tracker+ MT firmware image with the Master FX plugins (make plus)")
    a = ap.parse_args()
    if not a.firmware.exists():
        print(f"{a.firmware}: no firmware image; build one with `make plus` or pass --firmware")
        return 2
    paths = [q for p in a.plugins for q in (sorted(p.glob("*.ptmfx")) if p.is_dir() else [p])]
    if not paths:
        print("no .ptmfx files given")
        return 2
    failed = 0
    for path in paths:
        c = Checker(a.firmware, path)
        print(f"{path.name}:")
        try:
            if c.header():
                c.run()
        except RuntimeError as err:
            c.problem(f"it stopped the run: {err}")
        for n in c.notes:
            print(f"  {n}")
        for p in c.problems:
            print(f"  FAIL {p}")
        print(f"  {'passed' if not c.problems else 'FAILED'}")
        failed += 1 if c.problems else 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
