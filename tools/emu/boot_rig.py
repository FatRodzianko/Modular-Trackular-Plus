#!/usr/bin/env python3
"""The firmware's own start-up and main loop under Unicorn, with the audio interrupt between loops.

    python tools/emu/boot_rig.py <firmware.axf> [project folder] [loops]

SoundRig starts only the synth and the audio engine. This runs what main() runs: startDisplay,
TrackerImpl::init, and then TrackerImpl::update in a loop, so the interface, the file manager and the
heap are the real ones. What's fake is the Platform: no screen, no codec, and a card served from memory.

Still not real: time (see docs/mt/SOUND_TEST_PLAN.md). The clock moves when the test moves it, so
overload can't show here.
"""
import argparse
import random
import struct
import sys
from pathlib import Path

from unicorn import UcError
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_BASEPRI, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK, UC_ARM_REG_R0, UC_ARM_REG_SP

sys.path.insert(0, str(Path(__file__).parent))
from machine import SCRATCH, STACK_TOP, STUB_RET  # noqa: E402
from sound_rig import BLOCK, BLOCK_MS, SoundRig  # noqa: E402

STACK_SIZE = 20480


class BootRig(SoundRig):
    rng = random.Random(1)
    masked = 0
    in_interrupt = False
    interrupt_allocs = 0

    def _fake_timer(self):
        """Timer's real slots: millis, micros, delay, clockCycles, clockCyclesPerUs, then the MIDI clock."""
        m = self.m
        vtable = m.alloc(4 * 32, 4)
        slots = [lambda mm: self.now & 0xFFFFFFFF,
                 lambda mm: (self.now * 1000) & 0xFFFFFFFF,
                 self._delay,
                 lambda mm: (self.now * 528000) & 0xFFFFFFFF,
                 lambda mm: 528]
        for i in range(32):
            m.w32(vtable + 4 * i, m.add_stub(slots[i] if i < len(slots) else (lambda mm: 0)))
        self.timer = m.alloc(16, 4)
        m.w32(self.timer, vtable)
        for name in ("_ZN13elapsedMillis5timerE", "_ZN13elapsedMicros5timerE",
                     "_ZN18elapsedMicrosAudio5timerE", "_ZN18elapsedMillisAudio5timerE"):
            if name in self.syms:
                m.w32(self.syms[name].addr, self.timer)

    def _delay(self, mm):
        self.now += mm.arg(1)
        return 0

    def _fake_platform(self):
        super()._fake_platform()
        m = self.m
        # DisplayDriver: the coprocessor is always ready, so the display code runs. Slot 11 is isCoProEmpty
        vtable = m.alloc(4 * 64, 4)
        idle = m.add_stub(lambda mm: 0)
        for i in range(64):
            m.w32(vtable + 4 * i, idle)
        m.w32(vtable + 4 * 11, m.add_stub(lambda mm: 1))
        self.display_driver = m.alloc(4096, 8)
        m.w32(self.display_driver, vtable)
        m.w32(self.display_driver + 4, 0)
        platform_vtable = m.r32(self.platform)
        m.w32(platform_vtable + 4 * 14, m.add_stub(lambda mm: self.display_driver))

        # SwitchGroup, for isButtonPressed. Slots: setHoldTime, isButtonPressed, setActionCallback
        self.held = set()
        vtable = m.alloc(4 * 8, 4)
        for i in range(8):
            m.w32(vtable + 4 * i, idle)
        m.w32(vtable + 4, m.add_stub(lambda mm: 1 if mm.arg(1) in self.held else 0))
        self.buttons_object = m.alloc(64, 4)
        m.w32(self.buttons_object, vtable)
        m.w32(platform_vtable + 4 * 8, m.add_stub(lambda mm: self.buttons_object))
        self._complete_card()

    def _complete_card(self):
        """What FakeSd leaves out and the real start-up and project load use."""
        m, sd = self.m, self.sd
        impl_vt = m.addr("_ZTV14FileSystemImpl") + 8
        slots = m.sym("_ZTV14FileSystemImpl").size // 4 - 2
        extra = {
            "isSdCardInserted": lambda mm: 1,
            "isAsyncReadBusy": lambda mm: 0,
            "pollAsyncRead": lambda mm: 1,
            "waitAsyncRead": lambda mm: 1,
            "setMountEventCallback": lambda mm: 0,
            "getFileAvailable": self._available,
            "seekFileCur": self._seek_cur,
            "seekFile": self._seek,
            "isFileOpen": lambda mm: 1 if mm.arg(1) in sd.handles else 0,
            "endFile": lambda mm: 0,
            "rename": self._rename,
            "removeDirWithFiles": lambda mm: 1,
            "removeOnlyDirFiles": lambda mm: 1,
            "countContainedFiles": lambda mm: 0,
            "enableFastSeek": lambda mm: 0,
            "createFilesList": lambda mm: 0,
            "createInstrumentsList": lambda mm: 0,
            # a card answers 0 for a file that isn't there
            "getFileSizeEPKc": lambda mm: len(sd.files.get(mm.cstr(mm.arg(1)), b"")),
            "writeFile": self._write,
        }
        for i in range(slots):
            target = m.r32(impl_vt + 4 * i) & ~1
            name = next((n for n, y in m.elf.symbols.items()
                         if y.addr & ~1 == target and n.startswith("_ZN14FileSystemImpl")), "?")
            for key, func in extra.items():
                if key in name:
                    m.w32(sd.vtable + 4 * i, m.add_stub(func))

    def _write(self, mm):
        """At the file's position, creating it: FakeSd only appends to files opened as new."""
        h = self.sd.handles[mm.arg(3)]
        data = bytes(mm.read(mm.arg(1), mm.arg(2)))
        old = self.sd.files.get(h[0], b"")
        if h[1] == 2:           # kWriteAppend
            h[2] = len(old)
        self.sd.files[h[0]] = old[:h[2]] + data + old[h[2] + len(data):]
        h[2] += len(data)
        return len(data)

    def _available(self, mm):
        h = self.sd.handles.get(mm.arg(1))
        return len(self.sd.files.get(h[0], b"")) - h[2] if h else 0

    def _seek_cur(self, mm):
        h = self.sd.handles.get(mm.arg(2))
        if h:
            h[2] += struct.unpack("<i", struct.pack("<I", mm.arg(1)))[0]
        return 1

    def _seek(self, mm):
        h = self.sd.handles.get(mm.arg(2))
        if h:
            h[2] = mm.arg(1)
        return 1

    def _rename(self, mm):
        old, new = mm.cstr(mm.arg(1)), mm.cstr(mm.arg(2))
        if old in self.sd.files:
            self.sd.files[new] = self.sd.files.pop(old)
            return 1
        return 0

    def _init_synth(self):
        self.tracker = self.m.call(self.m.addr("_ZN11TrackerImpl11getInstanceEv"))
        self.synth = self.m.call(self.m.addr("_ZN11TrackerImpl8getSynthEv"), self.tracker)

    def _init_engine(self):
        self.engine = self.m.addr("engine")

    def boot(self):
        """startDisplay, then TrackerImpl::init. After this, a failed malloc would hang a device."""
        m = self.m
        start = m.call(m.addr("_ZN11TrackerImpl19getStartDisplayFuncEv"), self.tracker)
        m.call(start, self.platform, max_insns=500_000_000)
        m.call(m.addr("_ZN11TrackerImpl4initEP8Platform"), self.tracker, self.platform, max_insns=2_000_000_000)
        # Profiler::init takes all the heap on purpose, to measure it. That isn't a failure.
        self.failures = [f for f in self.failures if not f.startswith("heap over")]
        self.booted = True

    def loop(self, max_insns=300_000_000):
        self.m.call(self.m.addr("_ZN11TrackerImpl6updateEv"), self.tracker, max_insns=max_insns)

    def loop_preempted(self, shortest, longest):
        """A main loop with the audio interrupt firing in the middle of it, every shortest to longest
        instructions, on the same stack as a device takes it. Returns how many fired."""
        m, uc = self.m, self.m.uc
        uc.reg_write(UC_ARM_REG_R0, self.tracker)
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP - 0x100)
        uc.reg_write(UC_ARM_REG_LR, STUB_RET | 1)
        pc = m.addr("_ZN11TrackerImpl6updateEv") | 1
        fired = 0
        while True:
            try:
                uc.emu_start(pc, STUB_RET, count=self.rng.randint(shortest, longest))
            except UcError as e:
                raise RuntimeError(f"emulation stopped at pc={uc.reg_read(UC_ARM_REG_PC):#010x}: {e}") from e
            now = uc.reg_read(UC_ARM_REG_PC)
            if now == STUB_RET:
                return fired
            pc = now | 1
            # A device takes it only when the firmware lets it: AudioNoInterrupts masks it in the NVIC,
            # and __disable_irq everything. Held back, it fires at the next stop, as a pending one would.
            if self.audio_masked or uc.reg_read(UC_ARM_REG_PRIMASK) or uc.reg_read(UC_ARM_REG_BASEPRI):
                self.masked += 1
                continue
            state = uc.context_save()
            # the frame the CPU stacks, with the FP registers, and 8-byte aligned
            self.interrupt(stack=(uc.reg_read(UC_ARM_REG_SP) - 0x68) & ~7)
            uc.context_restore(state)
            fired += 1

    def fill_stack(self):
        """As ResetISR does for Profiler::maxStackBytes, so the deepest stack can be read after."""
        self.m.write(STACK_TOP - STACK_SIZE, bytes([0xAA]) * (STACK_SIZE - 0x400))

    def max_stack(self):
        data = bytes(self.m.read(STACK_TOP - STACK_SIZE, STACK_SIZE))
        return STACK_SIZE - (len(data) - len(data.lstrip(bytes([0xAA]))))

    def press(self, button):
        """A button press as the hardware reports one (hardware.cpp hands these to the interface)."""
        self.held.add(button)
        self.m.call(self.m.addr("_Z12onButtonPushh"), button, max_insns=300_000_000)

    def release(self, button):
        self.held.discard(button)
        self.m.call(self.m.addr("_Z15onButtonReleaseh"), button, max_insns=300_000_000)

    def interrupt(self, stack=None):
        self.in_interrupt = True
        try:
            self.m.call(self.m.addr("_ZN11TrackerImpl18updateAudioBuffersEPvS0_i"), self.tracker, self.left,
                        self.right, BLOCK, max_insns=100_000_000, stack=stack)
        finally:
            self.in_interrupt = False
        self.now += BLOCK_MS
        return (struct.unpack(f"<{BLOCK}h", self.m.read(self.left, 2 * BLOCK)),
                struct.unpack(f"<{BLOCK}h", self.m.read(self.right, 2 * BLOCK)))

    def free_heap(self):
        """What Config shows with /.develop: mallinfo().fordblks."""
        if not hasattr(self, "_mallinfo"):
            self._mallinfo = self.m.alloc(64, 4)
        self.m.call(self.m.addr("mallinfo"), self._mallinfo)
        return self.m.r32(self._mallinfo + 4 * 8)

    def open_project(self, name, parent="Projects"):
        """The call the Project screen's Open makes. Its work happens in the main loop after this."""
        m = self.m
        path, title = m.alloc(64, 4), m.alloc(64, 4)
        m.write(path, parent.encode() + bytes(1))
        m.write(title, name.encode() + bytes(1))
        fm = m.addr("newFileManager")
        return m.call(m.addr("_ZN12cFileManager11openProjectEPcS0_"), fm, path, title, max_insns=100_000_000)

    def card_from(self, folder, name, workspace=True):
        """A card with the project in /Projects/<name> and, unless told not to, in the Workspace too."""
        sd = self.sd
        folder = Path(folder)
        sd.dirs |= {"Projects", f"Projects/{name}", f"Projects/{name}/patterns", f"Projects/{name}/instruments",
                    "Workspace", "Workspace/patterns", "Workspace/instruments", "Workspace/samples"}
        for f in folder.rglob("*"):
            if f.is_file():
                rel = f.relative_to(folder).as_posix()
                data = f.read_bytes()
                sd.files[f"Projects/{name}/{rel}"] = data
                if not workspace:
                    continue
                if rel.startswith("instruments/"):     # "3 CH 707.pti" in a project, numbered in the Workspace
                    rel = f"instruments/instrument_{int(f.name.split()[0]):02d}.pti"
                sd.files[f"Workspace/{rel}"] = data


MARKER = "/SynthFX/.starting"
SKIPPED = 7                     # plugins::Status::skipped


def card_files(rig, folder):
    """The files of folder in /SynthFX/ on the card, as a user copies plugins there"""
    for f in sorted(Path(folder).iterdir()):
        if f.is_file():
            rig.sd.files[f"/SynthFX/{f.name}"] = f.read_bytes()


def second_start(axf, first, blocks):
    """A start from the card the first run left, as the lockup recorder's restart: CE opens the
    Workspace, Synth FX finds the marker, so the project's plugins aren't made (safe mode), and
    Play plays. Returns 0 if so."""
    r = BootRig(axf)
    r.sd.files, r.sd.dirs = dict(first.sd.files), set(first.sd.dirs)
    m = r.m
    pattern_updates, notes = [0], [0]

    def bump(counter):
        def f(mm):
            counter[0] += 1
        return f
    r.watch(m.addr("_ZN14cPatternEditor6updateEv"), bump(pattern_updates))
    r.watch(m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"), bump(notes))
    status = m.addr("_ZN4mods7synthfx10slotStatusEii")
    instance = m.addr("_ZN4mods7synthfx12slotInstanceEii")
    try:
        r.fill_stack()
        r.boot()
        for i in range(3000):
            r.loop()
            r.interrupt()
            if pattern_updates[0] > 5:
                break
        before, quiet = len(r.sd.calls), 0
        for i in range(20000):          # the Workspace's load, then the mods' main loop
            r.loop()
            r.interrupt()
            quiet = quiet + 1 if len(r.sd.calls) == before else 0
            before = len(r.sd.calls)
            if quiet > 200:
                break
        slots = m.addr("_ZN4mods7synthfx5slotsE")
        files = [m.read(slots + 100 * i, 48).split(b"\0")[0].decode() for i in range(6)]
        statuses = [m.call(status, i // 2, i % 2) & 0xFF for i in range(6)]
        made = [m.call(instance, i // 2, i % 2) < 0x80000000 for i in range(6)]
        print("second start: Pattern screen up, slots", files, "statuses", statuses, "made", made,
              "marker", MARKER in r.sd.files, "free heap", r.free_heap(), flush=True)
        PLAY = 23
        r.press(PLAY)
        r.loop()
        r.interrupt()
        r.release(PLAY)
        peak = 0
        for i in range(blocks):
            r.loop()
            left, right = r.interrupt()
            peak = max(peak, max(map(abs, left)), max(map(abs, right)))
        print(f"second start: {blocks} blocks played, notes {notes[0]}, peak {peak}, failures {r.failures}",
              flush=True)
    except RuntimeError as e:
        print("second start STOPPED", e, "failures", r.failures)
        return 1
    hung = [i for i, f in enumerate(files) if f == "GuardHang.ptsfx"]
    if not hung or any(made[i] or statuses[i] != SKIPPED for i in hung) or notes[0] == 0 or r.failures:
        print("FAILED: the second start isn't in safe mode with the project's plugin skipped, or doesn't play")
        return 1
    return 0


def main():
    import bisect

    ap = argparse.ArgumentParser()
    ap.add_argument("axf")
    ap.add_argument("project", nargs="?")
    ap.add_argument("--blocks", type=int, default=300)
    ap.add_argument("--preempt", default="", help="shortest,longest instructions between interrupts in the main loop")
    ap.add_argument("--preempt-play", default="", help="the same, once Play is pressed; a main loop is short then")
    ap.add_argument("--expect-tracklen", action="store_true",
                    help="fail unless the project's lengths are what the audio interrupt played")
    ap.add_argument("--expect-synthfx", action="store_true",
                    help="fail unless the project's Synth FX slots loaded, their plugins were made and the "
                         "insert ran on every block")
    ap.add_argument("--card-files", type=Path, help="a folder whose files go in /SynthFX/ on the card")
    ap.add_argument("--expect-hang", action="store_true",
                    help="the project's GuardHang plugin never returns: fail unless the Open hangs inside the "
                         "plugins' code with the start marker on the card and no file open, and a second start "
                         "from that card is in safe mode (the project's plugin not made) and plays")
    args = ap.parse_args()
    r = BootRig(args.axf)
    m = r.m
    if args.card_files:
        card_files(r, args.card_files)
    funcs = sorted((s.addr & ~1, n) for n, s in r.syms.items() if s.is_func and s.addr)
    addrs = [a for a, _ in funcs]

    def name(a):
        j = bisect.bisect_right(addrs, a & ~1) - 1
        return funcs[j][1] if j >= 0 else "?"

    def where():
        print("pc", hex(m.uc.reg_read(UC_ARM_REG_PC)), name(m.uc.reg_read(UC_ARM_REG_PC)))
        print("lr", hex(m.uc.reg_read(UC_ARM_REG_LR)), name(m.uc.reg_read(UC_ARM_REG_LR)))
        sp = m.uc.reg_read(UC_ARM_REG_SP)
        seen = []
        for k in range(0, 1600, 4):
            try:
                v = m.r32(sp + k)
            except Exception:
                break
            if v & 1 and (0x60000000 <= v < 0x61000000 or 0x100 < v < 0x20000):
                seen.append(name(v))
        print("  ", list(dict.fromkeys(seen))[:12])

    fired = [0]
    preempt = [tuple(int(x) for x in args.preempt.split(",")) if args.preempt else None]

    def text(symbol, part):
        """A std::string from one of Synth's open getters."""
        result = SCRATCH + 0xFF0000
        r.m.call(r.m.addr(symbol), result, r.synth, part)
        return r.m.read(r.m.r32(result), r.m.r32(result + 4)).decode()

    def step():
        """One main loop and at least one audio block."""
        if preempt[0]:
            fired[0] += r.loop_preempted(*preempt[0])
        else:
            r.loop()
        return r.interrupt()

    if args.project:
        r.card_from(args.project, "MFX test", workspace=False)
    pattern_updates = [0]
    played = [0]
    notes = [0]

    def bump(counter):
        def f(mm):
            counter[0] += 1
        return f
    r.watch(m.addr("_ZN14cPatternEditor6updateEv"), bump(pattern_updates))
    r.watch(m.addr("_ZN9Sequencer11playPatternEv"), bump(played))
    r.watch(m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"), bump(notes))
    fx_runs = [0]
    if args.expect_synthfx:
        r.watch(m.addr("_ZN4mods7synthfx3runERKNS0_6InsertEiPfS4_b"), bump(fx_runs))

    def on_alloc(mm):
        if r.in_interrupt:
            r.interrupt_allocs += 1
    for name in ("malloc", "_malloc_r", "calloc", "_calloc_r", "realloc", "_realloc_r"):
        if name in r.syms and r.syms[name].addr:
            r.watch(r.syms[name].addr, on_alloc)
    active = lambda: None
    if "_ZN4mods8masterfx12activeReverbE" in r.syms:
        active = lambda: (m.r8(m.addr("_ZN4mods8masterfx12activeReverbE")),
                          m.r8(m.addr("_ZN4mods8masterfx11activeDelayE")))
    PLAY = 23
    try:
        r.fill_stack()
        r.boot()
        print("boot done: failures", r.failures, "free heap", r.free_heap(), "stack", r.max_stack(), flush=True)
        for i in range(3000):
            step()
            if pattern_updates[0] > 5:
                print("Pattern screen up after", i, "loops; free heap", r.free_heap(), "stack", r.max_stack(),
                      "interrupts inside main", fired[0], flush=True)
                break
        else:
            print("Pattern screen never came up", flush=True)
            return 1
        if args.project:
            print("open:", r.open_project("MFX test"), flush=True)
            before = len(r.sd.calls)
            quiet = 0
            for i in range(20000):
                step()
                quiet = quiet + 1 if len(r.sd.calls) == before else 0
                before = len(r.sd.calls)
                if quiet > 200:
                    break
            print("parts:", [(text("_ZN5Synth12getSynthNameB5cxx11Ei", part), text("_ZN5Synth12getPatchNameB5cxx11Ei", part))
                             for part in range(3)], flush=True)
            print("opened after", i, "loops; free heap", r.free_heap(), "stack", r.max_stack(), "effects", active(),
                  "open files", len(r.sd.handles), "failures", r.failures,
                  "interrupts inside main", fired[0], flush=True)
        if args.preempt_play:
            preempt[0] = tuple(int(x) for x in args.preempt_play.split(","))
        r.press(PLAY)
        step()
        r.release(PLAY)
        print("Play pressed: play calls", played[0], flush=True)
        peak = 0
        for i in range(args.blocks):
            left, right = step()
            peak = max(peak, max(map(abs, left)), max(map(abs, right)))
            if i % 10 == 0 or i == args.blocks - 1:  # often enough that a slow run can be told from a stuck one
                print(f"block {i}: notes {notes[0]} pool {r.pool_peak()} peak {peak} free heap {r.free_heap()}"
                      f" stack {r.max_stack()} failures {r.failures} effects {active()}"
                      f" interrupts inside main {fired[0]}", flush=True)
    except RuntimeError as e:
        if args.expect_hang:
            pc = m.uc.reg_read(UC_ARM_REG_PC)
            pool = max((s for n, s in r.syms.items() if "synthfx" in n and "codePool" in n), key=lambda s: s.size)
            inside = pool.addr <= pc < pool.addr + pool.size
            marker = MARKER in r.sd.files
            workspace = [k for k in r.sd.files if k.endswith("Workspace/mods.bin")]
            print("hung:", e, "inside the plugins' code", inside, "marker", marker, "open files",
                  len(r.sd.handles), "Workspace mods.bin", workspace, flush=True)
            if not (inside and marker and not r.sd.handles and workspace):
                print("FAILED: not the hang the start marker is for")
                return 1
            return second_start(args.axf, r, args.blocks)
        print("STOPPED", e, "failures", r.failures)
        where()
        return 1
    if args.expect_hang:
        print("FAILED: the project's plugin never hung")
        return 1
    print("sd unexpected", sorted(set(r.sd.unexpected))[:12], "allocations in the interrupt", r.interrupt_allocs)
    if args.expect_tracklen:
        lengths = bytes(m.read(m.addr("_ZN4mods8tracklen13activeLengthsE"), 16))
        print("track lengths in the interrupt's copy:", list(lengths), "notes", notes[0], flush=True)
        if not any(lengths) or notes[0] == 0:
            print("FAILED: the lengths aren't in play")
            return 1
    if args.expect_synthfx:
        # Effect Phaser and Distortion on every part, as make_masterfx_test_project.SYNTHFX_WORST (a
        # version 1 record): converted on load to the Distortion in FX1 and the Phaser in FX2, each
        # made as a plugin from the card; each block runs the insert on every part sounding, so at
        # least one run a block
        slots = m.addr("_ZN4mods7synthfx5slotsE")
        # a Slot is 100 bytes (padded), its file name first
        files = [m.read(slots + 100 * i, 48).split(b"\0")[0].decode() for i in range(6)]
        made = [m.call(m.addr("_ZN4mods7synthfx12slotInstanceEii"), i // 2, i % 2) < 0x80000000 for i in range(6)]
        print("Synth FX slots:", files, "made", made, "insert runs", fx_runs[0],
              "allocations in the interrupt", r.interrupt_allocs, flush=True)
        if files != ["Distortion.ptsfx", "Phaser.ptsfx"] * 3 or not all(made) or fx_runs[0] < args.blocks or \
                r.interrupt_allocs:
            print("FAILED: Synth FX isn't in play")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
