# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Planted faults for the Synth FX plugins' runtime: each copies core/mods/synthfx, makes one
mistake in the copy's synthfxPlugins.cpp (or its copy of core/mods/plugins), builds the emulator rig (tools/synthfx/rig/build.sh)
or the host test (core/mods/tests/test_synthfx_plugins.cpp) from it, and runs the test, which
has to fail on the check named. A fault the tests miss is a gap in them.

    python tools/synthfx/rig/plant.py [--toolchain PREFIX] [--only NAME]

Builds and host tests run in WSL, the emulator test on this machine's Python, one at a time,
so the run stays well under the CPU limit. The copies and ELFs go to build/emu/plant_rig/.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "core" / "mods" / "synthfx"
OUT = ROOT / "build" / "emu" / "plant_rig"
TEST = ROOT / "tools" / "synthfx" / "test_runtime.py"
F = "synthfxPlugins.cpp"
KIT = ROOT / "core" / "mods" / "plugins"
# The faults in the shared kit, and the file each is in
KIT_FAULTS = {"guard: NaN let through": "pluginKit.h", "guard: no clamp": "pluginKit.h",
              "header: a default between steps passed": "pluginKit.h"}
KIT_FAULTS.update({name: "pluginPack.h" for name in (
    "pack: the user's file overwritten", "pack: a shipped file not replaced", "pack: the hash not checked",
    "pack: the copy not read back", "pack: a stray .new kept", "pack: a stopped replacement not finished")})

# (name, where: rig or host, old text, new text, a check that must fail: a substring of its line)
# The fault goes in synthfxPlugins.cpp, or in the shared kit (core/mods/plugins/pluginKit.h or
# pluginPack.h) for the names in KIT_FAULTS.
FAULTS = [
    ("guard: NaN let through", "rig", "return nan0 + nan1 == 0.0f && ", "return ", "NaN: becomes 0"),
    ("guard: no clamp", "rig", "__builtin_fmaxf(peak0, peak1) <= limit;", "__builtin_fmaxf(peak0, peak1) <= 1000.0f;",
     "loud: held"),
    ("guard: NaN doesn't reset the plugin", "rig", "    if (bad)\n    {\n        in.resetWanted = 1;\n    }\n", "",
     "NaN: the plugin reset"),
    ("time: bypass a block late", "rig", "if (++in.over >= kOverBlocks)", "if (++in.over > kOverBlocks)",
     "8 in a row: bypassed"),
    ("time: the count never starts again", "rig", "    else\n    {\n        in.over = 0;\n    }\n", "",
     "starts the count again"),
    ("knobs: dirty never cleared", "rig", "            in.dirty[k] = 0;\n", "", "no set_param for knobs that didn't move"),
    ("knobs: not on their steps", "rig", "in.value[knob] = valueAt(*p, stepNearest(*p, value));",
     "in.value[knob] = value;", "a moved knob, on its step"),
    ("load: cache not synced", "rig", "    env.syncCode(const_cast<uint8_t*>(code), h->code_size);\n", "",
     "its cache synced"),
    ("load: code not 32-byte aligned", "rig", "codePool.at(block) + 4;", "codePool.at(block) + 0;", "32-byte aligned"),
    ("load: a changed file's size not checked", "rig", "got != size || ", "", "a file changed since the list refused"),
    ("load: the wrong entry for process", "rig", "entryAt<PtsfxProcess>(code, h->process_offset)",
     "entryAt<PtsfxProcess>(code, h->reset_offset)", "through the runtime as called directly"),
    ("state not zeroed", "rig", "    memset(in.state, 0, p.header->state_size);\n", "", "state zeroed"),
    ("code freed with an instance left", "rig", "if (p.users > 0 && --p.users == 0)", "if (p.users > 0 && (p.users = 0) == 0)",
     "the code stays"),
    ("pack: the user's file overwritten", "rig", "        if (!resume && !packdetail::wasShipped(c, path, name))",
     "        if (false)", "left alone"),
    ("pack: a shipped file not replaced", "rig", "        named = sameText(c.shipped[i].name, name);",
     "        named = false;", "a shipped file becomes the pack's"),
    ("pack: the hash not checked", "rig", "fileHash(c.buffer, static_cast<uint32_t>(size)) == s.hash", "true",
     "a changed file and the user's own file left alone"),
    ("pack: the copy not read back", "rig",
     "    if (!c.io.write(fresh, data, size) || !packdetail::holds(c, fresh, data, size))",
     "    if (!c.io.write(fresh, data, size))", "a bad copy, the old file kept"),
    ("pack: a stray .new kept", "rig",
     "        resume = packdetail::holds(c, fresh, data, size); // the last start stopped while putting it in\n"
     "        c.io.remove(fresh);\n",
     "        resume = packdetail::holds(c, fresh, data, size); // the last start stopped while putting it in\n",
     "a stray .new beside the user's file"),
    ("pack: a stopped replacement not finished", "rig", "resume = packdetail::holds(c, fresh, data, size);",
     "resume = false;", "a whole .new beside a half-written file"),
    ("marker: never removed", "rig", "        io->remove(kMarker);\n", "", "marker: removed after 5 s"),
    ("safe mode ignored", "rig", "if (fromProject && inSafeMode)", "if (fromProject && inSafeMode && false)",
     "a project's plugin skipped"),
    ("header: a default between steps passed", "host",
     "!whole((p.default_val - p.min_val) / p.step, &at) || at < 0 || at > steps", "at < 0 || at > steps",
     "default between steps"),
    ("header: the file's size not checked", "host", " || h.code_offset + h.code_size != fileSize", "",
     "a file a byte long"),
    ("catalog: the pack not first", "host", "    for (size_t i = 0; i < packCount(); i++) // the pack first, in its order\n",
     "    for (size_t i = 0; i < 0; i++) // the pack first, in its order\n", "the pack, then the rest sorted"),
    ("catalog: too big called bad", "host", "? Status::tooBig", "? Status::badFile", "too much state: too big"),
]


def wsl_path(p: Path) -> str:
    s = str(p.resolve()).replace("\\", "/")
    return f"/mnt/{s[0].lower()}{s[2:]}" if s[1] == ":" else s


def wsl(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    pre = ["wsl", "-d", "Ubuntu", "--", "env"] + [f"{k}={v}" for k, v in (env or {}).items()]
    return subprocess.run(pre + cmd, capture_output=True, text=True, env={**os.environ, "MSYS_NO_PATHCONV": "1"})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolchain", default="arm-none-eabi-")
    ap.add_argument("--only")
    a = ap.parse_args()
    py = sys.executable
    missed = []
    for n, (name, where, old, new, expect) in enumerate(FAULTS):
        if a.only and a.only not in name:
            continue
        work = OUT / f"f{n:02d}"
        shutil.rmtree(work, ignore_errors=True)
        copy = work / "synthfx"
        shutil.copytree(SRC, copy)
        shutil.copytree(KIT, work / "plugins")
        target = work / "plugins" / KIT_FAULTS[name] if name in KIT_FAULTS else copy / F
        text = target.read_text(encoding="utf-8")
        if text.count(old) != 1:
            print(f"FAULT TEXT NOT FOUND ONCE: {name}")
            missed.append(name)
            continue
        target.write_text(text.replace(old, new), encoding="utf-8", newline="\n")
        if where == "rig":
            axf = work / "rig.axf"
            b = wsl(["bash", wsl_path(ROOT / "tools" / "synthfx" / "rig" / "build.sh"), a.toolchain],
                    {"RIG_SRC": wsl_path(copy), "RIG_OUT": wsl_path(axf), "RIG_KIT": wsl_path(work)})
            if b.returncode != 0:
                print(f"caught {name}: it doesn't build")
                continue
            r = subprocess.run([py, str(TEST), str(axf), "--quick"], capture_output=True, text=True, timeout=900)
            out = r.stdout + r.stderr
        else:
            tests = ROOT / "core" / "mods" / "tests"
            exe = work / "test_plugins"
            cmd = (f"cd {wsl_path(tests)} && g++ -std=gnu++20 -Wall -Wextra -g -DMT_HOST_TEST -I{wsl_path(work)} -I.. "
                   f"-o {wsl_path(exe)} {wsl_path(copy / F)} {wsl_path(copy / 'synthfxPack.cpp')} test_synthfx_plugins.cpp "
                   f"&& {wsl_path(exe)}")
            r = wsl(["nice", "bash", "-c", cmd])
            out = r.stdout + r.stderr
        failed = [line for line in out.splitlines() if line.startswith("FAIL")]
        if r.returncode != 0 and any(expect in line for line in failed):
            print(f"caught {name}: {failed[0][5:][:90]}")
        else:
            print(f"MISSED {name} (exit {r.returncode}; failed: {[f[5:][:60] for f in failed][:3]})")
            missed.append(name)
    print(f"{len(missed)} missed")
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
