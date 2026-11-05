#!/usr/bin/env python3
"""The lockup recorder on the real boot path: does a lockup get written down, and does it restart?

    python tools/emu/test_lockup_recorder.py <diagnostic firmware.axf>

Built with CUSTOM_DEFINES=-DMT_LOCKUP_RECORDER=1. Each case makes what a device would do happen by hand:
the SysTick interrupt keeps firing, the main loop stops counting, or a fault handler is entered with a
fault's stack frame. What this can't show is whether a real fault or stall on the device reaches them, or
whether SNVS and SDRAM really survive that reset.
"""
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_MEM_WRITE

sys.path.insert(0, str(Path(__file__).parent))
from boot_rig import BootRig  # noqa: E402

AIRCR = 0xE000ED0C
CFSR = 0xE000ED28
BFAR = 0xE000ED38
IABR0 = 0xE000E300
SNVS_LPGPR = 0x400D4100
FRAME_SP = 0x20050000
STALLED_PC = 0x60012344
STALLED_LR = 0x60012001
RETURN_ADDRESS = 0x60023457
PLAY = 23

failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    failures += 0 if ok else 1


class Rig(BootRig):
    def __init__(self, axf):
        super().__init__(axf)
        m = self.m
        self.resets = 0

        def on_reset(uc, access, address, size, value, user):
            if address == AIRCR and value & 4:
                self.resets += 1
                uc.emu_stop()
        m.uc.hook_add(UC_HOOK_MEM_WRITE, on_reset, begin=AIRCR, end=AIRCR + 3)

    def to_pattern_screen(self):
        updates = [0]

        def on_update(mm):
            updates[0] += 1
        self.watch(self.m.addr("_ZN14cPatternEditor6updateEv"), on_update)
        self.fill_stack()
        self.boot()
        for _ in range(3000):
            self.loop()
            self.interrupt()
            self.beat()
            if updates[0] > 5:
                return True
        return False

    def beat(self):
        self.m.call(self.m.addr("mtLockupBeat"))

    def frame(self):
        """An exception frame as the CPU stacks it, then a return address further up."""
        words = [0, 0, 0, 0, 0, STALLED_LR, STALLED_PC, 0x61000000, 0, 0, RETURN_ADDRESS]
        self.m.write(FRAME_SP, struct.pack(f"<{len(words)}I", *words))

    def tick(self):
        """SysTick, on the interrupted code's stack."""
        try:
            self.m.call(self.m.addr("SysTick_Handler"), stack=FRAME_SP, max_insns=3_000_000)
        except RuntimeError:
            pass        # the spin after a reset

    def log(self):
        return self.sd.files.get("/mfx_log.txt", b"").decode()

    def carry_over(self, old):
        """What survives a reset: SDRAM (record) and SNVS, and the card."""
        name = next(n for n, y in self.syms.items() if n.endswith("6recordE") and y.addr >= 0x80000000)
        at, size = self.syms[name].addr, self.syms[name].size
        self.m.write(at, bytes(old.m.read(at, size)))
        self.m.write(SNVS_LPGPR, bytes(old.m.read(SNVS_LPGPR, 32)))
        self.sd.files.update(old.sd.files)


def main():
    axf = sys.argv[1]
    r = Rig(axf)
    ok = r.to_pattern_screen()
    check(ok and not r.failures, f"boots to the Pattern screen ({r.failures})")
    check(r.log() == "", "nothing logged on a clean start")

    # Normal running: the main loop beats, so no restart however long
    r.frame()
    for _ in range(4000):
        r.beat()
        r.tick()
    check(r.resets == 0, "a running main loop never restarts it")

    # A pause while stopped is allowed up to 20 s
    for _ in range(3000):
        r.tick()
    check(r.resets == 0, "3 s without the main loop while stopped: no restart")
    r.beat()
    r.tick()

    # One Play, as the unit would see it
    r.press(PLAY)
    r.loop()
    r.interrupt()
    r.release(PLAY)
    r.loop()
    for _ in range(50):
        r.beat()
        r.loop()
        r.interrupt()
    r.press(PLAY)
    r.loop()
    r.interrupt()
    r.release(PLAY)
    for _ in range(4):
        r.loop()
    log = r.log()
    print("     " + log.strip())
    check(log.startswith("play ") and "Audio interrupt" in log and "voices" in log, "Stop writes the Play's line")

    # The main loop stalls while playing, with the interrupt looking active
    r.press(PLAY)
    r.loop()
    r.interrupt()
    r.release(PLAY)
    r.loop()
    r.beat()
    r.tick()
    r.m.w32(IABR0, 1)
    for i in range(2999):
        r.tick()
    check(r.resets == 0, "2,999 ms stalled while playing: not yet")
    r.tick()
    check(r.resets == 1, "3,000 ms stalled while playing: restarts")

    # The next start finds it
    r2 = Rig(axf)
    r2.carry_over(r)
    r2.to_pattern_screen()
    for _ in range(4):
        r2.loop()
    log = r2.log()
    print("     " + log[-600:].strip().replace("\n", "\n     "))
    check("LOCKUP: main loop stalled, playing" in log, "the next start logs the stall")
    check(f"pc {STALLED_PC:08x}" in log and f"lr {STALLED_LR:08x}" in log, "with the interrupted code's pc and lr")
    check(f"{RETURN_ADDRESS:08x}" in log, "and the return addresses on the stack")
    check("Inside the audio interrupt then: yes" in log, "and that it was inside the audio interrupt")

    r3 = Rig(axf)
    r3.carry_over(r2)
    before = r3.log()
    r3.to_pattern_screen()
    for _ in range(4):
        r3.loop()
    check(r3.log() == before, "it's logged once, not at every start")

    # A fault
    r3.m.w32(CFSR, 0x8200)
    r3.m.w32(BFAR, 0x20280000)
    r3.frame()
    try:
        r3.m.call(r3.m.addr("BusFault_Handler"), stack=FRAME_SP, max_insns=3_000_000)
    except RuntimeError:
        pass
    check(r3.resets == 1, "a bus fault restarts instead of spinning")
    r4 = Rig(axf)
    r4.carry_over(r3)
    r4.to_pattern_screen()
    for _ in range(4):
        r4.loop()
    log = r4.log()
    print("     " + log[-500:].strip().replace("\n", "\n     "))
    check("LOCKUP: bus fault" in log and "cfsr 00008200" in log and "bfar 20280000" in log,
          "the next start logs the fault, its registers and address")
    print(f"{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
