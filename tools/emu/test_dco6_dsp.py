# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Level A for DCO6: the engine alone (core/mods/synths/d6Engine.h), on the emulated Cortex-M7, against
the Python model (tools/synths/dco6_proto.py). docs/mt/VA_SYNTH_PLAN.md section 11 has the plan;
Level B, through CE's synth host, comes with the firmware steps.

The ELF is tools/synths/dco6_level_a/build.sh's: the engine built with the firmware's FPU flags,
behind plain C entry points.

1. The port against the model, sample for sample within float32's reach: the Init patch and one
   change at a time (each source, Range, Tune and Fine, PWM from the LFO and the envelope, the HPF and
   Bass Boost, Resonance, negative Env Amt, Key Track, VCA Gate, every LFO shape, LFO Delay,
   Vintage, a unison slot, glide), a held note and its release.
2. Pitch: within 1 cent from note 24 to 108, Range 16' 8' 4', Tune and Fine Tune.
3. Aliasing of the saw and a 30 % pulse, filter open: the bound the 4-point correction measured.
4. Filters, from the ratio of two renders with the same noise: the ladder's response against four
   one-poles; the HPF's corner; Bass Boost's +6 dB below and flat above; self-oscillation at
   Resonance 100 %, none at 90 %; bounded, no NaN, with everything at full and the filters swept;
   a NaN parameter.
5. The envelope: the attack peaks at the Attack time, Decay and Release reach 99 % in theirs, the
   voice stops after its release (Env and Gate), the Gate's ramp.
6. LFO Trigger: Free voices share one phase and its random values; Note restarts per voice.
7. Unison: L and R alike at Unison Off; Width spreads the stack; a stack's level within 1 dB of one
   voice; Vintage 0 % costs nothing and its drift stays inside the plan's bounds (the model).
8. Cost: instructions per block, under regression ceilings (the CPU load is the Profiler's).

Usage: python tools/emu/test_dco6_dsp.py build/emu/dco6_level_a.axf [--quick] [--only port,pitch,...]
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "synths"))
from machine import Machine  # noqa: E402
import dco6_proto as model  # noqa: E402

BLOCK = 128
RATE = 44100.0
QUICK = "--quick" in sys.argv

# parameter ids (d6Engine.h) by the model's names
IDS = dict(range=0, tune=1, fine=2, saw=3, pulse=4, sub=5, noise=6, pw=7, pwm=8, pwm_src=9, lfo_pitch=10,
           hpf=11, cutoff=12, res=13, env_amt=14, lfo_cut=15, key=16, a=17, d=18, s=19, r=20, vca=21,
           lfo_wave=22, lfo_rate=23, lfo_delay=24, lfo_free=26, unison=27, detune=28, width=29,
           vintage=30, glide=31, volume=32, boost=34)
P_COUNT = 35

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        failures.append(what)


class Rig:
    def __init__(self, axf: Path):
        self.m = Machine(axf)
        m = self.m
        assert m.r32(m.addr("a_d6_params")) == P_COUNT
        self.vsize = m.r32(m.addr("a_d6_size"))
        self.fn = {}
        self.p = m.alloc(4 * P_COUNT)
        self.ctl = m.alloc(8)
        self.st = m.alloc(32)
        self.L = m.alloc(4 * BLOCK * 64)
        self.R = m.alloc(4 * BLOCK * 64)
        self.voices = [m.alloc(self.vsize) for _ in range(8)]

    def call(self, name: str, *args: int) -> int:
        if name not in self.fn:
            self.fn[name] = self.m.addr(name)
        return self.m.call(self.fn[name], *args, max_insns=400_000_000)

    def put(self, a: int, xs) -> None:
        self.m.write(a, np.asarray(xs, dtype="<f4").tobytes())

    def get(self, a: int, n: int) -> np.ndarray:
        return np.frombuffer(self.m.read(a, 4 * n), dtype="<f4").astype(np.float64)

    def params(self, p: dict) -> None:
        arr = np.zeros(P_COUNT)
        for k, i in IDS.items():
            arr[i] = float(p[k])
        self.put(self.p, arr)

    def set_ctl(self, lfo_hz: float, bend: float = 0.0) -> None:
        self.put(self.ctl, [lfo_hz, bend])

    def state(self, v: int) -> list[float]:
        self.call("a_d6_state", self.voices[v], self.st)
        return list(self.get(self.st, 7))


def c_render(r: Rig, p: dict, events, blocks: int, voices=1, sizes=None, nan_at=None):
    """events: (block, voice, note or None for off, slot, size). Returns L, R (summed voices), and
    which voices sounded at the end."""
    r.call("a_d6_lfo_reset")
    for i in range(voices):
        r.call("a_d6_init", r.voices[i], i)
    r.params(p)
    r.set_ctl(p["lfo_rate"])
    ev = sorted(events, key=lambda e: e[0])
    L = np.zeros(blocks * BLOCK)
    R = np.zeros(blocks * BLOCK)
    sounding = [0] * voices
    b = 0
    while b < blocks:
        while ev and ev[0][0] <= b:
            _, vi, note, slot, size = ev.pop(0)
            if note is None:
                r.call("a_d6_off", r.voices[vi])
            else:
                r.call("a_d6_on", r.voices[vi], r.p, note, slot, size, r.ctl)
        if nan_at is not None and b == nan_at[0]:
            bad = dict(p)
            bad.update(nan_at[1])
            r.params(bad)
        if nan_at is not None and b == nan_at[0] + 1:
            r.params(p)
        n = min(64, blocks - b, (ev[0][0] - b) if ev else 64)
        n = max(n, 1)
        if nan_at is not None and b <= nan_at[0] + 1:
            n = 1
        for vi in range(voices):
            sounding[vi] = r.call("a_d6_render", r.voices[vi], r.p, r.L, r.R, n, r.ctl)
            L[b * BLOCK:(b + n) * BLOCK] += r.get(r.L, n * BLOCK)
            R[b * BLOCK:(b + n) * BLOCK] += r.get(r.R, n * BLOCK)
        b += n
    return L, R, sounding


def py_render(p: dict, events, blocks: int, voices=1):
    model.LFO_TAB.clear()
    vs = [model.Voice(i, p) for i in range(voices)]
    ev = sorted(events, key=lambda e: e[0])
    L, R = [], []
    for b in range(blocks):
        while ev and ev[0][0] <= b:
            _, vi, note, slot, size = ev.pop(0)
            if note is None:
                vs[vi].note_off_()
            else:
                vs[vi].note_on(note, slot, size)
        bl, br = [0.0] * BLOCK, [0.0] * BLOCK
        for v in vs:
            v.render(bl, br)
        L += bl
        R += br
    return np.array(L), np.array(R)


def init(**kw) -> dict:
    return model.default_params() | kw


# ---- 1. the port against the model ----------------------------------------------------------------
# The port runs in float32 and the model in float64. Their formulas agree to about -85 dB, but the
# oscillator's phase drifts apart by a few thousandths of a sample over a second, which shows as
# spikes at each wrap growing to about -40 dB, and a square or S&H LFO can step one control period
# apart. float32 alone puts the first blocks near -70 dB (2026-11-17). So: the first 4 blocks
# (before any drift) within -60 dB of the peak; nowhere more than
# -30 dB; and the spectrum, in third-octave bands from 50 Hz to 16 kHz, within 0.5 dB.
def band_diff(a: np.ndarray, b: np.ndarray) -> float:
    fa, fb = np.abs(np.fft.rfft(a)) ** 2, np.abs(np.fft.rfft(b)) ** 2
    fr = np.fft.rfftfreq(len(a), 1 / RATE)
    worst, total = 0.0, fb.sum()
    f = 50.0
    while f < 16000.0:
        m = (fr >= f) & (fr < f * 2 ** (1 / 3))
        if fb[m].sum() > 1e-6 * total:              # bands with something in them
            worst = max(worst, abs(10 * math.log10((fa[m].sum() + 1e-30) / (fb[m].sum() + 1e-30))))
        f *= 2 ** (1 / 3)
    return worst


def port(r: Rig) -> None:
    held = [(0, 0, 60, 0, 1), (90, 0, None, 0, 1)]
    cases = [
        ("Init", init(), held, 140),
        ("saw + sub, resonant", init(sub=0.7, res=0.6, cutoff=600.0, env_amt=0.5), held, 140),
        ("pulse 30 %", init(saw=0.0, pulse=1.0, pw=0.44), held, 120),
        ("noise", init(saw=0.0, noise=0.8), held, 100),
        ("Range 16', Tune +7, Fine -23", init(range=0, tune=7.0, fine=-23.0), held, 100),
        ("Range 4'", init(range=2), held, 100),
        ("PWM from the LFO", init(saw=0.0, pulse=1.0, pw=0.2, pwm=0.7, lfo_rate=3.0), held, 140),
        ("PWM from the envelope", init(saw=0.0, pulse=1.0, pwm=0.8, pwm_src=1), held, 120),
        ("HPF 240 Hz", init(sub=0.5, hpf=240.0), held, 100),
        ("Bass Boost and HPF 60 Hz", init(sub=0.5, boost=1, hpf=60.0), held, 100),
        ("Resonance 95 %", init(res=0.95, cutoff=900.0), held, 120),
        ("Env Amt -60 %", init(env_amt=-0.6, cutoff=5000.0, a=0.2), held, 120),
        ("Key Track 100 %, note 84", init(key=1.0), [(0, 0, 84, 0, 1), (60, 0, None, 0, 1)], 100),
        ("VCA Gate", init(vca=1, env_amt=0.6), held, 110),
        ("LFO Delay 0.5 s", init(lfo_delay=0.5, lfo_pitch=0.3, lfo_rate=5.0), held, 200),
        ("Vintage 100 %", init(vintage=1.0, lfo_cut=0.2), held, 160),
        ("unison slot 2 of 4, Detune 25, Width 100 %", init(detune=25.0, width=1.0),
         [(0, 0, 55, 2, 4), (90, 0, None, 2, 4)], 120),
        ("glide 0.3 s, a second note while held", init(glide=0.3),
         [(0, 0, 48, 0, 1), (40, 0, 60, 0, 1), (120, 0, None, 0, 1)], 160),
    ]
    for w, name in enumerate(model.LFO_SHAPES):
        cases.append((f"LFO {name} on pitch and cutoff",
                      init(lfo_wave=w, lfo_rate=4.0, lfo_pitch=0.3, lfo_cut=0.4), held, 140))
    cases.append(("LFO Free, two voices started apart", init(lfo_free=1, lfo_rate=3.0, lfo_pitch=0.3, lfo_wave=6),
                  [(0, 0, 60, 0, 1), (37, 1, 67, 0, 1), (100, 0, None, 0, 1), (100, 1, None, 0, 1)], 140))
    if QUICK:
        keep = ("Init", "saw + sub", "pulse 30", "noise", "unison slot", "LFO Free")
        cases = [c for c in cases if c[0].startswith(keep)]
    for name, p, ev, blocks in cases:
        voices = 1 + max(e[1] for e in ev)
        cl, cr, _ = c_render(r, p, ev, blocks, voices)
        pl, pr = py_render(p, ev, blocks, voices)
        peak = max(np.abs(pl).max(), np.abs(pr).max(), 1e-9)
        db = lambda x: 20 * math.log10(max(x, 1e-12))  # noqa: E731
        start = max(np.abs(cl - pl)[:4 * BLOCK].max(), np.abs(cr - pr)[:4 * BLOCK].max()) / peak
        err = max(np.abs(cl - pl).max(), np.abs(cr - pr).max()) / peak
        bands = band_diff(cl + cr, pl + pr)
        check(start < 10 ** (-60 / 20) and err < 10 ** (-30 / 20) and bands < 0.5,
              f"port = model: {name} (first 4 blocks {db(start):.0f} dB, all {db(err):.0f} dB of the peak; "
              f"third-octave bands within {bands:.2f} dB)")


# ---- 2. pitch -------------------------------------------------------------------------------------
def freq_of(x: np.ndarray, guess: float) -> float:
    n = len(x)
    X = np.abs(np.fft.rfft(x * np.blackman(n)))
    fr = np.fft.rfftfreq(n, 1 / RATE)
    lo, hi = np.searchsorted(fr, guess * 0.94), np.searchsorted(fr, guess * 1.06)
    k = lo + int(np.argmax(X[lo:hi]))
    a, b, c = np.log(X[k - 1:k + 2] + 1e-30)
    return fr[k] + 0.5 * (a - c) / (a - 2 * b + c) * (fr[1] - fr[0])


def pitch(r: Rig) -> None:
    worst = 0.0
    cases = [(n, 1, 0.0, 0.0) for n in (24, 36, 48, 60, 72, 84, 96, 108)]
    cases += [(48, 0, 0.0, 0.0), (48, 2, 0.0, 0.0), (60, 1, 5.0, 0.0), (60, 1, -12.0, 0.0), (60, 1, 0.0, 37.0)]
    if QUICK:
        cases = cases[::3]
    for note, rng, tune, fine in cases:
        p = init(range=rng, tune=tune, fine=fine, cutoff=20000.0, env_amt=0.0, s=1.0, a=0.001)
        L, _, _ = c_render(r, p, [(0, 0, note, 0, 1)], 380)
        f = 440.0 * 2 ** ((note + 12 * (rng - 1) + tune + fine / 100 - 69) / 12)
        got = freq_of(L[-1 << 15:], f)
        worst = max(worst, abs(1200 * math.log2(got / f)))
    check(worst < 1.0, f"pitch within 1 cent, notes 24-108, Range, Tune, Fine (worst {worst:.3f} ct)")


# ---- 3. aliasing ----------------------------------------------------------------------------------
def worst_alias(y: np.ndarray, f0: float, lim: float = 16000.0) -> float:
    n = len(y)
    X = np.abs(np.fft.rfft(y * np.blackman(n)))
    fr = np.fft.rfftfreq(n, 1 / RATE)
    h = np.zeros_like(X, bool)
    for k in range(1, int(22050 / f0) + 1):
        h |= np.abs(fr - k * f0) < 6 * RATE / n
    ref = X[np.abs(fr - f0) < 6 * RATE / n].max()
    m = (~h) & (fr < lim) & (fr > 20)
    return 20 * math.log10(X[m].max() / ref)


# measured 2026-11-17 with the 4-point correction, through the ladder and its soft clip (the model
# and this port agree): saw -56.4 / -50.8 dB, 30 % pulse -54.6 / -49.0 at notes 96 / 108
ALIAS_BOUNDS = {96: -54.0, 108: -48.0}


def aliasing(r: Rig) -> None:
    for label, extra in (("saw", {}), ("30 % pulse", dict(saw=0.0, pulse=1.0, pw=0.44))):
        for note, bound in ALIAS_BOUNDS.items():
            p = init(cutoff=20000.0, res=0.0, env_amt=0.0, key=0.0, s=1.0, a=0.001) | extra
            L, _, _ = c_render(r, p, [(0, 0, note, 0, 1)], 560)
            got = worst_alias(L[-65536:], 440 * 2 ** ((note - 69) / 12))
            check(got <= bound, f"aliasing, {label}, note {note}: {got:.1f} dB (bound {bound} dB)")


# ---- 4. filters -----------------------------------------------------------------------------------
def spectrum(x: np.ndarray) -> np.ndarray:
    return np.abs(np.fft.rfft(x * np.hanning(len(x))))


def ratio_db(r: Rig, p_on: dict, p_off: dict, blocks=200) -> tuple[np.ndarray, np.ndarray]:
    """the transfer function from two renders with the same noise (the same voice, from its start)"""
    a, _, _ = c_render(r, p_on, [(0, 0, 60, 0, 1)], blocks)
    b, _, _ = c_render(r, p_off, [(0, 0, 60, 0, 1)], blocks)
    n = 1 << 14
    fa, fb = spectrum(a[-n:]), spectrum(b[-n:])
    fr = np.fft.rfftfreq(n, 1 / RATE)
    # smooth over 1/6 octave: the ratio of two noise spectra is exact per bin, but leaky
    return fr, 20 * np.log10((fa + 1e-12) / (fb + 1e-12))


def band(fr, db, f, width=0.08):
    m = (fr > f * (1 - width)) & (fr < f * (1 + width))
    return float(np.median(db[m]))


def filters(r: Rig) -> None:
    noise = dict(saw=0.0, noise=0.25, env_amt=0.0, key=0.0, s=1.0, a=0.001, res=0.0)
    # the ladder at Resonance 0: four trapezoidal one-poles at the cutoff, prewarped
    fr, db = ratio_db(r, init(**noise, cutoff=1000.0), init(**noise, cutoff=20000.0))
    g0 = math.tan(math.pi * 1000.0 / RATE)
    worst = 0.0
    for f in (200, 500, 1000, 2000, 4000, 8000):
        g = math.tan(math.pi * f / RATE)
        # four bilinear one-poles: |H|^2 = (1 + (g/g0)^2)^-4; less the open filter's own (its cutoff is
        # clamped at 16 kHz)
        want = -40 * math.log10(1 + (g / g0) ** 2) + 40 * math.log10(1 + (g / math.tan(math.pi * 16000.0 / RATE)) ** 2)
        worst = max(worst, abs(band(fr, db, f) - want))
    check(worst < 1.5, f"ladder at Resonance 0 follows four one-poles at 1 kHz, 200 Hz-8 kHz (worst {worst:.2f} dB)")
    slope = band(fr, db, 4000) - band(fr, db, 8000)
    check(20 < slope < 28, f"ladder falls about 24 dB an octave above the cutoff (4-8 kHz: {slope:.1f} dB)")
    # the HPF: -3 dB at its setting
    for hz in (240.0, 1000.0):
        fr, db = ratio_db(r, init(**noise, hpf=hz), init(**noise))
        at = band(fr, db, hz, 0.04)
        check(abs(at + 3.01) < 0.6, f"HPF {hz:g} Hz: {at:.2f} dB at {hz:g} Hz (one-pole: -3.0)")
    fr, db = ratio_db(r, init(**noise, hpf=21.0), init(**noise))
    above = max(abs(band(fr, db, f)) for f in (200, 1000, 5000, 12000))
    check(above < 0.05, f"HPF at its lowest setting above Off changes nothing above 200 Hz ({above:.3f} dB)")
    fr, db = ratio_db(r, init(**noise, boost=1), init(**noise), blocks=400)
    lo, hi = band(fr, db, 30, 0.3), band(fr, db, 3000)
    check(abs(lo - 6.02) < 0.6 and abs(hi) < 0.3, f"Bass Boost: {lo:.2f} dB at 30 Hz, {hi:.2f} dB at 3 kHz")
    # self-oscillation
    osc = dict(saw=0.0, noise=0.001, env_amt=0.0, key=0.0, s=1.0, a=0.001, cutoff=1000.0)   # the noise starts it
    L, _, _ = c_render(r, init(**osc, res=1.0), [(0, 0, 60, 0, 1)], 400)
    tail = L[-1 << 14:]
    p100 = float(np.abs(tail).max())
    f = freq_of(tail, 1000.0) if p100 > 1e-3 else 0.0
    check(p100 > 0.01 and abs(1200 * math.log2(max(f, 1) / 1000.0)) < 20,
          f"Resonance 100 % oscillates at the cutoff ({f:.1f} Hz, peak {p100:.3f})")
    L, _, _ = c_render(r, init(**osc, res=0.9), [(0, 0, 60, 0, 1)], 400)
    p90 = float(np.abs(L[-1 << 14:]).max())
    check(p90 < p100 / 30, f"Resonance 90 % doesn't: the same noise peaks at {p90:.4f}")
    # everything at full, filters swept by the LFO and the envelope: bounded, finite
    hot = init(saw=1.0, pulse=1.0, sub=1.0, noise=1.0, res=1.0, boost=1, hpf=1500.0, env_amt=1.0,
               lfo_cut=1.0, lfo_rate=7.0, lfo_wave=2, cutoff=20.0, vintage=1.0)
    L, R, _ = c_render(r, hot, [(0, 0, 36, 0, 1), (150, 0, None, 0, 1)], 220)
    check(np.isfinite(L).all() and np.isfinite(R).all() and np.abs(L).max() < 4.0,
          f"everything at full, filters swept: finite, peak {np.abs(L).max():.2f}")
    # a NaN parameter for one block, then back
    L, _, _ = c_render(r, init(), [(0, 0, 60, 0, 1)], 60,
                       nan_at=(20, dict(cutoff=float("nan"), res=float("nan"), hpf=float("nan"), pw=float("nan"))))
    check(np.isfinite(L).all() and np.abs(L[-2048:]).max() > 0.01, "a NaN parameter: finite, and it plays on after")


# ---- 5. the envelope -------------------------------------------------------------------------------
def env_trace(r: Rig, p: dict, off_block: int, blocks: int):
    r.call("a_d6_lfo_reset")
    r.call("a_d6_init", r.voices[0], 0)
    r.params(p)
    r.set_ctl(p["lfo_rate"])
    r.call("a_d6_on", r.voices[0], r.p, 60, 0, 1, r.ctl)
    env, alive = [], []
    for b in range(blocks):
        if b == off_block:
            r.call("a_d6_off", r.voices[0])
        alive.append(r.call("a_d6_render", r.voices[0], r.p, r.L, r.R, 1, r.ctl))
        st = r.state(0)
        env.append(st[1] if p["vca"] else st[0])
    return np.array(env), alive


def envelope(r: Rig) -> None:
    bt = BLOCK / RATE
    for a in (0.1, 1.0):
        env, _ = env_trace(r, init(a=a, d=5.0, s=1.0), 10_000, int(a / bt) + 40)
        t = (int(np.argmax(env >= 0.999)) + 1) * bt
        check(abs(t - a) <= max(0.05 * a, bt), f"Attack {a:g} s peaks at {t:.3f} s")
    for d in (0.2, 1.0):
        env, _ = env_trace(r, init(a=0.001, d=d, s=0.0), 10_000, int(d / bt) + 40)
        t = (int(np.argmax(env <= 0.01)) + 1) * bt
        check(abs(t - d) <= max(0.05 * d, bt), f"Decay {d:g} s reaches 99 % in {t:.3f} s")
    rel = 0.5
    env, alive = env_trace(r, init(a=0.001, s=1.0, r=rel), 20, 20 + int(3 * rel / bt))
    t = (int(np.argmax(env[20:] <= 0.01)) + 1) * bt
    check(abs(t - rel) <= max(0.05 * rel, bt), f"Release {rel:g} s reaches 99 % in {t:.3f} s")
    stop = alive.index(0) if 0 in alive else None
    check(stop is not None and stop * bt < 20 * bt + 2.2 * rel, f"the voice stops after its release (block {stop})")
    env, alive = env_trace(r, init(vca=1, a=2.0, r=2.0), 20, 40)
    check(abs(env[0] - (1 - (1 - 0.0113) ** 128)) < 1e-3 and 0 in alive,
          "VCA Gate: the gate ramps in about 2 ms and the voice stops after it, whatever the Release")


# ---- 6. LFO Trigger -------------------------------------------------------------------------------
def lfo_trigger(r: Rig) -> None:
    for free in (1, 0):
        p = init(lfo_free=free, lfo_rate=2.3, lfo_wave=5, lfo_pitch=0.2)
        r.call("a_d6_lfo_reset")
        for i in range(3):
            r.call("a_d6_init", r.voices[i], i)
        r.params(p)
        r.set_ctl(p["lfo_rate"])
        # voice 0 at block 0, voice 1 at block 29, voice 2 at block 71; then 17 blocks of all three
        for vi, blocks in ((0, 29), (1, 42), (2, 17)):
            r.call("a_d6_on", r.voices[vi], r.p, 60 + 4 * vi, 0, 1, r.ctl)
            for _ in range(blocks):
                for k in range(vi + 1):
                    r.call("a_d6_render", r.voices[k], r.p, r.L, r.R, 1, r.ctl)
        st = [r.state(k) for k in range(3)]
        same = all(abs(s[2] - st[0][2]) < 1e-6 and s[3] == st[0][3] for s in st)
        if free:
            check(same, f"LFO Trigger Free: three voices started apart share one phase and cycle {[round(s[2], 4) for s in st]}")
        else:
            check(not same, f"LFO Trigger Note: each voice's LFO started with its note {[round(s[2], 4) for s in st]}")


# ---- 7. unison and Vintage --------------------------------------------------------------------------
def unison(r: Rig) -> None:
    L, R, _ = c_render(r, init(width=1.0), [(0, 0, 60, 0, 1)], 60)
    check(np.array_equal(L, R), "Unison Off: L and R alike, whatever the Width")
    # 1.5 s windows: a detuned stack beats, and its level is an average over the beats
    one, _, _ = c_render(r, init(), [(0, 0, 48, 0, 1)], 560)
    rms1 = math.sqrt(np.mean(one[-1 << 16:] ** 2))
    for u in (2, 4, 8):
        sides = []
        for w in (0.0, 1.0):
            ev = [(0, k, 48, k, u) for k in range(u)]
            L, R, _ = c_render(r, init(detune=25.0, width=w), ev, 560 if w else 120, voices=u)
            mid, side = (L + R) / 2, (L - R) / 2
            sides.append(math.sqrt(np.mean(side[-1 << 14:] ** 2)) / max(math.sqrt(np.mean(mid[-1 << 14:] ** 2)), 1e-9))
            if w == 1.0:
                rms = math.sqrt(np.mean(mid[-1 << 16:] ** 2) + np.mean(side[-1 << 16:] ** 2))
                lvl = 20 * math.log10(rms / rms1)
                check(abs(lvl) < 1.0, f"Unison {u}: the stack's level is {lvl:+.2f} dB from one voice's")
        check(sides[0] < 1e-6 and sides[1] > 0.1, f"Unison {u}: Width 0 is centred, Width 100 % spreads it "
              f"(side/mid {sides[0]:.2g} and {sides[1]:.2f})")
    # Vintage: drift inside the plan's bounds over 10 minutes (the model's walk, which the port matches)
    v = model.Voice(3, init(vintage=1.0))
    v.note_on(60)
    lo = hi = 0.0
    blocks = int(600 * RATE / BLOCK)
    for _ in range(blocks if not QUICK else blocks // 10):
        for k in range(2):
            v.drift[k] = 0.99635 * v.drift[k] + 0.074 * v.rnd.next()
        lo, hi = min(lo, v.drift[0]), max(hi, v.drift[0])
    check(-3.0 < lo and hi < 3.0, f"Vintage drift over 10 minutes stays within +-3 units (+-9 ct): {lo:.2f} to {hi:.2f}")


# ---- 8. cost ----------------------------------------------------------------------------------------
# Instructions per block on the emulated core: a regression ceiling, not the CPU load. About 15 %
# over the 2026-11-17 build's figures.
# "idle" is mostly the harness clearing the two output buffers (a_d6_render); the engine returns at once.
COST_CEILINGS = {"DCO6 voice, Init": 21_000, "DCO6 voice, heavy": 25_500, "DCO6 voice, idle": 700}


def cost(r: Rig) -> None:
    from unicorn import UC_HOOK_CODE
    n = [0]
    heavy = init(saw=1.0, pulse=0.8, sub=0.6, noise=0.2, pw=0.3, pwm=0.5, lfo_pitch=0.1, hpf=80.0, boost=1,
                 res=0.7, lfo_cut=0.3, vintage=1.0, lfo_wave=6, s=1.0)
    for label, p, note in (("DCO6 voice, Init", init(), True), ("DCO6 voice, heavy", heavy, True),
                           ("DCO6 voice, idle", init(), False)):
        r.call("a_d6_lfo_reset")
        r.call("a_d6_init", r.voices[0], 0)
        r.params(p)
        r.set_ctl(p["lfo_rate"])
        if note:
            r.call("a_d6_on", r.voices[0], r.p, 60, 0, 1, r.ctl)
        r.call("a_d6_render", r.voices[0], r.p, r.L, r.R, 5, r.ctl)
        h = r.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1))
        r.m.uc.ctl_flush_tb()
        n[0] = 0
        r.call("a_d6_render", r.voices[0], r.p, r.L, r.R, 4, r.ctl)
        r.m.uc.hook_del(h)
        r.m.uc.ctl_flush_tb()
        got = n[0] / 4
        print(f"info cost {label}: {got:,.0f} instructions per block (ceiling {COST_CEILINGS[label]:,})")
        check(got < COST_CEILINGS[label], f"cost {label} under its ceiling")


def main() -> int:
    axf = Path(sys.argv[1])
    r = Rig(axf)
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    for part in (port, pitch, aliasing, filters, envelope, lfo_trigger, unison, cost):
        if only and part.__name__ not in only:
            continue
        print(f"--- {part.__name__}", flush=True)
        part(r)
    print(f"\n{checks - len(failures)} of {checks} checks passed")
    for f in failures:
        print("FAILED:", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
