# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""A project's own synth patches, through CE's own code on the boot rig's fake card.

CE keeps a project's three synth patches in its folder (0.patch to 2.patch) and copies them with
Synth::copyWorkspacePatches, on open to the Workspace and on save back, with a "{folder}\\{part}.patch"
path. FatFs on the unit takes the backslash as a separator; the fake card does the same since
test_step0.card_path, and before that no rig had opened a project's patches: every part fell back to Init.

1. Open: a project whose parts are all DCO6 with a Unison patch (DCO6_LADDER's E4, written as
   make_dco6_test_projects writes it), on a card that has it only in Projects, opened as the
   Project screen's Open does. Every part has the patch's values, and the Workspace has its three files.
2. Save's copy: copyWorkspacePatches from the Workspace to another project folder writes the three
   files there, byte for byte.

    python tools/emu/test_project_patches.py <mods.axf> "<project>" <Synths folder>

The project given is only read; the copy is made in a temporary folder.
"""
from __future__ import annotations

import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_masterfx_test_project as maker  # noqa: E402
from boot_rig import BootRig  # noqa: E402
from make_dco6_test_projects import UNISON, check_patch, patch_bytes  # noqa: E402
from test_dco6 import P  # noqa: E402

NAME = "E4 8 DCO6 Unison 4"
failures: list[str] = []


def check(ok: bool, what: str) -> None:
    print(("PASS " if ok else "FAIL ") + what, flush=True)
    if not ok:
        failures.append(what)


def param(r: BootRig, part: int, pid: int) -> float:
    """The part's patch value, as the Synth page reads it (Patch::getParam, through its vtable)"""
    m = r.m
    patch = m.call(m.addr(next(n for n in r.syms if n.startswith("_ZN5Synth8getPatchEi"))), r.synth, part)
    out = m.alloc(8, 8)
    m.call(m.r32(m.r32(patch)), out, patch, pid)
    return struct.unpack("<f", m.read(out, 4))[0]


def open_project(axf: Path, folder: Path, synths: Path) -> BootRig:
    r = BootRig(axf)
    m, sd = r.m, r.sd
    r.card_from(folder, NAME, workspace=False)      # in Projects only: the Open must copy them
    sd.dirs.add("Synths")
    for f in synths.glob("*.psyn"):
        sd.files[f"Synths/{f.name}"] = f.read_bytes()
    updates = [0]
    r.watch(m.addr("_ZN14cPatternEditor6updateEv"), lambda mm: updates.__setitem__(0, updates[0] + 1))
    r.boot()
    for _ in range(3000):
        r.loop()
        r.interrupt()
        if updates[0] > 5:
            break
    r.open_project(NAME)
    before, quiet = len(sd.calls), 0
    for _ in range(20000):                          # until the open's file work is over
        r.loop()
        r.interrupt()
        quiet = quiet + 1 if len(sd.calls) == before else 0
        before = len(sd.calls)
        if quiet > 200:
            break
    return r


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    axf, source, synths = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    data = patch_bytes(axf, UNISON)
    check_patch(axf, data, UNISON)
    with tempfile.TemporaryDirectory() as temp:
        folder = maker.make(source, Path(temp), NAME, (8, 0, 0), engine="DCO6")
        for slot in range(3):
            (folder / f"{slot}.patch").write_bytes(data)
        r = open_project(axf, folder, synths)

    sd = r.sd
    copied = [sd.files.get(f"Workspace/{slot}.patch") == data for slot in range(3)]
    check(all(copied), f"Open: the project's 0.patch to 2.patch are in the Workspace ({copied})")
    got = [{k: round(param(r, part, P[k]), 3) for k in UNISON} for part in range(3)]
    check(all(all(abs(g[k] - v) < 1e-3 for k, v in UNISON.items()) for g in got),
          f"Open: every part has the project's patch, not Init ({got})")
    check(not r.failures, f"Open: no failures ({r.failures[:1]})")

    m = r.m
    sd.dirs.add("Projects/Saved")
    src, dst = m.put_str("Workspace"), m.put_str("Projects/Saved")
    m.call(m.addr("_ZN5Synth20copyWorkspacePatchesEPKcS1_"), r.synth, src, dst, max_insns=200_000_000)
    saved = [sd.files.get(f"Projects/Saved/{slot}.patch") == data for slot in range(3)]
    check(all(saved), f"Save's copy: the Workspace's patches reach the project folder ({saved})")

    print(f"{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
