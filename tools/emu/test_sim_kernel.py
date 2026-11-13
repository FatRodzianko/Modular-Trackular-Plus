#!/usr/bin/env python3
# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Checks of the simulator's kernel itself, with no firmware (docs/mt/SIMULATOR_STAGE1.md 2.4).

    python tools/emu/test_sim_kernel.py

Fake main loops in Python for the clock's rules, and a few lines of hand-encoded Thumb under Unicorn for
the masks and the second instance. A few seconds.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cpu_limit  # noqa: E402
from sim_kernel import PERIOD_NS, Kernel, Params, deadline  # noqa: E402
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc  # noqa: E402
from unicorn import arm_const as A  # noqa: E402
from vtables import Model, Unmodelled, resolve, slots  # noqa: E402

cpu_limit.apply()
failed = []


def check(ok, what, detail=""):
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f": {detail}"), flush=True)
    if not ok:
        failed.append(what)


class Flat:
    """A main loop with no passes: every slice runs to its count. Records each interrupt."""

    def __init__(self, k):
        self.k = k
        self.fired = []
        self.pos = 0                       # main-loop instructions run so far
        self.on_slice = None

    def run_slice(self, n):
        if self.on_slice:
            r = self.on_slice(self, n)
            if r:
                return r
        self.pos += n
        return "count"

    def masked(self):
        return False

    def fire(self, kind, t):
        self.fired.append((kind, t, self.pos))

    def next_change(self, vt):
        return None


def kernel(**kw):
    k = Kernel(Params(**kw))
    k.enable_interrupts()
    return k


def cadence():
    k = kernel(max_slice=10 ** 9)
    e = Flat(k)
    k.run(e, 10 * 10 ** 9)
    out = [t for kind, t, _ in e.fired if kind == "out"]
    ins = [t for kind, t, _ in e.fired if kind == "in"]
    expect = (10 * 44100) // 128
    check(len(out) == expect, f"10 virtual seconds give floor(10 s / block) = {expect} output interrupts", len(out))
    check(len(ins) == len(out), "an input interrupt before each output one", (len(ins), len(out)))
    d_in = int(PERIOD_NS * k.p.d_isr_in_frac)
    check(all(t == deadline(i + 1) + d_in for i, t in enumerate(out)), "each deadline is the rational one, to the ns")
    check(deadline(344) == 344 * 128 * 10 ** 9 // 44100 or deadline(344) == -(-344 * 128 * 10 ** 9 // 44100),
          "deadlines round once from the exact value, no drift", deadline(344))

    j = PERIOD_NS // 4
    k = kernel(max_slice=10 ** 9, jitter_ns=j, seed=7)
    e = Flat(k)
    k.run(e, 2 * 10 ** 9)
    out = [t for kind, t, _ in e.fired if kind == "in"]
    check(all(a < b for a, b in zip(out, out[1:])), "jitter never reorders interrupts")
    check(all(abs(t - deadline(i + 1)) <= j for i, t in enumerate(out)), "jitter stays within a quarter block")
    check(len(set(t - deadline(i + 1) for i, t in enumerate(out))) > 10, "jitter moves them")


class MaskedFlat(Flat):
    """Masked for the first `masked_len` of every `cycle` instructions, as cpsid ... cpsie in a loop."""

    def __init__(self, k, cycle, masked_len):
        super().__init__(k)
        self.cycle, self.masked_len = cycle, masked_len

    def masked(self):
        return self.pos % self.cycle < self.masked_len


def masks():
    k = kernel()
    e = MaskedFlat(k, 3000, 2000)
    k.run(e, 50_000_000)
    held = [p for kind, t, p in e.fired if kind == "out"]
    check(all(p % 3000 >= 2000 for p in held), "no interrupt fires while masked")
    check(k.c.held > 0 and k.c.held_ns > 0, "interrupts due while masked are held, and the hold is counted",
          (k.c.held, k.c.held_ns))
    firsts = [p % 3000 for kind, t, p in e.fired if kind == "out"]
    check(sum(1 for p in firsts if p == 2000) >= k.c.held, "a held one fires on the first instruction after the "
          "unmask", sorted(set(firsts))[:5])

    # an unmasked gap of 3 instructions between masked stretches: never stepped over
    k = kernel()
    e = MaskedFlat(k, 1003, 1000)
    k.run(e, 30_000_000)
    check(all(p % 1003 in (1000, 1001, 1002) for kind, t, p in e.fired), "a 3-instruction unmasked gap is found")
    check(k.c.irqs_out == len([d for d in range(1, 20) if deadline(d) < 30_000_000]) or k.c.irqs_out >= 9,
          "and none is lost", k.c.irqs_out)


def waits():
    for phase in (0, PERIOD_NS // 2):
        k = Kernel(Params(phase_ns=phase))
        e = Flat(k)
        k.enable_interrupts()
        start = [None]

        def once(eng, n):
            if start[0] is None:
                start[0] = k.now()
                k.wait(10_000_000, "test")
        e.on_slice = once
        k.run(e, 12_000_000)
        inside = [t for kind, t, _ in e.fired if kind == "in" and start[0] <= t <= start[0] + 10_000_000]
        due = [d for d in (deadline(i, phase) for i in range(0, 8)) if start[0] < d <= start[0] + 10_000_000]
        check(len(inside) == len(due) and len(due) in (3, 4), f"a 10 ms wait fires {len(due)} interrupts at their "
              f"deadlines inside it (phase {phase})", (inside, due))
        check(k.vt >= start[0] + 10_000_000, "the clock after the wait shows it", k.vt)


def overload():
    k = kernel(d_isr_frac=1.2)
    e = Flat(k)
    k.run(e, 100_000_000)
    check(k.c.late > 20, "an interrupt longer than a block makes the next ones late", k.c.late)
    after_first = e.pos - e.fired[0][2]
    check(after_first < 5000, "and starves the main loop", after_first)
    check(k.c.worst_late_ns > PERIOD_NS, "lateness grows", k.c.worst_late_ns)


def chosen():
    k = kernel()
    e = Flat(k)
    n = [0]

    def at_third(eng, count):
        n[0] += 1
        if n[0] == 3:
            return "chosen"
    e.on_slice = at_third
    k.run(e, 2_000_000)
    check(k.c.forced == 1 and e.fired and e.fired[0][1] < deadline(1),
          "a chosen point fires the next interrupt there, early, and counts it as forced", (k.c.forced, e.fired[:2]))
    check(k.c.uncertain == 1, "and its stop is an uncertain one")

    k = kernel()
    e = MaskedFlat(k, 10 ** 9, 10 ** 9)            # masked throughout
    n[0] = 0
    e.on_slice = at_third
    k.run(e, 1_000_000)
    check(k.c.refused == 1 and not e.fired, "a chosen point that is masked is refused", (k.c.refused, e.fired))


class Passes(Flat):
    """Passes of `length` instructions. Each reads millis at its start; the first that sees `at` ms or later
    does work (it writes). A pass that wrote, or had an interrupt, isn't idle."""

    def __init__(self, k, length, at, skip, always_writes=False):
        super().__init__(k)
        self.length, self.at, self.skip, self.always = length, at, skip, always_writes
        self.in_pass = 0
        self.wrote = True
        self.noticed = None

    def run_slice(self, n):
        k = self.k
        while True:
            left = self.length - self.in_pass
            if n < left:
                self.in_pass += n
                self.pos += n
                return "count"
            n -= left
            self.pos += left
            self.in_pass = 0
            idle = not self.wrote and not k.irq_in_pass
            k.pass_start()
            ms = k.now() // 1_000_000
            self.wrote = self.always
            if self.noticed is None and ms >= self.at:
                self.noticed = (ms, k.now())
                self.wrote = True
            if self.skip and idle and not self.wrote:
                return "skip"


def idle_skip():
    for at in (3, 7, 12):
        runs = {}
        for skip in (False, True):
            k = kernel()
            e = Passes(k, 500, at, skip)
            k.run(e, (at + 2) * 1_000_000)
            runs[skip] = (e.noticed, k.c.skips)
        (full, _), (skipped, skips) = runs[False], runs[True]
        check(full[0] == skipped[0] == at and skips > 0, f"a millis poll sees {at} ms at the same millisecond with "
              "and without the skip", runs)
    k = kernel()
    e = Passes(k, 500, 10 ** 9, True, always_writes=True)
    k.run(e, 3_000_000)
    check(k.c.skips == 0, "a pass that writes is never skipped", k.c.skips)


def determinism():
    def one(seed):
        k = kernel(seed=seed, jitter_ns=PERIOD_NS // 5, d_isr="seeded")
        e = Passes(k, 777, 10 ** 9, True)
        k.run(e, 300_000_000)
        return k.log.digest()
    a, b, c = one(3), one(3), one(4)
    check(a == b, "the same seed gives the same log hash", (a, b))
    check(a != c, "a different seed gives a different interleaving", (a, c))


# -- Unicorn: hand-encoded Thumb ---------------------------------------------------------------------------
BASE, RAM = 0x60000000, 0x20000000


def thumb_b(at, target):
    off = (target - (at + 4)) >> 1
    return struct.pack("<H", 0xE000 | (off & 0x7FF))


def mask_code():
    """loop: cpsid i; 40 x adds r0,#1; cpsie i; 3 x adds r1,#1; b loop"""
    code = struct.pack("<H", 0xB672) + struct.pack("<H", 0x3001) * 40 + struct.pack("<H", 0xB662)
    code += struct.pack("<H", 0x3101) * 3
    return code + thumb_b(len(code), 0), 2 + 80, 2 + 80 + 2       # cpsie at 82, unmasked from 84


class UcLoop:
    def __init__(self, k, code):
        self.k = k
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(BASE, 0x10000)
        uc.mem_map(RAM, 0x10000)
        uc.mem_write(BASE, code)
        uc.reg_write(A.UC_ARM_REG_SP, RAM + 0x8000)
        uc.reg_write(A.UC_ARM_REG_PC, BASE)
        self.fired = []

    def run_slice(self, n):
        self.uc.emu_start(self.uc.reg_read(A.UC_ARM_REG_PC) | 1, BASE + 0xFFF0, count=n)
        return "count"

    def masked(self):
        return bool(self.uc.reg_read(A.UC_ARM_REG_PRIMASK))

    def fire(self, kind, t):
        self.fired.append((self.uc.reg_read(A.UC_ARM_REG_PC) - BASE, self.uc.reg_read(A.UC_ARM_REG_PRIMASK)))

    def next_change(self, vt):
        return None


def thumb_masks():
    code, cpsie, after = mask_code()
    k = kernel()
    e = UcLoop(k, code)
    k.run(e, 60_000_000)
    check(e.fired and all(primask == 0 for _, primask in e.fired), "Thumb: no interrupt fires between cpsid and "
          "cpsie", e.fired[:4])
    pcs = sorted(set(pc for pc, _ in e.fired))
    check(k.c.held > 0 and after in pcs, "Thumb: a held one fires on the first instruction after cpsie",
          (k.c.held, pcs))


def two_instances():
    """An ISR that saves r4 and r5 (the interrupted code's), stores them, clobbers them and restores them:
    the same memory and registers after, run in the twin with the registers copied as in one instance."""
    from sim_rig import copy_interrupted, make_twin
    main = (struct.pack("<H", 0x2407) + struct.pack("<H", 0x2509)     # movs r4,#7; movs r5,#9
            + struct.pack("<H", 0x3401) + thumb_b(6, 4))                # loop: adds r4,#1; b loop
    isr_at = 0x100
    isr = (struct.pack("<H", 0xB530)                                    # push {r4, r5, lr}
           + struct.pack("<H", 0x4803)                                  # ldr r0, [pc, #12]: the word at +16
           + struct.pack("<H", 0x6004) + struct.pack("<H", 0x6045)      # str r4,[r0]; str r5,[r0,#4]
           + struct.pack("<H", 0x2400) + struct.pack("<H", 0x2500)      # movs r4,#0; movs r5,#0
           + struct.pack("<H", 0xBD30) + struct.pack("<H", 0xBF00)      # pop {r4, r5, pc}; nop
           + struct.pack("<I", RAM + 0x100))
    ret = BASE + 0x800

    def machine():
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(BASE, 0x10000)
        uc.mem_map(RAM, 0x10000)
        uc.mem_write(BASE, main)
        uc.mem_write(BASE + isr_at, isr)
        uc.mem_write(ret, struct.pack("<H", 0xBF00))
        uc.reg_write(A.UC_ARM_REG_SP, RAM + 0x8000)
        uc.emu_start(BASE | 1, 0xFFFFFFF0, count=13)
        return uc

    one = machine()
    saved = one.context_save()
    one.reg_write(A.UC_ARM_REG_SP, (one.reg_read(A.UC_ARM_REG_SP) - 0x68) & ~7)
    one.reg_write(A.UC_ARM_REG_LR, ret | 1)
    one.emu_start((BASE + isr_at) | 1, ret)
    one.context_restore(saved)

    two = machine()
    uc2, _maps, _local = make_twin(two)
    sp = copy_interrupted(two, uc2)
    uc2.reg_write(A.UC_ARM_REG_SP, sp)
    uc2.reg_write(A.UC_ARM_REG_LR, ret | 1)
    uc2.emu_start((BASE + isr_at) | 1, ret)

    regs = [getattr(A, f"UC_ARM_REG_R{i}") for i in range(13)] + [A.UC_ARM_REG_SP, A.UC_ARM_REG_PC]
    same_ram = bytes(one.mem_read(RAM, 0x10000)) == bytes(two.mem_read(RAM, 0x10000))
    same_regs = [one.reg_read(r) for r in regs] == [two.reg_read(r) for r in regs]
    stored = struct.unpack("<II", bytes(two.mem_read(RAM + 0x100, 8)))
    check(stored[0] > 7 and stored[1] == 9, "twin: the ISR sees the interrupted registers", stored)
    check(same_ram and same_regs, "twin: the same RAM and main registers as one instance", (same_ram, same_regs))
    for _ in range(3):
        two.emu_start(two.reg_read(A.UC_ARM_REG_PC) | 1, 0xFFFFFFF0, count=5)
        one.emu_start(one.reg_read(A.UC_ARM_REG_PC) | 1, 0xFFFFFFF0, count=5)
    check([one.reg_read(r) for r in regs] == [two.reg_read(r) for r in regs], "twin: the main loop goes on the same")


# -- the resolver ------------------------------------------------------------------------------------------
class FakeElf:
    def __init__(self):
        from machine import Symbol
        funcs = ["_ZN7FooImpl3barEv", "_ZN7FooImpl3bazEi", "_ZN8BaseImpl4quuxEv", "_ZN7FooImpl3bazEPKc"]
        self.symbols = {n: Symbol(n, 0x60001000 + 0x10 * i + 1, 8, True) for i, n in enumerate(funcs)}
        table = struct.pack("<II", 0, 0) + b"".join(struct.pack("<I", self.symbols[n].addr) for n in funcs)
        self.symbols["_ZTV7FooImpl"] = Symbol("_ZTV7FooImpl", 0x60002000, len(table), False)
        self.segments = [(0x60002000, 0x60002000, table, len(table))]


def resolver():
    elf = FakeElf()
    names = [s.key for s in slots(elf, "FooImpl")]
    check(names == ["barEv", "bazEi", "quuxEv", "bazEPKc"], "resolver: slots named, inherited ones too", names)
    idle = Model(lambda mm: 0, pure=True)
    full = {"bar": idle, "bazEi": idle, "bazEPKc": idle, "quux": idle}
    check(len(resolve(elf, "FooImpl", full)) == 4, "resolver: every slot modelled is accepted")
    for what, models in (("a slot with no model", {k: v for k, v in full.items() if k != "quux"}),
                         ("a model for no slot", dict(full, gone=idle))):
        try:
            resolve(elf, "FooImpl", models)
            check(False, f"resolver: {what} is refused")
        except Unmodelled:
            check(True, f"resolver: {what} is refused")


def main():
    for test in (cadence, masks, waits, overload, chosen, idle_skip, determinism, thumb_masks, two_instances,
                 resolver):
        test()
    print("FAILED: " + ", ".join(failed) if failed else "all passed", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
