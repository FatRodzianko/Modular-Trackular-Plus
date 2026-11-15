#!/bin/bash
# Modular-Trackular mods. SPDX-License-Identifier: MIT
# The Master FX guard tests and the SDK's examples, for tools/masterfx/test_guards.py and the
# device check's card (docs/mt/DEVICE_TEST_LADDER.md), in build/device/masterfx_card/MasterFX/:
# GuardSlow, GuardOutput, GuardGain and GuardHang (not for music), and Room and Ducker as a user
# would build them.
#   tools/masterfx/device/build.sh [toolchain prefix]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
T="${1:-arm-none-eabi-}"
OUT="$ROOT/build/device/masterfx_card/MasterFX"
mkdir -p "$OUT"
B="python3 $ROOT/masterfx-sdk/ptmfx_builder.py --toolchain $T"
$B "$ROOT/masterfx-sdk/examples/room.c" -o "$OUT/Room.ptmfx"
$B "$ROOT/masterfx-sdk/examples/ducker.c" -o "$OUT/Ducker.ptmfx"
$B "$HERE/guard_slow.c" -o "$OUT/GuardSlow.ptmfx"
$B "$HERE/guard_output.c" -o "$OUT/GuardOutput.ptmfx"
$B "$HERE/guard_gain.c" -o "$OUT/GuardGain.ptmfx"
$B "$HERE/guard_hang.c" -o "$OUT/GuardHang.ptmfx"
ls -l "$OUT"
