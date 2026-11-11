# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Synth FX plugins' runtime (core/mods/synthfx/synthfxPlugins.cpp) on the emulated
Cortex-M7, built as the firmware builds it (tools/synthfx/rig/build.sh: the runtime, the
firmware's copy of the default pack, a card in memory). The host table, the clock and the
cache sync are stubs here; the test plugin Probe.ptsfx (tools/synthfx/rig/probe.c) reports
what the host did to it and misbehaves on request.

1. The start: the folder, the pack written onto an empty card as built (build/synthfx/pack),
   a file already there left alone, the catalog.
2. Through the runtime, each pack plugin gives what its own entries give called directly (and
   tools/synthfx/test_pack.py has those bit for bit against the built-in effects): default
   knobs, knobs moved, mono, stereo, a widening plugin on a mono part. Two instances of one
   plugin share its code and keep their own state.
3. Knobs: every knob set after init, then only those that change; on their steps; stepped;
   their texts, the plugin's and the host's.
4. Loading: the code 32-byte aligned and its cache synced once per load; state zeroed; the
   code pool given back with the last instance; holes reused; the limits (plugins, instances,
   code and state room); a file changed since the list refused.
5. The guards: NaN and infinite output to 0 and a reset; output held at +-4; over the time
   budget 8 blocks in a row bypasses (7 doesn't, a block under starts the count again), told
   once; the plugin's own work measured (instructions as cycles).
6. The start marker: written with the first plugin, removed after 5 s; found at start, a
   project's plugins are skipped until the user picks one and it settles.
7. Cost: the runtime's own instructions per block around a plugin.

Planted faults (tools/synthfx/rig/plant.py) build the rig with one mistake each and expect this
test to fail.

Usage: python tools/synthfx/test_runtime.py [build/emu/synthfx_rig.axf] [--quick]
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "emu"))
from machine import Machine  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_S0  # noqa: E402

B = 128
HEADER = 476
NO_ENTRY = 0xFFFFFFFF
PACK_DIR = ROOT / "build" / "synthfx" / "pack"
PROBE = ROOT / "build" / "synthfx" / "rig" / "Probe.ptsfx"
PACK = ["Overdrive.ptsfx", "Distortion.ptsfx", "BitCrusher.ptsfx", "Chorus.ptsfx", "Flanger.ptsfx", "Phaser.ptsfx"]
WIDENS = {"Chorus.ptsfx", "Flanger.ptsfx", "Phaser.ptsfx"}
STATUS = ["ok", "missing", "badFile", "tooBig", "noCodeRoom", "noStateRoom", "tooMany", "skipped", "readFailed"]
CODE_POOL, STATE_POOL = 96 * 1024, 192 * 1024
SETTLE_MS = 5000
DIRECT_AT = 0x83800000          # the direct calls' copy of a plugin, clear of the rig's memory
CARD_FILE = 72 + 12             # rig.cpp's CardFile
CARD_FILES = 96

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def f32(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", v))[0]


def from32(b: int) -> float:
    return struct.unpack("<f", struct.pack("<I", b & 0xFFFFFFFF))[0]


def on_step(lo: float, step: float, i: int) -> float:
    """A knob's value as the host works it out: fmaf(i, step, lo), one rounding"""
    return from32(f32(from32(f32(lo)) + i * from32(f32(step))))


def rnd(seed: int, n: int, amp: float) -> np.ndarray:
    return (np.random.default_rng(seed).uniform(-amp, amp, n)).astype("<f4").astype(np.float64)


class Plugin:
    def __init__(self, data: bytes):
        self.data = data
        self.flags = struct.unpack_from("<H", data, 6)[0]
        self.nparams = data[56]
        (self.state_size, self.code_offset, self.code_size, *self.entries) = struct.unpack_from("<9I", data, 60)
        self.knobs = []
        for i in range(self.nparams):
            o = 92 + 32 * i
            lo, hi, de, st = struct.unpack_from("<ffff", data, o + 12)
            self.knobs.append((lo, hi, de, st))
        self.code = data[self.code_offset:self.code_offset + self.code_size]


class Rig:
    def __init__(self, axf: Path):
        self.m = m = Machine(axf)
        self.fn: dict[str, int] = {}
        self.ms = 0
        self.spend = 0                  # cycles each process() takes, by the stub clock
        self.clock = 0
        self.clock_phase = 0
        self.count_insns = False
        self.insns = 0
        self.syncs: list[tuple[int, int]] = []
        self.host_calls: list[str] = []

        def sync(mm):
            self.syncs.append((mm.arg(0), mm.arg(1)))

        def cycles(mm):
            if self.count_insns:
                return self.insns
            if self.clock_phase:
                self.clock += self.spend
            self.clock_phase ^= 1
            return self.clock

        def millis(mm):
            return self.ms

        def print_stub(mm):
            out, size, fmt = mm.arg(0), mm.arg(1), mm.cstr(mm.arg(2))
            v = from32(mm.uc.reg_read(UC_ARM_REG_S0))
            text = (fmt % v).encode()[:max(size - 1, 0)]
            mm.write(out, text + b"\0")
            return len(text)

        def unused(name):
            def f(mm):
                self.host_calls.append(name)
                return 0
            return f
        fns = [m.add_stub(unused(n)) for n in ("sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "sqrtf",
                                                 "fabsf", "fminf", "fmaxf", "random")]
        fns.append(m.add_stub(print_stub))
        self.host = m.alloc(4 * (3 + len(fns)))
        m.write(self.host, struct.pack("<IfI", 1, 44100.0, 128) + struct.pack(f"<{len(fns)}I", *fns))
        self.hooks = m.addr("rig_hooks")
        self.stubs = (m.add_stub(sync), m.add_stub(cycles), m.add_stub(millis))
        self.L = m.alloc(4 * B)
        self.R = m.alloc(4 * B)
        self.text = m.alloc(64)
        self.name = m.alloc(80)
        self.budget = 400_000

    def call(self, name: str, *args: int) -> int:
        if name not in self.fn:
            self.fn[name] = self.m.addr(name)
        return self.m.call(self.fn[name], *args, max_insns=400_000_000)

    def s32(self, name: str, *args: int) -> int:
        v = self.call(name, *args)
        return v - (1 << 32) if v & 0x80000000 else v

    # ---- the card --------------------------------------------------------------------
    def fresh(self, files: dict[str, bytes] | None = None, budget: int | None = None) -> None:
        """Power-up: nothing started, the card holding files (paths), garbage in the pools"""
        m = self.m
        self.call("rig_forget")
        for pool in ("rig_code_pool", "rig_state_pool"):
            a = m.addr(pool)
            n = CODE_POOL if pool == "rig_code_pool" else STATE_POOL
            m.write(a, (bytes(range(256)) * (n // 256))[:n])   # SDRAM isn't zeroed at startup
        for path, data in (files or {}).items():
            self.put_file(path, data)
        self.budget = budget if budget is not None else 400_000
        m.write(self.hooks, struct.pack("<5I", *self.stubs, self.host, self.budget))
        self.syncs.clear()

    def card_used(self) -> int:
        return self.m.r32(self.m.addr("rig_card_used"))

    def put_file(self, path: str, data: bytes) -> None:
        m = self.m
        files = m.addr("rig_files")
        for i in range(CARD_FILES):
            e = files + CARD_FILE * i
            if m.r32(e + 80) == 0 or m.cstr(e, 72) == path:
                at = self.card_used()
                m.write(m.addr("rig_card") + at, data)
                m.write(e, path.encode() + b"\0")
                m.write(e + 72, struct.pack("<3I", at, len(data), 1))
                m.w32(m.addr("rig_card_used"), at + ((len(data) + 3) & ~3))
                return
        raise RuntimeError("the rig's card is full")

    def file(self, path: str) -> bytes | None:
        m = self.m
        files = m.addr("rig_files")
        for i in range(CARD_FILES):
            e = files + CARD_FILE * i
            if m.r32(e + 80) and m.cstr(e, 72) == path:
                at, size = struct.unpack("<2I", m.read(e + 72, 8))
                return m.read(m.addr("rig_card") + at, size)
        return None

    def drop_file(self, path: str) -> None:
        m = self.m
        files = m.addr("rig_files")
        for i in range(CARD_FILES):
            e = files + CARD_FILE * i
            if m.r32(e + 80) and m.cstr(e, 72) == path:
                m.w32(e + 80, 0)

    def counter(self, name: str) -> int:
        return self.m.r32(self.m.addr(name))

    # ---- the runtime -----------------------------------------------------------------
    def start(self) -> None:
        self.call("rig_start")

    def catalog(self) -> list[tuple[str, str, str]]:
        out = []
        for i in range(self.call("rig_catalog_count")):
            e = self.call("rig_catalog_at", i)
            out.append((self.m.cstr(e, 48), self.m.cstr(e + 48, 32), STATUS[self.m.r8(e + 48 + 32 + 7)]))
        return out

    def create(self, file: str, project: bool = False) -> tuple[int, str]:
        self.m.write(self.name, file.encode() + b"\0")
        i = self.s32("rig_create", self.name, 1 if project else 0)
        return i, STATUS[self.counter("rig_status")]

    def set_knob(self, i: int, k: int, v: float) -> None:
        self.call("rig_set_knob", i, k, f32(v))

    def knob(self, i: int, k: int) -> float:
        return from32(self.call("rig_knob", i, k))

    def knob_text(self, i: int, k: int) -> str:
        self.m.write(self.text, b"\xff" * 64)
        self.call("rig_knob_text", i, k, self.text, 24)
        return self.m.cstr(self.text, 64)

    def probe(self, i: int, in_ch: int = 1, r: bool = False, x: np.ndarray | None = None):
        """A block through the probe in its Probe mode: its reports (x 64), and R"""
        ok, lo, ro = self.process(i, np.zeros(B) if x is None else x, None, in_ch, r)
        return ok, lo[:10] * 64.0, ro

    def process(self, i: int, x: np.ndarray, xr: np.ndarray | None, in_ch: int, r: bool) -> tuple[int, np.ndarray, np.ndarray | None]:
        self.m.write(self.L, np.asarray(x, dtype="<f4").tobytes())
        if xr is not None:
            self.m.write(self.R, np.asarray(xr, dtype="<f4").tobytes())
        ok = self.call("rig_process", i, self.L, self.R if r else 0, in_ch)
        out_r = np.frombuffer(self.m.read(self.R, 4 * B), dtype="<f4").astype(np.float64) if r else None
        return ok, np.frombuffer(self.m.read(self.L, 4 * B), dtype="<f4").astype(np.float64), out_r

    def blocks(self, i: int, x: np.ndarray, xr: np.ndarray | None, in_ch: int, r: bool):
        ls, rs = [], []
        for b in range(len(x) // B):
            _, lo, ro = self.process(i, x[b * B:(b + 1) * B], None if xr is None else xr[b * B:(b + 1) * B], in_ch, r)
            ls.append(lo)
            if ro is not None:
                rs.append(ro)
        return np.concatenate(ls), (np.concatenate(rs) if rs else None)

    # ---- a plugin called directly, as tools/synthfx/test_pack.py does ---------------------
    def direct(self, p: Plugin, knob_steps: list[list[float]], x, xr, in_ch: int, r: bool):
        """A fresh instance at DIRECT_AT: init, every knob set, 4 blocks per knob setting"""
        m = self.m
        m.write(DIRECT_AT, p.code)
        m.uc.ctl_flush_tb()
        e = [(DIRECT_AT + o) | 1 for o in p.entries[:4]]
        st = DIRECT_AT + 0x10000
        m.write(st, b"\0" * p.state_size)
        m.call(e[0], st, self.host)
        L, R = DIRECT_AT + 0x20000, DIRECT_AT + 0x21000
        ls, rs = [], []
        for s, knobs in enumerate(knob_steps):
            for k, v in enumerate(knobs):
                m.uc.reg_write(UC_ARM_REG_S0, f32(v))
                m.call(e[1], st, k)
            for b in range(4 * s, 4 * s + 4):
                m.write(L, np.asarray(x[b * B:(b + 1) * B], dtype="<f4").tobytes())
                if xr is not None:
                    m.write(R, np.asarray(xr[b * B:(b + 1) * B], dtype="<f4").tobytes())
                m.call(e[2], st, L, R if r else 0, B, in_ch, max_insns=400_000_000)
                ls.append(np.frombuffer(m.read(L, 4 * B), dtype="<f4").astype(np.float64))
                if r:
                    rs.append(np.frombuffer(m.read(R, 4 * B), dtype="<f4").astype(np.float64))
        m.uc.ctl_flush_tb()
        return np.concatenate(ls), (np.concatenate(rs) if r else None)


def pack_files() -> dict[str, bytes]:
    return {f: (PACK_DIR / f).read_bytes() for f in PACK}


# ---- 1. the start ---------------------------------------------------------------------------

def start(rig: Rig) -> None:
    rig.fresh()
    rig.start()
    check(rig.counter("rig_dirs") >= 1, "start: the folder made")
    files = pack_files()
    same = [f for f in PACK if rig.file(f"/SynthFX/{f}") == files[f]]
    check(len(same) == 6 and rig.counter("rig_writes") == 6,
          f"start: the pack written onto an empty card, each file as built ({len(same)} of 6)")
    cat = rig.catalog()
    check([c[0] for c in cat] == PACK and all(c[2] == "ok" for c in cat) and
          [c[1] for c in cat] == ["Overdrive", "Distortion", "Bit Crusher", "Chorus", "Flanger", "Phaser"],
          f"start: the catalog, the pack in its order {cat if len(cat) != 6 else ''}")
    check(not rig.call("rig_safe_mode") and rig.file("/SynthFX/.starting") is None, "start: no marker, no safe mode")

    mine = bytearray(files["Chorus.ptsfx"])
    mine[8:20] = b"My Chorus\0\0\0"
    rig.fresh({"/SynthFX/Chorus.ptsfx": bytes(mine), "/SynthFX/Probe.ptsfx": PROBE.read_bytes()})
    rig.start()
    check(rig.file("/SynthFX/Chorus.ptsfx") == bytes(mine) and rig.counter("rig_writes") == 5,
          "start: a file already there left alone, the rest written")
    cat = rig.catalog()
    check(cat[3][1] == "My Chorus" and cat[6] == ("Probe.ptsfx", "Probe", "ok"),
          "start: the catalog has the card's own name, and the user's plugin after the pack")
    rig.call("rig_start")
    check(rig.counter("rig_writes") == 5, "start again: only the list")


# ---- 2. the pack through the runtime ---------------------------------------------------------

def pack_through(rig: Rig, quick: bool) -> None:
    files = pack_files()
    rig.fresh({"/SynthFX/Probe.ptsfx": PROBE.read_bytes()})
    rig.start()
    x = rnd(1, 12 * B, 0.8)
    xr = rnd(2, 12 * B, 0.6)
    moves = {
        3: ([37.0, 60.0, -3.0], [63.0, 40.0, 4.5]),
        4: ([70.0, 80.0, 60.0, 50.0], [100.0, 10.0, -90.0, 100.0]),
    }
    cases = [(False, 1, False), (True, 2, True)] + ([] if quick else [(False, 1, True)])
    for f in PACK:
        p = Plugin(files[f])
        defaults = [k[2] for k in p.knobs]
        k1, k2 = moves[4] if p.nparams == 4 else moves[3]
        if f == "Chorus.ptsfx":
            k1, k2 = [70.0, 80.0, 50.0], [100.0, 10.0, 100.0]
        bad = []
        for stereo, in_ch, r in cases:
            if f in WIDENS and not r:
                r = True                                    # a widening plugin is always given R
            want = rig.direct(p, [defaults, k1, k2], x, xr if stereo else None, in_ch, r)
            i, s = rig.create(f)
            if i < 0:
                bad.append(f"create {s}")
                continue
            got_l, got_r = [], []
            for step, knobs in enumerate([None, k1, k2]):
                if knobs:
                    for k, v in enumerate(knobs):
                        rig.set_knob(i, k, v)
                seg = slice(4 * B * step, 4 * B * (step + 1))
                lo, ro = rig.blocks(i, x[seg], xr[seg] if stereo else None, in_ch, r)
                got_l.append(lo)
                if ro is not None:
                    got_r.append(ro)
            rig.call("rig_destroy", i)
            gl = np.concatenate(got_l)
            if not np.array_equal(gl, want[0]) or (r and not np.array_equal(np.concatenate(got_r), want[1])):
                bad.append(f"{'stereo' if stereo else 'mono'}{' widened' if r and not stereo else ''}")
        check(not bad, f"{f}: through the runtime as called directly, knobs moving {bad or ''}")
    check(not rig.host_calls, f"the pack calls none of the host's functions in its blocks {sorted(set(rig.host_calls))}")

    # two instances of one plugin: one code, two states
    p = Plugin(files["Flanger.ptsfx"])
    a, _ = rig.create("Flanger.ptsfx")
    b, _ = rig.create("Flanger.ptsfx")
    check(rig.call("rig_loaded") == 1 and rig.call("rig_instances") == 2, "two instances: the code loaded once")
    ka, kb = [80.0, 30.0, -60.0, 70.0], [20.0, 90.0, 40.0, 100.0]
    for k in range(4):
        rig.set_knob(a, k, ka[k])
        rig.set_knob(b, k, kb[k])
    wa = rig.direct(p, [ka], x, xr, 2, True)
    wb = rig.direct(p, [kb], xr, x, 2, True)
    la, ra, lb, rb = [], [], [], []
    for n in range(4):
        s = slice(n * B, (n + 1) * B)
        _, l1, r1 = rig.process(a, x[s], xr[s], 2, True)
        _, l2, r2 = rig.process(b, xr[s], x[s], 2, True)
        la.append(l1), ra.append(r1), lb.append(l2), rb.append(r2)
    ok = all(np.array_equal(np.concatenate(g), w) for g, w in ((la, wa[0]), (ra, wa[1]), (lb, wb[0]), (rb, wb[1])))
    check(ok, "two instances, interleaved: each its own state and knobs")
    rig.call("rig_destroy", a)
    check(rig.call("rig_loaded") == 1, "one of two destroyed: the code stays")
    rig.call("rig_destroy", b)
    check(rig.call("rig_loaded") == 0 and rig.call("rig_instances") == 0, "the last destroyed: the code goes")


# ---- 3. knobs, through the probe -------------------------------------------------------------

def knobs(rig: Rig) -> None:
    rig.fresh({"/SynthFX/Probe.ptsfx": PROBE.read_bytes()})
    rig.start()
    i, s = rig.create("Probe.ptsfx")
    check(i >= 0, f"probe: made ({s})")
    z = np.zeros(B)
    ok, lo, _ = rig.probe(i)
    fine0 = on_step(-1.0, 0.1, 11)
    check(ok == 1 and list(lo[:4]) == [25.0, fine0, 0.0, 0.0],
          f"probe: every knob at its default before the first block {list(lo[:4])}")
    check(lo[4] == 4 and lo[5] == 1 and lo[6] == 0 and lo[7] == 1 and lo[8] == 1 and lo[9] == 1,
          f"probe: four set_params, one init, no reset, mono with R null {list(lo[4:10])}")
    _, lo, _ = rig.probe(i)
    check(lo[4] == 4 and lo[9] == 2, "probe: no set_param for knobs that didn't move")
    rig.set_knob(i, 0, 60.4)
    _, lo, _ = rig.probe(i)
    check(lo[0] == 60.0 and lo[4] == 5, f"probe: a moved knob, on its step, and only it {lo[0]}, {lo[4]}")
    rig.set_knob(i, 0, 61.0)
    rig.set_knob(i, 0, 62.0)
    _, lo, _ = rig.probe(i)
    check(lo[0] == 62.0 and lo[4] == 6, "probe: moved twice between blocks: one set_param, the last value")
    rig.call("rig_step_knob", i, 1, 3)
    check(rig.knob(i, 1) == on_step(-1.0, 0.1, 14), f"step: three steps up ({rig.knob(i, 1)})")
    rig.call("rig_step_knob", i, 1, 1000)
    check(rig.knob(i, 1) == on_step(-1.0, 0.1, 20), "step: held at max")
    rig.call("rig_step_knob", i, 1, (-1000) & 0xFFFFFFFF)
    check(rig.knob(i, 1) == -1.0, "step: held at min")
    for v, want in ((-50.0, 0.0), (1e9, 100.0), (float("nan"), 0.0), (99.6, 100.0), (0.49, 0.0)):
        rig.set_knob(i, 0, v)
        if rig.knob(i, 0) != want:
            check(False, f"set: {v} gives {rig.knob(i, 0)}, not {want}")
            break
    else:
        check(True, "set: out of range held at the ends, NaN at min, between steps the nearest")
    rig.set_knob(i, 0, 60)
    rig.set_knob(i, 1, 0.4)
    rig.set_knob(i, 3, 7)
    texts = [rig.knob_text(i, k) for k in range(4)]
    check(texts == ["60 %", "0.4 st", "Probe", "7"],
          f"texts: the host's for 'd' and 'f' with the unit, the plugin's format() for its list {texts}")
    rig.set_knob(i, 2, 3)
    check(rig.knob_text(i, 2) == "Loud", "texts: format() follows the value")
    rig.set_knob(i, 2, 0)
    rig.set_knob(i, 3, 0)
    check(rig.knob_text(i, 9) == "" and rig.knob(i, 9) == 0.0 and rig.knob_text(55, 0) == "",
          "a knob or instance that isn't: nothing")

    # reset, both ways
    rig.call("rig_reset_soon", i)
    _, lo, _ = rig.probe(i)
    check(lo[6] == 1, "reset soon: before the next block")
    _, lo, _ = rig.probe(i)
    check(lo[6] == 1, "reset soon: once")
    rig.call("rig_reset", i)
    _, lo, _ = rig.probe(i)
    check(lo[6] == 2, "reset from the interrupt: at once")
    # widening: R given for output only
    x = rnd(3, B, 0.5)
    _, lo, ro = rig.probe(i, 1, True, x)
    check(lo[7] == 1 and lo[8] == 0 and np.array_equal(ro, x),
          "widening: R given, in_channels 1, R written from L")
    _, lo, ro = rig.process(i, x, rnd(4, B, 0.5), 2, True)
    check(lo[7] * 64 == 2 and lo[8] == 0, "stereo: in_channels 2")
    rig.call("rig_destroy", i)
    ok, _, _ = rig.process(i, z, None, 1, False)
    check(ok == 0 and not rig.call("rig_valid", i), "destroyed: process does nothing")
    check(rig.call("rig_process", 56, rig.L, 0, 1) == 0 and rig.call("rig_process", 0xFFFFFFFF, rig.L, 0, 1) == 0,
          "process: an id out of range does nothing")


# ---- 4. loading --------------------------------------------------------------------------------

def padded(data: bytes, extra: int) -> bytes:
    """A plugin with extra bytes of code (zeros past its end): still valid"""
    h = bytearray(data)
    size = struct.unpack_from("<I", h, 68)[0]
    struct.pack_into("<I", h, 68, size + extra)
    return bytes(h) + b"\0" * extra


def loading(rig: Rig) -> None:
    probe = PROBE.read_bytes()
    files = {"/SynthFX/Probe.ptsfx": probe}
    for n in range(1, 9):
        files[f"/SynthFX/P{n}.ptsfx"] = probe
    files["/SynthFX/Big1.ptsfx"] = padded(probe, 30 * 1024 - len(probe) + HEADER)
    files["/SynthFX/Big2.ptsfx"] = files["/SynthFX/Big1.ptsfx"]
    files["/SynthFX/Big3.ptsfx"] = files["/SynthFX/Big1.ptsfx"]
    files["/SynthFX/Big4.ptsfx"] = files["/SynthFX/Big1.ptsfx"]
    rig.fresh(files)
    rig.start()
    pool = rig.m.addr("rig_code_pool")

    i, _ = rig.create("Chorus.ptsfx")
    code = Plugin(pack_files()["Chorus.ptsfx"]).code_size
    check(len(rig.syncs) == 1 and rig.syncs[0][1] == code and rig.syncs[0][0] % 32 == 0 and
          pool <= rig.syncs[0][0] < pool + CODE_POOL,
          f"load: the code in the pool, 32-byte aligned, its cache synced {[(hex(a), n) for a, n in rig.syncs]}")
    j, _ = rig.create("Chorus.ptsfx")
    check(len(rig.syncs) == 1, "load: a second instance syncs nothing")
    rig.call("rig_destroy", i)
    rig.call("rig_destroy", j)

    # state zeroed: the probe counts its inits in its state, over garbage and over a used block
    a, _ = rig.create("Probe.ptsfx")
    _, lo, _ = rig.probe(a)
    first = lo[5]
    rig.call("rig_destroy", a)
    a, _ = rig.create("Probe.ptsfx")
    _, lo, _ = rig.probe(a)
    check(first == 1 and lo[5] == 1 and lo[9] == 1, f"load: state zeroed, over garbage and over a used block ({first}, {lo[5]})")
    rig.call("rig_destroy", a)

    # eight plugins at once, not nine
    ids = [rig.create(f"P{n}.ptsfx")[0] for n in range(1, 9)]
    nine = rig.create("Probe.ptsfx")
    check(all(x >= 0 for x in ids) and nine == (-1, "tooMany") and rig.call("rig_loaded") == 8,
          f"limits: eight plugins loaded at once, a ninth refused ({nine[1]})")
    again = rig.create("P3.ptsfx")
    check(again[0] >= 0 and rig.call("rig_loaded") == 8, "limits: another instance of a loaded one is fine")
    for x in ids + [again[0]]:
        rig.call("rig_destroy", x)

    # code room: 30 KB each, three fit in 96 KB, not four; the room comes back
    bigs = [rig.create(f"Big{n}.ptsfx") for n in range(1, 5)]
    check([b[1] for b in bigs] == ["ok", "ok", "ok", "noCodeRoom"] and rig.call("rig_loaded") == 3,
          f"limits: the code pool full ({[b[1] for b in bigs]})")
    rig.call("rig_destroy", bigs[1][0])
    late = rig.create("Big4.ptsfx")
    check(late[0] >= 0, f"limits: the hole reused ({late[1]})")
    for b in (bigs[0], bigs[2], late):
        rig.call("rig_destroy", b[0])

    # state room: Chorus has 4,528 bytes (4,544 in the pool): 43 in 192 KB
    ids = []
    while True:
        x, s = rig.create("Chorus.ptsfx")
        if x < 0:
            break
        ids.append(x)
    check(len(ids) == STATE_POOL // 4544 and s == "noStateRoom", f"limits: the state pool full at {len(ids)} ({s})")
    rig.call("rig_destroy", ids[20])
    x, s = rig.create("Chorus.ptsfx")
    check(x == ids[20], "limits: a freed state reused, the same id")
    for x in ids:
        rig.call("rig_destroy", x)
    check(rig.call("rig_loaded") == 0, "limits: all given back")
    ids = []
    while True:
        x, s = rig.create("Overdrive.ptsfx")
        if x < 0:
            break
        ids.append(x)
    check(len(ids) == 56 and s == "tooMany", f"limits: 56 instances, not 57 ({len(ids)}, {s})")
    for x in ids:
        rig.call("rig_destroy", x)

    # a file changed since the list, a file gone
    rig.put_file("/SynthFX/P1.ptsfx", probe + b"\0")
    check(rig.create("P1.ptsfx") == (-1, "badFile"), "load: a file changed since the list refused")
    rig.drop_file("/SynthFX/P2.ptsfx")
    check(rig.create("P2.ptsfx") == (-1, "readFailed"), "load: a file gone since the list")
    check(rig.create("Nope.ptsfx") == (-1, "missing"), "load: a file not listed")
    check(rig.call("rig_loaded") == 0 and rig.call("rig_instances") == 0, "load: nothing left behind")


# ---- 5. the guards -------------------------------------------------------------------------------

def guards(rig: Rig) -> None:
    rig.fresh({"/SynthFX/Probe.ptsfx": PROBE.read_bytes()}, budget=1000)
    rig.start()
    i, _ = rig.create("Probe.ptsfx")
    x = rnd(5, B, 0.5)
    rig.set_knob(i, 2, 1)
    ok, lo, _ = rig.process(i, x, None, 1, False)
    check(ok == 1 and lo[5] == 0.0 and np.array_equal(np.delete(lo, 5), np.delete(x, 5)),
          "NaN: becomes 0, the rest untouched")
    rig.set_knob(i, 2, 4)
    _, lo, _ = rig.process(i, x, None, 1, False)
    check(lo[0] * 64 == 1.0, f"NaN: the plugin reset before its next block ({lo[0] * 64} resets)")
    rig.set_knob(i, 2, 2)
    _, lo, _ = rig.process(i, x, None, 1, False)
    check(lo[5] == 0.0, "infinite: becomes 0")
    rig.set_knob(i, 2, 4)
    _, lo, _ = rig.process(i, x, None, 1, False)
    check(lo[0] * 64 == 2.0, "infinite: reset too")
    _, lo, _ = rig.process(i, x, None, 1, False)
    check(lo[0] * 64 == 2.0, "a clean block: no reset")
    rig.set_knob(i, 2, 3)
    _, lo, ro = rig.process(i, x, x, 2, True)
    check(np.all(lo == 4.0) and np.all(ro == -4.0), "loud: held at +4 and -4, both sides")
    rig.set_knob(i, 2, 0)

    # time: the stub clock says each block took `spend`
    rig.spend = 1001
    for _ in range(7):
        rig.process(i, x, None, 1, False)
    check(rig.call("rig_bypassed", i) == 0 and rig.process(i, x, None, 1, False)[0] == 1,
          "time: 7 blocks over budget, then the 8th runs")
    check(rig.call("rig_bypassed", i) == 1, "time: 8 in a row: bypassed")
    ok, lo, _ = rig.process(i, x, None, 1, False)
    check(ok == 0 and np.array_equal(lo, x.astype("<f4")), "time: bypassed, the block left alone")
    check(rig.s32("rig_take_bypassed") == i and rig.s32("rig_take_bypassed") == -1, "time: told once")
    rig.call("rig_destroy", i)
    i, _ = rig.create("Probe.ptsfx")
    for n in range(7):
        rig.process(i, x, None, 1, False)
    rig.spend = 1000
    rig.process(i, x, None, 1, False)
    rig.spend = 1001
    for n in range(7):
        rig.process(i, x, None, 1, False)
    check(rig.call("rig_bypassed", i) == 0, "time: a block at budget starts the count again")
    rig.call("rig_destroy", i)
    rig.spend = 0

    # the plugin's own work: instructions as cycles, Spin x 1000 turns of its loop
    rig.fresh({"/SynthFX/Probe.ptsfx": PROBE.read_bytes()}, budget=30_000)
    rig.start()
    i, _ = rig.create("Probe.ptsfx")
    m = rig.m
    hk = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: setattr(rig, "insns", rig.insns + 1))
    m.uc.ctl_flush_tb()
    rig.count_insns = True
    try:
        for _ in range(10):
            rig.process(i, x, None, 1, False)
        light = rig.call("rig_bypassed", i)
        rig.set_knob(i, 3, 10)
        for _ in range(8):
            rig.process(i, x, None, 1, False)
        heavy = rig.call("rig_bypassed", i)
    finally:
        rig.count_insns = False
        m.uc.hook_del(hk)
        m.uc.ctl_flush_tb()
    check(light == 0 and heavy == 1, "time: measured around the plugin's own work (light runs, 10,000 turns bypassed)")


# ---- 6. the start marker -------------------------------------------------------------------------

def marker(rig: Rig) -> None:
    rig.fresh()
    rig.ms = 1000
    rig.start()
    i, _ = rig.create("Phaser.ptsfx")
    check(rig.file("/SynthFX/.starting") is not None, "marker: written with the first plugin")
    w = rig.counter("rig_writes")
    rig.ms = 3000
    j, _ = rig.create("Chorus.ptsfx")
    check(rig.counter("rig_writes") == w, "marker: not written again while it's up")
    rig.ms = 3000 + SETTLE_MS - 1
    rig.call("rig_update")
    check(rig.file("/SynthFX/.starting") is not None, "marker: kept until the newest plugin has run 5 s")
    rig.ms = 3000 + SETTLE_MS
    rig.call("rig_update")
    check(rig.file("/SynthFX/.starting") is None, "marker: removed after 5 s")
    rig.ms = 20000
    k, _ = rig.create("Flanger.ptsfx")
    check(rig.file("/SynthFX/.starting") is not None, "marker: written again for a later plugin")
    for x in (i, j, k):
        rig.call("rig_destroy", x)

    # found at start: the last run stopped with a new plugin
    rig.fresh({"/SynthFX/.starting": b"1"})
    rig.ms = 0
    rig.start()
    check(rig.call("rig_safe_mode") == 1, "safe mode: the marker found at start")
    check(rig.create("Chorus.ptsfx", project=True) == (-1, "skipped") and rig.call("rig_loaded") == 0,
          "safe mode: a project's plugin skipped, nothing loaded")
    rig.ms = 60_000
    rig.call("rig_update")
    check(rig.call("rig_safe_mode") == 1, "safe mode: lasts until the user picks one")
    i, s = rig.create("Chorus.ptsfx")
    check(i >= 0 and rig.call("rig_safe_mode") == 1, f"safe mode: the user's pick loads ({s})")
    rig.ms += SETTLE_MS
    rig.call("rig_update")
    check(rig.call("rig_safe_mode") == 0 and rig.file("/SynthFX/.starting") is None,
          "safe mode: over once it has run 5 s, the marker gone")
    j, s = rig.create("Chorus.ptsfx", project=True)
    check(j >= 0, "safe mode over: a project's plugin loads")


# ---- 7. cost -------------------------------------------------------------------------------------

def cost(rig: Rig) -> None:
    rig.fresh()
    rig.start()
    n = [0]
    m = rig.m

    def counted(fn) -> int:
        hk = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1))
        m.uc.ctl_flush_tb()
        n[0] = 0
        fn()
        m.uc.hook_del(hk)
        m.uc.ctl_flush_tb()
        return n[0]
    x = rnd(6, B, 0.5)
    for f, r in (("Overdrive.ptsfx", False), ("Chorus.ptsfx", True)):
        i, _ = rig.create(f)
        rig.process(i, x, x, 1, r)
        rig.m.write(rig.L, np.asarray(x, dtype="<f4").tobytes())
        through = counted(lambda: rig.call("rig_process", i, rig.L, rig.R if r else 0, 1))
        p = Plugin(rig.file(f"/SynthFX/{f}"))
        st = m.alloc(p.state_size, 8)
        e = [(DIRECT_AT + o) | 1 for o in p.entries[:4]]
        m.write(DIRECT_AT, p.code)
        m.uc.ctl_flush_tb()
        m.call(e[0], st, rig.host)
        for k, kn in enumerate(p.knobs):
            m.uc.reg_write(UC_ARM_REG_S0, f32(kn[2]))
            m.call(e[1], st, k)
        m.call(e[2], st, rig.L, rig.R if r else 0, B, 1)
        alone = counted(lambda: m.call(e[2], st, rig.L, rig.R if r else 0, B, 1, max_insns=400_000_000))
        extra = through - alone
        print(f"info cost {f}: {alone:,} instructions per block alone, {through:,} through the runtime (+{extra:,})")
        check(extra < (2100 if r else 1200), f"cost: the runtime's own work around {f} is small (+{extra:,})")
        rig.call("rig_destroy", i)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    axf = Path(args[0]) if args else ROOT / "build" / "emu" / "synthfx_rig.axf"
    quick = "--quick" in sys.argv
    rig = Rig(axf)
    start(rig)
    pack_through(rig, quick)
    knobs(rig)
    loading(rig)
    guards(rig)
    marker(rig)
    cost(rig)
    print(f"{checks} checks, {len(failures)} failed")
    for f in failures:
        print("  " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
