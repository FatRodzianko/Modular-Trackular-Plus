#!/bin/bash
# Modular-Trackular mods. SPDX-License-Identifier: MIT
# The Synth FX plugins' rig: build/emu/synthfx_rig.axf (core/mods/synthfx's runtime and pack,
# built as the firmware builds them, behind tools/synthfx/rig/rig.cpp) and the test plugin
# build/synthfx/rig/Probe.ptsfx, for tools/synthfx/test_runtime.py.
#   tools/synthfx/rig/build.sh [toolchain prefix]
# RIG_SRC (a copy of core/mods/synthfx) and RIG_OUT (an .axf path) build a planted fault
# (tools/synthfx/rig/plant.py).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
T="${1:-arm-none-eabi-}"
SRC="${RIG_SRC:-$ROOT/core/mods/synthfx}"
AXF="${RIG_OUT:-$ROOT/build/emu/synthfx_rig.axf}"
OUT="$(dirname "$AXF")"
TAG="$(basename "$AXF" .axf)"
mkdir -p "$OUT" "$ROOT/build/synthfx/rig"
ARCH="-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb"
COMMON="$ARCH -O2 -g -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -Wall -Wextra -Wdouble-promotion"
# The shim's modsBuild.h and config.h first
INC="-I$HERE/shim -I$SRC -I$ROOT/core/mods -I$ROOT/core/include"
CXX="${T}g++ $COMMON $INC -std=gnu++20 -fno-exceptions -fno-rtti"
$CXX -c "$HERE/rig.cpp" -o "$OUT/${TAG}_rig.o"
$CXX -c "$SRC/synthfxPlugins.cpp" -o "$OUT/${TAG}_plugins.o"
$CXX -c "$SRC/synthfxPack.cpp" -o "$OUT/${TAG}_pack.o"
${T}gcc $COMMON -std=gnu11 -c "$ROOT/tools/synths/level_a/support.c" -o "$OUT/${TAG}_support.o"
${T}gcc $ARCH -nostdlib -T "$ROOT/tools/synths/level_a/link.ld" -Wl,-e,rig_start -Wl,--undefined=rig_start \
    "$OUT/${TAG}_rig.o" "$OUT/${TAG}_plugins.o" "$OUT/${TAG}_pack.o" "$OUT/${TAG}_support.o" -lgcc -o "$AXF"
${T}size "$AXF"
python3 "$ROOT/synthfx-sdk/ptsfx_builder.py" --toolchain "$T" "$HERE/probe.c" -o "$ROOT/build/synthfx/rig/Probe.ptsfx"
