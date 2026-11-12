#!/bin/bash
# Modular-Trackular mods. SPDX-License-Identifier: MIT
# The files for the Synth FX plugin set's card (docs/mt/DEVICE_TEST_LADDER.md), in
# build/device/synthfx_card/SynthFX/: the SDK's two examples, Delay and Tremolo, as a user
# would build them, and the two guard tests, GuardSlow and GuardHang.
#   tools/synthfx/device/build.sh [toolchain prefix]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
T="${1:-arm-none-eabi-}"
OUT="$ROOT/build/device/synthfx_card/SynthFX"
mkdir -p "$OUT"
B="python3 $ROOT/synthfx-sdk/ptsfx_builder.py --toolchain $T"
$B "$ROOT/synthfx-sdk/examples/delay.c" -o "$OUT/Delay.ptsfx"
$B "$ROOT/synthfx-sdk/examples/tremolo.c" -o "$OUT/Tremolo.ptsfx"
$B "$HERE/guard_slow.c" -o "$OUT/GuardSlow.ptsfx"
$B "$HERE/guard_hang.c" -o "$OUT/GuardHang.ptsfx"
ls -l "$OUT"
