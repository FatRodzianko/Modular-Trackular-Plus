# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Master FX runtime's guards acting, on the emulated Cortex-M7: the firmware's own
masterfxPlugins.cpp in a Tracker+ MT image, started by tools/emu/masterfx_runtime.py on a fake card
with the guard test plugins (tools/masterfx/device/, built by its build.sh), and a clock that counts
the plugins' own instructions as cycles, so the time guard reads what it would on the unit if an
instruction took a cycle. The budget is the firmware's: 600 cycles a us times an eighth of 2,902 us.

    python tools/masterfx/test_guards.py <TrackerPlus MT .axf> [card folder]

1. Time (GuardSlow, the dynamics slot): Light runs on; Heavy runs exactly 8 blocks, then the slot
   is bypassed and the page told once; 7 slow blocks, a fast one and 7 more aren't 8 in a row;
   installing it again un-bypasses it. The control: with no budget Heavy runs on.
2. Output (GuardOutput, a float reverb): a NaN or an infinity becomes 0 in that sample only, and the
   plugin is reset before its next block; 100.0 is held at full scale with no reset; a clean block
   comes back sample for sample.
3. The ducking gain and the meter (GuardGain, a float dynamics plugin with the duck flag): a NaN gain
   becomes 1 and resets it, 9 is held at 4 and -1 at 0, with no reset; the meter is held within 0 to
   1, a NaN reading 1.
4. The start marker (GuardHang): on the card before the plugin's first block, which never returns;
   at the next start (the card as the hang left it) safe mode skips a project's plugin and takes the
   user's pick, and the marker goes, with safe mode, 5 s after that pick. A plugin that runs its 5 s
   takes the marker off too.
Exit 0 if every check passed.
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "emu"))
from machine import Machine  # noqa: E402
from masterfx_runtime import SLOT, STATUS, Runtime, has_plugins  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402

CARD = ROOT / "build" / "device" / "masterfx_card" / "MasterFX"
BUDGET = 600 * (2902 // 8)          # masterfxPluginsCe.cpp: clockCyclesPerUs() * (2902u / 8u)
B = 128
MARKER = "/MasterFX/.starting"
P = "_ZN4mods8masterfx7plugins"
PROCESS = P + "7processENS1_4SlotER10PtmfxBlockPf"
SET_KNOB = P + "7setKnobENS1_4SlotEmf"
BYPASSED = P + "8bypassedENS1_4SlotE"
TAKE_BYPASSED = P + "12takeBypassedEv"
UPDATE = P + "6updateEv"
READ = P + "4readENS1_4SlotEPKcb"
INSTALL = P + "7installENS1_4SlotE"
SAFE = P + "12_GLOBAL__N_110inSafeModeE"
RAN = {0: "none", 1: "ran", 2: "bypassed"}

passed = 0
failed: list[str] = []


def check(ok: bool, what: str) -> None:
    global passed
    if ok:
        passed += 1
        print(f"  ok    {what}")
    else:
        failed.append(what)
        print(f"  FAIL  {what}")


class Rig:
    """The runtime on the fake card, with the clock counting instructions in the slots' code areas"""

    def __init__(self, axf: Path, files: dict[str, bytes], budget: int = BUDGET):
        m = self.m = Machine(axf)
        if not has_plugins(m):
            raise SystemExit(f"{axf}: no Master FX plugins in this firmware")
        self.insns = 0
        self.ms = 0
        self.rt = Runtime(m, files, cycles=lambda: self.insns, millis=lambda: self.ms, budget=budget)
        lo = self.rt.code[0]

        def count(uc, a, s, u):
            self.insns += 1
        m.uc.hook_add(UC_HOOK_CODE, count, begin=lo, end=lo + 3 * 8192 - 1)
        m.uc.ctl_flush_tb()
        self.left, self.right, self.inb = m.alloc(2 * B, 4), m.alloc(2 * B, 4), m.alloc(2 * B, 4)
        self.keyl, self.keyr = m.alloc(2 * B, 4), m.alloc(2 * B, 4)
        self.knobs = m.alloc(32, 4)
        self.gain = m.alloc(4 * B, 8)
        self.meter = m.alloc(4, 4)
        self.block = m.alloc(13 * 4, 4)

    def install(self, kind: str, file: str, project: bool = False) -> str:
        m = self.m
        name = m.put_str(file)
        status = STATUS[m.call(m.addr(READ), SLOT[kind], name, 1 if project else 0, max_insns=200_000_000) & 0xFF]
        if status == "ok":
            m.call(m.addr(INSTALL), SLOT[kind], max_insns=50_000_000)
            m.uc.ctl_flush_tb()
        return status

    def knob(self, kind: str, k: int, v: float) -> None:
        from unicorn.arm_const import UC_ARM_REG_S0
        self.m.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", v))[0])
        self.m.call(self.m.addr(SET_KNOB), SLOT[kind], k, max_insns=5_000_000)

    def run(self, kind: str, x: list[int], key: list[int] | None = None, duck: bool = False,
            max_insns: int = 50_000_000):
        """One block through the runtime's process(): (Ran, left, right, gain or None, meter, cost)"""
        m = self.m
        other = [v // 2 for v in x]
        m.write(self.left, struct.pack(f"<{B}h", *x))
        m.write(self.right, struct.pack(f"<{B}h", *other))
        m.write(self.inb, struct.pack(f"<{B}h", *x))
        m.write(self.knobs, bytes(32))
        kl = kr = 0
        if key is not None:
            m.write(self.keyl, struct.pack(f"<{B}h", *key))
            m.write(self.keyr, struct.pack(f"<{B}h", *key))
            kl, kr = self.keyl, self.keyr
        m.write(self.gain, struct.pack(f"<{B}f", *([-7.0] * B)))
        m.w32(self.meter, 0)
        m.write(self.block, struct.pack("<13I", self.left, self.right, self.inb if kind == "delay" else 0, kl, kr,
                                        0, 0, 0, 0, 0, self.knobs, B, self.gain if duck else 0))
        before = self.insns
        ran = m.call(m.addr(PROCESS), SLOT[kind], self.block, self.meter, max_insns=max_insns) & 0xFF
        left = list(struct.unpack(f"<{B}h", m.read(self.left, 2 * B)))
        right = list(struct.unpack(f"<{B}h", m.read(self.right, 2 * B)))
        gain = list(struct.unpack(f"<{B}f", m.read(self.gain, 4 * B))) if duck else None
        meter = struct.unpack("<f", m.read(self.meter, 4))[0]
        return RAN.get(ran, ran), left, right, gain, meter, self.insns - before

    def state(self, kind: str, word: int) -> int:
        return self.m.r32(self.rt.states[SLOT[kind]] + 4 * word)

    def bypassed(self, kind: str) -> bool:
        return self.m.call(self.m.addr(BYPASSED), SLOT[kind]) & 0xFF != 0

    def take_bypassed(self) -> int:
        v = self.m.call(self.m.addr(TAKE_BYPASSED)) & 0xFFFFFFFF
        return v - (1 << 32) if v & 0x80000000 else v

    def update(self) -> None:
        self.m.call(self.m.addr(UPDATE), max_insns=50_000_000)

    def safe(self) -> bool:
        return self.m.r8(self.m.addr(SAFE)) != 0


def signal(seed: int) -> list[int]:
    return [int(12000 * math.sin(0.05 * (i + 7 * seed))) for i in range(B)]


def card(folder: Path, *names: str) -> dict[str, bytes]:
    return {f"/MasterFX/{n}.ptmfx": (folder / f"{n}.ptmfx").read_bytes() for n in names}


def time_guard(axf: Path, folder: Path) -> None:
    print("1. the time guard (GuardSlow, dynamics)")
    r = Rig(axf, card(folder, "GuardSlow"))
    check(r.install("dynamics", "GuardSlow.ptmfx") == "ok", "GuardSlow installs")
    x = signal(1)
    light = [r.run("dynamics", x) for _ in range(20)]
    check(all(o[0] == "ran" for o in light), "Light: 20 blocks, all run")
    check(max(o[5] for o in light) < BUDGET, f"Light: {max(o[5] for o in light):,} instructions a block, "
                                              f"under the budget's {BUDGET:,}")
    check(all(o[1] == x for o in light), "Light: the master passes through")
    check(not r.bypassed("dynamics") and r.take_bypassed() == -1, "Light: not bypassed")

    r.knob("dynamics", 0, 1.0)
    heavy = [r.run("dynamics", x) for _ in range(12)]
    ran = [o[0] for o in heavy]
    check(ran == ["ran"] * 8 + ["bypassed"] * 4, f"Heavy: 8 blocks run, then bypassed ({ran})")
    check(heavy[0][5] > BUDGET, f"Heavy: {heavy[0][5]:,} instructions a block, over the budget")
    check(r.state("dynamics", 1) == 20 + 8, f"the plugin ran 28 blocks in all ({r.state('dynamics', 1)})")
    check(r.bypassed("dynamics"), "bypassed() says so")
    check(r.take_bypassed() == SLOT["dynamics"] and r.take_bypassed() == -1, "takeBypassed: the slot once, then none")
    check(heavy[-1][1] == x, "bypassed: the master isn't touched")

    check(r.install("dynamics", "GuardSlow.ptmfx") == "ok", "installed again")
    r.knob("dynamics", 0, 1.0)
    seq = []
    for load in [1] * 7 + [0] + [1] * 7:
        r.knob("dynamics", 0, float(load))
        seq.append(r.run("dynamics", x)[0])
    check(seq == ["ran"] * 15 and not r.bypassed("dynamics"),
          "7 slow, 1 fast, 7 slow: not 8 in a row, still running (and un-bypassed by the install)")
    seq = r.run("dynamics", x)[0], r.run("dynamics", x)[0]
    check(seq == ("ran", "bypassed"), f"the 8th slow block in a row bypasses it ({seq})")

    # the control: with no budget the same load runs on, so the bypass above came from the clock
    c = Rig(axf, card(folder, "GuardSlow"), budget=0xFFFFFFFF)
    c.install("dynamics", "GuardSlow.ptmfx")
    c.knob("dynamics", 0, 1.0)
    seq = [c.run("dynamics", x)[0] for _ in range(12)]
    check(seq == ["ran"] * 12 and not c.bypassed("dynamics"), "the control: no budget, Heavy runs 12 blocks")


def output_guard(axf: Path, folder: Path) -> None:
    print("2. the output guard (GuardOutput, a float reverb)")
    r = Rig(axf, card(folder, "GuardOutput"))
    check(r.install("reverb", "GuardOutput.ptmfx") == "ok", "GuardOutput installs")
    x = signal(2)
    o = r.run("reverb", x)
    check(o[0] == "ran" and o[1] == x and o[2] == [v // 2 for v in x], "None: the block comes back sample for sample")
    check(r.state("reverb", 1) == 1, f"reset once, before the first block ({r.state('reverb', 1)})")
    for fault, name in ((1, "NaN"), (3, "Inf")):
        r.knob("reverb", 0, float(fault))
        resets = r.state("reverb", 1)
        o = r.run("reverb", x)
        want = x[:5] + [0] + x[6:]
        check(o[1] == want and o[2] == [v // 2 for v in x], f"{name}: that sample 0, the rest as sent")
        check(r.state("reverb", 1) == resets, f"{name}: no reset within the block")
        r.knob("reverb", 0, 0.0)
        o = r.run("reverb", x)
        check(r.state("reverb", 1) == resets + 1 and o[1] == x,
              f"{name}: reset before the next block, which is clean ({r.state('reverb', 1) - resets} resets)")
    r.knob("reverb", 0, 2.0)
    resets = r.state("reverb", 1)
    o = r.run("reverb", x)
    check(o[1][5] == 32767 and o[1][:5] == x[:5] and o[1][6:] == x[6:], f"Big: 100.0 held at full scale ({o[1][5]})")
    r.run("reverb", x)
    check(r.state("reverb", 1) == resets, "Big: no reset")


def gain_guard(axf: Path, folder: Path) -> None:
    print("3. the ducking gain and the meter (GuardGain)")
    r = Rig(axf, card(folder, "GuardGain"))
    check(r.install("dynamics", "GuardGain.ptmfx") == "ok", "GuardGain installs")
    x, key = signal(3), signal(4)
    o = r.run("dynamics", x, key, duck=True)
    check(o[0] == "ran" and o[3] == [0.5] * B and abs(o[4] - 0.5) < 1e-6, "None: the gain 0.5 throughout, the meter 0.5")
    for fault, name, want, reset in ((1, "Gain NaN", 1.0, True), (2, "Gain 9", 4.0, False), (3, "Gain -1", 0.0, False)):
        r.knob("dynamics", 0, float(fault))
        resets = r.state("dynamics", 1)
        o = r.run("dynamics", x, key, duck=True)
        check(o[3][5] == want and o[3][:5] == [0.5] * 5 and o[3][6:] == [0.5] * (B - 6),
              f"{name}: frame 5 {o[3][5]}, the rest 0.5")
        r.knob("dynamics", 0, 0.0)
        r.run("dynamics", x, key, duck=True)
        got = r.state("dynamics", 1) - resets
        check(got == (1 if reset else 0), f"{name}: {'a reset' if reset else 'no reset'} ({got})")
    for fault, name, want in ((4, "Meter NaN", 1.0), (5, "Meter 2", 1.0)):
        r.knob("dynamics", 0, float(fault))
        o = r.run("dynamics", x)
        check(o[4] == want, f"{name}: the meter reads {o[4]}")
        halved = all(abs(a - b / 2) <= 1 for a, b in zip(o[1], x))
        check(o[3] is None and halved, f"{name}: no gain without the Ducks list; the master halved")


def marker(axf: Path, folder: Path) -> None:
    print("4. the start marker and safe mode (GuardHang)")
    r = Rig(axf, card(folder, "GuardHang", "GuardSlow"))
    check(MARKER not in r.rt.sd.files, "no marker before a plugin is installed")
    check(r.install("dynamics", "GuardHang.ptmfx") == "ok", "GuardHang installs")
    check(MARKER in r.rt.sd.files, "the marker is on the card before its first block")
    try:
        r.run("dynamics", signal(5), max_insns=3_000_000)
        hung = False
    except RuntimeError:
        hung = True
    check(hung, "its first block never returns")

    s = Rig(axf, dict(r.rt.sd.files))         # the next start, with the card as the hang left it
    check(s.safe(), "the next start is in safe mode")
    check(s.install("dynamics", "GuardHang.ptmfx", project=True) == "skipped", "a project's plugin is skipped")
    check(s.install("dynamics", "GuardSlow.ptmfx") == "ok", "the user's pick is taken")
    s.ms = 4999
    s.update()
    check(MARKER in s.rt.sd.files and s.safe(), "4.999 s after it: the marker and safe mode stay")
    s.ms = 5000
    s.update()
    check(MARKER not in s.rt.sd.files and not s.safe(), "5 s after it: the marker gone, safe mode over")
    check(s.install("dynamics", "GuardSlow.ptmfx", project=True) == "ok", "and a project's plugin loads again")

    t = Rig(axf, card(folder, "GuardSlow"))
    check(not t.safe(), "a card with no marker: not in safe mode")
    t.install("dynamics", "GuardSlow.ptmfx")
    for _ in range(3):
        t.run("dynamics", signal(6))
    t.ms = 5000
    t.update()
    check(MARKER not in t.rt.sd.files, "a plugin that runs its 5 s takes the marker off")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    axf = Path(sys.argv[1])
    folder = Path(sys.argv[2]) if len(sys.argv) > 2 else CARD
    missing = [n for n in ("GuardSlow", "GuardOutput", "GuardGain", "GuardHang") if not (folder / f"{n}.ptmfx").exists()]
    if missing:
        print(f"{folder}: no {', '.join(missing)}; build them with tools/masterfx/device/build.sh")
        return 2
    time_guard(axf, folder)
    output_guard(axf, folder)
    gain_guard(axf, folder)
    marker(axf, folder)
    print(f"{passed} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
