# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Planted faults for DCO6's Level A (tools/emu/test_dco6_dsp.py): each copies core/mods/synths (and
core/mods/synthfx beside it, for fxDsp.h), makes one mistake in the copy, builds the Level A ELF from
it and runs the test, which has to fail, on the check named. A fault the test misses is a gap in it.

    python tools/synths/dco6_level_a/plant.py [--toolchain PREFIX] [--only NAME]

Builds run in WSL (the Arm toolchain), the test on this machine's Python, one at a time, so the run
stays under the CPU limit. The copies and ELFs go to build/emu/plant_dco6/.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "core" / "mods" / "synths"
FX = ROOT / "core" / "mods" / "synthfx"
OUT = ROOT / "build" / "emu" / "plant_dco6"
TEST = ROOT / "tools" / "emu" / "test_dco6_dsp.py"
SCRIPT = ROOT / "tools" / "synths" / "dco6_level_a" / "build.sh"

# (name, text, replacement, a check the test must fail: a substring of its line); all in d6Engine.h
FAULTS = [
    ("4-point correction: a sign flipped", "    z[2] -= step * (0.5f", "    z[2] += step * (0.5f", "aliasing"),
    ("the sub's step left out", "2.0f * (mixPulse - mixSaw + mixSub * sub)", "2.0f * (mixPulse - mixSaw)",
     "port = model: saw + sub"),
    ("ladder: feedback through a unit delay", "const float y4 = (G4 * x + S) * fbDen;", "const float y4 = s3;",
     "port = model"),
    ("unison: pan sides swapped", "gl = fsqrt(2.0f * (1.0f - a)) * stack;", "gl = fsqrt(2.0f * a) * stack;",
     "port = model: unison slot"),
    ("attack: 99 % in the Attack time again", "#define D6_TC_ATTACK        2.1155f",
     "#define D6_TC_ATTACK        6.6365f", "Attack"),
    ("LFO Free: a new voice doesn't take the phase", "            v->lfoPhase = s->phase;\n", "", "LFO Trigger Free"),
    ("VCA Gate: waits for the envelope", "(gateVca ? gate < 1e-4f : env < 1e-4f)",
     "(env < 1e-4f && (!gateVca || gate < 1e-4f))", "VCA Gate"),
    ("HPF: corner an octave high", "tan_pade(3.14159265f * hpfHz / D6_RATE)", "tan_pade(6.2831853f * hpfHz / D6_RATE)",
     "HPF 240"),
]


def wsl_path(p: Path) -> str:
    s = str(p.resolve()).replace("\\", "/")
    return f"/mnt/{s[0].lower()}{s[2:]}" if s[1] == ":" else s


def build(src: Path, axf: Path, prefix: str) -> bool:
    """build.sh from its own folder (it finds dco6.cpp and Level A's support files from there), with
    LF line ends (the worktree's .sh files may be CRLF), on the planted copy of the engine"""
    lf = SCRIPT.parent / "build_lf.sh"
    lf.write_bytes(SCRIPT.read_bytes().replace(bytes((13, 10)), bytes((10,))))   # CRLF to LF
    cmd = ["wsl", "-d", "Ubuntu", "--", "env", f"SYNTHS_SRC={wsl_path(src)}", f"LEVEL_A_OUT={wsl_path(axf)}",
           "bash", wsl_path(lf), prefix]
    r = subprocess.run(cmd, capture_output=True, text=True, env={**os.environ, "MSYS_NO_PATHCONV": "1"})
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
    return r.returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--only")
    a = ap.parse_args()
    missed, ran = [], 0
    for name, old, new, expect in FAULTS:
        if a.only and a.only not in name:
            continue
        ran += 1
        tag = "".join(c if c.isalnum() else "_" for c in name)
        copy = OUT / tag / "mods" / "synths"
        if copy.parent.exists():
            shutil.rmtree(copy.parent)
        shutil.copytree(SRC, copy, ignore=shutil.ignore_patterns("build", "images"))
        shutil.copytree(FX, copy.parent / "synthfx")
        f = copy / "d6Engine.h"
        text = f.read_bytes().decode("utf-8")
        crlf = "\r\n" in text
        old_n, new_n = (old.replace("\n", "\r\n"), new.replace("\n", "\r\n")) if crlf else (old, new)
        assert text.count(old_n) == 1, f"{name}: the text to change is in d6Engine.h {text.count(old_n)} times"
        f.write_bytes(text.replace(old_n, new_n).encode("utf-8"))
        axf = OUT / tag / "dco6_level_a.axf"
        if not build(copy, axf, a.toolchain):
            print(f"FAIL {name}: didn't build")
            missed.append(name)
            continue
        r = subprocess.run([sys.executable, str(TEST), str(axf), "--quick"], capture_output=True, text=True)
        fails = [ln for ln in r.stdout.splitlines() if ln.startswith("FAIL")]
        caught = r.returncode != 0 and any(expect in ln for ln in fails)
        print(("ok   " if caught else "FAIL ") + f"{name}: " +
              (f"caught ({len(fails)} failing: {fails[0][5:][:90]})" if caught else
               f"not caught as expected (exit {r.returncode}, failing: {[ln[5:60] for ln in fails]})"), flush=True)
        if not caught:
            missed.append(name)
    (SCRIPT.parent / "build_lf.sh").unlink(missing_ok=True)
    print(f"{ran} planted, {len(missed)} missed")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
