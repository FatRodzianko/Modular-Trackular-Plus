# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Synth FX default pack (synthfx-sdk/pack/, built by tools/synthfx/build_pack.py) against
the built-in effects it replaces, on the emulated Cortex-M7.

The built-in effects are Level A's (tools/synths/level_a/build.sh: core/mods/synthfx as the
firmware builds it). Each plugin is loaded from its .ptsfx file into the same emulator, as the
firmware loads it (its code copied to an address, its state zeroed, init, then every knob
set), and called through its entries with a host table whose functions are stubs here.

1. The files: magic, version, sizes, the entries inside the code, flags (the modulation
   effects widen), the knobs as the old rows had them.
2. Bit for bit against the built-in: each Distortion type at several Gains, Tones and
   Levels, then every knob moved (a ramp), mono and on a stereo part; each modulation effect
   mono (L in, L and R out) and stereo, its knobs at two settings. From the first sample.
3. Wherever it is loaded: the same output with the code at two addresses.
4. reset(): the next block starts as a fresh instance does.
5. The knobs' texts, as the old rows showed them.
6. Cost: instructions per block against the built-in's (the same code: within 3 %).

Usage: python tools/synthfx/test_pack.py <synths_level_a.axf> <pack folder> [--quick]
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "emu"))
import test_synths_dsp as t  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_S0  # noqa: E402

B = t.BLOCK
HEADER = 476
NO_ENTRY = 0xFFFFFFFF
LOAD_AT = (0x83000000, 0x83123448)      # two places in SDRAM, 8-byte aligned, as the pools are

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


class Plugin:
    """A .ptsfx, parsed"""

    def __init__(self, path: Path):
        self.path = path
        d = self.data = path.read_bytes()
        (self.magic, self.version, self.flags) = struct.unpack_from("<IHH", d, 0)
        self.name = d[8:40].split(b"\0")[0].decode()
        self.nparams = d[56]
        (self.state_size, self.code_offset, self.code_size, *self.entries) = struct.unpack_from("<9I", d, 60)
        self.knobs = []
        for i in range(self.nparams):
            o = 92 + 32 * i
            label = d[o:o + 11].split(b"\0")[0].decode()
            lo, hi, de, st = struct.unpack_from("<ffff", d, o + 12)
            unit = d[o + 28:o + 32].split(b"\0")[0].decode()
            self.knobs.append((label, lo, hi, de, st, unit, chr(d[o + 11])))
        self.code = d[self.code_offset:self.code_offset + self.code_size]


class Host:
    """The pack's plugins in Level A's emulator: their code at an address, the host table"""

    def __init__(self, r: t.Rig):
        self.r = r
        m = r.m
        self.printed = ""

        def print_stub(mm):
            out, size, fmt = mm.arg(0), mm.arg(1), mm.cstr(mm.arg(2))
            v = struct.unpack("<f", struct.pack("<I", mm.uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF))[0]
            text = (fmt % v).encode()[:max(size - 1, 0)]
            mm.write(out, text + b"\0")
            return len(text)

        def unused(name):
            def f(mm):
                failures.append(f"the default pack called the host's {name}")
                return 0
            return f
        fns = [m.add_stub(unused(n)) for n in ("sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "sqrtf",
                                                 "fabsf", "fminf", "fmaxf", "random")]
        fns.append(m.add_stub(print_stub))
        self.table = m.alloc(4 * (3 + len(fns)))
        m.write(self.table, struct.pack("<IfI", 1, 44100.0, 128) + struct.pack(f"<{len(fns)}I", *fns))
        self.L = m.alloc(4 * B * 64)
        self.R = m.alloc(4 * B * 64)
        self.text = m.alloc(64)

    def load(self, p: Plugin, at: int) -> dict[str, int]:
        self.r.m.write(at, p.code)
        self.r.m.uc.ctl_flush_tb()          # code that was there before stays translated otherwise
        names =("init", "set_param", "process", "reset", "format")
        return {n: (at + off) | 1 if off != NO_ENTRY else 0 for n, off in zip(names, p.entries)}

    def call_f(self, fn: int, *args: int, f: float) -> int:
        self.r.m.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", f))[0])
        return self.r.m.call(fn, *args, max_insns=200_000_000)

    def new(self, p: Plugin, e: dict[str, int], knobs: list[float] | None = None) -> int:
        """An instance, as the firmware makes one: zeroed state, init, every knob set"""
        st = self.r.m.alloc(p.state_size, 8)
        self.r.m.call(e["init"], st, self.table)
        for i, k in enumerate(p.knobs):
            self.call_f(e["set_param"], st, i, f=k[3] if knobs is None else knobs[i])
        return st

    def set(self, p: Plugin, e: dict[str, int], st: int, knobs: list[float]) -> None:
        for i, v in enumerate(knobs):
            self.call_f(e["set_param"], st, i, f=v)

    def run(self, e: dict[str, int], st: int, x: np.ndarray, xr: np.ndarray | None, in_ch: int, R: bool = True):
        n = len(x) // B
        self.r.put(self.L, x)
        if xr is not None:
            self.r.put(self.R, xr)
        for b in range(n):
            self.r.m.call(e["process"], st, self.L + 4 * B * b, (self.R + 4 * B * b) if R else 0, B, in_ch,
                          max_insns=200_000_000)
        out = self.r.get(self.L, len(x))
        return out, (self.r.get(self.R, len(x)) if R else None)

    def fmt(self, e: dict[str, int], i: int, v: float) -> str:
        self.r.m.write(self.text, b"\0" * 64)
        self.r.m.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", v))[0])
        self.r.m.call(e["format"], self.table, i, self.text, 48)
        return self.r.m.cstr(self.text)


DIST = {"Overdrive.ptsfx": t.DIST_OVERDRIVE, "Distortion.ptsfx": t.DIST_DISTORTION, "BitCrusher.ptsfx": t.DIST_CRUSHER}
MOD = {"Chorus.ptsfx": t.FX_CHORUS, "Flanger.ptsfx": t.FX_FLANGER, "Phaser.ptsfx": t.FX_PHASER}


def inputs(quick: bool):
    yield "sine 220", t.sine(220, 8, 0.4)
    yield "noise", t.noise(8, 5, 0.6)
    if not quick:
        yield "sine 3k loud", t.sine(3000, 8, 1.5)
        yield "huge noise", t.noise(8, 6, 30.0)
        yield "note then silence", np.concatenate([t.sine(440, 3, 0.5), np.zeros(5 * B)])


def p_array(vals: dict[int, float]) -> np.ndarray:
    p = np.zeros(t.P_COUNT)
    for k, v in {**t.FX_INIT, **vals}.items():
        p[k] = v
    return p


def ref_dist(r: t.Rig, kind: int, knobs1, knobs2, x: np.ndarray) -> np.ndarray:
    """The built-in Distortion from rest, on (dist_reset 1), 4 blocks, then the knobs moved"""
    out = []
    for i, (g, tone, lv) in enumerate((knobs1, knobs2)):
        p = p_array({t.P_DIST: kind, t.P_GAIN: g / 100.0, t.P_TONE: tone / 100.0, t.P_LEVEL: lv})
        r.put(r.pa, p)
        if i == 0:
            r.dist_clear()
            r.call("a_dist_reset", r.dist_at, r.pa, 1)
        seg = x[4 * B * i:4 * B * (i + 1)]
        r.put(r.buf, seg)
        r.call("a_dist_run", r.dist_at, r.buf, r.pa, 1, 4)
        out.append(r.get(r.buf, len(seg)))
    return np.concatenate(out)


def to_f32(x: np.ndarray) -> np.ndarray:
    return x.astype("<f4").astype(np.float64)


def distortions(h: Host, pack: dict[str, Plugin], quick: bool) -> None:
    r = h.r
    for fname, kind in DIST.items():
        p = pack[fname]
        e = h.load(p, LOAD_AT[0])
        cases = [((50, 50, 0.0), (50, 50, 0.0)), ((37, 60, -3.0), (63, 40, 4.0)), ((100, 0, -30.0), (0, 100, 6.0))]
        if not quick:
            cases += [((0, 100, 6.0), (100, 0, -12.5)), ((80, 20, 1.5), (80, 20, 1.5))]
        bad = []
        for name, x in inputs(quick):
            x = to_f32(x)
            for k1, k2 in cases:
                ref = ref_dist(r, kind, k1, k2, x)
                st = h.new(p, e, list(k1))
                a, _ = h.run(e, st, x[:4 * B], None, 1, R=False)
                h.set(p, e, st, list(k2))
                b, _ = h.run(e, st, x[4 * B:], None, 1, R=False)
                got = np.concatenate([a, b])
                if got.astype("<f4").tobytes() != ref.astype("<f4").tobytes():
                    bad.append(f"{name} {k1}->{k2}: {int(np.sum(got != ref))} samples, largest {np.max(np.abs(got - ref)):.3g}")
        check(not bad, f"{p.name}: bit for bit against the built-in, mono, every knob moving {bad[:3] or ''}")
        # a stereo part: each side its own Distortion, as fx_block_stereo ran dist and dist2
        x = to_f32(t.noise(8, 9, 0.5))
        xr = to_f32(t.sine(330, 8, 0.7))
        k1, k2 = (70, 30, -1.5), (20, 90, 2.0)
        ref_l, ref_r = ref_dist(r, kind, k1, k2, x), ref_dist(r, kind, k1, k2, xr)
        st = h.new(p, e, list(k1))
        a = h.run(e, st, x[:4 * B], xr[:4 * B], 2)
        h.set(p, e, st, list(k2))
        b = h.run(e, st, x[4 * B:], xr[4 * B:], 2)
        gl, gr = np.concatenate([a[0], b[0]]), np.concatenate([a[1], b[1]])
        check(np.array_equal(gl, ref_l) and np.array_equal(gr, ref_r),
              f"{p.name}: bit for bit on a stereo part, each side its own")
        # anywhere: the same at another address
        e2 = h.load(p, LOAD_AT[1])
        st2 = h.new(p, e2, list(k1))
        c = h.run(e2, st2, x[:4 * B], None, 1, R=False)[0]
        check(np.array_equal(c, ref_l[:4 * B]), f"{p.name}: the same loaded at another address")
        # reset: the next block as a fresh instance's
        h.run(e, st, t.noise(3, 4, 0.9), None, 1, R=False)
        h.set(p, e, st, list(k1))
        r.m.call(e["reset"], st)
        d = h.run(e, st, x[:4 * B], None, 1, R=False)[0]
        check(np.array_equal(d, ref_l[:4 * B]), f"{p.name}: after reset, as a fresh instance")


def ref_mod(r: t.Rig, eff: int, knobs1, knobs2, has_fb: bool, x, xr, stereo: bool, owner: int):
    s = t.fx_state(r, owner)
    outs = []
    for i, k in enumerate((knobs1, knobs2)):
        rate, depth = k[0], k[1]
        fb = k[2] if has_fb else 50
        mix = k[3] if has_fb else k[2]
        p = p_array({t.P_EFFECT: eff, t.P_DIST: t.DIST_OFF, t.P_RATE: rate / 100.0, t.P_DEPTH: depth / 100.0,
                     t.P_FEEDBACK: fb / 100.0, t.P_MIX: mix / 100.0})
        r.put(r.pa, p)
        seg = slice(4 * B * i, 4 * B * (i + 1))
        r.put(r.buf, x[seg])
        if stereo:
            r.put(r.buf2, xr[seg])
        r.call("a_fx_run_stereo" if stereo else "a_fx_run", s, r.pa, r.buf, r.buf2, 0, 4)
        outs.append((r.get(r.buf, 4 * B), r.get(r.buf2, 4 * B)))
    r.call("a_fx_release", owner)
    return np.concatenate([o[0] for o in outs]), np.concatenate([o[1] for o in outs])


def modulations(h: Host, pack: dict[str, Plugin], quick: bool) -> None:
    r = h.r
    owner = 0x7000
    for fname, eff in MOD.items():
        p = pack[fname]
        has_fb = p.nparams == 4
        e = h.load(p, LOAD_AT[0])
        k1 = [70, 80, 60, 50] if has_fb else [70, 80, 50]
        k2 = [100, 10, -90, 100] if has_fb else [100, 10, 100]
        bad = []
        for name, x in inputs(quick):
            x = to_f32(x)
            xr = to_f32(x[::-1] * 0.7)
            for stereo in (False, True):
                owner += 1
                ref = ref_mod(r, eff, k1, k2, has_fb, x, xr, stereo, owner)
                st = h.new(p, e, k1)
                a = h.run(e, st, x[:4 * B], xr[:4 * B] if stereo else None, 2 if stereo else 1)
                h.set(p, e, st, k2)
                b = h.run(e, st, x[4 * B:], xr[4 * B:] if stereo else None, 2 if stereo else 1)
                got = (np.concatenate([a[0], b[0]]), np.concatenate([a[1], b[1]]))
                if not (np.array_equal(got[0], ref[0]) and np.array_equal(got[1], ref[1])):
                    bad.append(f"{name} {'stereo' if stereo else 'mono'}")
        check(not bad, f"{p.name}: bit for bit against the built-in, mono and stereo, knobs moving {bad[:3] or ''}")
        x = to_f32(t.noise(4, 3, 0.5))
        owner += 1
        ref = ref_mod(r, eff, k1, k1, has_fb, np.concatenate([x, x]), None, False, owner)
        e2 = h.load(p, LOAD_AT[1])
        st2 = h.new(p, e2, k1)
        c = h.run(e2, st2, x, None, 1)
        check(np.array_equal(c[0], ref[0][:4 * B]) and np.array_equal(c[1], ref[1][:4 * B]),
              f"{p.name}: the same loaded at another address")
        h.run(e2, st2, t.noise(3, 8, 0.9), None, 1)
        r.m.call(e2["reset"], st2)
        d = h.run(e2, st2, x, None, 1)
        check(np.array_equal(d[0], ref[0][:4 * B]) and np.array_equal(d[1], ref[1][:4 * B]),
              f"{p.name}: after reset, as a fresh instance")


def files(pack: dict[str, Plugin]) -> None:
    for fname, p in pack.items():
        e = p.entries
        ok = (p.magic == 0x46535450 and p.version == 1 and p.code_offset == HEADER and
              len(p.data) == HEADER + p.code_size and
              all(o < p.code_size and o % 2 == 0 for o in e[:4]) and (e[4] == NO_ENTRY or e[4] < p.code_size))
        check(ok, f"{fname}: header, sizes and entries inside the code ({p.code_size} B code, {p.state_size} B state)")
    widen = {f: bool(p.flags & 1) for f, p in pack.items()}
    check(all(widen[f] for f in MOD) and not any(widen[f] for f in DIST),
          "the modulation effects widen, the Distortions don't")
    labels = {f: [k[0] for k in p.knobs] for f, p in pack.items()}
    want = {"Overdrive.ptsfx": ["Gain", "Tone", "Level"], "Distortion.ptsfx": ["Gain", "Tone", "Level"],
            "BitCrusher.ptsfx": ["Bits", "Rate", "Level"], "Chorus.ptsfx": ["Rate", "Depth", "Mix"],
            "Flanger.ptsfx": ["Rate", "Depth", "Feedback", "Mix"], "Phaser.ptsfx": ["Rate", "Depth", "Feedback", "Mix"]}
    check(labels == want, f"the knobs, as the old rows {labels if labels != want else ''}")
    defaults = {f: [k[3] for k in p.knobs] for f, p in pack.items()}
    want_d = {f: ([50, 50, 0] if f in DIST else [50, 50, 50, 50] if len(want[f]) == 4 else [50, 50, 50]) for f in pack}
    check(defaults == want_d, "the knobs' defaults are the old Init (Gain, Tone, Rate, Depth, Mix 50 %, "
                              "Feedback +50 %, Level 0 dB)")


def texts(h: Host, pack: dict[str, Plugin]) -> None:
    want = [("Overdrive.ptsfx", 0, 50, "+24 dB"), ("Overdrive.ptsfx", 1, 37, "37 %"), ("Overdrive.ptsfx", 2, -3.0, "-3.0 dB"),
            ("Overdrive.ptsfx", 2, 0.0, "+0.0 dB"), ("Distortion.ptsfx", 0, 50, "+30 dB"), ("Distortion.ptsfx", 0, 100, "+50 dB"),
            ("BitCrusher.ptsfx", 0, 50, "10.0 bit"), ("BitCrusher.ptsfx", 1, 100, "44.1 kHz"), ("BitCrusher.ptsfx", 1, 0, "1.0 kHz"),
            ("Chorus.ptsfx", 0, 50, "0.50 Hz"), ("Chorus.ptsfx", 0, 100, "5.00 Hz"), ("Chorus.ptsfx", 2, 50, "50 %"),
            ("Flanger.ptsfx", 2, 50, "+50 %"), ("Flanger.ptsfx", 2, -95, "-95 %"), ("Flanger.ptsfx", 2, 0, "0 %"),
            ("Phaser.ptsfx", 3, 100, "100 %"), ("Phaser.ptsfx", 1, 0, "0 %")]
    bad = []
    for fname, i, v, text in want:
        e = h.load(pack[fname], LOAD_AT[0])
        got = h.fmt(e, i, float(v))
        if got != text:
            bad.append(f"{fname} knob {i} at {v}: {got!r}, not {text!r}")
    check(not bad, f"the knobs' texts, as the old rows' {bad[:4] or ''}")


def costs(h: Host, pack: dict[str, Plugin]) -> None:
    r = h.r
    built = t.cost(r)
    n = [0]

    def counted(fn, blocks):
        hk = r.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1))
        r.m.uc.ctl_flush_tb()
        n[0] = 0
        fn()
        r.m.uc.hook_del(hk)
        r.m.uc.ctl_flush_tb()
        return n[0] / blocks
    names = {"Overdrive.ptsfx": "Overdrive", "Distortion.ptsfx": "Distortion", "BitCrusher.ptsfx": "Bit Crusher",
             "Chorus.ptsfx": "Chorus", "Flanger.ptsfx": "Flanger", "Phaser.ptsfx": "Phaser"}
    x = t.sine(220, 8, 0.4)
    over = []
    for fname, label in names.items():
        p = pack[fname]
        e = h.load(p, LOAD_AT[0])
        st = h.new(p, e)
        h.run(e, st, x[:4 * B], None, 1, R=fname in MOD)
        h.r.put(h.L, x[:4 * B])

        def go():
            for b in range(4):
                r.m.call(e["process"], st, h.L + 4 * B * b, (h.R + 4 * B * b) if fname in MOD else 0, B, 1,
                         max_insns=200_000_000)
        c = counted(go, 4)
        print(f"info cost {p.name}: {c:,.0f} instructions per block as a plugin, {built[label]:,.0f} built in")
        if c > built[label] * 1.03:
            over.append(f"{p.name} {c:,.0f} against {built[label]:,.0f}")
    check(not over, f"each plugin costs what the built-in did, within 3 % {over or ''}")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    axf, folder = Path(sys.argv[1]), Path(sys.argv[2])
    quick = "--quick" in sys.argv
    pack = {f: Plugin(folder / f) for f in list(DIST) + list(MOD)}
    r = t.Rig(axf)
    h = Host(r)
    files(pack)
    distortions(h, pack, quick)
    modulations(h, pack, quick)
    texts(h, pack)
    costs(h, pack)
    print(f"{checks} checks, {len(failures)} failed")
    for f in failures:
        print("  " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
