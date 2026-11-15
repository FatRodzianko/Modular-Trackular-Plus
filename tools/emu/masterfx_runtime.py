# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Master FX plugin runtime (core/mods/masterfx/masterfxPlugins.cpp) in a firmware image that has
it, started in a bare Machine: for rigs with CE's reverb or delay object and no audio engine around it.

masterfx::startPlugins builds the runtime's Env from the engine's reverb object, which a bare Machine
doesn't have. Here the Env is built from the same symbols startPlugins uses: the slots' ITCM code
areas, their DTCM state, the float scratch, the load buffer, CE's reverb buffers (internalRamReverbBuff
and externalRamReverbBuff, which every AudioEffectPolyverb uses), CE's delay lines
(externalRamDelayBuff) and the dynamics slot's SDRAM, and the firmware's own host table. The clock
and the cycle counter are stubs, so the time guard never trips here; the cache sync is counted, and
the emulator's translated code is flushed after each install.

The card is test_step0's FakeSd behind the mods' own CeFileIo, so start() writes the firmware's pack
onto it and lists it, and read() and install() load a plugin as they do on the unit.

Firmware from before the plugins has no runtime: has_plugins() says so and a rig uses activeReverb
and activeDelay as list indices there.
"""
from __future__ import annotations

import struct

from test_step0 import FakeSd

START = "_ZN4mods8masterfx7plugins5startERNS_6FileIoERKNS1_3EnvE"
READ = "_ZN4mods8masterfx7plugins4readENS1_4SlotEPKcb"
INSTALL = "_ZN4mods8masterfx7plugins7installENS1_4SlotE"
REMOVE = "_ZN4mods8masterfx7plugins6removeENS1_4SlotE"
FILE_IO = "_ZN4mods12_GLOBAL__N_16fileIoE"
NS = "_ZN4mods8masterfx12_GLOBAL__N_1"

# masterfx-1.x's list indices as the pack's files (masterfx.cpp's kV1Reverbs and kV1Delays)
REVERB_FILES = ["", "Freeverb.ptmfx", "Plate.ptmfx", "Spring.ptmfx", "Shimmer.ptmfx", "Gated.ptmfx", "Reverse.ptmfx"]
DELAY_FILES = ["", "Tape.ptmfx", "BBD.ptmfx"]
SLOT = {"reverb": 0, "delay": 1, "dynamics": 2}
STATUS = ["ok", "missing", "badFile", "tooBig", "wrongSlot", "skipped", "readFailed", "notRead"]
CODE_AREA = 8192
DELAY_LINE = 262144                 # CE's MAX_DELAYLINE_LENGTH, int16 samples


def has_plugins(m) -> bool:
    return START in m.elf.symbols


class Runtime:
    """The runtime started in m, on a fake card (with `files`, path -> bytes, on it before the start)"""

    def __init__(self, m, files: dict[str, bytes] | None = None):
        self.m = m
        self.syncs = 0
        self.sd = FakeSd(m)
        if files:
            self.sd.dirs.add("/MasterFX")
            self.sd.files.update(files)
        sym = lambda n: m.addr(NS + n)  # noqa: E731
        code = sym("9codeAreasE")
        states = (sym("11reverbStateE"), sym("10delayStateE"), sym("13dynamicsStateE"))
        rev0, rev1 = m.addr("_ZL21internalRamReverbBuff"), m.addr("externalRamReverbBuff")
        dly = m.addr("externalRamDelayBuff")
        memories = (
            ((rev0, rev1, 0), (32768, 32768, 0)),
            ((dly, dly + 2 * DELAY_LINE, dly + 4 * DELAY_LINE), (2 * DELAY_LINE,) * 3),
            ((sym("14dynamicsMemoryE"), 0, 0), (65536, 0, 0)),
        )

        def synced(mm):
            self.syncs += 1
            return 0
        env = struct.pack("<3I", *(code + CODE_AREA * i for i in range(3)))
        env += struct.pack("<3I", *states)
        for areas, sizes in memories:
            env += struct.pack("<3I3I", *areas, *sizes)
        env += struct.pack("<7I", sym("10loadBufferE"), sym("7scratchE"), m.add_stub(synced),
                           m.add_stub(lambda mm: 0), m.add_stub(lambda mm: 0), 0xFFFFFFFF,
                           m.addr("_ZN4mods8masterfx12_GLOBAL__N_1L4hostE"))
        assert len(env) == 124, len(env)
        self.env = m.alloc(len(env), 4)
        m.write(self.env, env)
        self.code = tuple(code + CODE_AREA * i for i in range(3))
        self.states = states
        self.memories = memories
        self.scratch = sym("7scratchE")
        self.io = m.addr(FILE_IO)
        m.w32(self.io + 4, self.sd.obj)     # CeFileIo::fileSystem, as mods::init sets it
        m.call(m.addr(START), self.io, self.env, max_insns=400_000_000)

    def install(self, kind: str, file: str) -> str:
        """read() then install(), as masterfx::apply does with the object blocked. The status."""
        m = self.m
        name = m.put_str(file)
        status = STATUS[m.call(m.addr(READ), SLOT[kind], name, 0, max_insns=200_000_000) & 0xFF]
        if status == "ok":
            m.call(m.addr(INSTALL), SLOT[kind], max_insns=50_000_000)
            m.uc.ctl_flush_tb()             # the code area had other code in it before
        return status

    def remove(self, kind: str) -> None:
        self.m.call(self.m.addr(REMOVE), SLOT[kind])


def choose(m, runtime: Runtime | None, kind: str, which: int) -> None:
    """masterfx-1.x's choice `which` (a list index, 0 is Stock) in the slot, on either firmware: what
    masterfx::apply leaves in the object's hook. The caller then clears the object, as apply does."""
    name = "activeReverb" if kind == "reverb" else "activeDelay"
    active = m.addr(f"_ZN4mods8masterfx{len(name)}{name}E")
    if runtime is None:
        m.write(active, bytes([which]))
        return
    files = REVERB_FILES if kind == "reverb" else DELAY_FILES
    if which >= len(files):
        m.write(active, bytes([which]))     # a damaged value in the hook's byte, as the old rigs wrote it
        return
    m.write(active, b"\0")
    if which == 0:
        runtime.remove(kind)
        return
    file = files[which]
    status = runtime.install(kind, file)
    if status != "ok":
        raise RuntimeError(f"{file}: {status}")
    m.write(active, b"\1")
