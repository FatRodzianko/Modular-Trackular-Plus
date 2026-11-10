#!/bin/bash
# Modular-Trackular mods. SPDX-License-Identifier: MIT
# Level A: builds build/emu/synths_level_a.axf, WAVE's and PHASE's engines (core/mods/synths)
# and the synth effects (core/mods/synthfx) beside sy9's, from Modular-Trackular's patch/synths, as the reference.
# The reference uses sy9's own generated tables (its build/wt_tables.h and ph_tables.h).
#   tools/synths/level_a/build.sh <sy9's patch/synths folder> [toolchain prefix]
# SYNTHS_SRC (a copy of core/mods/synths, with synthfx beside it) and LEVEL_A_OUT (an .axf
# path) build a planted fault (tools/synths/level_a/plant.py).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SY9="${1:?the sy9 patch/synths folder}"
T="${2:-arm-none-eabi-}"
SRC="${SYNTHS_SRC:-$ROOT/core/mods/synths}"
FX="$SRC/../synthfx"
AXF="${LEVEL_A_OUT:-$ROOT/build/emu/synths_level_a.axf}"
OUT="$(dirname "$AXF")"
mkdir -p "$OUT"
ARCH="-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb"
COMMON="$ARCH -O2 -g -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Wall -Wextra -Wdouble-promotion"
INC="-I$HERE/shim -I$SRC -I$SRC/.. -I$ROOT/core/include"
${T}g++ $COMMON $INC -std=gnu++20 -fno-exceptions -fno-rtti -c "$HERE/ours.cpp" -o "$OUT/la_ours.o"
${T}g++ $COMMON $INC -std=gnu++20 -fno-exceptions -fno-rtti -c "$FX/synthFx.cpp" -o "$OUT/la_fx.o"
${T}gcc $COMMON -I"$SY9/build" -I$HERE/shim -I$SRC -I"$SY9" -std=gnu11 -Wno-unused-function -c "$HERE/ref.c" -o "$OUT/la_ref.o"
${T}gcc $COMMON -std=gnu11 -c "$HERE/support.c" -o "$OUT/la_support.o"
${T}gcc $ARCH -nostdlib -T "$HERE/link.ld" -Wl,-e,a_wave_init -Wl,--undefined=a_wave_init \
    "$OUT/la_ours.o" "$OUT/la_fx.o" "$OUT/la_ref.o" "$OUT/la_support.o" -lgcc -o "$AXF"
${T}size "$AXF"
