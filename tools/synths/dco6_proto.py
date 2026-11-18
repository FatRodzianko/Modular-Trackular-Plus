# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""DCO6 prototype: a host-side model of the planned virtual-analog engine (docs/mt/VA_SYNTH_PLAN.md).

PROTOTYPE ONLY. Plain Python floats (float64), one sample at a time, written to be read rather than
to be fast. It is the reference the .psyn port (core/mods/synths/d6Engine.h) is checked against, and
it renders the listening examples:

    python tools/synths/dco6_proto.py [out_dir]        (default build/dco6_wav)

The voice, after the Juno-106's design (a DCO, a high-pass, a 4-pole low-pass, one envelope):
- DCO: saw, pulse with PWM, a square sub an octave down, white noise. Saw, pulse and sub are
  band-limited with a 4-point step correction from the integrated cubic B-spline (Valimaki, Pekonen
  and Nam, JASA 2012), which makes the DCO's output 2 samples late.
- HPF: a continuous one-pole high-pass, 20 Hz (Off) to 2 kHz, and a Bass Boost switch (a +6 dB low
  shelf, the 106's position 0). The 106's HPF is a 4-notch slider; this one is smooth and the boost
  is separate, so both can be on.
- VCF: a 4-pole ladder of trapezoidal one-poles with the feedback solved without a unit delay
  (Zavalishin's topology-preserving transform, "The Art of VA Filter Design"), and a soft clip
  on the ladder's input.
- One ADSR (RC-style exponential segments) for the VCF and, in Env mode, the VCA; Gate mode uses
  a 2 ms gate instead.
- One LFO with seven shapes and a delay (fade-in).
- Vintage (slop) and unison (detune, width) as in the plan.

Control values (pitch, cutoff, LFO) update every CTRL samples, as the engine will.
"""
from __future__ import annotations

import math
import struct
import sys
import wave
from pathlib import Path

FS = 44100.0
BLOCK = 128
CTRL = 8                      # control-rate step, samples

LFO_SHAPES = ("Triangle", "Sine", "Square", "Ramp Up", "Ramp Down", "Random S&H", "Random Smooth")
HPF_OFF_HZ = 20.0                      # HPF at its minimum is Off (bypassed)
BOOST_HZ, BOOST_GAIN = 110.0, 1.0      # Bass Boost: +6 dB shelf (x + 1.0 * lowpass(x)); by ear, not a 106 measurement


def default_params():
    return dict(
        # Osc
        range=1, tune=0.0, fine=0.0, saw=1.0, pulse=0.0, sub=0.0, noise=0.0,
        pw=0.0, pwm=0.0, pwm_src=0, lfo_pitch=0.0,
        # Filter
        hpf=20.0, boost=0, cutoff=2400.0, res=0.15, env_amt=0.35, lfo_cut=0.0, key=0.5,
        # Env
        a=0.005, d=0.6, s=0.6, r=0.35, vca=0,
        # LFO
        lfo_wave=0, lfo_rate=4.0, lfo_delay=0.0, lfo_free=0,
        # Voice
        unison=1, detune=0.0, width=0.5, vintage=0.0, glide=0.0, volume=1.0,
    )


class Rand:
    """xorshift32, as the engine will use; uniform in [-1, 1)."""

    def __init__(self, seed):
        self.x = (seed * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF or 1

    def next(self):
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.x = x
        return (x - 4294967296 if x >= 2147483648 else x) / 2147483648.0

    def next_u32(self):
        self.next()
        return self.x


def hash01(x):
    """Chris Wellons's lowbias32 integer hash, mapped to [-1, 1): the random LFO shapes' value for a
    cycle, so voices sharing a cycle share its value."""
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x7FEB352D) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x846CA68B) & 0xFFFFFFFF
    x ^= x >> 16
    return (x - 4294967296 if x >= 2147483648 else x) / 2147483648.0


FREE_SEED = 0x51ED270B
# LFO Trigger Free: voices with the same LFO rate and shape share a phase, as the engine's table does
# (see d6Engine.h). play() empties it; the engine's belongs to the plugin, shared by every DCO6 part.
LFO_TAB = {}


def blep4(d):
    """The 4-point step correction: the integrated cubic B-spline minus a unit step (Valimaki, Pekonen
    and Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated
    polynomial interpolation", JASA 2012), at the four samples around a step d samples (0..1) before
    the first sample after it: returns the residuals for samples n-2, n-1, n, n+1."""
    u = 1.0 - d
    d2, u2 = d * d, u * u
    return (d2 * d2 / 24.0,
            0.5 - u * (2.0 / 3.0) + u2 * u / 3.0 - u2 * u2 / 8.0,
            -(0.5 - d * (2.0 / 3.0) + d2 * d / 3.0 - d2 * d2 / 8.0),
            -u2 * u2 / 24.0)


def soft_clip(x):
    """tanh-like rational curve, exact limit +-1 at |x| = 3"""
    if x > 3.0:
        return 1.0
    if x < -3.0:
        return -1.0
    x2 = x * x
    return x * (27.0 + x2) / (27.0 + 9.0 * x2)


def exp2_fast(x):
    """2^x as the engine's exp2f_fast (synthfx/fxDsp.h) works it out: the same polynomial on the
    fraction, so the model's pitch and cutoff carry the engine's error (under 2e-6) and the two can be
    compared sample for sample."""
    x = min(max(x, -100.0), 100.0)
    i = math.floor(x)
    f = x - i
    p = 1.0 + f * (0.69314718 + f * (0.24022651 + f * (0.05550411 + f * (0.00961813
              + f * (0.00133336 + f * 0.00015404)))))
    return math.ldexp(p, i)


def tan_pade(x):
    """tan for 0..1.3 rad, as the engine (synthfx/fxDsp.h)"""
    x2 = x * x
    return x * (15.0 - x2) / (15.0 - 6.0 * x2)


ATTACK_TC = math.log(1.3 / 0.3)      # the attack aims at 1.3 and peaks at 1.0 after this many time constants


def env_coef(t, tc=4.6):
    """one-pole coefficient going tc time constants in time t: 4.6 is 99 % of the way (decay and
    release); ATTACK_TC puts the attack's peak at t"""
    return 1.0 - math.exp(-tc / (min(max(t, 0.0005), 30.0) * FS))


class Voice:
    """One host voice. slot / size: its place in a unison stack (the host hook gives these)."""

    def __init__(self, index, p):
        self.index = index
        self.p = p
        self.rnd = Rand(1000 + index)
        # Vintage, per voice: fixed offsets, like part tolerances (drawn once from the voice's index)
        self.tol = [self.rnd.next() for _ in range(6)]   # pitch, cutoff, attack, decay/release, pw, level
        self.phase = (self.rnd.next() + 1.0) * 0.5          # free-running DCO: a random start
        self.sub = 1.0
        self.z = [0.0, 0.0, 0.0, 0.0]                         # the DCO's 4-point correction
        self.s = [0.0, 0.0, 0.0, 0.0]                         # ladder states
        self.hs = 0.0                                         # HPF state
        self.hb = 0.0                                         # Bass Boost shelf state
        self.env = 0.0
        self.stage = 0           # 0 idle, 1 attack, 2 decay/sustain, 3 release
        self.gate = 0.0          # Gate-mode VCA, smoothed
        self.gate_on = False
        self.note = 60
        self.pitch = None        # current pitch (for glide), semitones
        self.lfo_phase = 0.0
        self.lfo_seed = 0
        self.lfo_cycle = 0
        self.lfo_age = 0.0
        self.drift = [0.0, 0.0]  # pitch, cutoff random walks
        self.note_off = [0.0, 0.0]
        self.slot, self.size = 0, 1

    # --- notes -------------------------------------------------------------------------------
    def note_on(self, note, slot=0, size=1):
        p = self.p
        self.note = note
        self.slot, self.size = slot, size
        if self.pitch is None or p["glide"] <= 0.0 or self.stage == 0:
            self.pitch = float(note)
        self.stage = 1
        self.gate_on = True
        self.lfo_age = 0.0
        v = p["vintage"]
        self.note_off = [v * 1.5 * self.rnd.next(), v * 0.05 * self.rnd.next()]  # cents, octaves
        if not p["lfo_free"]:              # Note: the LFO starts again, with its own random values
            self.lfo_phase, self.lfo_cycle = 0.0, 0
            self.lfo_seed = self.rnd.next_u32()
        else:                               # Free: join the voices at this rate and shape
            self.lfo_seed = FREE_SEED
            if self.lfo_key() in LFO_TAB:
                self.lfo_phase, self.lfo_cycle = LFO_TAB[self.lfo_key()]

    def lfo_key(self):
        return (self.p["lfo_wave"], self.p["lfo_rate"])

    def note_off_(self):
        self.stage = 3
        self.gate_on = False

    def active(self):
        return self.stage != 0

    # --- the LFO, at control rate ------------------------------------------------------------
    def lfo(self, n):
        p = self.p
        w = p["lfo_wave"]
        ph = self.lfo_phase
        if w == 0:
            y = 4.0 * ph - 1.0 if ph < 0.5 else 3.0 - 4.0 * ph
        elif w == 1:   # a sine-like curve from the triangle, as the engine (-cos phase)
            x = 4.0 * ph - 1.0 if ph < 0.5 else 3.0 - 4.0 * ph
            y = x * (1.5 - 0.5 * x * x)
        elif w == 2:
            y = 1.0 if ph < 0.5 else -1.0
        elif w == 3:
            y = 2.0 * ph - 1.0
        elif w == 4:
            y = 1.0 - 2.0 * ph
        elif w == 5:
            y = hash01(self.lfo_seed + self.lfo_cycle)
        else:
            a, b = hash01(self.lfo_seed + self.lfo_cycle), hash01(self.lfo_seed + self.lfo_cycle + 1)
            y = a + (b - a) * (ph * ph * (3.0 - 2.0 * ph))
        ph += p["lfo_rate"] * n / FS
        if ph >= 1.0:
            whole = math.floor(ph)
            ph -= whole
            self.lfo_cycle += whole
        self.lfo_phase = ph
        d = p["lfo_delay"]
        self.lfo_age += n / FS
        fade = 1.0 if d <= 0.0 else min(1.0, self.lfo_age / d)
        return y * fade

    # --- one block ---------------------------------------------------------------------------
    def render(self, out_l, out_r):
        p = self.p
        if self.stage == 0:
            return
        v = p["vintage"]
        tol = self.tol
        # envelope times, with Vintage's per-voice spread (+-8 % at 100 %)
        ca = env_coef(p["a"] * (1.0 + 0.08 * v * tol[2]), ATTACK_TC)
        cd = env_coef(p["d"] * (1.0 + 0.08 * v * tol[3]))
        cr = env_coef(p["r"] * (1.0 + 0.08 * v * tol[3]))
        sus = p["s"]
        # Vintage drift: per voice, a random walk with a leak, stepped once a block: corner ~0.2 Hz,
        # standard deviation ~0.5 (peaks ~+-1.5)
        for k in range(2):
            self.drift[k] = 0.99635 * self.drift[k] + 0.074 * self.rnd.next()
        # unison: this voice's detune and pan
        size = self.size
        if size > 1:
            spread = 2.0 * self.slot / (size - 1) - 1.0           # -1 .. 1 by slot
            order = (self.slot * 2) if self.slot < (size + 1) // 2 else (2 * (size - 1 - self.slot) + 1)
            det_c = p["detune"] * (2.0 * order / (size - 1) - 1.0)  # detune interleaved against pan
            pan = p["width"] * spread
            stack_gain = 1.0 / math.sqrt(size)
        else:
            det_c, pan, stack_gain = 0.0, 0.0, 1.0
        a_ = (pan + 1.0) * 0.5                     # equal power (sqrt law), centre 1 each side
        gl, gr = math.sqrt(2.0 * (1.0 - a_)) * stack_gain, math.sqrt(2.0 * a_) * stack_gain
        level = p["volume"] * 0.2 * (1.0 + 0.046 * v * tol[5])
        hpf_hz = min(max(p["hpf"], HPF_OFF_HZ), 2000.0)
        hpf_on = hpf_hz > HPF_OFF_HZ + 0.5
        hg = tan_pade(math.pi * hpf_hz / FS)
        hG = hg / (1.0 + hg)                     # once per block: the HPF isn't modulated inside a block
        boost = p["boost"] >= 0.5
        bg = tan_pade(math.pi * BOOST_HZ / FS)
        bG = bg / (1.0 + bg)
        k = 4.1 * p["res"]
        comp = 1.0 + 0.25 * k                      # partial make-up for the ladder's bass loss
        for c0 in range(0, BLOCK, CTRL):
            lfo = self.lfo(CTRL)
            # glide
            if p["glide"] > 0.0:
                gc = 1.0 - math.exp(-4.6 * CTRL / (min(max(p["glide"], 0.001), 30.0) * FS))
                self.pitch += (self.note - self.pitch) * gc
            else:
                self.pitch = float(self.note)
            cents = (p["fine"] + det_c + v * (4.0 * tol[0] + 3.0 * self.drift[0]) + self.note_off[0])
            semis = (self.pitch + 12.0 * (p["range"] - 1) + p["tune"] + cents / 100.0
                     + 12.0 * p["lfo_pitch"] ** 2 * lfo)
            f = 440.0 * exp2_fast((semis - 69.0) / 12.0)
            dt = min(f / FS, 0.45)
            # pulse width: 50 % at 0, 5 % at 1; PWM from the LFO or the envelope
            mod = lfo if p["pwm_src"] == 0 else self.env
            pwv = min(1.0, max(0.0, p["pw"] + p["pwm"] * mod * (0.5 if p["pwm_src"] == 0 else 1.0)
                               + 0.03 * v * tol[4]))
            duty = 0.5 - 0.45 * pwv
            # cutoff in octaves
            oct_ = (8.0 * p["env_amt"] * self.env
                    + 4.0 * p["lfo_cut"] ** 2 * lfo + p["key"] * (self.pitch - 60.0) / 12.0
                    + v * (0.15 * tol[1] + 0.08 * self.drift[1]) + self.note_off[1])
            fc = min(min(max(p["cutoff"], 10.0), 20000.0) * exp2_fast(oct_), 16000.0)
            fc = max(fc, 10.0)
            g = tan_pade(math.pi * fc / FS)
            G = g / (1.0 + g)
            G2 = G * G
            G4 = G2 * G2
            for i in range(c0, c0 + CTRL):
                # envelope
                if self.stage == 1:
                    self.env += (1.3 - self.env) * ca
                    if self.env >= 1.0:
                        self.env, self.stage = 1.0, 2
                elif self.stage == 2:
                    self.env += (sus - self.env) * cd
                else:
                    self.env += (0.0 - self.env) * cr
                self.gate += ((1.0 if self.gate_on else 0.0) - self.gate) * 0.0113  # ~2 ms
                amp = self.env if p["vca"] == 0 else self.gate
                # DCO
                # naive saw, pulse and sub; each step gets the 4-point correction, so the DCO's
                # output is 2 samples late (z: samples n-2, n-1, n, and n+1's pending part)
                prev = self.phase
                ph = prev + dt
                wrapped = ph >= 1.0
                if wrapped:
                    ph -= 1.0
                    self.sub = -self.sub
                self.phase = ph
                z = self.z
                z[2] = (p["saw"] * (2.0 * ph - 1.0) + p["pulse"] * (1.0 if ph < duty else -1.0)
                        + p["sub"] * self.sub + z[3])
                z[3] = 0.0
                steps = []
                if wrapped:
                    steps.append((ph / dt, -2.0 * p["saw"] + 2.0 * p["pulse"] + 2.0 * p["sub"] * self.sub))
                    if prev < duty:             # fell before the wrap
                        steps.append(((ph + 1.0 - duty) / dt, -2.0 * p["pulse"]))
                    elif ph >= duty:            # fell after it (duty under dt)
                        steps.append(((ph - duty) / dt, -2.0 * p["pulse"]))
                elif prev < duty <= ph:
                    steps.append(((ph - duty) / dt, -2.0 * p["pulse"]))
                for d, step in steps:
                    r = blep4(min(d, 1.0))
                    for j in range(4):
                        z[j] += step * r[j]
                x = z[0] + p["noise"] * self.rnd.next()
                z[0], z[1] = z[1], z[2]
                # Bass Boost, then the HPF (each a trapezoidal one-pole)
                if boost:
                    vb = (x - self.hb) * bG
                    lb = vb + self.hb
                    self.hb = lb + vb
                    x = x + BOOST_GAIN * lb
                if hpf_on:
                    vh = (x - self.hs) * hG
                    lp = vh + self.hs
                    self.hs = lp + vh
                    x = x - lp
                # ladder: solve the feedback, clip the input, run the four stages
                s = self.s
                S = G * G2 * (1 - G) * s[0] + G2 * (1 - G) * s[1] + G * (1 - G) * s[2] + (1 - G) * s[3]
                y4 = (G4 * x + S) / (1.0 + k * G4)
                u = soft_clip(0.5 * (x - k * y4)) * 2.0
                for j in range(4):
                    vv = (u - s[j]) * G
                    y = vv + s[j]
                    s[j] = y + vv
                    u = y
                y = u * comp * amp * level
                out_l[i] += y * gl
                out_r[i] += y * gr
        if self.stage == 3 and (self.gate < 1e-4 if p["vca"] else self.env < 1e-4):   # Gate: silent once it closes
            self.stage = 0
            self.env = 0.0
        if p["lfo_free"]:
            LFO_TAB[self.lfo_key()] = (self.lfo_phase, self.lfo_cycle)
        if any(x != x for x in self.s) or self.hs != self.hs or self.hb != self.hb:
            self.s, self.hs, self.hb = [0.0] * 4, 0.0, 0.0


class Part:
    """A synth part with V host voices and unison U, as the planned host hook allocates them:
    floor(V / U) groups of U consecutive voices; the leftovers never sound."""

    def __init__(self, voices, p):
        self.p = p
        self.voices = [Voice(i, p) for i in range(voices)]
        self.age = [0] * voices
        self.held = {}

    def note_on(self, note):
        size = max(1, min(self.p["unison"], len(self.voices)))
        groups = len(self.voices) // size
        # the group whose leader is free, else the oldest
        best = None
        for g in range(groups):
            lead = self.voices[g * size]
            if not lead.active():
                best = g
                break
        if best is None:
            best = max(range(groups), key=lambda g: self.age[g * size])
        for i in range(len(self.age)):
            self.age[i] += 1
        for k in range(size):
            i = best * size + k
            self.voices[i].note_on(note, k, size)
            self.age[i] = 0
        self.held[note] = best

    def note_off(self, note):
        size = max(1, min(self.p["unison"], len(self.voices)))
        for v in self.voices:
            if v.note == note and v.gate_on:
                v.note_off_()

    def render(self, blocks):
        L, R = [], []
        for _ in range(blocks):
            bl, br = [0.0] * BLOCK, [0.0] * BLOCK
            for v in self.voices:
                v.render(bl, br)
            L.extend(bl)
            R.extend(br)
        return L, R


def play(p, voices, events, seconds):
    """events: (time s, note, on). p["_auto"], if there, is called with the time before each block and
    returns parameter changes (a knob moved while the note plays)."""
    p = dict(p)
    auto = p.pop("_auto", None)
    LFO_TAB.clear()
    part = Part(voices, p)
    blocks = int(seconds * FS / BLOCK)
    ev = sorted(events)
    L, R = [], []
    for b in range(blocks):
        t = b * BLOCK / FS
        if auto:
            p.update(auto(t))
        while ev and ev[0][0] <= t:
            _, n, on = ev.pop(0)
            part.note_on(n) if on else part.note_off(n)
        l, r = part.render(1)
        L.extend(l)
        R.extend(r)
    return L, R


def write_wav(path, L, R):
    peak = max(max(abs(x) for x in L), max(abs(x) for x in R), 1e-9)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(FS))
        frames = bytearray()
        for a, b in zip(L, R):
            frames += struct.pack("<hh", int(max(-1.0, min(1.0, a)) * 32767), int(max(-1.0, min(1.0, b)) * 32767))
        w.writeframes(bytes(frames))
    return peak


def chord(t0, t1, notes):
    return [(t0, n, True) for n in notes] + [(t1, n, False) for n in notes]


def examples():
    """name -> (params, voices, events, seconds, what it shows)"""
    ex = {}
    seq = [36, 36, 48, 36, 39, 36, 43, 41]
    bass_ev = []
    for i, n in enumerate(seq):
        bass_ev += [(i * 0.25, n, True), (i * 0.25 + 0.18, n, False)]
    p = default_params() | dict(saw=1.0, sub=0.6, cutoff=500.0, res=0.45, env_amt=0.45, d=0.25, s=0.0, r=0.1)
    ex["01_bass_saw_sub"] = (p, 1, bass_ev, 2.4, "saw + sub, resonant filter plucked by the envelope")
    p = default_params() | dict(saw=0.0, pulse=1.0, pw=0.2, pwm=0.6, lfo_rate=0.8, cutoff=3000.0, env_amt=0.1,
                                a=0.4, s=0.9, r=1.0, hpf=240.0)
    ex["02_pwm_pad_chord"] = (p, 8, chord(0.0, 3.0, (48, 55, 60, 64)), 4.5, "PWM pad, 4-note chord, HPF 240 Hz")
    p = default_params() | dict(cutoff=150.0, res=0.6, env_amt=0.0, s=1.0, a=0.01, r=0.5, lfo_cut=0.75,
                                lfo_rate=0.25, lfo_wave=1)
    ex["03_filter_sweep_res"] = (p, 1, chord(0.0, 4.0, (36,)), 4.6, "LFO (sine) sweeps the cutoff, Resonance 60 %")
    p = default_params() | dict(cutoff=400.0, res=1.0, env_amt=0.3, s=1.0)
    ex["04_self_oscillation"] = (p, 1, chord(0.0, 2.0, (48,)), 2.6, "Resonance 100 %: the filter whistles")
    for name, hz, b in (("off", 20.0, 0), ("240hz", 240.0, 0), ("720hz", 720.0, 0), ("boost", 20.0, 1),
                        ("boost_hpf60", 60.0, 1)):
        p = default_params() | dict(saw=1.0, sub=0.5, cutoff=6000.0, env_amt=0.0, s=1.0, hpf=hz, boost=b)
        label = f"HPF {'Off' if hz <= 20.0 else f'{hz:g} Hz'}, Bass Boost {'On' if b else 'Off'}"
        ex[f"05_hpf_{name}"] = (p, 1, chord(0.0, 1.5, (36,)), 1.8, label)
    p = default_params() | dict(saw=1.0, sub=0.5, cutoff=8000.0, env_amt=0.0, s=1.0, boost=1,
                                _auto=lambda t: dict(hpf=20.0 * 100.0 ** (1.0 - abs(1.0 - min(t, 4.0) / 2.0))))
    ex["05_hpf_sweep"] = (p, 1, chord(0.0, 4.0, (36,)), 4.4, "Bass Boost On, HPF swept 20 Hz to 2 kHz and back")
    for u in (1, 2, 4, 8):
        p = default_params() | dict(unison=u, detune=12.0, width=0.5, cutoff=4000.0, env_amt=0.1, s=0.9, a=0.01)
        ex[f"06_unison{u}_det12"] = (p, 8, chord(0.0, 2.0, (48,)), 2.5, f"Unison {u}, Detune 12 ct, Width 50 %")
    for d in (0.0, 7.0, 25.0, 50.0):
        p = default_params() | dict(unison=8, detune=d, width=1.0, cutoff=5000.0, env_amt=0.0, s=1.0)
        ex[f"07_unison8_detune{int(d)}"] = (p, 8, chord(0.0, 2.0, (48,)), 2.5, f"Unison 8, Detune {d:g} ct, Width 100 %")
    p = default_params() | dict(unison=4, detune=10.0, width=0.5, saw=1.0, cutoff=2500.0, a=0.05, s=0.8)
    ex["08_unison4_poly8"] = (p, 8, chord(0.0, 2.0, (55, 60)) + chord(2.2, 3.2, (55, 60, 64)), 3.8,
                              "8 voices, Unison 4: 2 notes; a third note steals the oldest stack")
    p = default_params() | dict(unison=4, detune=10.0)
    ex["09_unison4_poly7"] = (p, 7, chord(0.0, 2.0, (55, 60)), 2.5, "7 voices, Unison 4: 1 note, 3 voices unused")
    for v in (0.0, 0.5, 1.0):
        p = default_params() | dict(vintage=v, saw=1.0, pulse=0.0, cutoff=1800.0, env_amt=0.25, a=0.02, s=0.7, r=0.8)
        ev = []
        for i in range(4):
            ev += chord(i * 1.0, i * 1.0 + 0.8, (48, 52, 55, 59))
        ex[f"10_vintage{int(v * 100)}"] = (p, 8, ev, 4.8, f"Vintage {int(v * 100)} %: the same chord 4 times")
    for w in range(len(LFO_SHAPES)):
        p = default_params() | dict(lfo_wave=w, lfo_rate=3.0, lfo_pitch=0.2, s=1.0, cutoff=3000.0, env_amt=0.0)
        ex[f"11_lfo_{LFO_SHAPES[w].replace(' ', '').replace('&', '')}"] = (p, 1, chord(0.0, 2.0, (60,)), 2.2,
                                                                          f"LFO {LFO_SHAPES[w]} to pitch")
    p = default_params() | dict(lfo_delay=1.5, lfo_pitch=0.2, lfo_rate=6.0, s=1.0)
    ex["12_lfo_delay"] = (p, 1, chord(0.0, 3.0, (60,)), 3.3, "LFO Delay 1.5 s: vibrato fades in")
    p = default_params() | dict(vca=1, env_amt=0.6, d=0.3, s=0.0, cutoff=600.0, res=0.3)
    ex["13_vca_gate"] = (p, 1, chord(0.0, 1.0, (48,)) + chord(1.2, 2.2, (55,)), 2.6,
                         "VCA Gate: level by the gate, the envelope only moves the filter")
    p = default_params() | dict(env_amt=-0.5, cutoff=4000.0, a=0.6, d=0.6, s=0.3)
    ex["14_env_negative"] = (p, 1, chord(0.0, 2.0, (48,)), 2.6, "Env Amt -50 %: the envelope closes the filter")
    stagger = [(0.0, 48, True), (0.37, 55, True), (0.81, 60, True), (1.23, 64, True)]
    stagger += [(3.0, n, False) for n in (48, 55, 60, 64)]
    for free in (0, 1):
        p = default_params() | dict(lfo_free=free, lfo_rate=2.0, lfo_pitch=0.25, s=1.0, cutoff=3000.0, env_amt=0.0)
        ex[f"15_lfo_{'free' if free else 'note'}"] = (
            p, 8, stagger, 3.6, f"LFO Trigger {'Free' if free else 'Note'}: notes start apart; vibrato "
            + ("moves together (one LFO)" if free else "starts again with each note"))
    return ex


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "build" / "dco6_wav"
    only = sys.argv[2:]
    lines = []
    for name, (p, voices, ev, sec, what) in examples().items():
        if only and not any(o in name for o in only):
            continue
        L, R = play(p, voices, ev, sec)
        peak = write_wav(out / f"{name}.wav", L, R)
        rms = math.sqrt(sum(x * x for x in L + R) / (2 * len(L)))
        side = math.sqrt(sum((a - b) ** 2 for a, b in zip(L, R)) / len(L))
        lines.append(f"{name}.wav  peak {20 * math.log10(peak):6.1f} dBFS  rms {20 * math.log10(max(rms, 1e-9)):6.1f} dBFS"
                     f"  side/mid {20 * math.log10(max(side, 1e-9) / max(rms, 1e-9)):6.1f} dB  {what}")
        print(lines[-1], flush=True)
    (out / "index.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
