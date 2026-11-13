# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The whole firmware on the simulator's virtual clock (docs/mt/SIMULATOR_STAGE1.md).

Built on BootRig's start-up pieces (newlib stubs, static constructors, FakeSd's files, the guard), with:
- two Unicorn instances on one RAM: the main loop counted instruction by instruction, the interrupts not
  (1.1);
- the platform built from the .axf's vtables, a model registered for every slot (1.5, vtables.py);
- the device's own main loop as a Thumb trampoline, so slices run straight through pass ends (1.1);
- boot as the device boots, with the audio interrupt firing during TrackerImpl::init (1.7);
- idle passes skipped (1.10).

The closed libraries run as a black box: no hook inside them (the guard refuses one), and nothing they do
is traced or reported. The main loop's instructions in them are counted with the rest (D2), never logged.
"""
from __future__ import annotations

import ctypes
import hashlib
import mmap
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from boot_rig import STACK_SIZE, BootRig  # noqa: E402
from machine import SCRATCH, STACK_TOP, STUB_BASE, STUB_RET  # noqa: E402
from sim_kernel import BLOCK, Kernel, Log, Params, PLACEHOLDERS  # noqa: E402
from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS, UC_MODE_THUMB,  # noqa: E402
                     Uc, UcError)
from unicorn import arm_const as A  # noqa: E402
from vtables import CLASSES, Model, Unmodelled, resolve  # noqa: E402

# The device's while(1), in the stub region's style (built from this source with the WSL toolchain):
#   tramp: ldr r3, =pass_start ; blx r3 ; cbz r0, 2f
#          mov r12, r0 ; mov r0, r1 ; mov r1, r2 ; mov r2, r3 ; blx r12 ; b tramp     @ an input handler, or a boot step
#   2:     ldr r0, =tracker ; ldr r3, =TrackerImpl::update ; blx r3 ; b tramp
#   .word pass_start, tracker, update                                                  @ at +0x20, +0x24, +0x28
TRAMPOLINE = bytes.fromhex("074b984730b18446084611461a46e047fff7f6bf0348044b9847fff7f1bf00bf")
UNTIL = SCRATCH + 0xFFFFF0                  # never reached: main-loop slices end by count
COPIED = ([getattr(A, f"UC_ARM_REG_R{i}") for i in range(13)] + [getattr(A, f"UC_ARM_REG_S{i}") for i in range(32)]
          + [A.UC_ARM_REG_FPSCR])
ITCM = (0x00000000, 0x20000)
STACK_LO = STACK_TOP - STACK_SIZE
IRQ_HUNG_S = 120                            # an interrupt that takes two wall minutes has hung
PLAY = 23
LOGGED = {  # open functions whose entries go in the log (1.9); missing ones are inlined in this build
    "play": "_ZN9Sequencer4playEh",
    "playPattern": "_ZN9Sequencer11playPatternEh",
    "stop": "_ZN9Sequencer4stopENS_11StopContextE",
    "loadWorkspacePattern": "_ZN12cFileManager20loadWorkspacePatternEh",
    "startRequestedPatternLoad": "_ZN12cFileManager25startRequestedPatternLoadEv.part.0",
    "loadPatternFromWorkspace": "_ZN12cFileManager24loadPatternFromWorkspaceEh",
    "switchRamPatternsNow": "_ZN9Sequencer20switchRamPatternsNowEv",
}
NOTE_ON = "_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"


def make_twin(uc):
    """A second instance on the same RAM (1.1). Every region of uc moves to host memory that the twin maps
    too; from then on every hook added to uc is added to the twin as well. Returns the twin, the maps
    (begin, size, perms, ptr, buffer) and uc's own hook_add, for hooks only the main instance gets."""
    maps = []
    for begin, end, perms in list(uc.mem_regions()):
        size = end - begin + 1
        buf = mmap.mmap(-1, size)
        for off in range(0, size, 1 << 24):
            n = min(1 << 24, size - off)
            chunk = bytes(uc.mem_read(begin + off, n))
            if chunk.count(0) != n:
                buf[off:off + n] = chunk
        uc.mem_unmap(begin, size)
        ptr = ctypes.addressof(ctypes.c_char.from_buffer(buf))
        uc.mem_map_ptr(begin, size, perms, ptr)
        maps.append((begin, size, perms, ptr, buf))
    uc2 = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for begin, size, perms, ptr, _buf in maps:
        uc2.mem_map_ptr(begin, size, perms, ptr)
    local_add, main_del = uc.hook_add, uc.hook_del
    twins = {}

    def mirrored(htype, callback, user_data=None, begin=1, end=0, arg1=0, arg2=0):
        h = local_add(htype, callback, user_data, begin, end, arg1, arg2)
        twins[h] = uc2.hook_add(htype, callback, user_data, begin, end, arg1, arg2)
        return h

    def mirrored_del(h):
        main_del(h)
        if h in twins:
            uc2.hook_del(twins.pop(h))
    uc.hook_add, uc.hook_del = mirrored, mirrored_del
    return uc2, maps, local_add


def copy_interrupted(uc, uc2):
    """The interrupted code's registers into the twin, so an ISR that saves them saves the right values; and
    the stack pointer below the interrupted one, past the frame the CPU stacks with the FP registers,
    8-byte aligned. Returns that SP."""
    for reg in COPIED:
        uc2.reg_write(reg, uc.reg_read(reg))
    return (uc.reg_read(A.UC_ARM_REG_SP) - 0x68) & ~7


def pure(func):
    """A stub whose answer depends only on the clock and the inputs, and which changes nothing (1.5)."""
    def f(mm):
        return func(mm)
    f.sim_pure = True
    return f


class Sim(BootRig):
    """BootRig's machine on the virtual clock. sim.kernel is the clock; sim.run(seconds) runs the firmware."""

    def __init__(self, axf, params: Params | None = None, log: Log | None = None):
        self.p = params or Params()
        self.kernel = Kernel(self.p, log)
        self.invariants = []           # broken rules: a card or display call in an interrupt
        self.midi_clock = None
        self.copro_busy_until = 0
        self.booted = False
        self.boot_step = 0
        self.notes = 0
        self.blocks = 0
        self.peak = 0
        self.audio = hashlib.sha256()
        self._wrote = self._impure = self._input_in_pass = False
        self._ignore_next = self._skip_requested = False
        self._last = (0, 0, 0)
        self._dirty = set()            # indexes into self._code the main loop wrote since the last interrupt
        self.wall = {"main": 0.0, "in": 0.0, "out": 0.0}
        self.flushes = 0
        self._stopped = None
        self._irq_started = None
        super().__init__(axf)
        self._finish()

    # -- two instances on one RAM (1.1) -----------------------------------------------------------------
    def _stub_newlib(self):
        """The first thing SoundRig does after mapping memory: the twin goes in before any stub or hook."""
        self._make_twin()
        super()._stub_newlib()

    def _make_twin(self):
        m = self.m
        uc = m.uc
        self.uc2, self._maps, self._local_hook_add = make_twin(uc)
        self.uc2.hook_add(UC_HOOK_MEM_INVALID, m._on_invalid)     # Machine added this one to the main instance
        self.uc_main = uc

        # every Python stub says whether it's pure; a call to one that isn't makes the pass work (1.10)
        #
        # Few hooks: with count=, Unicorn checks every code hook on every instruction (no hook: about 250 M
        # instructions a second; 10: 23 M; 400: 1 M). So all the Python stubs share one code hook over the
        # stub region, and stubbed firmware functions and watches are block hooks on the main instance,
        # which cost about nothing there. The twin, which never counts, gets them as one-address code hooks
        # instead: there a block hook costs a check on every block, and a code hook nothing.
        stubs = {}

        def add_stub(func):
            a = m._stub_next
            m._stub_next += 4
            assert a < STUB_RET, "out of stub slots"
            uc.mem_write(a, struct.pack("<H", 0x4770))          # bx lr
            stubs[a] = self._tracked(func)
            return a | 1

        def dispatch(u, address, size, user):
            f = stubs.get(address)
            if f is not None:
                r = f(m)
                if r is not None:
                    u.reg_write(A.UC_ARM_REG_R0, r & 0xFFFFFFFF)
        uc.hook_add(UC_HOOK_CODE, dispatch, begin=STUB_BASE, end=STUB_RET - 1)

        def stub_function(addr, func):
            """At a function's entry, which always starts a translated block: the stub, then return."""
            f, target = self._tracked(func), addr & ~1

            def hook(u, address, size, user):
                if address == target:
                    r = f(m)
                    if r is not None:
                        u.reg_write(A.UC_ARM_REG_R0, r & 0xFFFFFFFF)
                    u.reg_write(A.UC_ARM_REG_PC, u.reg_read(A.UC_ARM_REG_LR))
            self._entry_hook(target, hook)
        m.add_stub, m.stub_function = add_stub, stub_function

        # code the interrupt runs from RAM, which the main loop may write: the twin's translations of it
        # must go (ITCM's audio code, and Synth FX's plugin pool)
        self._code = [ITCM]
        pools = [s for n, s in self.syms.items() if "synthfx" in n and "codePool" in n and s.size]
        if pools:
            pool = max(pools, key=lambda s: s.size)
            self._code.append((pool.addr & ~1, pool.size))
        main_write = uc.mem_write

        def write(address, data):
            for i, (lo, n) in enumerate(self._code):
                if lo < address + len(data) and address < lo + n:
                    self._dirty.add(i)
            return main_write(address, data)
        uc.mem_write = write

    def watch(self, addr, func):
        """func(machine) on entry to an open function, as SoundRig.watch, but a block hook (see _make_twin)."""
        a = addr & ~1
        self.guard.check(a, a)
        return self._entry_hook(a, lambda uc, x, s, u: func(self.m) if x == a else None)

    def _entry_hook(self, target, callback):
        """callback at a function's entry: a block hook on the main instance, a code hook on the twin."""
        self.uc2.hook_add(UC_HOOK_CODE, callback, begin=target, end=target)
        return self._local_hook_add(UC_HOOK_BLOCK, callback, begin=target, end=target)

    def _tracked(self, func):
        is_pure = getattr(func, "sim_pure", False)
        if is_pure:
            return func

        def wrapped(mm):
            if not self.in_interrupt:
                self._impure = True
            return func(mm)
        return wrapped

    def _watch_writes(self):
        """The main instance's writes outside its own stack (1.10). The peripherals aren't watched: they
        read as 0 whatever is written. Closed ranges in ITCM are left out of the watch."""
        lo_itcm, n_itcm = ITCM
        ranges = []
        closed = sorted((a, b) for a, b, _ in self.guard.ranges if a < lo_itcm + n_itcm)
        at = lo_itcm
        for a, b in closed:
            if a > at + 32:                 # smaller gaps between closed symbols are alignment padding: one
                ranges.append((at, a - 1))  # Unicorn hook per gap made writes about 15 % slower
            at = max(at, b)
        if at < lo_itcm + n_itcm:
            ranges.append((at, lo_itcm + n_itcm - 1))
        ranges += [(0x20000000, STACK_LO - 1), (0x20200000, 0x20207FFF), (0x80000000, 0x83FFFFFF),
                   (SCRATCH, SCRATCH + 0xFFFFFF)]
        code = self._code
        dirty = self._dirty

        def on_write(uc, access, address, size, value, user):
            self._wrote = True
            self.write_calls += 1
            if address < 0x20000:
                dirty.add(0)
            elif len(code) > 1 and code[1][0] <= address < code[1][0] + code[1][1]:
                dirty.add(1)
        self.write_calls = 0
        self.write_ranges = ranges
        self.write_hooks = [self._local_hook_add(UC_HOOK_MEM_WRITE, on_write, begin=lo, end=hi) for lo, hi in ranges]

    # -- the platform, by name (1.5) ----------------------------------------------------------------------
    def _object(self, key, models, size=4096):
        m = self.m
        found = resolve(m.elf, CLASSES[key], models)
        vt = m.alloc(4 * len(found), 4)
        for slot, model in found:
            func = model.func
            if model.pure:
                func = pure(func)
            m.w32(vt + 4 * slot.index, m.add_stub(func))
        obj = m.alloc(size, 8)
        m.w32(obj, vt)
        return obj

    def wait(self, ns, kind):
        if ns > 0 and not self.in_interrupt:
            self._impure = True
        self.kernel.wait(ns, kind)

    def _fake_timer(self):
        k, p = self.kernel, self.p

        def fine(value):
            k.fine_read = True
            return value & 0xFFFFFFFF

        def schedule(mm):
            self.midi_clock = (mm.arg(1), mm.arg(2))
            k.log.write({"vt": k.now(), "ctx": k.context, "event": "MIDI clock timer asked for: not modelled "
                         "in stage 1", "us": mm.arg(1)})
            return 0

        def cancel(mm):
            self.midi_clock = None
            return 0
        models = {
            "millis": Model(lambda mm: (k.now() // 1_000_000) & 0xFFFFFFFF, pure=True),
            "micros": Model(lambda mm: fine(k.now() // 1000), pure=True),
            "delay": Model(lambda mm: self.wait(mm.arg(1) * 1_000_000, "delay") or 0),
            "clockCycles": Model(lambda mm: fine(k.now() * p.cpu_mhz // 1000), pure=True),
            "clockCyclesPerUs": Model(lambda mm: p.cpu_mhz, pure=True),
            "scheduleMidiClockUs": Model(schedule),
            "cancelMidiClock": Model(cancel),
        }
        self.timer = self._object("Timer", models, size=16)
        for name in ("_ZN13elapsedMillis5timerE", "_ZN13elapsedMicros5timerE",
                     "_ZN18elapsedMicrosAudio5timerE", "_ZN18elapsedMillisAudio5timerE"):
            if name in self.syms:
                self.m.w32(self.syms[name].addr, self.timer)

    def _fake_platform(self):
        m, k, p = self.m, self.kernel, self.p

        def idle(why, value=0):
            return Model(lambda mm: value, pure=True, why=why)

        def unexpected(name):
            def f(mm):
                self.failures.append(f"{name} called")
                mm.uc.emu_stop()
                self._stopped = name
                return 0
            return Model(f)

        # buttons and pads (SwitchGroup); the rig calls hardware.cpp's onButtonPush itself
        self.held = set()
        switch = lambda pressed: {  # noqa: E731
            "setHoldTime": idle("hold times are hardware.cpp's"),
            "isButtonPressed": Model(pressed, pure=True),
            "setActionCallback": idle("inputs arrive through onButtonPush, as hardware.cpp delivers them"),
        }
        self.buttons_object = self._object("Buttons", switch(lambda mm: 1 if mm.arg(1) in self.held else 0))
        pads = self._object("Pads", switch(lambda mm: 0))

        # the display driver (1.6): the coprocessor is busy for t_copro after each list
        def end_list(mm):
            self.copro_busy_until = k.now() + p.t_copro_ns
            if self.in_interrupt:
                self.invariants.append(f"display call in an interrupt at {k.now()}")
            return 0

        def await_empty(mm):
            self.wait(self.copro_busy_until - k.now(), "display")
            return 0
        display = {name: idle("no screen in stage 1") for name in (
            "setPowerState", "setBrightness", "setRotation", "getImgProps", "getDisplayListOffset",
            "IncCMDOffset", "WriteData32", "beginCoProListNoCheck", "writeDataRAMG", "writeFontRAMG",
            "readDataRAMG", "readBackDataRAMG", "sendString", "writeDataToCMD", "initStreamDma",
            "updateStreamDma", "stopStreamDma")}
        display.update({
            "endCoProList": Model(end_list),
            "awaitCoProEmpty": Model(await_empty, pure=True),        # impure only when it waits: wait() says so
            "beginCoProList": Model(await_empty, pure=True),         # API_LIB_BeginCoProList awaits first
            "isCoProEmpty": Model(lambda mm: 1 if k.now() >= self.copro_busy_until else 0, pure=True),
        })
        self.display_driver = self._object("DisplayDriver", display)

        midi = {name: idle("MIDI out isn't modelled in stage 1") for name in (
            "sendRealTime", "sendControlChange", "sendProgramChange", "sendAfterTouch", "sendPolyPressure",
            "sendPitchBend", "sendNoteOn", "sendNoteOff", "setReceiveActions")}
        midi.update({"isReadAvailable": idle("no MIDI in"), "read": idle("no MIDI in")})
        uart = self._object("UartMidi", dict(midi, init=idle("nothing to start"),
                                             setReceiveNoteOnAction=idle("no MIDI in")))
        usb_midi = self._object("UsbMidi", midi)
        objects = {
            "getCodecAudio": self._object("Codec", {n: idle("no codec") for n in (
                "init", "setInputType", "setHeadphoneSource", "setLineInLevel", "setMicGain", "setHeadphoneMuted",
                "setMasterVolume", "setAutoVolumeEnabled", "configureAutoVolume", "setEnhanceBassEnabled",
                "configureEnhanceBass", "setEqualizerType", "configureFiveBandEqualizer", "enableSurroundSound",
                "configureSurroundSound", "enableHardwareProcessor")}),
            "getRadio": self._object("Radio", {n: idle("no radio") for n in (
                "enableLineInput", "enable", "setFrequency", "setSeekCallback", "seek", "updateSeek", "updateRDS",
                "getFrequency", "getBaseFrequency", "getSpacing", "isinitialized", "clearRDS", "setRegion",
                "isRegionChanged", "getStationName")}),
            "getButtons": self.buttons_object,
            "getPads": pads,
            "getLeds": self._object("Leds", {"setLed": idle("no LEDs")}),
            "getBattery": self._object("Battery", {n: idle("as boot_rig.py: every answer 0") for n in (
                "enableMonitoring", "getLevel", "getVoltage", "getCharging", "getTimeTo", "getState", "isEmpty")}),
            "getTimer": self.timer,
            "getEncoder": self._object("Encoder", {"setAcceleration": idle("no encoder"),
                                                   "setResolution": idle("no encoder")}),
            "getDisplayDriver": self.display_driver,
            "getUsbAudio": self._object("UsbAudio", {n: idle("updateBuffers' USB audio is platform code") for n in (
                "beginFillData", "fillChannelData", "finishFillData")}),
            "getUartMidi": uart,
            "getUsbMidi": usb_midi,
            "getConfigManager": self._object("ConfigManager", {"readData": idle("as boot_rig.py: 0"),
                                                               "writeData": idle("the config flash isn't modelled")}),
            "getFileSystem": self._card(),
            "getCrcCalculator": self._object("Crc", {"calculate32bitCrc": idle("as PlayRig: 0")}),
            "getSystemInfo": m.alloc(4096, 8),                    # a plain struct, no vtable
            "getUsb": self._object("Usb", {n: idle("no USB host") for n in (
                "init", "reset", "update", "setClassState", "getClassState", "isAttached")}),
        }
        platform = {name: Model(lambda mm, a=obj: a, pure=True) for name, obj in objects.items()}
        platform.update({
            "setPowerMode": idle("no power control"),
            "resetMCU": unexpected("resetMCU"),
            "runBootloader": unexpected("runBootloader"),
            "setPowerButtonCallback": idle("no power button"),
            "getPowerButtonState": idle("not pressed"),
            "readForceStartUp": idle("no forced start-up"),
        })
        self.platform = self._object("Platform", platform, size=8)

    def _card(self):
        """FakeSd's files behind FileSystemImpl's slots, each call a wait (1.6). FakeSd's own handlers and
        BootRig's _complete_card ones, by name."""
        sd, k, p = self.sd, self.kernel, self.p

        def call(name, func, sized=None):
            def f(mm):
                if self.in_interrupt:
                    self.invariants.append(f"card call {name} in an interrupt at {k.now()}")
                sd.calls.append(name)
                r = func(mm)
                n = 0 if sized is None else sized(mm, r)
                self.wait(p.t_card_call_ns + n * p.t_card_byte_ps // 1000, "card")
                return r
            return Model(f)

        def no_arg(method):
            return lambda mm: method()

        def odd(name):
            def f(mm):
                sd.unexpected.append(name)
                return 0
            return f
        models = {
            "isSdCardInserted": Model(lambda mm: 1, pure=True),
            "isAsyncReadBusy": Model(lambda mm: 0, pure=True),
            "setMountEventCallback": Model(lambda mm: 0, pure=True, why="the card never comes or goes"),
            "exists": call("exists", no_arg(sd.exists)),
            "rename": call("rename", self._rename),
            "mkdir": call("mkdir", no_arg(sd.mkdir)),
            "remove": call("remove", no_arg(sd.remove)),
            "removeDirWithFiles": call("removeDirWithFiles", lambda mm: 1),
            "removeOnlyDirFiles": call("removeOnlyDirFiles", lambda mm: 1),
            "getFileSizeEPKc": call("getFileSize", lambda mm: len(sd.files.get(mm.cstr(mm.arg(1)), b""))),
            "getFileSizeEPv": call("getFileSize", no_arg(sd.size_of_handle)),
            "getFileAvailable": call("getFileAvailable", self._available),
            "openFile": call("openFile", no_arg(sd.open_file)),
            "readFile": call("readFile", no_arg(sd.read_file), sized=lambda mm, r: r if r < 0x80000000 else 0),
            "writeFile": call("writeFile", self._write, sized=lambda mm, r: r if r < 0x80000000 else 0),
            "seekFileCur": call("seekFileCur", self._seek_cur),
            "seekFile": call("seekFile", self._seek),
            "closeFile": call("closeFile", no_arg(sd.close_file)),
            "endFile": call("endFile", lambda mm: 0),
            "isFileOpen": call("isFileOpen", lambda mm: 1 if mm.arg(1) in sd.handles else 0),
            "enableFastSeek": call("enableFastSeek", lambda mm: 0),
            "pollAsyncRead": call("pollAsyncRead", lambda mm: 1),
            "waitAsyncRead": call("waitAsyncRead", lambda mm: 1),
            "createInstrumentsList": call("createInstrumentsList", lambda mm: 0),
            "countContainedFiles": call("countContainedFiles", lambda mm: 0),
            "createFilesList": call("createFilesList", lambda mm: 0),
            "listDirContents": call("listDirContents", no_arg(sd.list_dir)),
        }
        for name in ("remount", "readSectorsAsync", "readNbFiles", "nesDir_open", "nesDir_readItem", "nesDir_close"):
            models[name] = call(name, odd(name))         # FakeSd has none: logged as unexpected, as before
        vt = self.m.alloc(4 * 64, 4)
        found = resolve(self.m.elf, CLASSES["FileSystem"], models)
        for slot, model in found:
            func = pure(model.func) if model.pure else model.func
            self.m.w32(vt + 4 * slot.index, self.m.add_stub(func))
        self.m.w32(sd.obj, vt)
        return sd.obj

    # -- the main loop and boot -------------------------------------------------------------------------
    def _finish(self):
        m = self.m
        self._watch_writes()
        self.tramp = m.alloc(len(TRAMPOLINE) + 12, 4)
        m.write(self.tramp, TRAMPOLINE)
        self.pass_addr = m.add_stub(pure(self._pass_start))
        m.w32(self.tramp + 0x20, self.pass_addr)
        m.w32(self.tramp + 0x24, self.tracker)
        m.w32(self.tramp + 0x28, m.addr("_ZN11TrackerImpl6updateEv") | 1)
        self.start_display = m.call(m.addr("_ZN11TrackerImpl19getStartDisplayFuncEv"), self.tracker)
        self.irq_out = m.addr("_ZN11TrackerImpl18updateAudioBuffersEPvS0_i") | 1
        self.irq_in = m.addr("_ZN11TrackerImpl23updateInputAudioBuffersEPvS0_i") | 1
        self.in_i2s = m.alloc(8 * BLOCK, 8)                # silence: 2 x 128 stereo words
        self.in_usb = m.alloc(4 * BLOCK, 8)
        for label, name in LOGGED.items():
            if name in self.syms:
                self.watch(self.syms[name].addr, lambda mm, n=label: self._log_fn(n))
        self.watch(m.addr(NOTE_ON), self._note)
        self.fill_stack()
        uc = self.uc_main
        uc.reg_write(A.UC_ARM_REG_SP, STACK_TOP - 0x100)
        uc.reg_write(A.UC_ARM_REG_PC, self.tramp)
        uc.ctl_flush_tb()

        # From here a helper call (free_heap and the like) runs beside the main loop, which may be stopped
        # deep in a call: below its stack, with its registers kept. It takes no virtual time.
        call = m.call

        def aside(func, *args, **kw):
            saved = uc.context_save()
            kw.setdefault("stack", (uc.reg_read(A.UC_ARM_REG_SP) - 0x200) & ~7)
            try:
                return call(func, *args, **kw)
            finally:
                uc.context_restore(saved)
        m.call = aside

    def _log_fn(self, name):
        k = self.kernel
        k.log.write({"vt": k.now(), "ctx": k.context, "pass": k.c.passes, "fn": name})

    def _note(self, mm):
        self.notes += 1
        if self.p.log_pass_every:
            self._log_fn("sendNoteOn")

    def _boot_call(self):
        """The device's start-up as steps of the trampoline: startDisplay, then (audio on) init."""
        m, k = self.m, self.kernel
        step = self.boot_step
        self.boot_step += 1
        if step == 0:
            return self.start_display | 1, self.platform, 0
        if step == 1:
            if self.p.interrupts_during_init:
                k.enable_interrupts()           # AudioOutputI2S::begin, at the end of initHardware
            return m.addr("_ZN11TrackerImpl4initEP8Platform") | 1, self.tracker, self.platform
        # init returned: Profiler::init takes all the heap on purpose, to measure it. That isn't a failure.
        self.failures = [f for f in self.failures if not f.startswith("heap over")]
        self.booted = True
        k.enable_interrupts()
        k.log.write({"vt": k.now(), "ctx": "main", "event": "booted"})
        return None

    def _pass_start(self, mm):
        uc = mm.uc
        if self._ignore_next:              # the same call again, after the skip stopped on it
            self._ignore_next = False
            ret, r1, r2 = self._last
            uc.reg_write(A.UC_ARM_REG_R1, r1)
            uc.reg_write(A.UC_ARM_REG_R2, r2)
            return ret
        k = self.kernel
        if not self.booted:
            call = self._boot_call()
            if call is not None:
                ret, r1, r2 = call
                uc.reg_write(A.UC_ARM_REG_R1, r1)
                uc.reg_write(A.UC_ARM_REG_R2, r2)
                return ret
        idle = not (self._wrote or self._impure or self._input_in_pass or k.irq_in_pass)
        self._wrote = self._impure = self._input_in_pass = False
        k.pass_start()
        ret = r1 = r2 = 0
        payload = k.pop_input()
        if payload is not None:
            kind, button = payload
            (self.held.add if kind == "push" else self.held.discard)(button)
            ret = self.m.addr("_Z12onButtonPushh" if kind == "push" else "_Z15onButtonReleaseh") | 1
            r1 = button
            self._input_in_pass = True
        elif idle and self.p.skip and self.booted:
            self._skip_requested = True
            uc.emu_stop()
        self._last = (ret, r1, r2)
        uc.reg_write(A.UC_ARM_REG_R1, r1)
        uc.reg_write(A.UC_ARM_REG_R2, r2)
        return ret

    # -- the kernel's engine ------------------------------------------------------------------------------
    def run_slice(self, n):
        uc = self.uc_main
        self._skip_requested = False
        before = len(self.failures)
        pc = uc.reg_read(A.UC_ARM_REG_PC)
        t0 = time.perf_counter()
        try:
            uc.emu_start(pc | 1, UNTIL, count=n)
            self.wall["main"] += time.perf_counter() - t0
        except UcError as e:
            raise RuntimeError(f"main loop stopped at pc={uc.reg_read(A.UC_ARM_REG_PC):#010x}: {e}; "
                               f"{self.where(uc)}") from e
        if self._skip_requested:
            self._ignore_next = (uc.reg_read(A.UC_ARM_REG_PC) & ~1) == (self.pass_addr & ~1)
            return "skip"
        if any(f.startswith("assert") for f in self.failures[before:]):
            self._stopped = "assert"
        if self._stopped:
            return "stop"
        return "count"

    def where(self, uc):
        """The open functions on the stack, for a stop: lr and the return addresses above sp. An address in
        the closed libraries is shown only as that."""
        import bisect
        if not hasattr(self, "_funcs"):
            self._funcs = sorted((s.addr & ~1, n) for n, s in self.syms.items() if s.is_func and s.addr)
            self._starts = [a for a, _ in self._funcs]

        def name(a):
            a &= ~1
            if any(lo <= a < hi for lo, hi, _ in self.guard.ranges):
                return "(closed library)"
            j = bisect.bisect_right(self._starts, a) - 1
            return self._funcs[j][1] if j >= 0 else hex(a)
        sp = uc.reg_read(A.UC_ARM_REG_SP)
        seen = [name(uc.reg_read(A.UC_ARM_REG_PC)), name(uc.reg_read(A.UC_ARM_REG_LR))]
        for off in range(0, 2048, 4):
            try:
                v = struct.unpack("<I", bytes(uc.mem_read(sp + off, 4)))[0]
            except UcError:
                break
            if v & 1 and (0x60000000 <= v < 0x61000000 or 0x100 < v < 0x20000):
                seen.append(name(v))
        return "pc, lr and stack: " + " < ".join(list(dict.fromkeys(seen))[:12])

    def masked(self):
        uc = self.uc_main
        return bool(self.audio_masked or uc.reg_read(A.UC_ARM_REG_PRIMASK) or uc.reg_read(A.UC_ARM_REG_BASEPRI))

    def hold_info(self):
        """Which mask holds a due interrupt, and the open function the main loop is in (for the log)."""
        uc = self.uc_main
        kinds = [k for k, on in (("NVIC", self.audio_masked), ("PRIMASK", uc.reg_read(A.UC_ARM_REG_PRIMASK)),
                                 ("BASEPRI", uc.reg_read(A.UC_ARM_REG_BASEPRI))) if on]
        return {"mask": kinds, "in": self.where(uc).replace("pc, lr and stack: ", "")[:300]}

    def next_change(self, vt):
        return self.copro_busy_until if self.copro_busy_until > vt else None

    def fire(self, kind, t):
        """One interrupt in the twin, on the main loop's stack below the interrupted SP, with the
        interrupted registers in place, as the CPU stacks them."""
        uc, uc2, m = self.uc_main, self.uc2, self.m
        if self._dirty:
            for i in self._dirty:
                lo, n = self._code[i]
                uc2.ctl_remove_cache(lo, lo + n)
            self._dirty.clear()
            self.flushes += 1
        t0 = time.perf_counter()
        sp = copy_interrupted(uc, uc2)
        if kind == "out":
            func, args = self.irq_out, (self.tracker, self.left, self.right, BLOCK)
        else:
            func, args = self.irq_in, (self.tracker, self.in_i2s, self.in_usb, BLOCK)
        for reg, v in zip((A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3), args):
            uc2.reg_write(reg, v)
        uc2.reg_write(A.UC_ARM_REG_SP, sp)
        uc2.reg_write(A.UC_ARM_REG_LR, STUB_RET | 1)
        self.in_interrupt = True
        m.uc = uc2
        self._irq_started = time.monotonic()
        self._watchdog()
        try:
            # no timeout=: Unicorn starts a timer thread for each call that has one, about 15 ms a call
            uc2.emu_start(func, STUB_RET)
        except UcError as e:
            raise RuntimeError(f"{kind} interrupt stopped at pc={uc2.reg_read(A.UC_ARM_REG_PC):#010x}: {e}") from e
        finally:
            self._irq_started = None
            m.uc = uc
            self.in_interrupt = False
            self.wall[kind] += time.perf_counter() - t0
        if uc2.reg_read(A.UC_ARM_REG_PC) != STUB_RET:
            raise RuntimeError(f"{kind} interrupt didn't return (pc={uc2.reg_read(A.UC_ARM_REG_PC):#010x})")
        if kind == "out":
            data = m.read(self.left, 2 * BLOCK) + m.read(self.right, 2 * BLOCK)
            self.audio.update(data)
            self.blocks += 1
            samples = struct.unpack(f"<{2 * BLOCK}h", data)
            self.peak = max(self.peak, max(samples), -min(samples))

    def _watchdog(self):
        """One thread for the whole run that stops an interrupt running longer than IRQ_HUNG_S of wall time;
        fire() then reports it as not returning."""
        if getattr(self, "_dog", None):
            return
        import threading

        def watch():
            while True:
                time.sleep(1.0)
                started = self._irq_started
                if started is not None and time.monotonic() - started > IRQ_HUNG_S:
                    self.uc2.emu_stop()
        self._dog = threading.Thread(target=watch, daemon=True)
        self._dog.start()

    # -- running ------------------------------------------------------------------------------------------
    def run(self, seconds=None, until_ns=None, stop=lambda: False):
        """Run the firmware on the clock, from wherever it is, for `seconds` of virtual time (or to
        until_ns), or until stop() or a failure."""
        k = self.kernel
        end = until_ns if until_ns is not None else k.vt + int(seconds * 1e9)
        k.run(self, end, stop=lambda: bool(self._stopped) or stop())

    def press(self, button, hold_ms=30):
        """A press and its release, as inputs at the next pass starts."""
        k = self.kernel
        k.queue_input(k.vt, ("push", button))
        k.queue_input(k.vt + hold_ms * 1_000_000, ("release", button))

    def uncalibrated(self):
        return [f"{name}={getattr(self.p, name)}" for name in PLACEHOLDERS]

    def report(self):
        out = self.kernel.summary()
        out.update({"booted": self.booted, "notes": self.notes, "blocks": self.blocks, "peak": self.peak,
                    "audio": self.audio.hexdigest()[:16], "failures": self.failures[:5],
                    "invariants": self.invariants[:5], "sd_unexpected": sorted(set(self.sd.unexpected))[:8],
                    "midi_clock_asked": self.midi_clock is not None, "max_stack": self.max_stack(),
                    "heap_used": self.heap_brk - self.heap_base, "pool_peak": self.pool_peak(),
                    "wall": {k: round(v, 2) for k, v in self.wall.items()}, "twin_flushes": self.flushes})
        return out

    def state_hash(self):
        """Registers, all RAM, the card and the clock: for comparing two runs that should be equal."""
        h = hashlib.sha256()
        uc = self.uc_main
        for reg in COPIED + [A.UC_ARM_REG_SP, A.UC_ARM_REG_PC, A.UC_ARM_REG_PRIMASK]:
            h.update(struct.pack("<I", uc.reg_read(reg) & 0xFFFFFFFF))
        for begin, size, _perms, _ptr, buf in self._maps:
            if begin != 0x60000000 and begin < 0xA0000000 and begin != 0x40000000:
                h.update(buf[:])
        h.update(repr((self.heap_brk, sorted(self.sd.files.items()), self.kernel.vt)).encode())
        return h.hexdigest()[:16]


__all__ = ["Sim", "Params", "Log", "Unmodelled", "PLAY"]
