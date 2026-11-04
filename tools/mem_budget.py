#!/usr/bin/env python3
# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Real free memory per region of a Tracker+ build (.axf), and a check that each keeps a floor.

    python3 tools/mem_budget.py platforms/imx/build-PLUS/TrackerPlus.axf [--top N]

Exit 1 if a region is under its floor. A plain-CE build (-DMT_NO_MODS) only reports SDRAM,
because plain CE leaves about 8 KB there by design.

Free means space nothing reserves. DTCM's is the gap between the heap and the stack: the heap
is a fixed block, so counting it as used or the stack as free gives the wrong answer. The heap
and the stack have their own limits that only the device can measure: put an empty .develop
file at the root of the SD card and Config shows free heap and max stack.

Layout and readings: docs/mt/CE_FIRMWARE_MAP.md section 15.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

PLAIN_SAMPLE_MEMORY = (60 * 1024 - 64) * 1024

# name: (start, end, floor in bytes). platforms/imx/linker/TrackerPlus/memory.ld
REGIONS = {
    "Flash": (0x60030000, 0x61000000, 1024 * 1024),
    "ITCM": (0x00000008, 0x00020000, 1024),
    "OCRAM": (0x20200000, 0x20208000, 1024),
    "SDRAM": (0x80000000, 0x84000000, 256 * 1024),
}
DTCM_GAP_FLOOR = 4096  # heap end to stack base
DTCM = (0x20000000, 0x20058000)


def read_elf(path: Path):
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit(f"{path}: not an ELF32 little-endian file")
    e_phoff, e_shoff = struct.unpack_from("<II", data, 28)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from("<HHHH", data, 42)

    segments = []
    for i in range(e_phnum):
        p_type, _, _, p_paddr, p_filesz = struct.unpack_from("<IIIII", data, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz:
            segments.append((p_paddr, p_filesz))

    sections = []
    symbols = []
    for i in range(e_shnum):
        sh = struct.unpack_from("<10I", data, e_shoff + i * e_shentsize)
        sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link = sh[1], sh[2], sh[3], sh[4], sh[5], sh[6]
        if sh_flags & 2 and sh_size:  # SHF_ALLOC
            sections.append((sh_addr, sh_size))
        if sh_type == 2:  # SHT_SYMTAB
            strs = struct.unpack_from("<10I", data, e_shoff + sh_link * e_shentsize)[4]
            for off in range(sh_offset, sh_offset + sh_size, 16):
                st_name, st_value, st_size, st_info = struct.unpack_from("<IIIB", data, off)
                end = data.index(b"\0", strs + st_name)
                symbols.append((data[strs + st_name:end].decode(), st_value, st_size, st_info & 15))
    return segments, sections, symbols


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("axf", type=Path)
    ap.add_argument("--top", type=int, default=0, help="list each region's N largest variables")
    args = ap.parse_args()

    segments, sections, symbols = read_elf(args.axf)
    by_name = {name: (value, size) for name, value, size, _ in symbols}
    for needed in ("_pvHeapLimit", "_vStackBase", "externalRamSampleMemory"):
        if needed not in by_name:
            sys.exit(f"{args.axf}: no symbol {needed}")
    heap_start = by_name["_pvHeapStart"][0]
    heap_end = by_name["_pvHeapLimit"][0]
    stack_base = by_name["_vStackBase"][0]
    stack_top = by_name["_vStackTop"][0]
    sample_memory = by_name["externalRamSampleMemory"][1]
    plain = sample_memory == PLAIN_SAMPLE_MEMORY

    failures = 0

    def report(name: str, size: int, free: int, floor: int | None, note: str = "") -> None:
        nonlocal failures
        ok = floor is None or free >= floor
        failures += not ok
        floor_text = f"{floor:>10,}" if floor is not None else "         -"
        print(f"{name:<8}{size:>12,}{free:>12,}{floor_text}  {'ok  ' if ok else 'LOW '}{note}")

    print(f"{'region':<8}{'size':>12}{'free':>12}{'floor':>10}")
    for name, (start, end, floor) in REGIONS.items():
        if name == "Flash":
            used_end = max((a + n for a, n in segments if start <= a < end), default=start)
        else:
            used_end = max((a + n for a, n in sections if start <= a < end), default=start)
        report(name, end - start, end - used_end, None if name == "SDRAM" and plain else floor)
    report("DTCM", DTCM[1] - DTCM[0], stack_base - heap_end, DTCM_GAP_FLOOR, "heap end to stack base")

    print()
    print(f"heap        {heap_end - heap_start:>10,}  fixed; free amount is a device reading")
    print(f"stack       {stack_top - stack_base:>10,}  fixed; max use is a device reading")
    print(f"sample pool {sample_memory:>10,}  {'plain CE' if plain else 'mods build'}")

    if args.top:
        regions = dict(REGIONS, DTCM=(DTCM[0], DTCM[1], 0))
        for name, (start, end, _) in regions.items():
            if name == "Flash":
                continue
            found = sorted((s for s in symbols if start <= s[1] < end and s[3] == 1), key=lambda s: -s[2])
            print(f"\n{name}:")
            for sym, _, size, _ in found[: args.top]:
                print(f"  {size:>10,}  {sym}")

    if failures:
        print(f"\n{failures} region(s) under the floor")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
