#!/usr/bin/env python3
"""What the mods add to the audio interrupt, in instructions of our own and CE's open code.

    python tools/emu/test_bundle_cost.py <mods.axf> <reference.axf> <worst-case project folder>

Carried over from 1.2.0's test of the same name, with one part left out: 1.2.0 also counted whole blocks
through the synths, and the closed libraries' licence rules that out here. Their instructions aren't
counted, so this says nothing about the total. That's measured on the device, with the Profiler.

1. Sequencer side: Sequencer::tick on the real project, Play pressed on the real path, mods against
   reference, block for block. Limit: SEQ_LIMIT more in the worst block.
2. The effects: each one's own process call alone, and after a clear. Limits are the figures in
   docs/mt/MASTERFX_PORT_PLAN.md plus a margin, and no unaligned word access: a Cortex-M7 can fault on
   those (always for VLDR, LDRD and LDM) and Unicorn doesn't.

1 % of the CPU is about 15,300 cycles or 3,800 instructions (1.2.0's device readings).
"""
import bisect
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_S0,
                               UC_ARM_REG_SP)

sys.path.insert(0, str(Path(__file__).parent))
from boot_rig import BootRig  # noqa: E402
from test_masterfx import DELAYS, REVERBS  # noqa: E402
from test_masterfx_rig import Graph  # noqa: E402

SEQ_LIMIT = 3800        # 1 % of the CPU in the worst block
SEQ_BLOCKS = 1500
REVERB_LIMIT = 52000    # Shimmer, the heaviest: 47,500
DELAY_LIMIT = 26000     # BBD, the heaviest: 23,300
FIRST_BLOCK_LIMIT = 150000
PLAY = 23
BLOCK_SECONDS = 128 / 44100

failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    failures += 0 if ok else 1


def tick_counts(axf, project):
    r = BootRig(axf)
    m = r.m
    updates = [0]

    def on_update(mm):
        updates[0] += 1
    r.watch(m.addr("_ZN14cPatternEditor6updateEv"), on_update)
    r.card_from(project, "MFX test", workspace=False)
    r.boot()
    for _ in range(3000):
        r.loop()
        r.interrupt()
        if updates[0] > 5:
            break
    r.open_project("MFX test")
    before, quiet = len(r.sd.calls), 0
    while quiet <= 200:
        r.loop()
        r.interrupt()
        quiet = quiet + 1 if len(r.sd.calls) == before else 0
        before = len(r.sd.calls)
    r.press(PLAY)
    r.loop()
    r.release(PLAY)

    # Addresses inside the closed libraries aren't counted, and nothing is kept about them.
    closed = sorted((s.addr & ~1, (s.addr & ~1) + s.size) for n, s in r.syms.items()
                    if s.is_func and s.size and ("polysynth" in n.lower() or "polydsp" in n.lower()))
    starts = [a for a, _ in closed]
    count = [0]

    def on_code(uc, address, size, user):
        j = bisect.bisect_right(starts, address) - 1
        if j < 0 or address >= closed[j][1]:
            count[0] += 1

    hook = m.uc.hook_add(UC_HOOK_CODE, on_code)
    m.uc.ctl_flush_tb()
    tick = m.addr("_ZN9Sequencer4tickEf")
    seq = m.addr("sequencer")
    counts = []
    for _ in range(SEQ_BLOCKS):
        count[0] = 0
        m.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", BLOCK_SECONDS))[0])
        m.call(tick, seq, max_insns=20_000_000)
        counts.append(count[0])
    m.uc.hook_del(hook)
    return counts


def sequencer_side(mods, ref, project):
    print("-- sequencer side --", flush=True)
    a = tick_counts(mods, project)
    b = tick_counts(ref, project)
    extra = [x - y for x, y in zip(a, b)]
    print(f"     mods worst {max(a)}, average {sum(a) // len(a)}; reference worst {max(b)}, average {sum(b) // len(b)}")
    print(f"     worst block's extra {max(extra)}, average extra {sum(extra) // len(extra)}", flush=True)
    check(max(a) > 2000 and max(b) > 2000, "the project keeps the sequencer busy")
    check(max(a) - max(b) <= SEQ_LIMIT, f"the mods add at most {SEQ_LIMIT} to the worst block ({max(a) - max(b)})")


def effect_cost(mods):
    print("-- the effects' own calls --", flush=True)

    def measure(m, fn, args):
        state = {"insns": 0, "odd": {}}

        def code(uc, addr, size, user):
            state["insns"] += 1

        def mem(uc, access, addr, size, value, user):
            if size >= 4 and addr % 4:
                pc = uc.reg_read(15)
                state["odd"][pc] = state["odd"].get(pc, 0) + 1

        h1 = m.uc.hook_add(UC_HOOK_CODE, code)
        h2 = m.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, mem)
        m.uc.ctl_flush_tb()
        m.call(fn, *args, max_insns=50_000_000)
        m.uc.hook_del(h1)
        m.uc.hook_del(h2)
        m.uc.ctl_flush_tb()
        return state

    def captured_args(reverb, delay):
        g = Graph(mods, 60, 60)
        m = g.m
        g.set_voices(0, 4)
        g.choose(True, reverb, delay)
        out = {}
        regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)

        def grab(name, stack):
            def f(mm):
                sp = mm.uc.reg_read(UC_ARM_REG_SP)
                out[name] = [mm.uc.reg_read(x) for x in regs] + [mm.r32(sp + 4 * i) for i in range(stack)]
            return f
        rp = m.addr("_ZN4mods8masterfx13reverbProcessERKNS0_11ReverbKnobsEPtS4_PsS5_")
        dp = m.addr("_ZN4mods8masterfx12delayProcessERKNS0_10DelayKnobsEPsS4_PKsS4_S4_")
        g.watch(rp, grab("r", 1))
        g.watch(dp, grab("d", 2))
        for note in (48, 55, 60, 64):
            g.note(0, note, True)
        g.blocks(30)
        return g, m, rp, dp, out

    for which, name in REVERBS.items():
        g, m, rp, _, out = captured_args(which, 0)
        normal = max(measure(m, rp, out["r"])["insns"] for _ in range(3))
        odd = measure(m, rp, out["r"])["odd"]
        m.call(m.addr("_ZN4mods8masterfx13reverbClearedEv"))
        first = measure(m, rp, out["r"])["insns"]
        check(normal <= REVERB_LIMIT, f"{name}: {normal} instructions a block, limit {REVERB_LIMIT}")
        check(first <= FIRST_BLOCK_LIMIT, f"{name}: {first} in the block after a clear, limit {FIRST_BLOCK_LIMIT}")
        check(not odd, f"{name}: no unaligned word access {[hex(k) for k in odd][:4]}")
    for which, name in DELAYS.items():
        g, m, _, dp, out = captured_args(0, which)
        normal = max(measure(m, dp, out["d"])["insns"] for _ in range(3))
        odd = measure(m, dp, out["d"])["odd"]
        m.call(m.addr("_ZN4mods8masterfx12delayClearedEv"))
        first = measure(m, dp, out["d"])["insns"]
        check(normal <= DELAY_LIMIT, f"{name}: {normal} instructions a block, limit {DELAY_LIMIT}")
        check(first <= FIRST_BLOCK_LIMIT, f"{name}: {first} in the block after a clear, limit {FIRST_BLOCK_LIMIT}")
        check(not odd, f"{name}: no unaligned word access {[hex(k) for k in odd][:4]}")


def main():
    mods, ref, project = sys.argv[1], sys.argv[2], sys.argv[3]
    effect_cost(mods)
    sequencer_side(mods, ref, project)
    print(f"{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
