#!/usr/bin/env python3
"""CE's real start-up and audio graph under Unicorn, with the synth engines in.

Static constructors, Synth::init and audioEngine::init run as they are, on a fake Platform built
from core/include/Platform.h. Notes go in through Synth's public calls, and audio comes out of
audioEngine::updateAudioBuffers, the interrupt's own entry. See docs/mt/SOUND_TEST_PLAN.md.

Since CE's plugin synths the synth host (libPolySynth.a, built from synth/source) is CE's open code,
and the engines are .psyn plugins on the card: this rig puts CE's on the fake card's /Synths/
(build/Synths from `make psyn`: the factory ones from synth/prebuilt and CHIP), and Synth::init loads
them as the firmware does. The factory .psyn files are a black box (their licence, section 2(c)):
they run, and what comes out can be compared between our builds, nothing more. PsynLoader relocates
them into the synth's pool (s_psynRamPool, Synth.cpp), so the guard is that pool's range: the
helpers refuse any hook in it, and nothing is read from it. If something fails inside one, stop and
report. Don't dig.

WAVE and PHASE are .psyn plugins too, but ours, open (core/mods/synths/psyn, built with their ELFs
in core/mods/synths/psyn/build): the one exception. The fake card sees PsynLoader read each image
into the pool (the read just past the 128-byte header lands on the image's base), so the rig
knows where each of ours is, and the guard lets hooks in there, and only there, at the ELF's
symbols (linked at 0) plus that base. An image read over one of ours (an eviction) stops the
test: the rig doesn't follow an engine that moves.

A firmware from before them (test_synthfx.py's --before) has the closed libraries linked in
instead: the guard is then their symbols' ranges, as before, with the names from nm (names only)
on the two libraries as git last held them (build/emu/closed_symbols.txt keeps the list).
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from machine import Machine, SCRATCH  # noqa: E402
from test_step0 import FakeSd  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_S0  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SYNTHS = REPO / "build" / "Synths"           # `make psyn`
OWN_PSYN_BUILD = REPO / "core" / "mods" / "synths" / "psyn" / "build"
SYNTHS_FALLBACK = (REPO / "synth" / "prebuilt", REPO / "synth-plugin-sdk" / "build", OWN_PSYN_BUILD)
FACTORY_SYNTHS = ("ACD", "DIRT", "FAT", "PERC", "VAP", "WTFM")  # synth/prebuilt; CE dropped GRAIN (CE 26baedf)
OWN_SYNTHS = ("WAVE", "PHASE")                # ours, open: core/mods/synths/psyn
PSYN_HEADER = 128
HEAP_SIZE = 138_752                 # CE's fixed heap
BLOCK = 128
BLOCK_MS = 3                        # the fake clock's step per block: 128 samples at 44.1 kHz
PATCH_SIZE = 255


class ClosedCode(Exception):
    """A hook was asked for inside the closed engines' memory."""


def synth_files():
    """The .psyn engines for the fake card's /Synths/: name -> bytes."""
    folders = [SYNTHS] if SYNTHS.is_dir() and any(SYNTHS.glob("*.psyn")) else list(SYNTHS_FALLBACK)
    files = {}
    for folder in folders:
        for p in sorted(folder.glob("*.psyn")):
            files[p.name] = p.read_bytes()
    missing = [n for n in FACTORY_SYNTHS + OWN_SYNTHS if f"{n}.psyn" not in files]
    if missing:
        raise RuntimeError(f"no {', '.join(missing)}.psyn for the fake card: run `make psyn`")
    return files


def psyn_header(data):
    """The fields of a .psyn file's header (psyn_plugin.h's PsynHeader) that the rig uses."""
    magic, _api, _flags, type_wave, type_tracker = struct.unpack_from("<IHHHH", data, 0)
    code_name = data[12:20].split(b"\0")[0].decode()
    code, rodata, init_data, bss = struct.unpack_from("<IIII", data, 64)
    if magic != 0x4E595350:
        raise RuntimeError("not a .psyn file")
    return {"code_name": code_name, "type_id_wave": type_wave, "type_id_play_tracker": type_tracker,
            "image_size": code + rodata + init_data + bss, "code_size": code}


class OwnEngine:
    """One of our .psyn engines: its ELF (linked at 0) and, once PsynLoader has read it into the
    pool, its base there."""

    def __init__(self, name, data):
        from machine import Elf
        self.name = name
        self.header = psyn_header(data)
        elf_path = OWN_PSYN_BUILD / f"{name}.elf"
        if not elf_path.exists():
            raise RuntimeError(f"no {elf_path}: build core/mods/synths/psyn (make psyn)")
        self.elf = Elf(elf_path)
        text = next((blob for vaddr, _p, blob, _m in self.elf.segments if vaddr == 0), b"")
        image = data[PSYN_HEADER:PSYN_HEADER + self.header["code_size"]]
        if not text or image != text[:len(image)]:
            raise RuntimeError(f"{name}.psyn on the fake card isn't the one {elf_path.name} made: run make psyn")
        self.base = None
        self.pending = []           # (symbol, func): watches for when it is loaded

    def addr(self, symbol):
        """Where a symbol (exact, or a unique substring) is now; the engine must be loaded."""
        if self.base is None:
            raise RuntimeError(f"{self.name} isn't loaded yet")
        hits = [s for n, s in self.elf.symbols.items() if n == symbol] or \
               [s for n, s in self.elf.symbols.items() if symbol in n]
        if len(hits) != 1:
            raise KeyError(f"{self.name}: {symbol!r}: {len(hits)} matches")
        return self.base + hits[0].addr


class PsynCard(FakeSd):
    """The fake card, telling the rig where each .psyn image is read to."""

    def __init__(self, m, on_image):
        self.on_image = on_image
        super().__init__(m)

    def read_file(self):
        buf, n, h = self.m.arg(1), self.m.arg(2), self.m.arg(3)
        path, _mode, pos = self.handles[h]
        got = super().read_file()
        if path.startswith("/Synths/") and pos == PSYN_HEADER:
            self.on_image(path[len("/Synths/"):], buf, got)
        return got


OLD_LIBS = ("synth/lib/libPolySynth.a", "synth/lib/libPolyDSP.a")   # closed, before CE's plugin synths


def _old_libs():
    """The two closed libraries from git, as its last commit with them held them, in build/emu/."""
    out = REPO / "build" / "emu" / "closed_libs"
    paths = [out / Path(lib).name for lib in OLD_LIBS]
    if all(p.exists() for p in paths):
        return paths
    git = lambda *a: subprocess.run(["git", "-C", str(REPO), *a], capture_output=True, check=True).stdout  # noqa: E731
    rev = git("log", "-n1", "--format=%H", "HEAD", "--", OLD_LIBS[0]).decode().strip()
    if not rev:
        raise RuntimeError("git has no closed libraries: the guard can't work on this image")
    if subprocess.run(["git", "-C", str(REPO), "cat-file", "-e", f"{rev}:{OLD_LIBS[0]}"]).returncode != 0:
        rev += "^1"                     # the commit that deleted them: its first parent has them
    out.mkdir(parents=True, exist_ok=True)
    for lib, p in zip(OLD_LIBS, paths):
        p.write_bytes(git("show", f"{rev}:{lib}"))
    return paths


def _nm_lines(paths):
    tool = os.environ.get("MT_NM")
    paths = [str(p) for p in paths]
    if tool:
        return subprocess.run([tool, "--defined-only", *paths], capture_output=True, text=True, check=True).stdout
    try:
        return subprocess.run(["arm-none-eabi-nm", "--defined-only", *paths],
                              capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        pass
    # Windows, with the toolchain in WSL
    wsl = []
    for p in paths:
        drive, rest = p[0].lower(), p[2:].replace("\\", "/")
        wsl.append(f"/mnt/{drive}{rest}")
    script = '$HOME/agt/10.3/bin/arm-none-eabi-nm --defined-only ' + " ".join(f"'{p}'" for p in wsl)
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    return subprocess.run(["wsl", "-d", "Ubuntu", "--", "bash", "-c", script],
                          capture_output=True, text=True, check=True, env=env).stdout


def old_closed_symbol_names():
    """The names the two old libraries define, and nothing else from them."""
    cache = REPO / "build" / "emu" / "closed_symbols.txt"
    if cache.exists():
        return set(cache.read_text().split("\n"))
    names = set()
    for line in _nm_lines(_old_libs()).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in "TtWwVvDdBbRr" and len(parts[-2]) == 1:
            names.add(parts[-1])
    if not names:
        raise RuntimeError("no symbols from the closed libraries: the guard can't work")
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text("\n".join(sorted(names)))
    return names


class Guard:
    """The closed engines' place in this image: the synth's pool, where PsynLoader puts them, or on a
    firmware from before CE's plugin synths, the closed libraries' symbols."""

    def __init__(self, m):
        pools = [(n, s) for n, s in m.elf.symbols.items() if n.endswith("s_psynRamPool") and s.size]
        if len(pools) == 1:
            name, s = pools[0]
            self.ranges = [(s.addr, s.addr + s.size, "the .psyn pool")]
            return
        closed = old_closed_symbol_names()
        self.ranges = []
        for name, s in m.elf.symbols.items():
            if name in closed and s.size:
                a = s.addr & ~1
                self.ranges.append((a, a + s.size, f"a closed library's {name}"))
        if not self.ranges:
            raise RuntimeError("neither the .psyn pool nor the closed libraries are in this image: "
                               "run tests on a full build")

    open = ()                       # our engines' images in the pool: (begin, end, name)

    def allow(self, begin, end, name):
        self.open = tuple(o for o in self.open if o[2] != name) + ((begin, end, name),)

    def _open(self, begin, end):
        return any(a <= begin and end <= b for a, b, _ in self.open)

    def check(self, begin, end):
        if self._open(begin, end):
            return
        for a, b, name in self.ranges:
            if begin < b and a <= end:
                raise ClosedCode(f"{begin:#x}..{end:#x} overlaps {name}")

    def inside(self, address):
        return not self._open(address, address) and any(a <= address < b for a, b, _ in self.ranges)


class SoundRig:
    def __init__(self, axf):
        self.m = m = Machine(axf)
        self.guard = Guard(m)
        m.uc.mem_map(0x40000000, 0x10000000)        # peripherals read as 0
        m.uc.mem_map(0xE0000000, 0x100000)
        self.syms = m.elf.symbols
        self.now = 0
        self.failures = []
        self.interrupts = {"disable": 0, "enable": 0}
        self.audio_masked = False
        self.heap_base = self.heap_brk = SCRATCH + 0x400000
        self.heap_peak = 0

        self._stub_newlib()
        self._stub_hardware()
        self._fake_timer()
        self._run_static_constructors()
        self.sd = PsynCard(m, self._image_read)
        self.sd.dirs.add("/Synths")
        self.own = {}
        for name, data in synth_files().items():    # CE's engines and ours, as on a card with its Synths folder
            self.sd.files[f"/Synths/{name}"] = data
            if name[:-len(".psyn")] in OWN_SYNTHS:
                self.own[name[:-len(".psyn")]] = OwnEngine(name[:-len(".psyn")], data)
        self._fake_platform()
        self._init_synth()
        self._init_engine()
        self.left, self.right = m.alloc(2 * BLOCK, 4), m.alloc(2 * BLOCK, 4)
        self._update = m.addr("_ZN11audioEngine18updateAudioBuffersEPvS0_i")
        self._trigger = self.first("_ZN5Synth15triggerTestNote")

    # -- helpers that keep out of the closed code ---------------------------------------------
    def first(self, prefix):
        hits = [n for n in self.syms if n.startswith(prefix)]
        if not hits:
            raise KeyError(prefix)
        return self.syms[hits[0]].addr

    def _image_read(self, file, base, length):
        """PsynLoader read a .psyn image into the pool at base (main loop, audio masked)."""
        name = file[:-len(".psyn")] if file.endswith(".psyn") else file
        for e in self.own.values():
            if e.base is not None and e.name != name and base < e.base + e.header["image_size"] and \
                    e.base < base + length:
                raise ClosedCode(f"{file} read over {e.name}'s image (an eviction): the rig can't follow it")
        e = self.own.get(name)
        if e is None:
            return
        e.base = base
        self.guard.allow(base, base + e.header["image_size"], f"{name}.psyn")
        pending, e.pending = e.pending, []
        for symbol, func in pending:
            self.watch(e.addr(symbol), func)
        self.m.uc.ctl_flush_tb()

    def watch_own(self, engine, symbol, func):
        """Call func(machine) on entry to one of our engine's functions (by its ELF symbol), now if
        PsynLoader has loaded it, or as soon as it does."""
        e = self.own[engine]
        if e.base is None:
            e.pending.append((symbol, func))
        else:
            self.watch(e.addr(symbol), func)
            self.m.uc.ctl_flush_tb()

    def stub(self, addr, func):
        self.guard.check(addr & ~1, addr & ~1)
        self.m.stub_function(addr, func)

    def watch(self, addr, func):
        """Call func(machine) on entry to an open function, which then runs as usual."""
        a = addr & ~1
        self.guard.check(a, a)
        return self.m.uc.hook_add(UC_HOOK_CODE, lambda uc, x, s, u: func(self.m), begin=a, end=a)

    def hook_range(self, begin, end, func):
        """A code hook over a range of open code. There is no hook without a range."""
        self.guard.check(begin & ~1, end)
        return self.m.uc.hook_add(UC_HOOK_CODE, func, begin=begin & ~1, end=end)

    def call(self, name_or_addr, *args, max_insns=200_000_000):
        addr = self.m.addr(name_or_addr) if isinstance(name_or_addr, str) else name_or_addr
        return self.m.call(addr, *args, max_insns=max_insns)

    # -- start-up -----------------------------------------------------------------------------
    def _stub_newlib(self):
        m = self.m
        index = 1 if "_sbrk_r" in self.syms else 0
        name = "_sbrk_r" if index else "_sbrk"

        def sbrk(mm):
            increase = mm.arg(index)
            old = self.heap_brk
            if old + increase > self.heap_base + HEAP_SIZE:
                self.failures.append(f"heap over its {HEAP_SIZE} bytes")
                return 0xFFFFFFFF
            self.heap_brk += increase
            self.heap_peak = max(self.heap_peak, self.heap_brk - self.heap_base)
            return old
        self.stub(self.syms[name].addr, sbrk)

        def on_assert(mm):
            self.failures.append(f"assert {mm.cstr(mm.arg(0))}:{mm.arg(1)} {mm.cstr(mm.arg(3))}")
            mm.uc.emu_stop()
            return 0
        if "__assert_func" in self.syms:
            self.stub(self.syms["__assert_func"].addr, on_assert)

    def _stub_hardware(self):
        def counted(kind):
            def f(mm):
                self.interrupts[kind] += 1
                self.audio_masked = kind == "disable"
                return 0
            return f
        for name, s in self.syms.items():
            if not (s.is_func and s.addr):
                continue
            if name.startswith(("_ZN8Sgtl5000", "_ZN5Radio", "_ZN11audioEngine17attachUpdateTimer")):
                self.stub(s.addr, lambda mm: 0)
            elif "enableAudioInterrupt" in name and "disable" not in name:
                self.stub(s.addr, counted("enable"))
            elif "disableAudioInterrupt" in name:
                self.stub(s.addr, counted("disable"))

    def _fake_timer(self):
        m = self.m
        vtable = m.alloc(4 * 32, 4)
        stub = m.add_stub(lambda mm: self.now)
        for i in range(32):
            m.w32(vtable + 4 * i, stub)
        self.timer = m.alloc(16, 4)
        m.w32(self.timer, vtable)
        for name in ("_ZN13elapsedMillis5timerE", "_ZN13elapsedMicros5timerE",
                     "_ZN18elapsedMicrosAudio5timerE", "_ZN18elapsedMillisAudio5timerE"):
            if name in self.syms:
                m.w32(self.syms[name].addr, self.timer)

    def _run_static_constructors(self):
        m = self.m
        start, end = m.addr("__init_array_start"), m.addr("__init_array_end")
        for i in range((end - start) // 4):
            m.call(m.r32(start + 4 * i), max_insns=30_000_000)

    def _fake_platform(self):
        m = self.m
        dummy_vtable = m.alloc(4 * 64, 4)
        stub = m.add_stub(lambda mm: 0)
        for i in range(64):
            m.w32(dummy_vtable + 4 * i, stub)

        def dummy():
            o = m.alloc(4096, 8)
            m.w32(o, dummy_vtable)
            return o
        objects = {i: dummy() for i in range(23)}
        objects[12] = self.timer                    # Platform::getTimer
        objects[19] = self.sd.obj                   # Platform::getFileSystem
        vtable = m.alloc(4 * 23, 4)
        for i in range(23):
            m.w32(vtable + 4 * i, m.add_stub(lambda mm, a=objects[i]: a))
        self.platform = m.alloc(8, 4)
        m.w32(self.platform, vtable)

    def _init_synth(self):
        m = self.m
        # TrackerImpl's own Synth, which its static constructor built: the sequencer's notes reach
        # it through TrackerImpl::getInstance()->getSynth()
        self.tracker = m.call(m.addr("_ZN11TrackerImpl11getInstanceEv"))
        self.synth = m.call(m.addr("_ZN11TrackerImpl8getSynthEv"), self.tracker)

        def tempo(mm):
            bits = struct.unpack("<I", struct.pack("<f", 120.0))[0]
            mm.uc.reg_write(UC_ARM_REG_S0, bits)
            return bits
        function = m.alloc(16, 4)                   # std::function: manager, invoker
        m.write(function, b"\0" * 16)
        m.w32(function + 8, m.add_stub(lambda mm: 0))
        m.w32(function + 12, m.add_stub(tempo))
        m.call(self.first("_ZN5Synth4initE"), self.synth, self.platform, function, max_insns=400_000_000)

    def _init_engine(self):
        self.engine = self.m.addr("engine")
        self.m.call(self.m.addr("_ZN11audioEngine4initEP8PlatformP5Synth"), self.engine, self.platform, self.synth,
                    max_insns=400_000_000)

    # -- notes, blocks ------------------------------------------------------------------------
    def note(self, part, note, on):
        self.m.call(self._trigger, self.synth, part, note, 1 if on else 0, max_insns=50_000_000)

    def set_instrument(self, part, type_id):
        self.m.call(self.first("_ZN5Synth17setInstrumentType"), self.synth, part, type_id, max_insns=100_000_000)

    def set_voices(self, part, count):
        self.m.call(self.first("_ZN5Synth13setVoiceCount"), self.synth, part, count, max_insns=50_000_000)

    def _send(self, name, part, value):
        self.m.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", value))[0])
        self.m.call(self.m.addr(name), self.engine, part, max_insns=5_000_000)

    def reverb_send(self, part, value):
        self._send("_ZN11audioEngine18setSynthReverbSendEif", part, value)

    def delay_send(self, part, value):
        self._send("_ZN11audioEngine17setSynthDelaySendEif", part, value)

    def block(self):
        self.m.call(self._update, self.engine, self.left, self.right, BLOCK, max_insns=200_000_000)
        self.now += BLOCK_MS
        return (struct.unpack(f"<{BLOCK}h", self.m.read(self.left, 2 * BLOCK)),
                struct.unpack(f"<{BLOCK}h", self.m.read(self.right, 2 * BLOCK)))

    def blocks(self, n):
        left, right = [], []
        for _ in range(n):
            a, b = self.block()
            left += a
            right += b
        return left, right

    def pool_peak(self):
        return struct.unpack("<H", self.m.read(self.m.addr("_ZN11AudioStream15memory_used_maxE"), 2))[0]


class PlayRig(SoundRig):
    """A real project opened and played: the path a device takes when you press Play.

    open_project runs the load steps of WorkspaceProjectLoad on the project's own files, and press_play
    calls what the pattern screen's Play calls. interrupt() is TrackerImpl::updateAudioBuffers itself:
    midiUpdate, sequencer.tick, engine.update, the audio engine. So notes go through the sequencer and
    the sends are the project's. Still not here: the main loop, and CPU, which only a device can measure.
    """

    def __init__(self, axf):
        super().__init__(axf)
        m = self.m
        self.fm = m.addr("newFileManager")
        self.seq = m.addr("sequencer")
        self._interrupt = m.addr("_ZN11TrackerImpl18updateAudioBuffersEPvS0_i")
        self.has_mods = "_ZN4mods4initEP10FileSystem" in self.syms
        self.notes = 0
        # Interface::begin's: cProjectEditor::loadProjectValues checks each part through it (CE's
        # validateSynthPartSettings), and left at 0 that reads the bottom of the ITCM as a Synth
        m.w32(m.addr("_ZN10ModuleBase5synthE"), self.synth)

        crc_vtable = m.alloc(64, 4)
        for i in range(16):
            m.w32(crc_vtable + 4 * i, m.add_stub(lambda mm: 0))
        crc = m.alloc(16, 4)
        m.w32(crc, crc_vtable)
        m.call(m.addr("_ZN9Sequencer4initEv"), self.seq, max_insns=50_000_000)
        m.call(m.addr("_ZN9Sequencer16setCrcCalculatorEP13CrcCalculator"), self.seq, crc)
        m.call(m.addr("_ZN12cFileManager4initEP10FileSystemP5Synthb"), self.fm, self.sd.obj, self.synth, 0,
               max_insns=30_000_000)
        m.call(m.addr("_ZN12cFileManager16setCrcCalculatorEP13CrcCalculator"), self.fm, crc)
        if self.has_mods:
            m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: None)
            m.call(m.addr("_ZN4mods4initEP10FileSystem"), self.sd.obj)
            self.stub(m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: self.platform)
            m.call(m.addr("_ZN4mods15masterfxStartedEv"), max_insns=50_000_000)

    def open_project(self, folder, name="Test"):
        """`folder` as if at /Projects/<name> and in the Workspace. Returns which patches loaded."""
        m, sd = self.m, self.sd
        sd.dirs |= {"Workspace", "Workspace/patterns", "Workspace/instruments", "Workspace/samples", "Projects",
                    f"Projects/{name}"}
        if self.has_mods:
            # before any project is in: finding the path field needs the fields empty
            path_at = find_project_path(self)
        folder = Path(folder)
        for f in folder.iterdir():
            if f.is_file():
                sd.files[f"Workspace/{f.name}"] = sd.files[f"Projects/{name}/{f.name}"] = f.read_bytes()
        for f in (folder / "patterns").iterdir():
            sd.files[f"Workspace/patterns/{f.name}"] = f.read_bytes()

        if self.has_mods:
            set_names(self, path_at, "Projects", name)
            m.call(m.addr("_ZN4mods17onProjectOpenCopyEv"), max_insns=30_000_000)
            m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"), max_insns=50_000_000)

        project = sd.files["Workspace/project.mt"]
        buf = m.alloc(len(project) + 64, 8)
        m.write(buf, project)
        # strMtProject's values are the last thing the reader copies in. Its offset isn't in the symbols.
        copies = []
        hook = self.watch(m.addr("memcpy"), lambda mm: copies.append(mm.arg(0)))
        ok = m.call(m.addr("_ZN12cFileManager29loadProjectFileFormFileStructEP12strMtProjectP14strProjectFile"),
                    self.fm, m.addr("mtProject"), buf, max_insns=50_000_000)
        m.uc.hook_del(hook)
        if not ok & 0xFF:
            raise RuntimeError("CE's reader refused project.mt")
        self.values = copies[-1]

        m.call(m.addr("_ZN12cFileManager24loadPatternFromWorkspaceEh"), self.fm, 1, max_insns=50_000_000)
        m.call(m.addr("_ZN9Sequencer20switchRamPatternsNowEv"), self.seq, max_insns=50_000_000)
        m.call(m.addr("_ZN5Synth16updateToSettingsERK11strMtValuesb"), self.synth, self.values, 1,
               max_insns=400_000_000)
        no_name = m.alloc(32, 8)                    # std::optional<std::string>, empty
        m.write(no_name, bytes(32))
        load = m.addr("_ZN5Synth22loadPatchFromWorkspaceEiSt8optionalINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEE")
        loaded = [m.call(load, self.synth, part, no_name, max_insns=400_000_000) & 0xFF for part in range(3)]
        m.call(m.addr("_ZN12cFileManager17loadProjectValuesEv"), self.fm, max_insns=100_000_000)
        return loaded

    def count_notes(self):
        def f(mm):
            self.notes += 1
        self.watch(self.m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"), f)

    def press_play(self):
        self.m.call(self.m.addr("_ZN9Sequencer11playPatternEh"), self.seq, 0, max_insns=50_000_000)

    def interrupt(self):
        """One audio block. max_insns is how a hang shows up."""
        self.m.call(self._interrupt, self.tracker, self.left, self.right, BLOCK, max_insns=100_000_000)
        self.now += BLOCK_MS
        return (struct.unpack(f"<{BLOCK}h", self.m.read(self.left, 2 * BLOCK)),
                struct.unpack(f"<{BLOCK}h", self.m.read(self.right, 2 * BLOCK)))

    def interrupts_run(self, n):
        left, right = [], []
        for _ in range(n):
            a, b = self.interrupt()
            left += a
            right += b
            if self.failures:
                break
        return left, right


def find_project_path(r):
    """cFileManager's path and name fields: wherever the hooks read them from."""
    m, sd = r.m, r.sd
    fm = m.sym("newFileManager")
    sd.files["?//mods.bin"] = b"not a real one"
    for at in range(fm.addr, fm.addr + fm.size):
        before = m.read(at, 2)
        m.write(at, b"?\0")
        sd.calls.clear()
        try:
            m.call(m.addr("_ZN4mods17onProjectOpenCopyEv"))
        except RuntimeError:
            pass
        found = any("readFile" in c for c in sd.calls)
        m.write(at, before)
        if found:
            del sd.files["?//mods.bin"]
            return at
    raise RuntimeError("no project path field in cFileManager")


def set_names(r, path_at, folder, name):
    m = r.m
    m.write(path_at, folder.encode() + b"\0")
    m.write(path_at + PATCH_SIZE, name.encode() + b"\0")


def peak(x):
    return max((abs(v) for v in x), default=0)
