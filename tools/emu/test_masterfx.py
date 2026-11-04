#!/usr/bin/env python3
"""masterfx's effects at the object level: CE's own reverb and delay objects in the real firmware,
with AudioStream's block calls stubbed.

Usage: python tools/emu/test_masterfx.py <mods.axf> <reference.axf> [120_rig.elf]

The third file is 1.2.0's patch/masterfx (mf8) linked by itself, with its reverb and delay update()
at known addresses. With it, each effect must match 1.2.0's output bit for bit.

The graph, the synths and the screens are in test_masterfx_rig.py.
"""
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from machine import Machine  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402

BLOCK = 128
RATE = 44100
OBJECT = 0x20050000
REVERBS = {1: "Freeverb", 2: "Plate", 3: "Spring", 4: "Shimmer", 5: '"Gated"', 6: "Reverse"}
DELAYS = {1: "Tape", 2: "BBD"}

# AudioEffectShortDelay's fields (effect_shortDelay.h)
DL_BUF0, DL_BUF1, DL_BUF2 = 0x14, 0x18, 0x1C
DL_TIME, DL_FEEDBACK, DL_NOREFRESH, DL_PINGPONG = 0x28, 0x2C, 0x2D, 0x2E
DL_A0, DL_A1, DL_B1 = 0x30, 0x34, 0x38
DL_SILENT = 0x54

failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def f32(v):
    return struct.pack("<f", v)


def pole_of(hz):
    qc = 2 * math.pi * hz / RATE
    return int(1023 * -(math.cos(qc) / (1 + math.sin(qc))))


def coeffs_of(hz):
    qc = 2 * math.pi * hz / RATE
    y = math.cos(qc) / (1 + math.sin(qc))
    a = int(1023 * ((1.0 - y) / 2.0))
    return a, a, int(1023 * -y)


class Rig:
    """One object in one firmware. Blocks in, blocks out, as AudioStream would do it."""

    def __init__(self, axf):
        self.m = Machine(axf)
        m = self.m
        self.blocks = [m.alloc(4 + 2 * BLOCK, 4) for _ in range(4)]
        self.sent = {}
        self.released = []
        self.handed = []
        self.input_missing = [False, False]
        self.alloc_missing = 0
        self.next_block = 0
        m.stub_function(m.addr("_ZN11AudioStream15receiveWritableEj"), self._receive)
        m.stub_function(m.addr("_ZN11AudioStream15receiveReadOnlyEj"), self._receive)
        m.stub_function(m.addr("_ZN11AudioStream8allocateEv"), self._allocate)
        m.stub_function(m.addr("_ZN11AudioStream8transmitEP18audio_block_structh"), self._transmit)
        m.stub_function(m.addr("_ZN11AudioStream7releaseEP18audio_block_struct"), self._release)
        self.received = 0

    def _receive(self, mm):
        i = mm.arg(1)
        if self.input_missing[i]:
            return 0
        self.received += 1
        self.handed.append(self.blocks[i])
        return self.blocks[i]

    def _allocate(self, mm):
        if self.alloc_missing:
            self.alloc_missing -= 1
            return 0
        self.next_block = 1 + (self.next_block % 3)
        b = self.blocks[self.next_block]
        self.handed.append(b)
        return b

    def _transmit(self, mm):
        self.sent[mm.arg(2)] = struct.unpack(f"<{BLOCK}h", mm.read(mm.arg(1) + 4, 2 * BLOCK))
        return 0

    def _release(self, mm):
        self.released.append(mm.arg(0))
        return 0

    def put(self, i, samples):
        self.m.write(self.blocks[i] + 4, struct.pack(f"<{BLOCK}h", *samples))

    def active(self, kind, which):
        name = "activeReverb" if kind == "reverb" else "activeDelay"
        self.m.write(self.m.addr(f"_ZN4mods8masterfx{len(name)}{name}E"), bytes([which]))


class ReverbRig(Rig):
    def __init__(self, axf):
        super().__init__(axf)
        m = self.m
        m.write(OBJECT, b"\0" * 0x1000)
        m.call(m.addr("_ZN19AudioEffectPolyverbC1Ev"), OBJECT, max_insns=50_000_000)
        # Its fields, found by the ctor's values: amount, input gain, time, diffusion, lp.
        want = f32(0.8) + f32(0.1) + f32(0.7) + f32(0.625) + f32(0.2)
        data = m.read(OBJECT, 0x1000)
        self.off = data.index(want)
        self.update_fn = m.addr("_ZN19AudioEffectPolyverb6updateEv")
        self.reset()

    def reset(self):
        self.m.call(self.m.addr("_ZN19AudioEffectPolyverb5resetEv"), OBJECT, max_insns=50_000_000)

    def knobs(self, size=0.5, damp=0.5, pre=0.0, diff=0.625, amount=0.8, gain=0.1):
        m, o = self.m, OBJECT + self.off
        m.write(o, f32(amount) + f32(gain) + f32(size) + f32(diff) + f32(1.0 - damp))
        # lp_decay_0..2, then predelay_length_ (reverb.h)
        m.w32(o + 32, int(pre * 5000))

    def block(self, left, right=None):
        self.put(0, left)
        self.put(1, left if right is None else right)
        self.sent = {}
        self.m.call(self.update_fn, OBJECT, max_insns=20_000_000)
        return self.sent.get(0), self.sent.get(1)

    def run(self, inputs):
        left, right = [], []
        for x in inputs:
            a, b = self.block(x)
            left += a or []
            right += b or []
        return left, right


class DelayRig(Rig):
    def __init__(self, axf):
        super().__init__(axf)
        m = self.m
        m.write(OBJECT, b"\0" * 0x200)
        buf = m.addr("externalRamDelayBuff")
        for off, n in ((DL_BUF0, 0), (DL_BUF1, 1), (DL_BUF2, 2)):
            m.w32(OBJECT + off, buf + n * 2 * 262144)
        m.w32(OBJECT + DL_SILENT, 262144)
        self.update_fn = m.addr("_ZN21AudioEffectShortDelay6updateEv")
        self.knobs()
        self.clear()

    def clear(self):
        self.m.call(self.m.addr("_ZN21AudioEffectShortDelay5clearEv"), OBJECT, max_insns=20_000_000)

    def knobs(self, time=4410, fb=0.5, pp=False, hz=20000.0):
        m = self.m
        m.w32(OBJECT + DL_TIME, time)
        m.write(OBJECT + DL_FEEDBACK, bytes([int(255 * min(fb, 0.99))]))
        m.write(OBJECT + DL_PINGPONG, bytes([1 if pp else 0]))
        a0, a1, b1 = coeffs_of(hz)
        for off, v in ((DL_A0, a0), (DL_A1, a1), (DL_B1, b1)):
            m.w32(OBJECT + off, v & 0xFFFFFFFF)

    def block(self, x):
        self.put(0, x)
        self.sent = {}
        self.m.call(self.update_fn, OBJECT, max_insns=20_000_000)
        return self.sent.get(0), self.sent.get(1)

    def run(self, inputs):
        ch0, ch1 = [], []
        for x in inputs:
            a, b = self.block(x)
            ch0 += a or []
            ch1 += b or []
        return ch0, ch1

    def silent_count(self):
        return self.m.r32(OBJECT + DL_SILENT)


class Rig120:
    """1.2.0's mf8 DSP: a fake object with 1.2.0's offsets, as tools/emu/test_masterfx.py there did."""

    def __init__(self, elf, kind, which):
        self.m = m = Machine(elf)
        self.kind = kind
        self.blocks = [m.alloc(4 + 2 * BLOCK, 4) for _ in range(4)]
        self.sent = {}
        self.a = 0
        m.stub_function(0x600305D4, lambda mm: self.blocks[mm.arg(1)])
        m.stub_function(0x600305BC, lambda mm: self.blocks[0])
        m.stub_function(0x60030478, self._alloc)
        m.stub_function(0x60030574, self._transmit)
        m.stub_function(0x60030524, lambda mm: 0)
        self.obj = 0x20050000
        m.write(self.obj, b"\0" * 0x100)
        mx = m.addr("mfx_state_ram")
        m.w32(mx, 0x3558464D)
        m.w32(mx + 16, 0x59444552)
        if kind == "reverb":
            m.w32(self.obj + 0x24, 0x20030000)
            m.w32(self.obj + 0x28, 0x80240000)
            m.write(mx + 8, bytes([which]))
            self.fn = m.addr("mt_mfx_reverb_update") | 1
        else:
            m.w32(self.obj + 0x14, 0x800C0000)
            m.w32(self.obj + 0x18, 0x80100000)
            m.w32(self.obj + 0x1C, 0x80140000)
            m.write(mx + 13, bytes([which]))
            self.fn = m.addr("mt_mfx_delay_update") | 1

    def _alloc(self, mm):
        self.a += 1
        return self.blocks[1 + self.a % 2]

    def _transmit(self, mm):
        self.sent[mm.arg(2)] = struct.unpack(f"<{BLOCK}h", mm.read(mm.arg(1) + 4, 2 * BLOCK))
        return 0

    def reverb_knobs(self, size, damp, pre, diff, amount=0.8, gain=0.1):
        m, o = self.m, self.obj
        for off, v in ((0x4C, amount), (0x50, gain), (0x54, size), (0x58, diff), (0x5C, 1.0 - damp)):
            m.write(o + off, f32(v))
        m.w32(o + 0x6C, int(pre * 5000))

    def delay_knobs(self, time, fb, pp, hz):
        m, o = self.m, self.obj
        m.w32(o + 0x28, time)
        m.write(o + 0x2C, bytes([int(255 * min(fb, 0.99)), 0, 1 if pp else 0]))
        m.w32(o + 0x38, coeffs_of(hz)[2] & 0xFFFFFFFF)

    def settle(self):
        """1.2.0 cleared a piece per block and sent nothing. The first block it sends is the effect's
        first, on silence."""
        self.sent = {}
        for _ in range(200):
            self.m.call(self.fn, self.obj, max_insns=20_000_000)
            if self.sent:
                return
        raise RuntimeError("1.2.0's effect never started")

    def run(self, inputs):
        a, b = [], []
        for x in inputs:
            data = struct.pack(f"<{BLOCK}h", *x)
            self.m.write(self.blocks[0] + 4, data)
            if self.kind == "reverb":
                self.m.write(self.blocks[1] + 4, data)
            self.sent = {}
            self.m.call(self.fn, self.obj, max_insns=20_000_000)
            a += self.sent.get(0) or []
            b += self.sent.get(1) or []
        return a, b


def noise(seed, n, amp=8000):
    r = random.Random(seed)
    return [[r.randint(-amp, amp) for _ in range(BLOCK)] for _ in range(n)]


def silence(n):
    return [[0] * BLOCK for _ in range(n)]


def impulse(amp=20000):
    x = [0] * BLOCK
    x[0] = amp
    return x


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def first_loud(x, threshold=300):
    for i, v in enumerate(x):
        if abs(v) > threshold:
            return i
    return None


def highs(x):
    """Share of the signal that is high frequency: first difference against the signal."""
    d = [x[i + 1] - x[i] for i in range(len(x) - 1)]
    return rms(d) / max(1.0, rms(x))


def decay_time(x, block=1024):
    levels = [rms(x[i:i + block]) for i in range(0, len(x) - block, block)]
    top = max(levels) if levels else 0
    for i in range(len(levels) - 1, -1, -1):
        if levels[i] > top / 100:  # 40 dB
            return (i + 1) * block / RATE
    return 0.0


# ---- CE's own, when Stock plays ------------------------------------------------------------------

def off_is_ce(mods, ref):
    inputs = noise(1, 60) + [impulse()] + silence(60)
    a, b = ReverbRig(mods), ReverbRig(ref)
    for r in (a, b):
        r.knobs(size=0.7, damp=0.3, pre=0.2, diff=0.5)
    check(a.run(inputs) == b.run(inputs), "Stock reverb: same as the reference build, sample for sample")

    for pp in (False, True):
        a, b = DelayRig(mods), DelayRig(ref)
        for r in (a, b):
            r.knobs(time=2000, fb=0.7, pp=pp, hz=3000.0)
        same = a.run(inputs) == b.run(inputs)
        check(same, f"Stock delay, PingPong {'On' if pp else 'Off'}: same as the reference build")
        check(a.silent_count() == b.silent_count(), "and the same isSilent count")


def only_the_chosen_runs(mods):
    r = ReverbRig(mods)
    r.knobs()
    sym = r.m.sym("_ZN19AudioEffectPolyverb6updateEv")
    n = [0]
    r.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1),
                    begin=sym.addr & ~1, end=(sym.addr & ~1) + sym.size)
    r.block(noise(2, 1)[0])
    stock = n[0]
    r.active("reverb", 3)
    r.reset()
    n[0] = 0
    r.block(noise(2, 1)[0])
    check(stock > 5000 and n[0] < 200,
          f"a chosen reverb: CE's reverb doesn't run ({stock} instructions in its update, then {n[0]})")

    d = DelayRig(mods)
    d.knobs()
    sym = d.m.sym("_ZN21AudioEffectShortDelay7processEPsS0_S0_i")
    n = [0]
    d.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1),
                    begin=sym.addr & ~1, end=(sym.addr & ~1) + sym.size)
    d.block(noise(2, 1)[0])
    stock = n[0]
    d.active("delay", 1)
    d.clear()
    n[0] = 0
    d.block(noise(2, 1)[0])
    check(stock > 3000 and n[0] == 0, f"a chosen delay: CE's delay doesn't run ({stock} instructions, then {n[0]})")


# ---- 1.2.0's sound --------------------------------------------------------------------------------

def same_as_120(mods, rig):
    inputs = noise(3, 40) + [impulse()] + silence(90)
    knob_sets = (dict(size=0.5, damp=0.5, pre=0.5, diff=0.68), dict(size=0.9, damp=0.1, pre=0.0, diff=0.2))
    for which, name in REVERBS.items():
        for k in knob_sets:
            a = ReverbRig(mods)
            a.active("reverb", which)
            a.reset()
            a.knobs(**k)
            a.block(silence(1)[0])
            b = Rig120(rig, "reverb", which)
            b.reverb_knobs(**k)
            b.settle()
            x, y = a.run(inputs), b.run(inputs)
            check(x == y and rms(x[0]) > 1, f"{name}: same as 1.2.0, bit for bit (Size {k['size']}, {len(x[0])} samples)")

    for which, name in DELAYS.items():
        for pp in (False, True):
            k = dict(time=3000, fb=0.6, pp=pp, hz=4000.0)
            a = DelayRig(mods)
            a.active("delay", which)
            a.knobs(**k)
            a.clear()
            a.block(silence(1)[0])
            b = Rig120(rig, "delay", which)
            b.delay_knobs(**k)
            b.settle()
            x, y = a.run(inputs), b.run(inputs)
            check(x == y and rms(x[0]) > 1, f"{name}, PingPong {'On' if pp else 'Off'}: same as 1.2.0, bit for bit")


def pingpong_sides(mods, ref):
    inputs = [impulse()] + silence(40)
    a = DelayRig(ref)
    a.knobs(time=2000, fb=0.5, pp=True)
    stock = a.run(inputs)
    side = 0 if first_loud(stock[0]) is not None and (first_loud(stock[1]) is None or first_loud(stock[0]) < first_loud(stock[1])) else 1
    for which, name in DELAYS.items():
        d = DelayRig(mods)
        d.active("delay", which)
        d.knobs(time=2000, fb=0.5, pp=True)
        d.clear()
        out = d.run(inputs)
        f0, f1 = first_loud(out[0]), first_loud(out[1])
        got = 0 if f0 is not None and (f1 is None or f0 < f1) else 1
        check(got == side, f"{name}, PingPong: the first echo on the side CE's delay uses (channel {side}, got {got})")


# ---- the knobs and the block handling --------------------------------------------------------------

def reverb_checks(mods, which, name):
    def make(**k):
        r = ReverbRig(mods)
        r.active("reverb", which)
        r.reset()
        r.knobs(**k)
        return r

    tone = noise(5, 16)
    tail = silence(700)

    r = make(amount=0.0)
    x = noise(6, 4)
    l, _ = r.run(x)
    check(l == [v for b in x for v in b], f"{name}: amount 0, the send comes back unchanged")

    gated = which in (5, 6)
    small = make(size=0.2)
    big = make(size=0.9)
    ls, _ = small.run(tone + tail)
    lb, rb = big.run(tone + tail)
    check(decay_time(lb) > decay_time(ls), f"{name}: a larger Size rings longer ({decay_time(ls):.2f} -> {decay_time(lb):.2f} s)")
    check(lb != rb, f"{name}: the two channels differ")
    check(max(map(abs, lb)) < 32767, f"{name}: stays below full scale")

    a = make(size=0.8, damp=0.0)
    b = make(size=0.8, damp=1.0)
    la, _ = a.run(tone + silence(120))
    ld, _ = b.run(tone + silence(120))
    start = BLOCK * 50  # past the first pass round the longest lines
    check(highs(ld[start:]) < highs(la[start:]), f"{name}: more Damping, a duller tail ({highs(la[start:]):.3f} -> {highs(ld[start:]):.3f})")

    p0 = make(size=0.5, amount=1.0, pre=0.0)
    p1 = make(size=0.5, amount=1.0, pre=0.5)
    i = [impulse()] + silence(50)
    on0, on1 = first_loud(p0.run(i)[0], 30), first_loud(p1.run(i)[0], 30)
    check(on0 is not None and on1 is not None and 2200 <= on1 - on0 <= 2800,
          f"{name}: Predelay 0.5 moves the reverb by about 2,500 samples ({on1} against {on0})")

    d0 = make(diff=0.0)
    d1 = make(diff=0.85)
    check(d0.run(tone + silence(30)) != d1.run(tone + silence(30)), f"{name}: Diffusion changes it")

    s = make(size=1.0, damp=0.0, diff=0.85)
    loud = noise(7, 150, amp=30000)
    l, _ = s.run(loud)
    q, _ = s.run(silence(400 if not gated else 60))
    check(max(map(abs, q[-BLOCK:])) < 32767, f"{name}: top knobs and loud noise, still in range")

    q = make()
    q.input_missing[0] = True
    x = q.block(noise(8, 1)[0])
    check(x == (None, None) and q.released == [], f"{name}: no left block: nothing sent")
    q = make()
    q.input_missing[1] = True
    x = q.block(noise(8, 1)[0])
    check(x == (None, None) and q.released == [q.blocks[0]], f"{name}: no right block: nothing sent, the left released")

    r1 = make(size=0.5)
    r2 = make(size=0.5)
    y = noise(9, 20)
    r2.run(y)
    r2.reset()
    check(r1.run(y) == r2.run(y), f"{name}: after CE's reset it plays as from new")

    r = make()
    z = r.run(silence(20))
    check(max(map(abs, z[0])) == 0, f"{name}: silence in, silence out")


def delay_checks(mods, which, name):
    def make(**k):
        d = DelayRig(mods)
        d.active("delay", which)
        d.knobs(**k)
        d.clear()
        return d

    T = 4410
    d = make(time=T, fb=0.5)
    a, b = d.run([impulse()] + silence(120))
    f = first_loud(a)
    check(f is not None and abs(f - T) <= 300, f"{name}: the first echo at Time ({f}, Time {T})")
    check(a == b, f"{name}: PingPong Off, the same on both sides")

    d = make(time=T, fb=0.0)
    a, _ = d.run([impulse()] + silence(120))
    late = max(map(abs, a[T + 2000:]))
    check(late < 300, f"{name}: Feedback 0, one echo (peak after it {late})")

    d = make(time=T, fb=0.99)
    a, _ = d.run(noise(10, 300, amp=30000))
    check(max(map(abs, a)) <= 32767 and rms(a[-BLOCK * 8:]) > 100, f"{name}: Feedback at the top stays in range and alive")

    def echo(hz):
        d = make(time=T, fb=0.0, hz=hz)
        a, _ = d.run(noise(11, 8) + silence(50))
        return a[T:T + BLOCK * 8]
    bright, dark = highs(echo(20000.0)), highs(echo(1000.0))
    check(dark < bright * 0.8, f"{name}: Filter 1 kHz, duller echoes ({bright:.3f} -> {dark:.3f})")

    d = make(time=T, fb=0.5)
    d.block(impulse())
    d.input_missing[0] = True
    a, _ = d.run(silence(60))
    check(max(map(abs, a)) > 300, f"{name}: no send block: the echoes carry on")
    check(sorted(d.released) == sorted(d.handed), f"{name}: every block released")

    d = make(time=T, fb=0.5)
    d.alloc_missing = 1
    x = d.block(noise(12, 1)[0])
    check(x == (None, None) and sorted(d.released) == sorted(d.handed), f"{name}: an output block missing: nothing sent, every block released")

    d = make(time=T, fb=0.5)
    d.m.write(OBJECT + DL_NOREFRESH, b"\1")
    x = d.block(noise(12, 1)[0])
    check(x == (None, None) and sorted(d.released) == sorted(d.handed), f"{name}: blocked: nothing sent, every block released")
    d.m.write(OBJECT + DL_NOREFRESH, b"\0")

    # isSilent, which an export's tail reads
    d = make(time=T, fb=0.3)
    check(d.silent_count() >= T, f"{name}: after a clear, isSilent")
    d.block(noise(13, 1)[0])
    check(d.silent_count() == 0, f"{name}: with a send, not silent")
    a, _ = d.run(silence(600))
    check(d.silent_count() >= T and max(map(abs, a[-BLOCK:])) == 0, f"{name}: when the tail has died, silent (count {d.silent_count()})")

    x = noise(14, 20)
    r1 = make(time=T, fb=0.4)
    r2 = make(time=T, fb=0.4)
    r2.run(x)
    r2.clear()
    check(r1.run(x) == r2.run(x), f"{name}: after CE's clear it plays as from new")


def switching(mods):
    r = ReverbRig(mods)
    r.knobs(size=0.8)
    r.block(noise(15, 1)[0])
    fresh = ReverbRig(mods)
    fresh.knobs(size=0.8)
    y = noise(16, 30)
    # Stock -> Spring -> Stock: the buffers and CE's state are clean again
    r.active("reverb", 3)
    r.reset()
    r.run(noise(17, 20))
    r.active("reverb", 0)
    r.reset()
    a = r.run(y)[0]
    b = fresh.run(y)[0]
    check(rms(a) > 1 and len(a) == len(b), "Stock -> Spring -> Stock: CE's reverb sounds again")

    # every pair of reverbs
    outs = {}
    x = noise(18, 16) + silence(30)
    for which, name in REVERBS.items():
        q = ReverbRig(mods)
        q.active("reverb", which)
        q.reset()
        q.knobs()
        outs[which] = q.run(x)[0]
    names = list(outs)
    different = all(outs[names[i]] != outs[names[j]] for i in range(len(names)) for j in range(i + 1, len(names)))
    check(different, "the 6 reverbs differ")

    # going from one to another leaves nothing of the first
    for a_, b_ in ((1, 3), (3, 4), (4, 2), (2, 5), (5, 1)):
        q = ReverbRig(mods)
        q.active("reverb", a_)
        q.reset()
        q.knobs()
        q.run(noise(19, 20))
        q.active("reverb", b_)
        q.reset()
        o1 = q.run(x)[0]
        check(o1 == outs[b_], f"{REVERBS[a_]} -> {REVERBS[b_]}: as from new, nothing left of the first")

    d = DelayRig(mods)
    d.knobs(time=2000, fb=0.8)
    d.active("delay", 1)
    d.clear()
    d.run(noise(20, 40))
    d.active("delay", 0)
    d.clear()
    x = [impulse()] + silence(40)
    f = DelayRig(mods)
    f.knobs(time=2000, fb=0.8)
    check(d.run(x) == f.run(x), "Tape -> Stock: CE's delay as from new")

    d.active("delay", 1)
    d.clear()
    d.run(noise(21, 40))
    d.active("delay", 2)
    d.clear()
    f = DelayRig(mods)
    f.knobs(time=2000, fb=0.8)
    f.active("delay", 2)
    f.clear()
    check(d.run(x) == f.run(x), "Tape -> BBD: as from new")


def placement(mods):
    m = Machine(mods)
    names = {n: s for n, s in m.elf.symbols.items() if "masterfx" in n and not n.endswith("_veneer")}

    def find(part):
        return [s for n, s in names.items() if part in n]

    def in_itcm(part):
        hits = find(part)
        return bool(hits) and all(s.addr < 0x20000 for s in hits)

    def in_dtcm(part):
        hits = find(part)
        return bool(hits) and all(0x20000000 <= s.addr < 0x20070000 for s in hits)

    for f in ("reverbProcess", "delayProcess", "shimmer_process", "bbd_process"):
        check(in_itcm(f), f"{f} is in ITCM")
    for f in ("fv_process", "plate_process", "spring_process", "tape_process", "gated_process"):
        hits = find(f)
        check(hits and all(s.addr >= 0x60000000 for s in hits), f"{f} is in flash")
    for s in ("activeReverb", "activeDelay", "fv_state_ram", "tape_state_ram", "bbd_state_ram", "shimmer_state_ram",
              "plate_state_ram", "spring_state_ram", "gated_state_ram", "gated_cache_ram"):
        check(in_dtcm(s), f"{s} is in DTCM")
    # Spring's floats need the first buffer word-aligned
    sym = m.elf.symbols.get("_ZL21internalRamReverbBuff")
    check(sym is not None and sym.addr % 4 == 0, "the reverb's first buffer is word-aligned")


def main():
    mods, ref = Path(sys.argv[1]), Path(sys.argv[2])
    rig = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    only = sys.argv[4] if len(sys.argv) > 4 else ""

    def run(name, fn, *args):
        if not only or only in name:
            fn(*args)

    run("placement", placement, mods)
    run("off", off_is_ce, mods, ref)
    run("only", only_the_chosen_runs, mods)
    if rig is not None:
        run("same", same_as_120, mods, rig)
    else:
        print("SKIP same as 1.2.0: no rig given")
    run("sides", pingpong_sides, mods, ref)
    for which, name in REVERBS.items():
        run("reverb", reverb_checks, mods, which, name)
    for which, name in DELAYS.items():
        run("delay", delay_checks, mods, which, name)
    run("switch", switching, mods)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
