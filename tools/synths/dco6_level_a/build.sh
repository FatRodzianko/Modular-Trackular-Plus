#!/bin/bash
# Modular-Trackular mods. SPDX-License-Identifier: MIT
# Level A for DCO6: builds build/emu/dco6_level_a.axf, the engine (core/mods/synths/d6Engine.h) behind
# plain C entry points, with the firmware's FPU flags. Uses tools/synths/level_a's link script, C
# support and shim (WAVE's Level A); needs no reference sources, the reference is the Python model.
#   tools/synths/dco6_level_a/build.sh [toolchain prefix]
# SYNTHS_SRC (a copy of core/mods/synths, with synthfx beside it) and LEVEL_A_OUT (an .axf path)
# build a planted fault (tools/synths/dco6_level_a/plant.py).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
LA="$ROOT/tools/synths/level_a"
T="${1:-arm-none-eabi-}"
SRC="${SYNTHS_SRC:-$ROOT/core/mods/synths}"
AXF="${LEVEL_A_OUT:-$ROOT/build/emu/dco6_level_a.axf}"
OUT="$(dirname "$AXF")"
mkdir -p "$OUT"
ARCH="-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb"
COMMON="$ARCH -O2 -g -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Wall -Wextra -Wdouble-promotion"
INC="-I$LA/shim -I$SRC -I$SRC/.. -I$ROOT/core/include"
B="$(basename "$AXF" .axf)"
${T}g++ $COMMON $INC -std=gnu++20 -fno-exceptions -fno-rtti -c "$HERE/dco6.cpp" -o "$OUT/${B}_d6.o"
${T}gcc $COMMON -std=gnu11 -c "$LA/support.c" -o "$OUT/${B}_support.o"
${T}gcc $ARCH -nostdlib -T "$LA/link.ld" -Wl,-e,a_d6_init -Wl,--undefined=a_d6_init \
    "$OUT/${B}_d6.o" "$OUT/${B}_support.o" -lgcc -o "$AXF"
${T}size "$AXF"
