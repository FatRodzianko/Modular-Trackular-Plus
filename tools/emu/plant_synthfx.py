# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Planted faults for Synth FX: each makes one mistake, builds the full firmware with it and runs the
test section that has to fail, on the check named. A fault the test misses is a gap in the test.
The slots' faults go to tools/emu/test_synthfx.py (as_before with --before, the phase 1 build); the
patch editor's and the project's to test_synthfx_page.py ("page:<section>"); the record's rules to the
host test, core/mods/tests/test_synthfx.cpp ("host", built in the tree with its g++). The plugins'
own arithmetic has its faults in tools/synthfx/rig/plant.py.

    python tools/emu/plant_synthfx.py <reference.axf> --before <phase 1 axf> [--tree WSL_DIR]
        [--toolchain PREFIX] [--jobs N] [--only NAME] [--keep]

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
PAGE_TEST = ROOT / "tools" / "emu" / "test_synthfx_page.py"
MOD = "core/mods/synthfx/synthfxMod.cpp"         # the slots in the audio interrupt
SLOTS = "core/mods/synthfx/synthfxSlots.cpp"     # the slots' plugins made and let go
PAGE = "core/mods/synthfx/synthfxPage.cpp"       # the rows
SETTINGS = "core/mods/synthfx/synthfxSettings.cpp"
EDITOR = "core/interface/instrumentEditor/SynthPatchEditor.cpp"   # CRLF: a fault's \n matches its \r\n
INSTR = "core/interface/instrumentEditor/fIinstrEdit.cpp"         # CRLF

# (name, file, text, replacement, test section, engines, a check the test must fail: a substring of its line)
FAULTS = [
    # the slots in the audio interrupt
    ("Off doesn't bypass", MOD, "if (!mods::on(Mod::synthfx) || patch == nullptr || hasOwnEffects(patch))",
     "if (patch == nullptr || hasOwnEffects(patch))", "off_is_ce", "1", "Synth FX Off with Distortion"),
    ("a mono part never goes stereo", MOD, "return { true, engineStereo || widens };", "return { true, engineStereo };",
     "each_effect", "1", "ACD, Chorus in FX1"),
    ("WAVE not skipped", MOD, "return synths::ownsLayout(patch->getMetadata().instrumentTypeId);", "return false;",
     "skipped_and_shared", "", "WAVE: skipped"),
    ("no fade in", MOD, "r.phase = id >= 0 ? kFadeIn : kGone;", "r.phase = id >= 0 ? kOn : kGone;",
     "each_effect", "1", "ACD, Overdrive in FX1"),
    ("no fade out", MOD, "runs[part][slot].phase = kLeaving;", "runs[part][slot].phase = kGone;",
     "release", "", "ACD: cleared, the slots fade out"),
    ("FX2 before FX1", MOD, "    for (int slot = 0; slot < kSlots; slot++)\n    {\n        Run& r = runs[part][slot];",
     "    for (int s = 0; s < kSlots; s++)\n    {\n        const int slot = kSlots - 1 - s;\n        Run& r = runs[part][slot];",
     "order_and_swap", "", "ACD: FX1 runs before FX2"),
    # the slots' plugins in the main loop
    ("the old plugin never freed", SLOTS, "        plugins::destroy(l.old);\n", "",
     "release", "", "ACD: cleared, the plugins fade out"),
    ("freed before it faded", SLOTS, "if (!retired(part, slot) && plugins::now() - l.oldSince < kRetireMs)", "if (false)",
     "order_and_swap", "", "ACD: Overdrive swapped"),
    ("a project's knobs not set", SLOTS, "if (isFinite(want.knobs[k]))", "if (false)",
     "as_before", "1", "ACD, Overdrive"),
    ("knobs not snapped", SLOTS, "        want.knobs[k] = plugins::knob(id, k); // on the plugin's steps, as it will be saved\n",
     "", "garbage", "", "ACD: garbage knobs"),
    ("an opened project's slots never made", SLOTS, "if (takeLoaded())", "if (false && takeLoaded())",
     "page:projects", "", "Open: each slot's plugin made"),
    # the record
    ("the record never written", SETTINGS, "return store.put(kRecord, kRecordVersion, nowBytes, kRecordSize);",
     "return false;", "page:projects", "", "Save: reopened with its slots"),
    ("load ignores the record", SETTINGS, "const uint8_t* data = store.find(kRecord, &version, &length);",
     "const uint8_t* data = nullptr;", "page:projects", "", "Save: reopened with its slots"),
    ("version 1's Feedback a step off", SETTINGS, "static_cast<int>(v[V1_FEEDBACK]) - 95", "static_cast<int>(v[V1_FEEDBACK]) - 96",
     "host", "", "Phaser: Rate, Depth, Feedback"),
    ("FX2 may repeat FX1", SETTINGS, " || (slot == 1 && sameFile(file, out[part][0].file))", "",
     "host", "", "the plugin FX1 has, in FX2 as well"),
    ("a path taken for a name", SETTINGS, r" || c == '/' || c == '\\' || c == ':'", "", "host", "", "a path"),
    ("a short version 2 record read", SETTINGS, "if (version == kRecordVersion && length == kRecordSize)",
     "if (version == kRecordVersion)", "host", "", "another length"),
    ("infinite knobs kept", SETTINGS, "s.knobs[k] = finite(f) ? f : noKnob();", "s.knobs[k] = f;",
     "host", "", "infinities and NaNs are the default"),
    # the page
    ("button 3 with the mod Off", MOD, "return mods::on(Mod::synthfx) && !hasOwnEffects(&patch);",
     "return !hasOwnEffects(&patch);", "page:off", "", "mod Off"),
    ("button 3 on WAVE", MOD, "return mods::on(Mod::synthfx) && !hasOwnEffects(&patch);",
     "return mods::on(Mod::synthfx);", "page:off", "", "WAVE, mod On: no button 3"),
    ("an edit not marked for saving", EDITOR, "    mods::projectSettingEdited();\n    mtFxShowList(true);",
     "    mtFxShowList(true);", "page:page", "", "the pick lands in the project's record 16"),
    ("the same section keeps the rows", EDITOR, "if (previousValue != sectionListPos || mtFxLeft)",
     "if (previousValue != sectionListPos)", "page:page", "", "a section, even the same one"),
    ("no redraw when a plugin is made", INSTR, "    mtSynthFxPoll();\n", "", "page:page", "", "then its knobs"),
    ("the other slot's plugin offered", PAGE, "e->status != plugins::Status::ok || usedByOtherSlot(part, slot, e->file)",
     "e->status != plugins::Status::ok", "page:page", "", "FX2's list"),
    ("Delete leaves a knob", PAGE, "        plugins::setKnob(id, static_cast<uint32_t>(knob), p->default_val);\n", "",
     "page:page", "", "Delete: Gain back"),
    ("knobs two steps a tick", PAGE, "return ticks * (per > 1 ? per : 1);", "return ticks * 2;",
     "page:page", "", "Gain: 1 % a step"),
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
    ap.add_argument("--before", required=True, help="the phase 1 build, for test_synthfx.py's as_before")
    ap.add_argument("--tree", default="~/mtplant_synthfx")
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--only", help="faults whose name has this, or whose test is this (host, page)")
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
    faults = [f for f in FAULTS if not a.only or a.only in f[0] or a.only == f[4].split(":")[0]]
    for name, file, old, new, section, engines, expect in faults:
        text = (ROOT / file).read_bytes().decode("utf-8")
        if "\r\n" in text:
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        assert text.count(old) == 1, f"{name}: the text to change is in {file} {text.count(old)} times"
        planted = text.replace(old, new)
        tag = "".join(c if c.isalnum() else "_" for c in name)
        staged = OUT / f"{tag}.src"
        staged.write_bytes(planted.encode("utf-8"))
        axf = OUT / f"{tag}.axf"
        host = None
        if section == "host":
            # the host test alone, with the planted file; no firmware
            host = wsl(f"cp {wsl_path(staged)} {tree}/{file} && cd {tree}/core/mods/tests && "
                       f"make build/test_synthfx >/dev/null && ./build/test_synthfx")
            # built if it ran: its failing checks are what's wanted, so its exit code isn't the build's
            r = subprocess.CompletedProcess([], 0 if "checks," in host.stdout else 1, host.stdout, host.stderr)
        else:
            r = wsl(f"cp {wsl_path(staged)} {tree}/{file} && {make} && "
                    f"cp {tree}/platforms/imx/build-PLUS/TrackerPlus.axf {wsl_path(axf)}")
        restored = wsl(f"cp {src}/{file} {tree}/{file}")
        staged.unlink()
        if r.returncode != 0 or restored.returncode != 0:
            print(f"FAIL {name}: didn't build", r.stdout[-1200:], r.stderr[-1200:])
            missed.append(name)
            continue
        if host is not None:
            t = host
        elif section.startswith("page:"):
            cmd = [sys.executable, str(PAGE_TEST), str(axf), a.ref, "--only", section[5:]]
            t = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        else:
            cmd = [sys.executable, str(TEST), str(axf), a.ref, "--before", a.before, "--only", section] +                 (["--engines", engines] if engines else [])
            t = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        fails = [ln for ln in t.stdout.splitlines() if ln.startswith("FAIL")]
        caught = t.returncode != 0 and any(expect in ln for ln in fails)
        print(("ok   " if caught else "FAIL ") + f"{name}: " +
              (f"caught ({len(fails)} failing: {fails[0][5:][:100]})" if caught else
               f"not caught as expected (exit {t.returncode}, failing: {[ln[5:70] for ln in fails]}) {t.stdout[-600:]}"))
        if not caught:
            missed.append(name)
        if not a.keep:
            axf.unlink(missing_ok=True)
    # the tree as this checkout has it again, built, so a next run starts from there
    wsl(f"{sync} && {make}")
    if not a.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print(f"{len(faults)} planted, {len(missed)} missed")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
