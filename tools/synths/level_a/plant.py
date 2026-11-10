# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Planted faults for the Synths mod's level A (tools/emu/test_synths_dsp.py): each copies
core/mods/synths and core/mods/synthfx (the effects) beside it, makes one mistake in the copy,
builds the level-A ELF from it and runs the test, which has to fail, on the check named. A fault
the test misses is a gap in the test.

    python tools/synths/level_a/plant.py <sy9's patch/synths folder> [--toolchain PREFIX] [--only NAME]

Builds run in WSL (the Arm toolchain), the test on this machine's Python; one at a time, so the
run stays well under the CPU limit. The copies and ELFs go to build/emu/plant/.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "core" / "mods" / "synths"
FX = ROOT / "core" / "mods" / "synthfx"
OUT = ROOT / "build" / "emu" / "plant"
TEST = ROOT / "tools" / "emu" / "test_synths_dsp.py"

# (name, file, text, replacement, a check the test must fail: a substring of its line)
FAULTS = [
    ("WAVE: oscillator seed", "wtEngine.h", "0x12345678u ^", "0x12345679u ^", "WAVE as sy9"),
    ("PHASE: one shape short", "phEngine.h", "#define PH_SHAPES           8u", "#define PH_SHAPES           7u",
     "PHASE as sy9"),
    ("Chorus: tap low-pass", "../synthfx/synthFx.cpp", "#define CHORUS_LP           0.7226f", "#define CHORUS_LP           0.7f",
     "Chorus as sy9"),
    ("WAVE: Post Distortion run in the voice", "wtEngine.h", "dist_block(&e->pre, out, p + P_EFFECT, dist_pre(p + P_EFFECT));",
     "dist_block(&e->pre, out, p + P_EFFECT, dist_on(p + P_EFFECT));", "Placement Post: the voice itself is untouched"),
    ("dist_reset: Pre starts faded out", "../synthfx/synthFx.cpp", "d->amount = (on && type != DIST_OFF) ? 1.0f : 0.0f;",
     "d->amount = 0.0f;", "dist_reset"),
    ("Overdrive: no trim", "../synthfx/synthFx.cpp", "#define OD_TRIM             0.28510f", "#define OD_TRIM             1.0f",
     "Level default"),
    ("Bit Crusher: Rate range", "../synthfx/synthFx.cpp", "#define CRUSH_HZ_OCT        5.4627571f",
     "#define CRUSH_HZ_OCT        4.4627571f", "Bits"),
    ("Bit Crusher: Bits from 1", "../synthfx/synthFx.cpp", "#define CRUSH_BITS_MIN      4.0f", "#define CRUSH_BITS_MIN      1.0f",
     "sounds at Gain 0"),
    ("Distortion: linear region of the anti-aliased clip", "../synthfx/synthFx.cpp", "        return 0.5f * s;\n",
     "        return s;\n", "Distortion"),
    ("half-band: one coefficient off", "../synthfx/synthFx.cpp", "0.6095243149f", "0.6395243149f", "aliases"),
    ("Flanger: second delay line left dirty", "../synthfx/synthFx.cpp", "        s->buf2[n] = 0.0f;\n", "",
     "garbage left in a state"),
    ("Phaser: feedback over 1", "../synthfx/synthFx.cpp", "#define FEEDBACK_MAX        0.95f",
     "#define FEEDBACK_MAX        1.30f", "Feedback"),
]


def wsl_path(p: Path) -> str:
    s = str(p.resolve()).replace("\\", "/")
    return f"/mnt/{s[0].lower()}{s[2:]}" if s[1] == ":" else s


def build(src: Path, axf: Path, sy9: str, prefix: str) -> bool:
    script = ROOT / "tools" / "synths" / "level_a" / "build.sh"
    cmd = ["wsl", "-d", "Ubuntu", "--", "env", f"SYNTHS_SRC={wsl_path(src)}", f"LEVEL_A_OUT={wsl_path(axf)}",
           "bash", wsl_path(script), sy9, prefix]
    r = subprocess.run(cmd, capture_output=True, text=True, env={**__import__("os").environ, "MSYS_NO_PATHCONV": "1"})
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
    return r.returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("sy9")
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--only")
    a = ap.parse_args()
    sy9 = a.sy9 if a.sy9.startswith("/") else wsl_path(Path(a.sy9))
    missed = []
    for name, file, old, new, expect in FAULTS:
        if a.only and a.only not in name:
            continue
        tag = "".join(c if c.isalnum() else "_" for c in name)
        copy = OUT / tag / "mods" / "synths"
        if copy.parent.exists():
            shutil.rmtree(copy.parent)
        shutil.copytree(SRC, copy)
        shutil.copytree(FX, copy.parent / "synthfx")
        f = copy / file
        text = f.read_bytes().decode("utf-8")
        old_n = old.replace("\n", "\r\n") if "\r\n" in text and "\n" in old else old
        assert text.count(old_n) == 1, f"{name}: the text to change is in {file} {text.count(old_n)} times"
        f.write_bytes(text.replace(old_n, new.replace("\n", "\r\n") if old_n != old else new).encode("utf-8"))
        axf = OUT / tag / "level_a.axf"
        if not build(copy, axf, sy9, a.toolchain):
            print(f"FAIL {name}: didn't build")
            missed.append(name)
            continue
        r = subprocess.run([sys.executable, str(TEST), str(axf), "--quick"], capture_output=True, text=True)
        fails = [l for l in r.stdout.splitlines() if l.startswith("FAIL")]
        caught = r.returncode != 0 and any(expect in l for l in fails)
        print(("ok   " if caught else "FAIL ") + f"{name}: " +
              (f"caught ({len(fails)} failing: {fails[0][5:][:90]})" if caught else
               f"not caught as expected (exit {r.returncode}, failing: {[l[5:60] for l in fails]})"))
        if not caught:
            missed.append(name)
    print(f"{len(FAULTS) if not a.only else 1} planted, {len(missed)} missed")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
