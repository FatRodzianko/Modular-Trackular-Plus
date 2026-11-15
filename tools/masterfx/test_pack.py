# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Master FX default pack (masterfx-sdk/pack/, built by tools/masterfx/build_pack.py) against
the built-in effects it replaces, on the emulated Cortex-M7.

The built-in effects are masterfx-1.x's, in a Tracker+ MT build from before the plugins (its
mods::masterfx::reverbProcess and delayProcess, with activeReverb or activeDelay set). Each
plugin is loaded from its .ptmfx file into the same emulator as the firmware loads it (its code
copied to an address, its state zeroed, init with the slot's memory, reset) and called through
its entries with a host table of stubs it must not call.

1. The files: magic, version, slot, sizes, the entries inside the code, no own knobs, the
   labels as masterfx-1.x's pages named the knobs.
2. Bit for bit against the built-in, from the first sample: each reverb and delay on noise, a
   sine and a note then silence, at several knob settings, the knobs moved while it plays, and
   a clear (CE's reset, the plugin's reset()) halfway, after which both lay out again.
3. Wherever it is loaded: the same output with the code at two addresses.
4. The test can fail: on each signal, some run of each effect gives output that isn't silent and
   isn't its input (a delay longer than the run is silent), and another effect of the same slot
   run through the same script is caught as different.

Usage: python tools/masterfx/test_pack.py <TrackerPlus_MT before plugins .axf> [pack folder] [--quick]
"""
from __future__ import annotations

import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "emu"))
from machine import Machine  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_S0  # noqa: E402

B = 128
HEADER = 240
NO_ENTRY = 0xFFFFFFFF
MAGIC = 0x464D5450
LOAD_AT = (0x83000000, 0x83123460)      # two places in SDRAM, 32-byte aligned as the code areas are
REVERB_BUFFER = 32768                   # bytes, each of CE's two
DELAY_BUFFER = 2 * 262144               # bytes, each of CE's lines
REVERBS = {"Freeverb.ptmfx": 1, "Plate.ptmfx": 2, "Spring.ptmfx": 3, "Shimmer.ptmfx": 4,
           "Gated.ptmfx": 5, "Reverse.ptmfx": 6}
DELAYS = {"Tape.ptmfx": 1, "BBD.ptmfx": 2}
LABELS = {
    "Freeverb.ptmfx": [], "Plate.ptmfx": ["Decay"], "Spring.ptmfx": ["Decay", "", "", "Drip"],
    "Shimmer.ptmfx": ["Decay", "Tone", "", "Shimmer"], "Gated.ptmfx": ["Length", "Tone", "", "Release"],
    "Reverse.ptmfx": ["Length", "Tone", "", "Release"], "Tape.ptmfx": [], "BBD.ptmfx": [],
}

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


class Plugin:
    """A .ptmfx, parsed"""

    def __init__(self, path: Path):
        d = self.data = path.read_bytes()
        (self.magic, self.version, self.flags, self.slot, self.nparams) = struct.unpack_from("<IHHBB", d, 0)
        self.reserved = d[10:12]
        self.name = d[12:44].split(b"\0")[0].decode()
        self.labels = [d[60 + 12 * i:72 + 12 * i].split(b"\0")[0].decode() for i in range(6)]
        (self.state_size, self.code_offset, self.code_size, *self.entries) = struct.unpack_from("<8I", d, 132)
        self.code = d[self.code_offset:self.code_offset + self.code_size]


def reverb_knobs(size, damp, pre, diff, amount=0.8, gain=0.1) -> bytes:
    return struct.pack("<fffffi", amount, gain, size, diff, 1.0 - damp, int(pre * 5000))


def delay_knobs(time, feedback, pole, pingpong) -> bytes:
    return struct.pack("<IiBBxx", time, pole, feedback, pingpong)


class Rig:
    """The built-in effects and the plugins in one emulator, on the same blocks"""

    def __init__(self, axf: Path):
        m = self.m = Machine(axf)
        self.left, self.right, self.inb = m.alloc(2 * B, 4), m.alloc(2 * B, 4), m.alloc(2 * B, 4)
        self.knobs = m.alloc(32, 4)
        self.bufs = [m.alloc(DELAY_BUFFER, 32) for _ in range(3)]  # big enough for either slot
        self.mem = m.alloc(24, 4)
        self.block = m.alloc(48, 4)
        self.state = m.alloc(4096, 8)

        def unused(name):
            def f(mm):
                failures.append(f"the default pack called the host's {name}")
                return 0
            return f
        names = ("sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "sqrtf", "fabsf", "fminf", "fmaxf",
                 "random", "print", "tempo")
        fns = [m.add_stub(unused(n)) for n in names]
        self.host = m.alloc(4 * (3 + len(fns)), 4)
        m.write(self.host, struct.pack("<IfI", 1, 44100.0, B) + struct.pack(f"<{len(fns)}I", *fns))
        self.reverb_fn = m.addr("_ZN4mods8masterfx13reverbProcessERKNS0_11ReverbKnobsEPtS4_PsS5_")
        self.delay_fn = m.addr("_ZN4mods8masterfx12delayProcessERKNS0_10DelayKnobsEPsS4_PKsS4_S4_")
        self.reverb_cleared = m.addr("_ZN4mods8masterfx13reverbClearedEv")
        self.delay_cleared = m.addr("_ZN4mods8masterfx12delayClearedEv")
        self.active = {"reverb": m.addr("_ZN4mods8masterfx12activeReverbE"),
                       "delay": m.addr("_ZN4mods8masterfx11activeDelayE")}

    def clear(self, sizes) -> None:
        for b, n in zip(self.bufs, sizes):
            self.m.write(b, b"\0" * n)

    def put(self, a: int, x: list[int]) -> None:
        self.m.write(a, struct.pack(f"<{B}h", *x))

    def get(self, a: int) -> list[int]:
        return list(struct.unpack(f"<{B}h", self.m.read(a, 2 * B)))

    # -- the built-in --------------------------------------------------------------------
    def builtin(self, kind: str, which: int, script) -> list[int]:
        """script: (knobs, left, right or mono in, clear before) per block"""
        m = self.m
        m.write(self.active[kind], bytes([which]))
        out = []
        for knobs, a, b, cleared in script:
            if cleared:
                self.clear([REVERB_BUFFER] * 2 if kind == "reverb" else [DELAY_BUFFER] * 2)
                m.call(self.reverb_cleared if kind == "reverb" else self.delay_cleared)
            m.write(self.knobs, knobs)
            if kind == "reverb":
                self.put(self.left, a)
                self.put(self.right, b)
                m.call(self.reverb_fn, self.knobs, self.bufs[0], self.bufs[1], self.left, self.right,
                       max_insns=50_000_000)
            else:
                self.put(self.inb, a)
                m.call(self.delay_fn, self.knobs, self.bufs[0], self.bufs[1], self.inb, self.left, self.right,
                       max_insns=50_000_000)
            out += self.get(self.left) + self.get(self.right)
        return out

    # -- the plugin, as the runtime runs it ----------------------------------------------
    def load(self, p: Plugin, at: int) -> dict[str, int]:
        self.m.write(at, p.code)
        self.m.uc.ctl_flush_tb()
        names = ("init", "set_param", "process", "reset", "format")
        return {n: (at + off) | 1 if off != NO_ENTRY else 0 for n, off in zip(names, p.entries)}

    def plugin(self, p: Plugin, e: dict[str, int], kind: str, script) -> list[int]:
        m = self.m
        sizes = [REVERB_BUFFER] * 2 if kind == "reverb" else [DELAY_BUFFER] * 3
        m.write(self.mem, struct.pack("<3I3I", *(self.bufs + [0] * 3)[:3], *(sizes + [0])[:3]))
        m.write(self.state, b"\0" * p.state_size)
        m.call(e["init"], self.state, self.mem, self.host)
        out = []
        for knobs, a, b, cleared in script:
            if cleared:
                self.clear(sizes)
                m.call(e["reset"], self.state)
            m.write(self.knobs, knobs)
            if kind == "reverb":
                self.put(self.left, a)
                self.put(self.right, b)
                ptrs = (self.left, self.right, 0, 0, 0)
            else:
                self.put(self.inb, a)
                m.write(self.left, b"\0" * 2 * B)   # the host's silence before (not used by the pack)
                m.write(self.right, b"\0" * 2 * B)
                ptrs = (self.left, self.right, self.inb, 0, 0)
            m.write(self.block, struct.pack("<12I", *ptrs, 0, 0, 0, 0, 0, self.knobs, B))
            m.call(e["process"], self.state, self.block, max_insns=50_000_000)
            gain = struct.unpack("<f", struct.pack("<I", m.uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF))[0]
            if gain != 1.0:
                failures.append(f"{p.name}: process returned {gain}, not 1.0")
            out += self.get(self.left) + self.get(self.right)
        return out


def signals(quick: bool, rnd: random.Random):
    n = 24 if quick else 64
    noise = [[rnd.randint(-12000, 12000) for _ in range(B)] for _ in range(n)]
    yield "noise", noise
    sine = [[int(9000 * __import__("math").sin(2 * 3.14159265 * 330 * (b * B + i) / 44100)) for i in range(B)]
            for b in range(n)]
    yield "sine", sine
    if not quick:
        loud = [[rnd.choice((-32768, 32767, rnd.randint(-32768, 32767))) for _ in range(B)] for _ in range(n)]
        yield "full scale", loud
        note = noise[:6] + [[0] * B for _ in range(n - 6)]
        yield "note then silence", note


def reverb_cases(quick: bool):
    cases = [(0.5, 0.5, 0.0, 0.625), (0.9, 0.1, 0.3, 0.85), (0.0, 1.0, 1.0, 0.0)]
    if not quick:
        cases += [(1.0, 0.0, 0.05, 0.4), (0.3, 0.7, 0.6, 0.2)]
    return [reverb_knobs(*c) for c in cases]


def delay_cases(quick: bool):
    cases = [(5000, 120, -500, 0), (300, 252, -100, 1), (44100, 60, -1000, 0)]
    if not quick:
        cases += [(130000, 200, -800, 1), (1, 0, 0, 0)]
    return [delay_knobs(*c) for c in cases]


def script_of(kind: str, knobs: list[bytes], k: int, sig: list[list[int]], rnd: random.Random):
    """Blocks with knob set k, then moved to the next set a third in, a clear two thirds in"""
    n = len(sig)
    out = []
    for i, x in enumerate(sig):
        kn = knobs[k] if i < n // 3 else knobs[(k + 1) % len(knobs)]
        other = [v // 2 for v in x] if kind == "reverb" else None
        out.append((kn, x, other, i == 0 or i == 2 * n // 3))
    return out


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    quick = "--quick" in sys.argv
    if not args:
        print(__doc__)
        return 2
    axf = Path(args[0])
    pack_dir = Path(args[1]) if len(args) > 1 else ROOT / "build" / "masterfx" / "pack"
    rig = Rig(axf)
    pack = {f: Plugin(pack_dir / f) for f in list(REVERBS) + list(DELAYS)}

    # 1. the files
    for f, p in pack.items():
        reverb = f in REVERBS
        ok = (p.magic == MAGIC and p.version == 1 and p.flags == 0 and p.nparams == 0 and p.reserved == b"\0\0"
              and p.slot == (1 if reverb else 2) and p.code_offset == HEADER and len(p.data) == HEADER + p.code_size
              and p.code_size <= 8192 and p.state_size <= (1024 if reverb else 512)
              and all(o < p.code_size and o % 2 == 0 for o in p.entries[:4])
              and (p.entries[4] == NO_ENTRY or p.entries[4] < p.code_size))
        check(ok, f"{f}: the header (slot, sizes {p.code_size}/{p.state_size}, entries)")
        want = LABELS[f] + [""] * (6 - len(LABELS[f]))
        check(p.labels == want, f"{f}: the labels {p.labels}")

    # 2 and 3. bit for bit, at two addresses
    rnd = random.Random(7)
    sigs = list(signals(quick, rnd))
    for f, p in pack.items():
        kind = "reverb" if f in REVERBS else "delay"
        which = REVERBS.get(f) or DELAYS[f]
        knobs = reverb_cases(quick) if kind == "reverb" else delay_cases(quick)
        bad = []
        cases = 0
        live = {}
        for name, sig in sigs:
            for k in range(len(knobs)):
                script = script_of(kind, knobs, k, sig, rnd)
                ref = rig.builtin(kind, which, script)
                dry = [v for _, a, b, _ in script for v in (a + (b or a) if kind == "reverb" else [0] * 2 * B)]
                live[name] = live.get(name, False) or (any(ref) and ref != dry)
                for at in (LOAD_AT if k == 0 else LOAD_AT[:1]):
                    got = rig.plugin(p, rig.load(p, at), kind, script)
                    cases += 1
                    if got != ref:
                        diff = sum(1 for a, b in zip(got, ref) if a != b)
                        first = next(i for i, (a, b) in enumerate(zip(got, ref)) if a != b)
                        bad.append(f"{name} knobs {k} at {at:#x}: {diff} samples differ, first at {first}")
        check(not bad and cases > 0, f"{f}: as the built-in, bit for bit, {cases} runs" +
              (": " + "; ".join(bad[:3]) if bad else ""))
        check(all(live.values()), f"{f}: on each signal, a run neither silent nor the dry send {live}")

    # 4. a different effect is caught
    for f, other in (("Plate.ptmfx", 4), ("Reverse.ptmfx", 5), ("BBD.ptmfx", 1)):
        kind = "reverb" if f in REVERBS else "delay"
        knobs = reverb_cases(True) if kind == "reverb" else delay_cases(True)
        script = script_of(kind, knobs, 0, sigs[0][1], rnd)
        got = rig.plugin(pack[f], rig.load(pack[f], LOAD_AT[0]), kind, script)
        check(got != rig.builtin(kind, other, script), f"{f} against built-in effect {other}: caught as different")

    print(f"masterfx pack: {checks} checks, {len(failures)} failed")
    for f in failures:
        print("  " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
