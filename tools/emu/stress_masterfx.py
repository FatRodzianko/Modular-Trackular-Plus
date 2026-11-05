#!/usr/bin/env python3
"""Long randomized sessions through the reverb and delay objects, carried over from 1.2.0's test.

    python tools/emu/stress_masterfx.py <mods.axf> [--blocks 20000] [--seed 1] [--seed 2] [--delay-only]

CE's own objects in the real firmware, with AudioStream's block calls stubbed (test_masterfx.py's rigs),
so no closed code runs here. 20,000 blocks are 58 s of audio.

Each session, at random:
  - sends: silence, noise at random levels, full-scale noise, full-scale DC, a loud sine, impulses;
  - knobs: random, extreme, or garbage (NaN, infinities, negatives, huge, predelay out of range;
    Time 0 or huge, Feedback 255, any Filter coefficient);
  - the effect switched, as apply() does, or the active index set out of range;
  - a missing input block, or no output block to be had.
After every block:
  - no fault, and update() returns within 400,000 instructions;
  - nothing written or read outside the effect's buffers, its object, the block pool, masterfx's own
    state, the stack it's allowed and, for reads, flash and ITCM;
  - each block handed over was released exactly once.
After garbage, with normal knobs back and the buffers cleared, each effect makes sound again.

Stock's own code gets no garbage: it has no protection from it, on any firmware, and the UI can't send it.
"""
import argparse
import math
import random
import sys
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE

sys.path.insert(0, str(Path(__file__).parent))
from machine import STACK_TOP  # noqa: E402
from test_masterfx import BLOCK, DL_A0, DL_B1, DL_FEEDBACK, DL_TIME, OBJECT, DelayRig, ReverbRig, f32  # noqa: E402

LIMIT = 400_000
STACK_ALLOWED = 2048
SP_AT_CALL = STACK_TOP - 0x100


def garbage_float(rng):
    return rng.choice((math.nan, math.inf, -math.inf, -1.0, 1e30, -1e30, 1e-45))


def signal(rng, kind):
    if kind == "silence":
        return [0] * BLOCK
    if kind == "noise":
        level = rng.choice((100, 2000, 8000, 20000))
        return [rng.randint(-level, level) for _ in range(BLOCK)]
    if kind == "full":
        return [rng.choice((-32768, 32767)) for _ in range(BLOCK)]
    if kind == "dc":
        v = rng.choice((-32768, 32767))
        return [v] * BLOCK
    if kind == "sine":
        hz = rng.uniform(40, 15000)
        return [int(32000 * math.sin(2 * math.pi * hz * i / 44100)) for i in range(BLOCK)]
    x = [0] * BLOCK
    x[rng.randrange(BLOCK)] = rng.choice((-32768, 32767))
    return x


def guard(rig, allowed_writes, allowed_reads):
    """Watch everything outside the allowed ranges. A range hook costs nothing while nothing lands in it."""
    m = rig.m
    problems = []
    memories = [(0x00000000, 0x00020000), (0x20000000, 0x20058000), (0x20200000, 0x20208000),
                (0x60000000, 0x61000000), (0x80000000, 0x84000000)]

    def gaps(allowed):
        out = []
        for base, end in memories:
            at = base
            for a, b in sorted(r for r in allowed if r[1] > base and r[0] < end):
                if a > at:
                    out.append((at, a))
                at = max(at, b)
            if at < end:
                out.append((at, end))
        return out

    def on_access(uc, access, address, size, value, user):
        problems.append((user, hex(address), size, hex(uc.reg_read(15))))

    for begin, end in gaps(allowed_writes):
        m.uc.hook_add(UC_HOOK_MEM_WRITE, on_access, "write", begin=begin, end=end - 1)
    for begin, end in gaps(allowed_reads):
        m.uc.hook_add(UC_HOOK_MEM_READ, on_access, "read", begin=begin, end=end - 1)
    return problems


def state_ranges(rig):
    """masterfx's own RAM, by symbol."""
    out = []
    for name, s in rig.m.elf.symbols.items():
        if name.startswith("_ZN4mods8masterfx") and not s.is_func and s.size and s.addr >= 0x20000000:
            out.append((s.addr, s.addr + s.size))
    return out


def common_ranges(rig):
    blocks = [(b, b + 4 + 2 * BLOCK) for b in rig.blocks]
    stack = (SP_AT_CALL - STACK_ALLOWED, STACK_TOP)
    return blocks + [stack, (OBJECT, OBJECT + 0x1000)] + state_ranges(rig)


def run_block(rig, rng, x, problems, name, block):
    """One block with the checks every block gets."""
    if rng.random() < 0.01:
        rig.input_missing[rng.randrange(2)] = True
    if rng.random() < 0.01:
        rig.alloc_missing = rng.randint(1, 2)
    rig.handed.clear()
    rig.released.clear()
    rig.put(0, x)
    rig.put(1, x)
    try:
        rig.m.call(rig.update_fn, OBJECT, max_insns=LIMIT)
    except RuntimeError as e:
        return [f"{name}: block {block}: {e}"]
    rig.input_missing = [False, False]
    rig.alloc_missing = 0
    out = []
    if sorted(rig.handed) != sorted(rig.released):
        out.append(f"{name}: block {block}: handed {len(rig.handed)} blocks, released {len(rig.released)}")
    if problems:
        out.append(f"{name}: block {block}: outside its memory {problems[:3]}")
        problems.clear()
    return out


def reverb_session(axf, seed, blocks):
    rng = random.Random(seed)
    rig = ReverbRig(axf)
    m = rig.m
    buffers = []
    for name in ("internalRamReverbBuff", "externalRamReverbBuff"):
        s = m.sym(name)
        buffers.append((s.addr, s.addr + s.size))
    ranges = common_ranges(rig) + buffers
    problems = guard(rig, ranges, ranges + [(0x60000000, 0x61000000), (0, 0x20000)])
    which = 4
    rig.active("reverb", which)
    rig.reset()
    rig.knobs()
    kind = "noise"
    bad = []
    stats = {"switches": 0, "garbage": 0, "signals": 0}
    garbled = False
    for block in range(blocks):
        roll = rng.random()
        if roll < 0.01:
            kind = rng.choice(("silence", "noise", "noise", "full", "dc", "sine", "impulse"))
            stats["signals"] += 1
        elif roll < 0.02:
            which = rng.randrange(7)
            rig.active("reverb", which)
            rig.reset()
            rig.knobs(rng.random(), rng.random(), rng.random())
            garbled = False
            stats["switches"] += 1
        elif roll < 0.03:
            rig.knobs(rng.choice((0, 1, rng.random())), rng.choice((0, 1, rng.random())), rng.choice((0, 1, rng.random())),
                      rng.random(), rng.random(), rng.random())
        elif roll < 0.035 and which != 0:
            o = OBJECT + rig.off
            for i in range(5):
                if rng.random() < 0.5:
                    m.write(o + 4 * i, f32(garbage_float(rng)))
            if rng.random() < 0.5:
                m.w32(o + 32, rng.choice((0, 5000, 5001, 65536, 0x7FFFFFFF, 0xFFFFFFFF)))
            garbled = True
            stats["garbage"] += 1
        elif roll < 0.036 and which != 0:
            rig.active("reverb", rng.randrange(7, 256))
        elif roll < 0.037 and which != 0:
            rig.active("reverb", which)
        bad += run_block(rig, rng, signal(rng, kind), problems, f"reverb seed {seed}", block)
        if len(bad) > 8:
            break

    # After garbage: normal knobs and a clear (what any switch does), and each one sounds
    silent = []
    for choice in (1, 2, 3, 4, 5, 6):
        rig.active("reverb", choice)
        rig.reset()
        rig.knobs()
        peak = 0
        for _ in range(60):
            a, b = rig.block(signal(rng, "noise"))
            peak = max(peak, max(map(abs, a or [0])))
        if peak == 0:
            silent.append(choice)
    problems.clear()
    return bad, silent, stats, garbled


def delay_session(axf, seed, blocks):
    rng = random.Random(seed)
    rig = DelayRig(axf)
    m = rig.m
    s = m.sym("externalRamDelayBuff")
    ranges = common_ranges(rig) + [(s.addr, s.addr + s.size)]
    z = m.sym("_ZL9zeroblock")   # read in place of a missing input
    problems = guard(rig, ranges, ranges + [(0x60000000, 0x61000000), (0, 0x20000), (z.addr, z.addr + z.size)])
    which = 2
    rig.active("delay", which)
    rig.clear()
    kind = "noise"
    bad = []
    stats = {"switches": 0, "garbage": 0, "signals": 0}
    for block in range(blocks):
        roll = rng.random()
        if roll < 0.01:
            kind = rng.choice(("silence", "noise", "noise", "full", "dc", "sine", "impulse"))
            stats["signals"] += 1
        elif roll < 0.02:
            which = rng.randrange(3)
            rig.active("delay", which)
            rig.clear()
            rig.knobs()
            stats["switches"] += 1
        elif roll < 0.03:
            rig.knobs(rng.choice((rng.randrange(1, 35280), rng.randrange(1, 176400))), rng.random(),
                      rng.random() < 0.5, rng.uniform(100, 20000))
        elif roll < 0.035 and which != 0:
            m.w32(OBJECT + DL_TIME, rng.choice((0, 1, 262143, 262144, 0x7FFFFFFF, 0xFFFFFFFF)))
            m.write(OBJECT + DL_FEEDBACK, bytes([255]))
            for off in range(DL_A0, DL_B1 + 4, 4):
                m.w32(OBJECT + off, rng.getrandbits(32))
            stats["garbage"] += 1
        bad += run_block(rig, rng, signal(rng, kind), problems, f"delay seed {seed}", block)
        if len(bad) > 8:
            break

    silent = []
    for choice in (1, 2):
        rig.active("delay", choice)
        rig.clear()
        rig.knobs(time=2000, fb=0.5)
        peak = 0
        impulse = [20000] + [0] * (BLOCK - 1)
        for i in range(120):
            a, b = rig.block(impulse if i == 0 else [0] * BLOCK)
            if i > 2:
                peak = max(peak, max(map(abs, a or [0])))
        if peak == 0:
            silent.append(choice)
    problems.clear()
    return bad, silent, stats, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("axf")
    ap.add_argument("--blocks", type=int, default=20000)
    ap.add_argument("--seed", type=int, action="append")
    args = ap.parse_args()
    failed = 0
    for seed in args.seed or [1]:
        for name, session in (("reverb", reverb_session), ("delay", delay_session)):
            bad, silent, stats, _ = session(args.axf, seed, args.blocks)
            print(f"{name} seed {seed}: {args.blocks} blocks, {stats}")
            for line in bad[:8]:
                print("FAIL", line)
            if silent:
                print(f"FAIL {name} seed {seed}: silent after garbage: {silent}")
            failed += len(bad) + len(silent)
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
