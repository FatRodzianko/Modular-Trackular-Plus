# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Planted faults for Synth FX: each makes one mistake, builds the full firmware with it and runs the
test section that has to fail, on the check named. A fault the test misses is a gap in the test.
The slots' faults go to tools/emu/test_synthfx.py (as_before and old_patches with --before, the phase 1
build); the
patch editor's and the project's to test_synthfx_page.py ("page:<section>"); the record's rules to the
host test, core/mods/tests/test_synthfx.cpp ("host", built in the tree with its g++); USB storage
mode's through CE's own switch to boot_rig.py ("boot", the release tier's Synth FX project, made from
--project). The plugins' own arithmetic has its faults in tools/synthfx/rig/plant.py.

    python tools/emu/plant_synthfx.py <reference.axf> --before <phase 1 axf> [--tree WSL_DIR]
        [--toolchain PREFIX] [--jobs N] [--only NAME] [--keep] [--project SOURCE]

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
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "emu" / "plant_synthfx"
TEST = ROOT / "tools" / "emu" / "test_synthfx.py"
PAGE_TEST = ROOT / "tools" / "emu" / "test_synthfx_page.py"
BOOT = ROOT / "tools" / "emu" / "boot_rig.py"
MOD = "core/mods/synthfx/synthfxMod.cpp"         # the slots in the audio interrupt
SLOTS = "core/mods/synthfx/synthfxSlots.cpp"     # the slots' plugins made and let go
PAGE = "core/mods/synthfx/synthfxPage.cpp"       # the rows
SETTINGS = "core/mods/synthfx/synthfxSettings.cpp"
WAVE = "core/mods/synths/waveSynth.cpp"          # its voices' Pre hook
PLUGINS = "core/mods/synthfx/synthfxPlugins.cpp"  # the guards: the marker and safe mode
PLATFORM = "core/mods/synthfx/synthfxPluginsCe.cpp"  # the time guard's budget
EDITOR = "core/interface/instrumentEditor/SynthPatchEditor.cpp"   # CRLF: a fault's \n matches its \r\n
INSTR = "core/interface/instrumentEditor/fIinstrEdit.cpp"         # CRLF

# (name, file, text, replacement, test section, engines, a check the test must fail: a substring of its line)
FAULTS = [
    # the slots in the audio interrupt
    ("Off doesn't bypass", MOD, "if (!mods::on(Mod::synthfx) || patch == nullptr)",
     "if (patch == nullptr)", "off_is_ce", "1", "Synth FX Off with Distortion"),
    ("a mono part never goes stereo", MOD, "return { true, engineStereo || widens, engineStereo && !sidesAlike(patch) };",
     "return { true, engineStereo, engineStereo && !sidesAlike(patch) };", "each_effect", "1", "ACD, Chorus in FX1"),
    ("WAVE taken as stereo in", MOD, "return { true, engineStereo || widens, engineStereo && !sidesAlike(patch) };",
     "return { true, engineStereo || widens, engineStereo };", "wave_off_and_mono", "", "WAVE: FX1 runs on"),
    ("no fade in", MOD, "r.phase = count > 0 && ids[0] >= 0 ? kFadeIn : kGone;",
     "r.phase = count > 0 && ids[0] >= 0 ? kOn : kGone;", "each_effect", "1", "ACD, Overdrive in FX1"),
    ("no fade out", MOD, "runs[part][slot].phase = kLeaving;", "runs[part][slot].phase = kGone;",
     "release", "", "ACD: cleared, the slots fade out"),
    ("FX2 before FX1", MOD, "    for (int slot = 0; slot < kSlots; slot++)\n    {\n        Run& r = runs[part][slot];",
     "    for (int s = 0; s < kSlots; s++)\n    {\n        const int slot = kSlots - 1 - s;\n        Run& r = runs[part][slot];",
     "order_and_swap", "", "ACD: FX1 runs before FX2"),
    # Placement Pre on WAVE, in the audio interrupt
    ("a Pre slot run on the part as well", MOD, "if (preInVoices[part] && r.count > 1)", "if (false)",
     "pre", "", "WAVE, FX1 Distortion Pre"),
    ("the voices share one instance", MOD, "const int id = r.id[seat];", "const int id = r.id[0];",
     "pre", "", "WAVE, FX1 Distortion Pre"),
    ("Pre never reset", MOD, "        if (fresh)\n        {\n            plugins::reset(id);",
     "        if (false)\n        {\n            plugins::reset(id);", "pre", "", "WAVE, FX1 Distortion Pre"),
    ("WAVE's voices never run Pre", WAVE, "mods::synthfx::runPre(v, buf, v->eng.fresh != 0);", "(void)v;",
     "pre", "", "WAVE: Pre goes through the filter"),
    # an old WAVE or PHASE patch's effects, in the main loop
    ("an old patch's own effects left on", MOD,
     "        patch.setParamValue(kOldFirstParam + kOldEffect, 0, polysynth::Patch::ParamChangeSource::kPatchAssign);\n"
     "        patch.setParamValue(kOldFirstParam + kOldDist, 0, polysynth::Patch::ParamChangeSource::kPatchAssign);\n",
     "", "old_patches", "", "an old patch's Distortion and Phaser move"),
    ("converted while the file manager is busy", MOD,
     "if (newFileManager.getStatus() != fmIdle || newFileManager.getCurrentOperation() != fmNoOperation)",
     "if (false)", "old_patches", "", "an old patch's Distortion and Phaser move"),
    ("an old patch's Gain not in percent", SETTINGS, "fx1.knobs[0] = knobFrom(fx[kOldGain], 100.0f);",
     "fx1.knobs[0] = knobFrom(fx[kOldGain], 1.0f);", "host", "", "WAVE: the Distortion in FX1"),
    ("an old PHASE patch's Distortion Pre", SETTINGS, "fx1.pre = wave && pick(fx[kOldPlacement], 2) == 0 ? 1 : 0;",
     "fx1.pre = ((void)wave, pick(fx[kOldPlacement], 2) == 0) ? 1 : 0;", "host", "", "PHASE: the same, Post"),
    # the slots' plugins in the main loop
    ("the old plugin never freed", SLOTS, "            plugins::destroy(l.old[v]);\n", "",
     "release", "", "ACD: cleared, the plugins fade out"),
    ("a Pre slot made once", SLOTS, "want.pre != 0 && (h->flags & PTSFX_FLAG_PRE) != 0 ? kVoices : 1", "1",
     "pre", "", "WAVE, FX1 Distortion Pre"),
    ("a knob edit on the first voice's alone", SLOTS, "        plugins::setKnob(l.id[i], knob, v);\n", "",
     "pre", "", "a knob edit on FX1 reaches every voice's"),
    ("freed before it faded", SLOTS, "if (!retired(part, slot) && plugins::now() - l.oldSince < kRetireMs)", "if (false)",
     "order_and_swap", "", "ACD: Overdrive swapped"),
    ("a project's knobs not set", SLOTS, "if (isFinite(want.knobs[k]))", "if (false)",
     "as_before", "1", "ACD, Overdrive"),
    ("knobs not snapped", SLOTS, "        want.knobs[k] = plugins::knob(first, k); // on the plugin's steps, as it will be saved\n",
     "", "garbage", "", "ACD: garbage knobs"),
    ("an opened project's slots never made", SLOTS, "const bool loaded = takeLoaded();",
     "const bool loaded = false && takeLoaded();", "page:projects", "", "Open: each slot's plugin made"),
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
    ("the Placements never saved", SETTINGS, "bytes[kRecordSizeV2 + part * kSlots + slot] = s.file[0] != 0 && s.pre != 0 ? 1 : 0;",
     "bytes[kRecordSizeV2 + part * kSlots + slot] = 0;", "host", "", "the Placements: a byte for each slot"),
    ("any Placement byte read as Pre", SETTINGS, "placement[part * kSlots + slot] == 1 ? 1 : 0",
     "placement[part * kSlots + slot] != 0 ? 1 : 0", "host", "", "Pre only as 1"),
    ("version 2 read at any length", SETTINGS, "else if (version == kRecordVersionV2 && length == kRecordSizeV2)",
     "else if (version == kRecordVersionV2)", "host", "", "version 2 at version 3's length"),
    # the guards on the real firmware, with the device test's plugins (test_synthfx.py's guards)
    ("the time budget a whole block", PLATFORM, "timer()->clockCyclesPerUs() * (2902u / 4u)",
     "timer()->clockCyclesPerUs() * 2902u", "guards", "", "GuardSlow Heavy"),
    ("safe mode skips nothing", PLUGINS, "if (fromProject && inSafeMode)", "if (fromProject && inSafeMode && !fromProject)",
     "guards", "", "safe mode: the marker found at start"),
    ("the marker never written", PLUGINS, "        io->write(kMarker, &one, 1);\n", "        (void)one;\n",
     "guards", "", "GuardHang: the marker on the card"),
    # the page
    ("button 3 with the mod Off", MOD, "    return mods::on(Mod::synthfx);\n}\n\nbool partIsWave(int part)",
     "    return true;\n}\n\nbool partIsWave(int part)", "page:off", "", "mod Off"),
    ("a Placement row on every engine", PAGE, "return h != nullptr && (h->flags & PTSFX_FLAG_PRE) != 0 && partIsWave(part);",
     "return h != nullptr && (h->flags & PTSFX_FLAG_PRE) != 0;", "page:page", "", "PHASE, FX1 Distortion: no Placement"),
    ("Placement Pre not made again", PAGE, "        remake(part, slot, false); // a Pre plugin is one instance per voice: made again\n",
     "", "page:page", "", "WAVE, FX1 Distortion: Placement"),
    ("an edit not marked for saving", EDITOR, "    mods::projectSettingEdited();\n    mtFxShowList(true);",
     "    mtFxShowList(true);", "page:page", "", "Pre then"),  # a pick's record is stored once it is made
    ("the same section keeps the rows", EDITOR, "if (previousValue != sectionListPos || mtFxLeft)",
     "if (previousValue != sectionListPos)", "page:page", "", "a section, even the same one"),
    ("no redraw when a plugin is made", INSTR, "    if (mtSynthFxPoll())", "    if (false)", "page:page", "",
     "then its knobs"),
    # the device's P2, 2026-11-12: CE calls the editor's update only while moduleRefresh is set
    ("the rows polled only once", INSTR, "        moduleRefresh = 1; // polled every main loop while the rows are shown",
     "", "page:page", "", "the rows redrawn by themselves"),
    ("button 3 doesn't start the poll", EDITOR, "        IE->moduleRefresh = 1; // CE calls the editor's update",
     "        // CE calls the editor's update", "page:page", "", "the rows redrawn by themselves"),
    # the device, 2026-11-12: turning through a list made every plugin it passed
    ("the pick made at once", PAGE, "    remakeWhenStill(part, slot); // made once", "    remake(part, slot, false); // made once",
     "page:page", "", "none of them read or made"),
    ("the wait never ends", SLOTS, "if (l.picked && plugins::now() - l.pickedAt >= kPickRestMs)",
     "if (l.picked && plugins::now() - l.pickedAt >= kPickRestMs * 1000)", "page:page", "", "FX1 picks the Overdrive"),
    ("a waiting slot's knobs shown", SLOTS, "live[part][slot].count > 0 && !live[part][slot].picked ?",
     "live[part][slot].count > 0 ?", "page:page", "", "with no knob rows"),
    ("the record not stored when a pick is made", SLOTS, "    if (picksMade)", "    if (false)", "page:page", "",
     "the pick lands in the project's record"),
    ("the record stored before a swap is made", SLOTS, "if (l.storeWhenMade && !l.pending)",
     "if (l.storeWhenMade)", "page:page", "", "stored once it was made over the old one"),
    ("Delete on a slot waits", PAGE, "        if (changed)\n        {\n            remake(part, slot, false);\n",
     "        if (false)\n        {\n            remake(part, slot, false);\n", "page:page", "", "Delete on FX1"),
    # USB storage mode (the unit, 2026-11-12): the mods touched the card while the computer had it
    ("CE never lends the card", "core/interface/core/interface.cpp",
     "        mods::cardLent(true);\n", "", "boot", "", "the card touched in USB storage mode"),
    ("the card never given back", "core/interface/core/interface.cpp",
     "        mods::cardLent(false);\n", "", "boot", "", "the card touched in USB storage mode"),
    ("the mods' update ignores the lent card", "core/mods/mods.cpp",
     "    if (lentNow)\n    {\n        return; // USB", "    if (false)\n    {\n        return; // USB",
     "page:page", "", "USB storage mode with a pick waiting"),
    ("the mods' file calls ignore the lent card", "core/mods/mods.cpp",
     "return fileSystem != nullptr && !lentNow;", "return fileSystem != nullptr;", "page:page", "",
     "the mods' file calls fail"),
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
    ap.add_argument("--project", type=Path, help="a project to make the boot faults' Synth FX project from")
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
    boot_project = None
    if any(f[4] == "boot" for f in faults):
        if a.project is None:
            print("FAIL the boot faults need --project")
            return 1
        import make_masterfx_test_project as maker
        boot_project = maker.make_synthfx(a.project, Path(tempfile.mkdtemp()))
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
        elif section == "boot":
            cmd = [sys.executable, str(BOOT), str(axf), str(boot_project), "--blocks", "300", "--expect-synthfx",
                   "--expect-usb"]
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
