#!/usr/bin/env python3
# Modular-Trackular Plus: Master FX plugins (.ptmfx). SPDX-License-Identifier: MIT
"""Builds a Master FX plugin (.ptmfx) from C or C++ sources: the 336-byte header (ptmfx.h, format version 2),
then the plugin's code, which the firmware copies into its slot's code area and calls in the
audio interrupt.

    python ptmfx_builder.py my_effect.c [more.c ...] -o my_effect.ptmfx
           [--toolchain PREFIX] [-I DIR ...] [-D NAME[=VALUE] ...]

The sources define the header's fields (PTMFX_INFO) and the entries (PTMFX_INIT,
PTMFX_SET_PARAM, PTMFX_PROCESS, PTMFX_RESET, and PTMFX_FORMAT if wanted); see ptmfx.h and
docs/masterfx_plugins.md. The builder compiles them for the Tracker+ (Cortex-M7, single-precision
FPU) as position-independent code, with ptmfx_support.c (memset, memcpy, memmove), and refuses
the plugin if:
  - an entry is missing, or PTMFX_INFO is;
  - it has data that changes (.data, .bss, a GOT): the state belongs in the memory the host gives;
  - its code isn't the same when linked at two addresses (it wouldn't run where it's loaded);
  - it calls double-precision helpers (slow in software), unless --allow-double;
  - its code and constants are over the slot's 8 KB;
  - the header's fields are out of range (slot, knobs, labels, steps, the state's size).
Needs the Arm GNU toolchain (arm-none-eabi-gcc), e.g. Arm's 10.3 release. Built on MT+'s
synthfx-sdk/ptsfx_builder.py, itself adapted in part from CE's editor-fx-sdk/ptfx_builder.py,
Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors,
under the MIT License (LICENSE at the repository's root, part 1); the format is MT+'s own.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

MAGIC = 0x464D5450
VERSION = 2
HEADER_SIZE = 336
PARAM_SIZE = 32
MAX_PARAMS = 5
MAX_LABELS = 6
LABEL_SIZE = 12
NO_ENTRY = 0xFFFFFFFF
MAX_CODE = 8 * 1024            # a slot's code area
SLOTS = {1: ("reverb", 1024, 4), 2: ("delay", 512, 6), 3: ("dynamics", 512, 3)}  # name, most state, CE labels
FLAG_FLOAT, FLAG_DUCK, FLAG_OWN_DYNAMICS, FLAG_BUS = 0x0001, 0x0002, 0x0004, 0x0008
FLAGS_KNOWN = FLAG_FLOAT | FLAG_DUCK | FLAG_OWN_DYNAMICS | FLAG_BUS
DYNAMICS = 3
ENTRIES = ("ptmfx_init", "ptmfx_set_param", "ptmfx_process", "ptmfx_reset")
OPTIONAL = ("ptmfx_format",)
BASES = (0x0, 0x12340)         # the two link addresses compared

# Header offsets (ptmfx.h)
O_FLAGS, O_SLOT, O_NPARAMS, O_NAME, O_AUTHOR, O_LABELS = 6, 8, 9, 12, 44, 60
O_STATE, O_CODE = 132, 136
O_RESERVED2, O_PARAMS = 164, 176

ARCH = ["-mcpu=cortex-m7", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard", "-mthumb"]
CFLAGS = ARCH + ["-O2", "-ffreestanding", "-fno-builtin", "-fno-common", "-fPIC",
                 "-mpic-data-is-text-relative", "-fvisibility=hidden", "-ffunction-sections",
                 "-fdata-sections", "-Wall", "-Wextra", "-Wdouble-promotion", "-g0"]
CXXFLAGS = ["-std=gnu++20", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
            "-fno-use-cxa-atexit"]

LINKER_SCRIPT = """
ENTRY(ptmfx_process)
SECTIONS
{{
    . = {base:#x};
    .text : ALIGN(8)
    {{
        *(.text.ptmfx_*)
        *(.text*)
        *(.rodata*)
        *(.glue_7*)
        *(.vfp11_veneer)
        . = ALIGN(8);
    }}
    .data : {{ *(.data*) }}
    .bss : {{ *(.bss*) *(COMMON) }}
    .got : {{ *(.got*) *(.igot*) }}
    .ptmfx_info 0x40000000 : {{ KEEP(*(.ptmfx_info)) }}
    /DISCARD/ : {{ *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.init_array*) *(.fini_array*) }}
}}
"""


class BuildError(Exception):
    pass


def run(cmd: list[str]) -> str:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise BuildError(f"{' '.join(cmd[:2])} ...: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout


def sections(tool: str, elf: Path) -> dict[str, int]:
    """Section name -> size, from readelf"""
    out = run([tool + "readelf", "-S", "-W", str(elf)])
    sizes = {}
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith("["):
            continue
        parts = line[line.index("]") + 1:].split()
        if len(parts) >= 5 and parts[0].startswith("."):
            sizes[parts[0]] = int(parts[4], 16)
    return sizes


def symbols(tool: str, elf: Path) -> dict[str, int]:
    out = run([tool + "nm", str(elf)])
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("latin-1")


def check_header(h: bytearray) -> list[str]:
    """The problems with a filled header's plugin fields (the firmware checks the same)"""
    errs = []
    flags, = struct.unpack_from("<H", h, O_FLAGS)
    slot, nparams = h[O_SLOT], h[O_NPARAMS]
    name, author = h[O_NAME:O_NAME + 32], h[O_AUTHOR:O_AUTHOR + 16]
    state_size, = struct.unpack_from("<I", h, O_STATE)
    if flags & ~FLAGS_KNOWN:
        errs.append(f"unknown flags {flags:#x}")
    elif flags & FLAG_DUCK and (slot != DYNAMICS or not flags & FLAG_FLOAT):
        errs.append("PTMFX_FLAG_DUCK needs a dynamics plugin with PTMFX_FLAG_FLOAT")
    elif flags & FLAG_BUS and (slot != DYNAMICS or not flags & FLAG_FLOAT or flags & FLAG_DUCK):
        errs.append("PTMFX_FLAG_BUS needs a dynamics plugin with PTMFX_FLAG_FLOAT, and not PTMFX_FLAG_DUCK")
    elif flags & FLAG_OWN_DYNAMICS and (slot != DYNAMICS or nparams < 3):
        errs.append("PTMFX_FLAG_OWN_DYNAMICS needs a dynamics plugin with 3 own knobs or more")
    if slot not in SLOTS:
        errs.append(f"slot {slot}: PTMFX_SLOT_REVERB, PTMFX_SLOT_DELAY or PTMFX_SLOT_DYNAMICS")
        return errs
    slot_name, most_state, labels = SLOTS[slot]
    if h[10] or h[11] or any(h[O_RESERVED2:O_RESERVED2 + 12]):
        errs.append("reserved fields must be 0")
    if b"\0" not in name or not cstr(name):
        errs.append("name: empty or not ended by a 0 (31 characters at most)")
    if b"\0" not in author:
        errs.append("author: 15 characters at most")
    for i in range(MAX_LABELS):
        lab = h[O_LABELS + i * LABEL_SIZE:O_LABELS + (i + 1) * LABEL_SIZE]
        if b"\0" not in lab:
            errs.append(f"label {i}: 11 characters at most")
        elif i >= labels and cstr(lab):
            errs.append(f"label {i}: the {slot_name} page has {labels} of CE's knobs")
    if nparams > MAX_PARAMS:
        errs.append(f"{nparams} knobs: {MAX_PARAMS} at most")
    elif nparams > 2 and not flags & FLAG_OWN_DYNAMICS:
        errs.append(f"{nparams} knobs: 2 at most without PTMFX_FLAG_OWN_DYNAMICS")
    if not 0 < state_size <= most_state:
        errs.append(f"state_size {state_size}: 1 to {most_state} bytes for a {slot_name} plugin")
    for i in range(min(nparams, MAX_PARAMS)):
        o = O_PARAMS + i * PARAM_SIZE
        label, dtype = h[o:o + 11], h[o + 11]
        lo, hi, de, st = struct.unpack_from("<ffff", h, o + 12)
        unit = h[o + 28:o + 32]
        what = f"knob {i} ({cstr(label)!r})"
        if b"\0" not in label or not cstr(label):
            errs.append(f"{what}: label empty or over 10 characters")
        if b"\0" not in unit:
            errs.append(f"{what}: unit over 3 characters")
        if dtype not in b"fdl":
            errs.append(f"{what}: display type {dtype!r}")
        if not (st > 0 and hi > lo):
            errs.append(f"{what}: needs step > 0 and max > min")
            continue
        steps = (hi - lo) / st
        if abs(steps - round(steps)) > 1e-3 or round(steps) > 100000:
            errs.append(f"{what}: (max - min) / step isn't a whole number of steps")
        k = (de - lo) / st
        if de < lo or de > hi or abs(k - round(k)) > 1e-3:
            errs.append(f"{what}: default {de} isn't on a step between min and max")
    return errs


def build(sources: list[Path], out: Path, tool: str, includes: list[str], defines: list[str],
          allow_double: bool = False, quiet: bool = False) -> dict:
    if shutil.which(tool + "gcc") is None:
        raise BuildError(f"{tool}gcc not found: pass --toolchain with the Arm GNU toolchain's prefix")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        objs = []
        inc = [f"-I{HERE}"] + [f"-I{i}" for i in includes] + [f"-D{d}" for d in defines]
        for i, src in enumerate(list(sources) + [HERE / "ptmfx_support.c"]):
            obj = tmp / f"{i}_{src.stem}.o"
            cxx = src.suffix.lower() in (".cpp", ".cc", ".cxx")
            cmd = [tool + ("g++" if cxx else "gcc")] + CFLAGS + (CXXFLAGS if cxx else ["-std=gnu11"]) + inc
            run(cmd + ["-c", str(src), "-o", str(obj)])
            objs.append(str(obj))
        bins, syms = [], []
        for base in BASES:
            ld = tmp / f"plugin_{base:x}.ld"
            ld.write_text(LINKER_SCRIPT.format(base=base))
            elf = tmp / f"plugin_{base:x}.elf"
            run([tool + "gcc"] + ARCH + ["-nostdlib", "-Wl,--gc-sections", "-T", str(ld)] +
                [f"-Wl,--undefined={e}" for e in ENTRIES + OPTIONAL + ("ptmfx_info",)] +
                objs + ["-lgcc", "-o", str(elf)])
            sec = sections(tool, elf)
            for s in (".data", ".bss", ".got"):
                if sec.get(s, 0):
                    raise BuildError(f"{s} is {sec[s]} bytes: a plugin keeps no globals that change "
                                     "(state goes in the memory the host gives; constants need const)")
            if not sec.get(".ptmfx_info"):
                raise BuildError("no PTMFX_INFO: the plugin's header fields are missing")
            text = tmp / f"text_{base:x}.bin"
            info = tmp / f"info_{base:x}.bin"
            run([tool + "objcopy", "-O", "binary", "-j", ".text", str(elf), str(text)])
            run([tool + "objcopy", "-O", "binary", "-j", ".ptmfx_info", str(elf), str(info)])
            bins.append((text.read_bytes(), info.read_bytes()))
            syms.append({k: v - base for k, v in symbols(tool, elf).items()})
        (code, info), (code2, _) = bins
        if code != code2:
            raise BuildError("the code changes with where it's linked: something takes an absolute "
                             "address (a table of pointers, or a global outside the state)")
        s = syms[0]
        missing = [e for e in ENTRIES if e not in s]
        if missing:
            raise BuildError(f"missing entries: {', '.join(missing)}")
        dbl = sorted(n for n in s if n.startswith(("__aeabi_d", "__aeabi_f2d", "__aeabi_i2d", "__aeabi_ui2d",
                                                      "__aeabi_l2d", "__adddf3", "__muldf3", "__divdf3")))
        if dbl and not allow_double:
            raise BuildError(f"double precision in software ({', '.join(dbl[:4])}): use float, 0.5f, sinf")
        if len(info) != HEADER_SIZE:
            raise BuildError(f"PTMFX_INFO is {len(info)} bytes, not {HEADER_SIZE}")
        if len(code) > MAX_CODE:
            raise BuildError(f"the code and constants are {len(code)} bytes: {MAX_CODE} at most")
        h = bytearray(info)
        struct.pack_into("<IH", h, 0, MAGIC, VERSION)
        offsets = [s[e] & ~1 for e in ENTRIES] + [s[o] & ~1 if o in s else NO_ENTRY for o in OPTIONAL]
        struct.pack_into("<7I", h, O_CODE, HEADER_SIZE, len(code), *offsets)
        errs = check_header(h)
        if errs:
            raise BuildError("the header: " + "; ".join(errs))
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(bytes(h) + code)
        flags, = struct.unpack_from("<H", h, O_FLAGS)
        summary = {"name": cstr(h[O_NAME:O_NAME + 32]), "slot": SLOTS[h[O_SLOT]][0], "code": len(code),
                   "state": struct.unpack_from("<I", h, O_STATE)[0], "knobs": h[O_NPARAMS], "flags": flags}
        if not quiet:
            print(f"{out.name}: {summary['name']!r}, {summary['slot']}, {summary['code']} bytes of code, "
                  f"{summary['state']} bytes of state, {summary['knobs']} knobs, flags {flags:#x}")
        return summary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("sources", nargs="+", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--toolchain", default="arm-none-eabi-", help="the tools' prefix, e.g. /opt/arm-gnu-toolchain/bin/arm-none-eabi-")
    ap.add_argument("-I", dest="includes", action="append", default=[])
    ap.add_argument("-D", dest="defines", action="append", default=[])
    ap.add_argument("--allow-double", action="store_true")
    a = ap.parse_args()
    try:
        build(a.sources, a.output, a.toolchain, a.includes, a.defines, a.allow_double)
    except BuildError as e:
        print(f"ptmfx_builder: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
