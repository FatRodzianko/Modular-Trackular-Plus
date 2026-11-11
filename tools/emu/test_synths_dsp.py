# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Level A for the Synths mod (WAVE and PHASE): the engines and effects alone, on the emulated
Cortex-M7, beside sy9's (Modular-Trackular's patch/synths, the code the 1.2.0 synths patch ran).
docs/mt/SOUND_TEST_PLAN.md has the levels; B and C, through CE's synth host, are
tools/emu/test_synths.py.

The ELF is tools/synths/level_a/build.sh's: core/mods/synths built as the firmware builds it,
and sy9's engines and Chorus, each behind plain C entry points.

1. WAVE and PHASE against sy9, sample for sample, with the Distortion Off: the Init patches and
   one change at a time (every filter type, every Wave and shape, Digital, Noise, glide, Ring
   Mod, the Mod Matrix's sources and destinations, Sync, Polarity, Drive, pitch bend), a held
   note and its release, a second note while the first is held.
2. The Chorus against sy9's, and Effect None leaves R a copy of L.
3. Flanger and Phaser: stereo, notches where the delay or the all-passes put them, Feedback
   deepens them, bounded at Feedback 0.95, Mix 0 is dry, switching fades over about 60 ms.
4. Distortion: Overdrive and Distortion add odd harmonics that grow with Gain, Distortion more
   than Overdrive, Tone brightens, Level is dB, aliasing of a 5 kHz sine stays low, the output
   is bounded; the dry/wet fade; Bit Crusher's Bits and Rate; Pre against Post on WAVE; PHASE's
   voice ignores Placement; a Pre Distortion is there from a new note's first sample.
5. The effect states: three instruments, a fourth gets none, release frees one, a stale one is
   taken over, garbage left in SDRAM changes nothing.
6. The Level default: what the Distortion does to the Init patch's loudness, printed.
7. Cost: instructions per block for each engine and effect, under regression ceilings (the CPU
   load itself is the device's Profiler's).

Planted faults (tools/synths/level_a/plant.py) build the ELF with one mistake each and expect
this test to fail.

Usage: python tools/emu/test_synths_dsp.py build/emu/synths_level_a.axf [--quick]
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402

BLOCK = 128
RATE = 44100.0
P_COUNT = 72
P_EFFECT, P_RATE, P_DEPTH, P_MIX, P_FEEDBACK, P_DIST, P_GAIN, P_TONE, P_PLACEMENT, P_LEVEL = range(62, 72)
FX_NONE, FX_CHORUS, FX_FLANGER, FX_PHASER = range(4)
DIST_OFF, DIST_OVERDRIVE, DIST_DISTORTION, DIST_CRUSHER = range(4)
PRE, POST = 0, 1

# sy9's Init values by parameter id (its tools/emu/test_synths.py and test_phase.py). 39 is
# Volume, per voice (velocity).
WAVE_INIT = {0: 0.6667, 1: 0.6667, 2: 4.0, 3: 4.0, 4: 0.0, 5: 0.0, 6: 0.0, 7: 8.0, 8: 0.0, 9: 0.0, 10: 0.0,
             11: 0.0, 12: 2400.0, 13: 0.2, 14: 0.15, 15: 0.005, 16: 0.8, 17: 0.35, 18: 0.5, 19: 0.005, 20: 0.5,
             21: 0.8, 22: 0.4, 23: 0.5, 24: 1.0, 25: 0.0, 26: 0.5, 27: 1.0, 28: 0.0, 29: 2.0, 30: 0.0, 31: 0.25,
             32: 1.0, 33: 2.0, 34: 0.0, 35: 0.0, 36: 0.0, 37: 0.5, 38: 0.0, 40: 0.0, 41: 0.0, 42: 3.0, 43: 0.0,
             44: 0.0, 45: 2.0, 46: 0.0, 47: 0.0, 60: 0.0, 61: 0.0}
PHASE_INIT = {0: 0.0, 1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0, 5: 0.0, 6: 0.0, 7: 8.0, 8: 0.0, 9: 0.0, 10: 0.0, 11: 0.0,
              12: 0.5, 13: 0.5, 14: 0.3, 15: 0.005, 16: 0.3, 17: 0.0, 18: 0.3, 19: 0.005, 20: 0.5, 21: 0.8,
              22: 0.4, 23: 0.005, 24: 0.6, 25: 0.3, 26: 0.5, 27: 1.0, 28: 0.0, 29: 2.0, 30: 0.0, 31: 0.25,
              32: 1.0, 33: 2.0, 34: 0.0, 35: 0.0, 36: 0.0, 37: 0.0, 38: 0.3, 40: 0.0, 41: 0.0, 42: 3.0, 43: 0.0,
              44: 0.0, 45: 4.0, 46: 3.0, 47: 0.0, 60: 0.0, 61: 0.0}
# Ours: the effects' Init (synthsSdk.cpp): Effect None, Rate, Depth, Mix 0.5, Feedback 0.5,
# Distortion Off, Gain and Tone 0.5, Placement Post, Level 0 dB.
FX_INIT = {P_EFFECT: 0.0, P_RATE: 0.5, P_DEPTH: 0.5, P_MIX: 0.5, P_FEEDBACK: 0.5, P_DIST: 0.0, P_GAIN: 0.5,
           P_TONE: 0.5, P_PLACEMENT: 1.0, P_LEVEL: 0.0}

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


class Rig:
    def __init__(self, axf: Path):
        self.m = Machine(axf)
        m = self.m
        self.size = {k: m.r32(m.addr(k)) for k in ("a_wave_size", "a_phase_size", "a_fx_size", "a_dist_size",
                                                     "r_wave_size", "r_phase_size", "r_fx_size",
                                                     "r_wave_params", "r_phase_params", "r_slot_params")}
        assert m.r32(m.addr("a_params")) == P_COUNT
        n = self.size["r_slot_params"]
        self.wt_slot = list(m.read(m.r32(m.addr("r_wt_param_slot")), n))
        self.ph_slot = list(m.read(m.r32(m.addr("r_ph_param_slot")), n))
        self.fn = {}
        self.ctl = m.alloc(12)
        self.pa = m.alloc(4 * P_COUNT)
        self.pr = m.alloc(4 * P_COUNT)
        self.fxp = m.alloc(16)
        self.buf = m.alloc(4 * BLOCK * 400)
        self.buf2 = m.alloc(4 * BLOCK * 400)
        # ours and sy9's voice at one address: both seed their oscillators' phases from it
        at = {e: m.alloc(max(self.size[f"a_{e}_size"], self.size[f"r_{e}_size"])) for e in ("wave", "phase")}
        self.voices = {f"{side}_{e}_size": at[e] for side in ("a", "r") for e in ("wave", "phase")}
        self.dist_at = m.alloc(self.size["a_dist_size"])
        self.rfx = m.alloc(self.size["r_fx_size"])

    def call(self, name: str, *args: int) -> int:
        if name not in self.fn:
            self.fn[name] = self.m.addr(name)
        return self.m.call(self.fn[name], *args, max_insns=200_000_000)

    def put(self, a: int, xs) -> None:
        self.m.write(a, np.asarray(xs, dtype="<f4").tobytes())

    def get(self, a: int, n: int) -> np.ndarray:
        return np.frombuffer(self.m.read(a, 4 * n), dtype="<f4").astype(np.float64)

    def set_ctl(self, tempo=120.0, bend=0.0, volume=0.8):
        self.put(self.ctl, [tempo, bend, volume])

    # ---- the engines, ours and sy9's --------------------------------------------------
    def params(self, engine: str, values: dict[int, float]):
        """Ours: p by parameter id. sy9's: its slot copy, through its own map."""
        ours = np.zeros(P_COUNT)
        for k, v in values.items():
            ours[k] = v
        slot = self.wt_slot if engine == "wave" else self.ph_slot
        ref = np.zeros(P_COUNT)
        for k, v in values.items():
            if k < len(slot) and slot[k] != 0xFF:
                ref[slot[k]] = v
        self.put(self.pa, ours)
        self.put(self.pr, ref)

    def play(self, engine: str, values: dict[int, float], script, which=("a", "r")) -> dict[str, np.ndarray]:
        """script: ("on", note) / ("off",) / ("render", blocks) / ("bend", st) / ("set", id, v)."""
        out = {}
        for side in which:
            vals = dict(values)
            v = self.voices[f"{side}_{engine}_size"]
            self.call(f"{side}_{engine}_init", v)
            self.params(engine, vals)
            p = self.pa if side == "a" else self.pr
            bend = 0.0
            self.set_ctl(bend=bend)
            chunks = []
            for step in script:
                if step[0] == "on":
                    self.call(f"{side}_{engine}_on", v, p, step[1], self.ctl)
                elif step[0] == "off":
                    self.call(f"{side}_{engine}_off", v)
                elif step[0] == "bend":
                    bend = step[1]
                    self.set_ctl(bend=bend)
                elif step[0] == "set":
                    vals[step[1]] = step[2]
                    self.params(engine, vals)
                else:
                    n = step[1]
                    self.call(f"{side}_{engine}_render", v, p, self.buf, self.ctl, n)
                    chunks.append(self.get(self.buf, BLOCK * n))
            out[side] = np.concatenate(chunks)
        return out

    def sounding(self, engine: str) -> int:
        return self.call(f"a_{engine}_sounding", self.voices[f"a_{engine}_size"])

    # ---- the effects --------------------------------------------------------------------
    def fx(self, state: int, values: dict[int, float], x: np.ndarray, dist_wanted: int) -> tuple[np.ndarray, np.ndarray]:
        p = np.zeros(P_COUNT)
        for k, v in values.items():
            p[k] = v
        self.put(self.pa, p)
        n = len(x) // BLOCK
        self.put(self.buf, x)
        self.call("a_fx_run", state, self.pa, self.buf, self.buf2, dist_wanted, n)
        return self.get(self.buf, len(x)), self.get(self.buf2, len(x))

    def dist(self, values: dict[int, float], x: np.ndarray, want=1, reset: int | None = None) -> np.ndarray:
        p = np.zeros(P_COUNT)
        for k, v in values.items():
            p[k] = v
        self.put(self.pa, p)
        if reset is not None:
            self.call("a_dist_reset", self.dist_at, self.pa, reset)
        n = len(x) // BLOCK
        self.put(self.buf, x)
        self.call("a_dist_run", self.dist_at, self.buf, self.pa, want, n)
        return self.get(self.buf, len(x))

    def dist_clear(self):
        self.m.write(self.dist_at, b"\0" * self.size["a_dist_size"])


# ---- signal helpers ------------------------------------------------------------------------
def sine(hz: float, blocks: int, amp=0.5) -> np.ndarray:
    t = np.arange(blocks * BLOCK) / RATE
    return amp * np.sin(2 * np.pi * hz * t)


def noise(blocks: int, seed=1, amp=0.3) -> np.ndarray:
    return amp * np.random.default_rng(seed).uniform(-1, 1, blocks * BLOCK)


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


def db(x: float) -> float:
    return 20 * math.log10(max(x, 1e-12))


def spectrum(x: np.ndarray) -> np.ndarray:
    w = np.hanning(len(x))
    return np.abs(np.fft.rfft(x * w)) / (np.sum(w) / 2)


def harmonic(x: np.ndarray, hz: float, k: int) -> float:
    s = spectrum(x)
    b = k * hz * len(x) / RATE
    i = int(round(b))
    return float(np.max(s[max(i - 2, 0):i + 3]))


def centroid(x: np.ndarray) -> float:
    s = spectrum(x)
    f = np.arange(len(s)) * RATE / len(x)
    return float(np.sum(f * s) / max(np.sum(s), 1e-12))


def same(a: np.ndarray, b: np.ndarray) -> float:
    """The largest difference, relative to the larger signal's peak."""
    return float(np.max(np.abs(a - b)) / max(np.max(np.abs(a)), np.max(np.abs(b)), 1e-9))


# ---- 1. the engines against sy9 ----------------------------------------------------------
PARITY_LIMIT = 1e-5     # the same code, built as C++ and as C: float rounding at most


def parity(r: Rig, quick: bool) -> None:
    held = [("on", 60), ("render", 30), ("off",), ("render", 30)]
    legato = [("on", 48), ("render", 12), ("on", 55), ("render", 12), ("off",), ("render", 20)]
    wave_cases = [("Init", {}, held), ("second note held", {}, legato)]
    wave_cases += [(f"Type {t}", {11: t, 13: 1.2}, held) for t in range(15)]
    wave_cases += [(f"Wave {w}", {2: w, 3: (w + 3) % 9, 0: 0.3, 1: 0.9, 4: -0.4}, held) for w in range(9)]
    wave_cases += [("Digital", {40: 1.0}, held), ("Noise", {41: 0.6}, held), ("Drive", {38: 0.8}, held),
                   ("Reset", {9: 1.0, 10: 1.0}, held), ("Tune, Finetune", {5: 7, 6: -12, 8: 33, 7: 40}, held),
                   ("Env Amt -1, Key Track 2", {14: -1.0, 37: 2.0}, held),
                   ("Glide Always", {35: 0.0, 36: 0.3}, legato), ("Glide Legato", {35: 2.0, 36: 0.2}, legato),
                   ("LFO Sync, Unipolar", {30: 1.0, 27: 7.0, 60: 1.0, 44: 0.7}, held),
                   ("LFO 2 S&H, One Shot", {32: 5.0, 33: 1.0, 31: 9.0, 47: 0.5, 46: 7.0, 45: 4.0}, held),
                   ("pitch bend", {}, [("on", 60), ("render", 8), ("bend", 2.0), ("render", 8), ("bend", -1.5),
                                       ("render", 8)])]
    for d in range(11):
        for s in (0, 1, 3, 5, 6):
            if quick and (d + s) % 3:
                continue
            wave_cases.append((f"Mod {s}->{d}", {48: s, 49: d, 50: 0.6, 51: 4.0, 52: d, 53: -0.3}, held))
    phase_cases = [("Init", {}, held), ("second note held", {}, legato)]
    phase_cases += [(f"Shape {k}", {2: k, 3: (k + 2) % 8, 0: (k + 1) % 9, 1: k % 9, 4: 0.3}, held) for k in range(8)]
    phase_cases += [("Ring Mod", {11: 1.0, 6: 7.0}, held), ("DCW 0 and 1", {12: 0.0, 13: 1.0}, held),
                    ("Env Amt -1, Key Follow 1", {14: -1.0, 38: 1.0, 37: 1.0}, held),
                    ("Noise Mod", {40: 0.7}, held), ("DCO Env Amt 24", {41: 24.0}, held),
                    ("Glide Overlap", {35: 1.0, 36: 0.25}, legato),
                    ("pitch bend", {}, [("on", 60), ("render", 8), ("bend", 2.0), ("render", 8)])]
    for d in range(9):
        for s in (0, 2, 3, 6):
            if quick and (d + s) % 3:
                continue
            phase_cases.append((f"Mod {s}->{d}", {48: s, 49: d, 50: 0.6}, held))

    for engine, init, cases in (("wave", WAVE_INIT, wave_cases), ("phase", PHASE_INIT, phase_cases)):
        worst, worst_case, silent = 0.0, "", []
        for name, change, script in cases:
            vals = {**init, **FX_INIT, **change}
            out = r.play(engine, vals, script)
            e = same(out["a"], out["r"])
            if e > worst:
                worst, worst_case = e, name
            if rms(out["r"]) < 1e-4:
                silent.append(name)
            if e > PARITY_LIMIT:
                print(f"     {engine} {name}: differs by {e:.3g}")
        check(worst <= PARITY_LIMIT, f"{engine.upper()} as sy9, {len(cases)} cases: largest difference "
                                     f"{worst:.3g} of the peak ({worst_case or 'none'})")
        check(not silent, f"{engine.upper()}: every case sounds {silent or ''}")

    # the voice goes idle after its release
    r.play("wave", {**WAVE_INIT, **FX_INIT}, [("on", 60), ("render", 10), ("off",), ("render", 200)], which=("a",))
    idle_w = r.sounding("wave")
    r.play("phase", {**PHASE_INIT, **FX_INIT}, [("on", 60), ("render", 10), ("off",), ("render", 200)], which=("a",))
    idle_p = r.sounding("phase")
    check(idle_w == 0 and idle_p == 0, "WAVE and PHASE go idle after the release")


# ---- 2. the Chorus against sy9 --------------------------------------------------------------
def fx_state(r: Rig, owner: int) -> int:
    s = r.call("a_fx_claim", owner)
    assert s, "no effect state"
    return s


def chorus(r: Rig) -> None:
    x = np.concatenate([sine(220, 40, 0.4) + noise(40, 3, 0.1), np.zeros(10 * BLOCK)])
    worst = 0.0
    for rate, depth, mix in ((0.5, 0.5, 0.5), (1.0, 1.0, 1.0), (0.0, 0.2, 0.3)):
        owner = 0x1000
        s = fx_state(r, owner)
        vals = {**FX_INIT, P_EFFECT: FX_CHORUS, P_RATE: rate, P_DEPTH: depth, P_MIX: mix}
        lo, ro = r.fx(s, vals, x, 0)
        r.call("a_fx_release", owner)
        r.m.write(r.rfx, b"\0" * r.size["r_fx_size"])
        r.put(r.fxp, [FX_CHORUS, rate, depth, mix])
        r.put(r.buf, x)
        r.call("r_fx_run", r.rfx, r.fxp, r.buf, r.buf2, len(x) // BLOCK)
        lr, rr = r.get(r.buf, len(x)), r.get(r.buf2, len(x))
        worst = max(worst, same(lo, lr), same(ro, rr))
    check(worst <= PARITY_LIMIT, f"Chorus as sy9's (3 settings): largest difference {worst:.3g}")
    s = fx_state(r, 0x1100)
    lo, ro = r.fx(s, {**FX_INIT}, x, 0)
    r.call("a_fx_release", 0x1100)
    check(np.array_equal(lo, x.astype(np.float32).astype(np.float64)) and np.array_equal(lo, ro),
          "Effect None, Distortion Off: L untouched, R a copy")


# ---- 3. Flanger and Phaser ----------------------------------------------------------------
def notch_depth(dry: np.ndarray, wet: np.ndarray) -> tuple[float, float]:
    """How far the deepest band falls below the dry signal's, and the highest rises above it, in dB
    (averaged spectra)."""
    seg = 2048
    def avg(x):
        acc = None
        for i in range(0, len(x) - seg, seg // 2):
            s = np.abs(np.fft.rfft(x[i:i + seg] * np.hanning(seg))) ** 2
            acc = s if acc is None else acc + s
        return acc
    a, b = avg(dry), avg(wet)
    lo, hi = int(100 * seg / RATE), int(15000 * seg / RATE)
    ratio = 10 * np.log10((b[lo:hi] + 1e-20) / (a[lo:hi] + 1e-20))
    k = 5
    smooth = np.convolve(ratio, np.ones(k) / k, mode="same")
    return float(-np.min(smooth[k:-k])), float(np.max(smooth[k:-k]))


def modulation(r: Rig) -> None:
    blocks = 120
    x = noise(blocks, 7, 0.3)
    for name, kind in (("Flanger", FX_FLANGER), ("Phaser", FX_PHASER)):
        res = {}
        for fb in (0.0, 0.9):
            s = fx_state(r, 0x2000)
            # Rate 0: the sweep barely moves, so the notches stay put to measure
            vals = {**FX_INIT, P_EFFECT: kind, P_RATE: 0.0, P_DEPTH: 0.4, P_MIX: 0.5, P_FEEDBACK: fb}
            lo, ro = r.fx(s, vals, x, 0)
            r.call("a_fx_release", 0x2000)
            res[fb] = (lo, ro)
        lo, ro = res[0.0]
        tail = slice(30 * BLOCK, None)
        nd0, pk0 = notch_depth(x[tail], lo[tail])
        nd9, pk9 = notch_depth(x[tail], res[0.9][0][tail])
        check(nd0 > 12.0, f"{name}: Mix 50 % cuts notches, deepest {nd0:.1f} dB")
        # feedback turns the peaks between the notches into resonances (and makes the notches
        # shallower: with feedback the dry and the delayed copy no longer cancel exactly)
        check(pk9 > pk0 + 3.0 and nd9 > 3.0 and np.max(np.abs(res[0.9][0])) < 4.0,
              f"{name}: Feedback 0.9 raises the peaks ({pk0:+.1f} -> {pk9:+.1f} dB), notches stay "
              f"({nd9:.1f} dB), bounded (peak {np.max(np.abs(res[0.9][0])):.2f})")
        check(same(lo[tail], ro[tail]) > 0.05, f"{name}: L and R differ (stereo)")
        # Feedback at its limit, a loud sine at the worst frequency for a while: bounded
        s = fx_state(r, 0x2001)
        loud = {**FX_INIT, P_EFFECT: kind, P_RATE: 1.0, P_DEPTH: 1.0, P_MIX: 1.0, P_FEEDBACK: 0.95}
        lo2, ro2 = r.fx(s, loud, sine(1000, 200, 1.0), 0)
        lo3, _ = r.fx(s, {**loud, P_FEEDBACK: -0.95}, sine(3000, 100, 1.0), 0)
        r.call("a_fx_release", 0x2001)
        peak = max(np.max(np.abs(lo2)), np.max(np.abs(ro2)), np.max(np.abs(lo3)))
        check(np.all(np.isfinite(lo2)) and peak < 8.0, f"{name}: Feedback +-0.95 at full level stays bounded "
                                                        f"(peak {peak:.2f})")
        # a value past the control's range (a damaged patch, a macro) is held to +-0.95: the same
        # output as 0.95 itself
        outs = []
        for fbv in (0.95, 5.0):
            s = fx_state(r, 0x2001)
            outs.append(r.fx(s, {**loud, P_FEEDBACK: fbv}, sine(1000, 60, 1.0), 0)[0])
            r.call("a_fx_release", 0x2001)
        check(np.array_equal(outs[0], outs[1]), f"{name}: Feedback 5.0 is held to 0.95")
        # switching on fades in; Mix 0 is dry
        s = fx_state(r, 0x2002)
        on = {**FX_INIT, P_EFFECT: kind, P_RATE: 0.5, P_DEPTH: 0.8, P_MIX: 1.0, P_FEEDBACK: 0.5}
        y, _ = r.fx(s, on, x[:30 * BLOCK], 0)
        first = same(y[:BLOCK], x[:BLOCK])
        late = same(y[25 * BLOCK:], x[25 * BLOCK:30 * BLOCK])
        check(first < 0.2 and late > 0.3, f"{name}: fades in (first block {first:.2f} from dry, after 25 "
                                          f"blocks {late:.2f})")
        y, _ = r.fx(s, {**on, P_MIX: 0.0}, x[:40 * BLOCK], 0)
        d = same(y[30 * BLOCK:], x[30 * BLOCK:40 * BLOCK])
        check(d < 1e-6, f"{name}: Mix 0 is dry once faded ({d:.2g})")
        y, _ = r.fx(s, {**on, P_EFFECT: FX_NONE}, x[:40 * BLOCK], 0)
        st = r.m.r32(s)     # running
        check(st == 0, "{}: Effect None stops it once faded out".format(name))
        r.call("a_fx_release", 0x2002)

    # Chorus -> Flanger: the old fades out before the new starts (no click: no step over 0.25)
    s = fx_state(r, 0x2003)
    x = sine(300, 60, 0.5)
    y1, _ = r.fx(s, {**FX_INIT, P_EFFECT: FX_CHORUS, P_MIX: 1.0}, x[:20 * BLOCK], 0)
    y2, _ = r.fx(s, {**FX_INIT, P_EFFECT: FX_FLANGER, P_MIX: 1.0, P_FEEDBACK: 0.0}, x[20 * BLOCK:], 0)
    y = np.concatenate([y1, y2])
    step = float(np.max(np.abs(np.diff(y))))
    check(step < 0.25, f"Chorus to Flanger: no step (largest sample to sample {step:.3f})")
    r.call("a_fx_release", 0x2003)


# ---- 4. Distortion ----------------------------------------------------------------------------
def thd(y: np.ndarray, hz: float) -> float:
    f = harmonic(y, hz, 1)
    return math.sqrt(sum(harmonic(y, hz, k) ** 2 for k in range(2, 10))) / max(f, 1e-12)


def distortion(r: Rig) -> None:
    hz = 220.5      # a whole number of cycles in the window
    x = sine(hz, 80, 0.5)
    tail = slice(40 * BLOCK, None)
    res = {}
    for kind in (DIST_OVERDRIVE, DIST_DISTORTION):
        for g in (0.0, 0.5, 1.0):
            r.dist_clear()
            y = r.dist({**FX_INIT, P_DIST: kind, P_GAIN: g, P_TONE: 1.0}, x)
            res[kind, g] = y[tail]
    names = {DIST_OVERDRIVE: "Overdrive", DIST_DISTORTION: "Distortion"}
    for kind in (DIST_OVERDRIVE, DIST_DISTORTION):
        t = [thd(res[kind, g], hz) for g in (0.0, 0.5, 1.0)]
        check(t[0] < t[1] < t[2], f"{names[kind]}: harmonics grow with Gain (THD {t[0]:.3f}, {t[1]:.3f}, {t[2]:.3f})")
        y = res[kind, 1.0]
        odd = sum(harmonic(y, hz, k) for k in (3, 5, 7))
        even = sum(harmonic(y, hz, k) for k in (2, 4, 6))
        check(odd > 4 * even, f"{names[kind]}: odd harmonics lead (odd {db(odd):.1f} dB, even {db(even):.1f} dB)")
    check(thd(res[DIST_DISTORTION, 0.5], hz) > thd(res[DIST_OVERDRIVE, 0.5], hz),
          "Distortion clips harder than Overdrive at the same Gain")

    # Tone
    for kind in (DIST_OVERDRIVE, DIST_DISTORTION):
        c = []
        for tone in (0.0, 1.0):
            r.dist_clear()
            c.append(centroid(r.dist({**FX_INIT, P_DIST: kind, P_GAIN: 0.7, P_TONE: tone}, x)[tail]))
        check(c[1] > 1.3 * c[0], f"{names[kind]}: Tone brightens (centroid {c[0]:.0f} -> {c[1]:.0f} Hz)")

    # Level: dB
    lv = {}
    for level in (0.0, -6.0, -30.0, 6.0):
        r.dist_clear()
        lv[level] = rms(r.dist({**FX_INIT, P_DIST: DIST_OVERDRIVE, P_GAIN: 0.5, P_LEVEL: level}, x)[tail])
    got = [db(lv[l] / lv[0.0]) for l in (-6.0, -30.0)]
    check(abs(got[0] + 6) < 0.3 and abs(got[1] + 30) < 0.5, f"Level: -6 dB gives {got[0]:.2f}, -30 dB {got[1]:.2f}")
    check(lv[6.0] > lv[0.0], "Level: +6 dB is louder")

    # aliasing: sines of 1-7 kHz at full Gain, Tone 1. The strongest non-harmonic line against
    # the fundamental. Overdrive runs at 2x, Distortion at 4x; at 1x (first-order ADAA alone)
    # Distortion gave -19 dB at 5 kHz, and 2x -36 dB at 4 kHz.
    for kind, at_1k, worst_bound in ((DIST_OVERDRIVE, -55.0, -44.0), (DIST_DISTORTION, -55.0, -44.0)):
        got = {}
        for f in (1000, 2000, 3000, 4000, 5000, 7000):
            n = 4096
            f0 = round(f * n / RATE) * RATE / n
            r.dist_clear()
            y = r.dist({**FX_INIT, P_DIST: kind, P_GAIN: 1.0, P_TONE: 1.0}, sine(f0, 72, 0.5))[-n:]
            s = spectrum(y)
            k0 = round(f0 * n / RATE)
            mask = np.ones(len(s), bool)
            for h in range(1, 60):
                if h * k0 < len(s):
                    mask[max(h * k0 - 4, 0):h * k0 + 5] = False
            mask[:4] = False
            got[f] = db(float(np.max(s[mask])) / float(s[k0]))
        worst = max(got.values())
        check(got[1000] < at_1k and worst < worst_bound,
              f"{names[kind]}: aliases at full Gain, 1-7 kHz: "
              + ", ".join(f"{f // 1000}k {v:.0f}" for f, v in got.items()) + " dB")

    # high notes at Gain 0: their 5th to 7th harmonics land at 55-90 kHz, where the inner half-band
    # (88.2 to 176.4 kHz) works, so this sees it; 1-7 kHz at full Gain don't. -68 and -69 dB on
    # 2026-11-11 (3 coefficients and 4 alike); an inner coefficient 0.03 off gave -59 and -58.
    got = {}
    for f in (11000, 13000):
        n = 4096
        f0 = round(f * n / RATE) * RATE / n
        r.dist_clear()
        y = r.dist({**FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 0.0, P_TONE: 1.0}, sine(f0, 72, 0.5))[-n:]
        s = spectrum(y)
        k0 = round(f0 * n / RATE)
        mask = np.ones(len(s), bool)
        for h in range(1, 60):
            if h * k0 < len(s):
                mask[max(h * k0 - 4, 0):h * k0 + 5] = False
        mask[:4] = False
        got[f] = db(float(np.max(s[mask])) / float(s[k0]))
    check(max(got.values()) < -63.0, "Distortion: aliases of high notes at Gain 0 (the inner half-band): "
          + ", ".join(f"{f // 1000}k {v:.0f}" for f, v in got.items()) + " dB")

    # bounded, whatever comes in
    r.dist_clear()
    y = r.dist({**FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 1.0, P_LEVEL: 6.0}, noise(20, 9, 50.0))
    check(np.all(np.isfinite(y)) and np.max(np.abs(y)) <= 4.0, f"Distortion: bounded on a huge input "
                                                               f"(peak {np.max(np.abs(y)):.2f})")

    # fade in and out, and a type change goes out first
    r.dist_clear()
    y = r.dist({**FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 1.0}, x[:30 * BLOCK])
    a1 = same(y[:BLOCK], x[:BLOCK])
    check(a1 < 0.15, f"Distortion fades in (first block {a1:.2f} from dry)")
    y = r.dist({**FX_INIT, P_DIST: DIST_OFF}, x[:30 * BLOCK], want=1)
    check(same(y[25 * BLOCK:], x[25 * BLOCK:30 * BLOCK]) < 1e-6, "Distortion Off: dry once faded out")
    r.dist_clear()
    y = r.dist({**FX_INIT, P_DIST: DIST_OVERDRIVE}, x[:10 * BLOCK], want=0)
    check(np.array_equal(y, x[:10 * BLOCK].astype(np.float32)), "Distortion not wanted here: untouched")

    # Bit Crusher
    def levels(y):
        return len(np.unique(np.round(y, 7)))
    for bits_g, bits in ((0.0, 4), (2 / 12, 6), (4 / 12, 8)):
        r.dist_clear()
        y = r.dist({**FX_INIT, P_DIST: DIST_CRUSHER, P_GAIN: bits_g, P_TONE: 1.0}, sine(220.5, 40, 0.9))
        n = levels(y[20 * BLOCK:])
        check(n <= 2 ** bits + 1, f"Bit Crusher, {bits} bits: {n} levels (at most {2 ** bits + 1})")
    r.dist_clear()
    y = r.dist({**FX_INIT, P_DIST: DIST_CRUSHER, P_GAIN: 1.0, P_TONE: 1.0}, x)
    e = float(np.max(np.abs(y[tail] - x[tail])))
    check(e < 1e-4, f"Bit Crusher at 16 bits and 44.1 kHz: transparent (largest error {e:.2g})")
    r.dist_clear()
    y = r.dist({**FX_INIT, P_DIST: DIST_CRUSHER, P_GAIN: 1.0, P_TONE: 0.0}, sine(97, 40, 0.5))[20 * BLOCK:]
    runs = np.diff(np.flatnonzero(np.abs(np.diff(y)) > 1e-6))   # not the dry/wet sum's rounding
    mean_run = float(np.mean(runs)) if len(runs) else 0.0
    check(abs(mean_run - RATE / 1000) < 1.5, f"Bit Crusher at Rate 0: holds each sample {mean_run:.1f} samples "
                                             f"(1 kHz: {RATE / 1000:.1f})")
    vals = r.m.alloc(12)
    outv = r.m.alloc(12)
    r.put(vals, [0.5, 0.0, 1.0])
    r.call("a_values", vals, outv)
    got = r.get(outv, 3)
    r.put(vals, [0.0, 1.0, 0.0])
    r.call("a_values", vals, outv)
    got2 = r.get(outv, 3)
    check(abs(got[1] - 4) < 1e-4 and abs(got2[1] - 16) < 1e-3 and abs(got[2] - 44100) < 50
          and abs(got2[2] - 1000) < 2 and abs(got[0] - 0.5) < 0.01,
          f"Bits {got[1]:.0f}-{got2[1]:.0f}, Rate {got2[2]:.0f}-{got[2]:.0f} Hz, effect Rate 50 % {got[0]:.3f} Hz")

    # a Pre Distortion is there from the first sample of a note from idle
    r.dist_clear()
    y = r.dist({**FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 1.0}, x[:4 * BLOCK], reset=1)
    r.dist_clear()
    y_ref = r.dist({**FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 1.0}, x[:60 * BLOCK])
    r.dist_clear()
    r.dist({**FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 1.0}, x[:4 * BLOCK], reset=1)
    full = np.frombuffer(r.m.read(r.dist_at + 8 * 4, 4), "<f4")[0]
    check(full == 1.0 and same(y[:BLOCK], x[:BLOCK]) > 0.05,
          f"dist_reset: a Pre Distortion is at full from the first block (amount {full})")
    _ = y_ref

    # WAVE: Pre against Post. A low Cutoff: Pre's harmonics are filtered, Post's aren't.
    base = {**WAVE_INIT, **FX_INIT, 12: 500.0, 13: 0.0, 14: 0.0, 2: 0.0, 0: 0.0, 1: 0.0, 3: 0.0,
            P_DIST: DIST_DISTORTION, P_GAIN: 1.0, P_TONE: 1.0}
    script = [("on", 48), ("render", 40)]
    pre = r.play("wave", {**base, P_PLACEMENT: PRE}, script, which=("a",))["a"]
    post_dry = r.play("wave", {**base, P_PLACEMENT: POST}, script, which=("a",))["a"]
    off = r.play("wave", {**base, P_DIST: DIST_OFF}, script, which=("a",))["a"]
    check(same(post_dry, off) == 0.0, "WAVE, Placement Post: the voice itself is untouched")
    r.dist_clear()
    post = r.dist({**base, P_PLACEMENT: POST}, post_dry)
    tail = slice(20 * BLOCK, None)
    cp, cq = centroid(pre[tail]), centroid(post[tail])
    check(cq > 1.5 * cp and same(pre, off) > 0.05,
          f"WAVE: Pre goes through the filter (centroid {cp:.0f} Hz), Post doesn't ({cq:.0f} Hz)")

    # PHASE: Placement does nothing in the voice
    pbase = {**PHASE_INIT, **FX_INIT, P_DIST: DIST_DISTORTION, P_GAIN: 1.0}
    a = r.play("phase", {**pbase, P_PLACEMENT: PRE}, script, which=("a",))["a"]
    b = r.play("phase", {**pbase, P_PLACEMENT: POST}, script, which=("a",))["a"]
    c = r.play("phase", {**pbase, P_DIST: DIST_OFF}, script, which=("a",))["a"]
    check(np.array_equal(a, b) and np.array_equal(a, c), "PHASE: the voice ignores the Distortion and Placement")


# ---- 5. the effect states ---------------------------------------------------------------------
def states(r: Rig) -> None:
    owners = [0x3000, 0x3100, 0x3200]
    got = [r.call("a_fx_claim", o) for o in owners]
    fourth = r.call("a_fx_claim", 0x3300)
    check(all(got) and len(set(got)) == 3 and fourth == 0, "three instruments get a state each, a fourth none")
    again = r.call("a_fx_claim", owners[1])
    check(again == got[1], "an owner gets its own state back")
    r.call("a_fx_release", owners[0])
    got4 = r.call("a_fx_claim", 0x3300)
    check(got4 == got[0], "a released state goes to the next instrument")
    # stale: the owners at 0x3100 and 0x3200 stop calling; 0x3300 calls many times
    for _ in range(20):
        r.call("a_fx_claim", 0x3300)
    got5 = r.call("a_fx_claim", 0x3400)
    check(got5 in (got[1], got[2]), "a state whose owner stopped is taken over")
    for o in (0x3100, 0x3200, 0x3300, 0x3400):
        r.call("a_fx_release", o)

    # garbage left in SDRAM (start-up doesn't clear it): the same output as from clean
    x = sine(330, 30, 0.4)
    outs = []
    for fill in (b"\0", b"\x7f"):
        s = r.call("a_fx_claim", 0x3500)
        r.call("a_fx_release", 0x3500)
        r.m.write(s, fill * r.size["a_fx_size"])
        s = r.call("a_fx_claim", 0x3500)
        lo, ro = r.fx(s, {**FX_INIT, P_EFFECT: FX_FLANGER, P_DIST: DIST_OVERDRIVE}, x, 1)
        outs.append((lo, ro))
        r.call("a_fx_release", 0x3500)
    check(np.array_equal(outs[0][0], outs[1][0]) and np.array_equal(outs[0][1], outs[1][1]),
          "garbage left in a state changes nothing")


# ---- 7. cost --------------------------------------------------------------------------------------
# Instructions per block on the emulated core: a regression ceiling, not the CPU load (that is
# cycles, measured on the device with the Profiler, all 8 voices sounding). Each ceiling is
# about 15 % over what the build measured on 2026-11-09 (Overdrive, Distortion and Phaser:
# 2026-11-11, after their loops kept the filters' states in registers); sy9's own were 500 idle
# and 16,000 playing for a WAVE voice.
COST_CEILINGS = {
    "WAVE voice, Init": 16_000, "WAVE voice, idle": 500, "WAVE voice, super saw on both": 55_000,
    "PHASE voice, Init": 16_000, "Overdrive": 23_600, "Distortion": 38_200, "Bit Crusher": 4_000,
    "Chorus": 10_000, "Flanger": 11_500, "Phaser": 10_800,
}


def cost(r: Rig) -> dict[str, float]:
    from unicorn import UC_HOOK_CODE
    n = [0]

    def counted(fn, blocks):
        h = r.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1))
        r.m.uc.ctl_flush_tb()           # hooks reach only code translated after them
        n[0] = 0
        fn()
        r.m.uc.hook_del(h)
        r.m.uc.ctl_flush_tb()
        return n[0] / blocks

    out = {}

    def voice(engine, vals, label, note=True):
        v = r.voices[f"a_{engine}_size"]
        r.call(f"a_{engine}_init", v)
        r.params(engine, vals)
        r.set_ctl()
        if note:
            r.call(f"a_{engine}_on", v, r.pa, 60, r.ctl)
            r.call(f"a_{engine}_render", v, r.pa, r.buf, r.ctl, 5)
        out[label] = counted(lambda: r.call(f"a_{engine}_render", v, r.pa, r.buf, r.ctl, 4), 4)

    w, ph = {**WAVE_INIT, **FX_INIT}, {**PHASE_INIT, **FX_INIT}
    voice("wave", w, "WAVE voice, Init")
    voice("wave", w, "WAVE voice, idle", note=False)
    voice("wave", {**w, 2: 7.0, 3: 7.0}, "WAVE voice, super saw on both")
    voice("phase", ph, "PHASE voice, Init")
    x = sine(220, 8, 0.4)
    for kind, label in ((DIST_OVERDRIVE, "Overdrive"), (DIST_DISTORTION, "Distortion"), (DIST_CRUSHER, "Bit Crusher")):
        vals = {**FX_INIT, P_DIST: kind}
        r.dist_clear()
        r.dist(vals, x[:4 * BLOCK])
        p = np.zeros(P_COUNT)
        for k, v in vals.items():
            p[k] = v
        r.put(r.pa, p)
        r.put(r.buf, x[:4 * BLOCK])
        out[label] = counted(lambda: r.call("a_dist_run", r.dist_at, r.buf, r.pa, 1, 4), 4)
    for kind, label in ((FX_CHORUS, "Chorus"), (FX_FLANGER, "Flanger"), (FX_PHASER, "Phaser")):
        s = fx_state(r, 0x4000 + kind)
        vals = {**FX_INIT, P_EFFECT: kind}
        r.fx(s, vals, np.tile(x, 4), 0)
        p = np.zeros(P_COUNT)
        for k, v in vals.items():
            p[k] = v
        r.put(r.pa, p)
        r.put(r.buf, x[:4 * BLOCK])
        out[label] = counted(lambda: r.call("a_fx_run", s, r.pa, r.buf, r.buf2, 0, 4), 4)
        r.call("a_fx_release", 0x4000 + kind)
    return out


def costs(r: Rig) -> None:
    got = cost(r)
    over = {k: v for k, v in got.items() if v > COST_CEILINGS[k]}
    for k, v in got.items():
        print(f"info cost {k}: {v:,.0f} instructions per block (ceiling {COST_CEILINGS[k]:,})")
    check(not over, f"costs under their ceilings {over or ''}")


# ---- 6. the Level default -----------------------------------------------------------------------
def level_default(r: Rig) -> None:
    """Level 0 dB (its default) and the default Gain: a held Init note, note 60, as loud as with
    the Distortion Off within 1 dB, on WAVE and PHASE (the plan's rule; synthfx/synthFx.cpp's trims).
    Other notes and Gains are printed: Overdrive's 720 Hz high-pass leaves low notes cleaner."""
    tail = slice(20 * BLOCK, None)
    names = {DIST_OVERDRIVE: "Overdrive", DIST_DISTORTION: "Distortion", DIST_CRUSHER: "Bit Crusher"}
    at60, at0 = {}, {}
    for engine, init in (("wave", WAVE_INIT), ("phase", PHASE_INIT)):
        for note in (36, 60, 84):
            dry = r.play(engine, {**init, **FX_INIT}, [("on", note), ("render", 60)], which=("a",))["a"]
            row = []
            for kind in (DIST_OVERDRIVE, DIST_DISTORTION, DIST_CRUSHER):
                for g in (0.0, 0.5, 1.0):
                    r.dist_clear()
                    y = r.dist({**FX_INIT, P_DIST: kind, P_GAIN: g}, dry)
                    v = db(rms(y[tail]) / rms(dry[tail]))
                    if note == 60 and g == 0.5:
                        at60[engine, kind] = v
                    if g == 0.0:
                        at0[engine, note, kind] = v
                    row.append(v)
            print(f"info {engine.upper()} note {note}, Level 0 dB, Gain 0/50/100 %: " + "; ".join(
                f"{names[k]} " + "/".join(f"{v:+.1f}" for v in row[3 * i:3 * i + 3])
                for i, k in enumerate((DIST_OVERDRIVE, DIST_DISTORTION, DIST_CRUSHER))) + " dB")
    worst = max(abs(v) for v in at60.values())
    check(worst <= 1.0, f"Level default: note 60 at the default Gain within 1 dB of Off (largest {worst:.2f} dB)")
    check(min(at0.values()) > -12.0, f"every type sounds at Gain 0 on an Init note (quietest {min(at0.values()):.1f} dB)")


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    quick = "--quick" in sys.argv
    if len(args) != 1:
        print(__doc__)
        return 2
    r = Rig(Path(args[0]))
    print(f"sizes: WAVE voice {r.size['a_wave_size']} B (sy9 {r.size['r_wave_size']}), PHASE voice "
          f"{r.size['a_phase_size']} B (sy9 {r.size['r_phase_size']}), effect state {r.size['a_fx_size']} B")
    parity(r, quick)
    chorus(r)
    modulation(r)
    distortion(r)
    states(r)
    level_default(r)
    costs(r)
    print(f"{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
