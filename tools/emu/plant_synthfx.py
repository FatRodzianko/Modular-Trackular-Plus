# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Planted faults for Synth FX's level B (tools/emu/test_synthfx.py): each makes one mistake in the
insert or the effects, builds the full firmware with it and runs the test section that has to fail,
on the check named. A fault the test misses is a gap in the test.

    python tools/emu/plant_synthfx.py <reference.axf> [--tree WSL_DIR] [--toolchain PREFIX] [--jobs N] [--only NAME] [--keep]

The builds run in WSL in a copy of this checkout (--tree, default ~/mtplant_synthfx), synced from it
first, so each fault rebuilds only its own file; one fault at a time with --jobs compilers (default
3), so the run stays under the CPU limit. Each fault's firmware goes to build/emu/plant_synthfx/,
and with --keep stays there to look into.
The tree is left as this checkout has it.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "emu" / "plant_synthfx"
TEST = ROOT / "tools" / "emu" / "test_synthfx.py"
MOD = "core/mods/synthfx/synthfxMod.cpp"
FX = "core/mods/synthfx/synthFx.cpp"

# (name, file, text, replacement, test section, engines, a check the test must fail: a substring of its line)
FAULTS = [
    ("Off doesn't bypass", MOD, "const bool on = mods::on(Mod::synthfx);", "const bool on = true;",
     "off_is_ce", "1", "Synth FX Off with Phaser"),
    ("a mono part never goes stereo", MOD,
     "return { s, engineStereo || s->running || list_pick(fx[FXP_EFFECT], FX_TYPES) != FX_NONE };",
     "return { s, engineStereo };", "each_effect", "1", "ACD, Chorus"),
    ("the state is never let go", MOD, "if (!wanted(fx) && fx_idle(s))", "if (!wanted(fx) && fx_idle(s) && part < 0)",
     "release", "", "ACD: cleared"),
    ("PERC through the mono effects", MOD, "    if (engineStereo)\n    {\n        fx_block_stereo",
     "    if (engineStereo && part < 0)\n    {\n        fx_block_stereo", "each_effect", "6", "PERC, Overdrive"),
    ("WAVE not skipped", MOD, "return synths::ownsLayout(patch->getMetadata().instrumentTypeId);",
     "return patch == nullptr;", "skipped_and_shared", "", "WAVE: skipped"),
    ("stereo dry right from the left", FX, "float x = L[n], xr = Stereo ? R[n] : x;\n            s->buf[pos]",
     "float x = L[n], xr = x;\n            s->buf[pos]", "mix_zero", "6", "PERC, Chorus at Mix 0"),
    # Mix unclamped alone heals (step_to gives its target for a NaN), so eased as well: a NaN then stays
    ("Mix unclamped and eased (a NaN stays)", FX,
     "float want = type == s->active ? clampf(fx[FXP_MIX], 0.0f, 1.0f) : 0.0f;\n    float m0 = s->mix, m1 = step_to(m0, want);",
     "float want = type == s->active ? fx[FXP_MIX] : 0.0f;\n    float m0 = s->mix, m1 = m0 + 0.5f * (want - m0);",
     "garbage", "", "garbage settings"),
]


def wsl_path(p: Path) -> str:
    s = str(p.resolve()).replace("\\", "/")
    return f"/mnt/{s[0].lower()}{s[2:]}" if s[1] == ":" else s


def wsl(script: str) -> subprocess.CompletedProcess:
    return subprocess.run(["wsl", "-d", "Ubuntu", "--exec", "bash", "-c", script], capture_output=True, text=True,
                          env={**os.environ, "MSYS_NO_PATHCONV": "1"})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("--tree", default="~/mtplant_synthfx")
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--only")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    tree, src = a.tree, wsl_path(ROOT)
    # by content, and without the source's times: a changed file gets a new one, so make rebuilds it
    # even when the tree's object is newer than the checkout's edit
    sync = (f"mkdir -p {tree} && rsync -rlpD --checksum --exclude .git --exclude 'build/' --exclude 'build-*' "
            f"{src}/ {tree}/")
    make = f"cd {tree} && nice -n 10 make plus NPROC={a.jobs} TOOLCHAIN={a.toolchain} EDITION_NAME=MT"
    r = wsl(f"{sync} && {make}")
    if r.returncode != 0:
        print("FAIL the unchanged tree didn't build:", r.stdout[-1500:], r.stderr[-1500:])
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    missed = []
    faults = [f for f in FAULTS if not a.only or a.only in f[0]]
    for name, file, old, new, section, engines, expect in faults:
        text = (ROOT / file).read_bytes().decode("utf-8")
        assert text.count(old) == 1, f"{name}: the text to change is in {file} {text.count(old)} times"
        planted = text.replace(old, new)
        tag = "".join(c if c.isalnum() else "_" for c in name)
        staged = OUT / f"{tag}.src"
        staged.write_bytes(planted.encode("utf-8"))
        axf = OUT / f"{tag}.axf"
        r = wsl(f"cp {wsl_path(staged)} {tree}/{file} && {make} && "
                f"cp {tree}/platforms/imx/build-PLUS/TrackerPlus.axf {wsl_path(axf)}")
        restored = wsl(f"cp {src}/{file} {tree}/{file}")
        staged.unlink()
        if r.returncode != 0 or restored.returncode != 0:
            print(f"FAIL {name}: didn't build", r.stdout[-1200:], r.stderr[-1200:])
            missed.append(name)
            continue
        cmd = [sys.executable, str(TEST), str(axf), a.ref, "--only", section] + (["--engines", engines] if engines else [])
        t = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        fails = [ln for ln in t.stdout.splitlines() if ln.startswith("FAIL")]
        caught = t.returncode != 0 and any(expect in ln for ln in fails)
        print(("ok   " if caught else "FAIL ") + f"{name}: " +
              (f"caught ({len(fails)} failing: {fails[0][5:][:100]})" if caught else
               f"not caught as expected (exit {t.returncode}, failing: {[ln[5:70] for ln in fails]}) {t.stdout[-600:]}"))
        if not caught:
            missed.append(name)
        if not a.keep:
            axf.unlink()
    # the tree as this checkout has it again, built, so a next run starts from there
    wsl(f"{sync} && {make}")
    if not a.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print(f"{len(faults)} planted, {len(missed)} missed")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
