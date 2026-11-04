# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Run functions of a Tracker+ CE build (.axf) under Unicorn (Cortex-M7, Thumb).

Adapted from Modular-Trackular's 1.2.0 emulator. Loads the ELF's segments at their
load addresses and copies initialised RAM sections to their run addresses, as the
startup code would. Functions are found by symbol name, never by address. Nothing runs
the boot path: tests set up the state a function needs and call it.

Licence note: since CE 9cc7aae the closed synth libraries' licence
(synth/lib/LICENSE-PROPRIETARY.txt, 1(b)) allows non-commercial use off Polyend hardware,
so full builds may run here. Treat those libraries as a black box: unmodified, with no
hooks inside them and no disassembling or tracing their code (2(c)). See
docs/mt/SOUND_TEST_PLAN.md. Mod tests run on full builds; MT_NO_SYNTH=1 builds, without the libraries, also still work.
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_MODE_MCLASS,
                     UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP)

# Our own region for stubs (unused by the chip's map).
STUB_BASE = 0x70000000
STUB_RET = STUB_BASE + 0x0FF0           # functions "return" here
SCRATCH = 0x90000000                    # outside the chip's map, for structures tests build
STACK_TOP = 0x20058000

MAPS = [  # (base, size): the i.MX RT1051 memories the Tracker+ uses
    (0x00000000, 0x20000),      # ITCM
    (0x20000000, 0x58000),      # DTCM
    (0x20200000, 0x8000),       # OCRAM (the one FlexRAM bank set up as OCRAM)
    (0x60000000, 0x1000000),    # FlexSPI flash, 16 MB
    (0x80000000, 0x4000000),    # SDRAM, 64 MB
    (SCRATCH, 0x1000000),
    (STUB_BASE, 0x1000),
]

PT_LOAD = 1
SHT_SYMTAB = 2
STT_FUNC = 2


@dataclass
class Symbol:
    name: str
    addr: int
    size: int
    is_func: bool


class Elf:
    """Just enough of ELF32 little-endian: load segments and the symbol table."""

    def __init__(self, path: Path):
        self.data = data = Path(path).read_bytes()
        assert data[:4] == b"\x7fELF" and data[4] == 1 and data[5] == 1, "not an ELF32 LE file"
        (self.entry, phoff, shoff, _flags, _ehsize, phentsize, phnum, shentsize, shnum,
         _shstrndx) = struct.unpack_from("<IIIIHHHHHH", data, 24)

        self.segments = []  # (vaddr, paddr, bytes, memsz)
        for i in range(phnum):
            p_type, off, vaddr, paddr, filesz, memsz, _fl, _al = struct.unpack_from(
                "<IIIIIIII", data, phoff + i * phentsize)
            if p_type == PT_LOAD:
                self.segments.append((vaddr, paddr, data[off:off + filesz], memsz))

        self.symbols: dict[str, Symbol] = {}
        sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
                    for i in range(shnum)]
        for sh in sections:
            if sh[1] != SHT_SYMTAB:
                continue
            _name, _type, _flags, _addr, off, size, link, _info, _align, entsize = sh
            stroff = sections[link][4]
            for j in range(size // entsize):
                st_name, value, st_size, info, _other, _shndx = struct.unpack_from(
                    "<IIIBBH", data, off + j * entsize)
                if not st_name:
                    continue
                end = data.index(b"\0", stroff + st_name)
                name = data[stroff + st_name:end].decode("latin-1")
                self.symbols.setdefault(name, Symbol(name, value, st_size, (info & 0xF) == STT_FUNC))


class Machine:
    def __init__(self, axf_path: Path):
        self.elf = Elf(axf_path)
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for base, size in MAPS:
            self.uc.mem_map(base, size)
        for vaddr, paddr, blob, _memsz in self.elf.segments:
            if not blob:
                continue
            self.uc.mem_write(paddr, blob)          # where the image holds it
            if vaddr != paddr:
                self.uc.mem_write(vaddr, blob)      # where startup copies it
        self.uc.mem_write(STUB_RET, struct.pack("<H", 0x4770))   # bx lr
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_invalid)
        self.scratch_next = SCRATCH
        self._stub_next = STUB_BASE + 0x100

    # -- symbols --------------------------------------------------------------
    def sym(self, name: str) -> Symbol:
        """A symbol by exact (mangled) name, or by a unique substring of one."""
        if name in self.elf.symbols:
            return self.elf.symbols[name]
        hits = [s for n, s in self.elf.symbols.items() if name in n]
        if len(hits) != 1:
            raise KeyError(f"{name!r}: {len(hits)} matches "
                           f"{[h.name for h in hits[:8]]}")
        return hits[0]

    def addr(self, name: str) -> int:
        return self.sym(name).addr

    # -- memory helpers -------------------------------------------------------
    def r8(self, a: int) -> int:
        return self.uc.mem_read(a, 1)[0]

    def r32(self, a: int) -> int:
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def w32(self, a: int, v: int) -> None:
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def write(self, a: int, data: bytes) -> None:
        self.uc.mem_write(a, bytes(data))

    def read(self, a: int, n: int) -> bytes:
        return bytes(self.uc.mem_read(a, n))

    def cstr(self, a: int, limit: int = 512) -> str:
        out = bytearray()
        while len(out) < limit:
            b = self.read(a + len(out), 1)
            if b == b"\0":
                break
            out += b
        return out.decode("latin-1")

    def alloc(self, size: int, align: int = 8) -> int:
        a = (self.scratch_next + align - 1) & ~(align - 1)
        self.scratch_next = a + size
        self.uc.mem_write(a, b"\0" * size)
        return a

    def put_str(self, s: str) -> int:
        a = self.alloc(len(s) + 1)
        self.write(a, s.encode("latin-1") + b"\0")
        return a

    # -- stubs ----------------------------------------------------------------
    def add_stub(self, func) -> int:
        """A Thumb address that runs `func(machine)` in Python and returns (r0 = result)."""
        a = self._stub_next
        self._stub_next += 4
        assert a < STUB_RET, "out of stub slots"
        self.uc.mem_write(a, struct.pack("<H", 0x4770))

        def hook(uc, address, size, user):
            r = func(self)
            if r is not None:
                uc.reg_write(UC_ARM_REG_R0, r & 0xFFFFFFFF)
        self.uc.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
        return a | 1

    def stub_function(self, addr: int, func) -> None:
        """Replace a firmware function: run `func(machine)` at `addr` and return."""
        def hook(uc, address, size, user):
            r = func(self)
            if r is not None:
                uc.reg_write(UC_ARM_REG_R0, r & 0xFFFFFFFF)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        self.uc.hook_add(UC_HOOK_CODE, hook, begin=addr & ~1, end=addr & ~1)

    def arg(self, i: int) -> int:
        if i < 4:
            return self.uc.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i])
        return self.r32(self.uc.reg_read(UC_ARM_REG_SP) + 4 * (i - 4))

    def _on_invalid(self, uc, access, address, size, value, user):
        pc = uc.reg_read(UC_ARM_REG_PC)
        print(f"invalid memory access {address:#010x} (size {size}) at pc={pc:#010x}", file=sys.stderr)
        return False

    # -- calls ----------------------------------------------------------------
    def call(self, func: int, *args: int, max_insns: int = 5_000_000) -> int:
        regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
        sp = STACK_TOP - 0x100
        stack_args = list(args[4:])
        sp -= 4 * len(stack_args)
        for i, v in enumerate(stack_args):
            self.w32(sp + 4 * i, v)
        for r, v in zip(regs, args[:4]):
            self.uc.reg_write(r, v & 0xFFFFFFFF)
        self.uc.reg_write(UC_ARM_REG_SP, sp)
        self.uc.reg_write(UC_ARM_REG_LR, STUB_RET | 1)
        try:
            self.uc.emu_start(func | 1, STUB_RET, count=max_insns)
        except UcError as e:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise RuntimeError(f"emulation stopped at pc={pc:#010x}: {e}") from e
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        if pc != STUB_RET:
            raise RuntimeError(f"function did not return (pc={pc:#010x}); raise max_insns?")
        return self.uc.reg_read(UC_ARM_REG_R0)
