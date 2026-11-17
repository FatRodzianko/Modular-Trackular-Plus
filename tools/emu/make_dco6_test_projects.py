# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""DCO6's device test set (docs/mt/DEVICE_TEST_LADDER.md, "The DCO6 set"), lightest first: copies of a
project made by make_masterfx_test_project.make, all three parts on DCO6, every voice held.

    python tools/emu/make_dco6_test_projects.py <mods.axf> "<project>" "<out folder>"

E1 to E3 play DCO6's Init patch: the project has no patch files for the parts, so the firmware starts
each on Init. E4 and E5 need patches, which are CE's own protobuf files (`0.patch` to `2.patch` in
the project folder, copied to the Workspace on open). Those are written by the firmware itself: the
mods build in the sound rig sets the parameters and saves the part with Synth::savePatchToWorkspace,
as the unit does. Each one is then loaded back on a fresh rig (Synth::loadPatchFromWorkspace) and its
values checked, so a patch that wouldn't open isn't put in a project.

The project given is only read.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_masterfx_test_project as maker  # noqa: E402
from measure_dco6 import HEAVY  # noqa: E402
from test_dco6 import DCO6, P, Rig  # noqa: E402

UNISON = dict(unison=4.0, detune=25.0, width=1.0)

# name, voices on the three parts, the parts' patch (None: Init), what it isolates
DCO6_LADDER = (
    ("E1 1 voice DCO6 Init", (1, 0, 0), None),
    ("E2 4 voices DCO6 Init", (4, 0, 0), None),
    ("E3 8 voices DCO6 Init", (4, 2, 2), None),
    # one part with all 8: its 4 held notes make 2 stacks of 4 (each note takes a stack, the later
    # two take the earlier two's), so all 8 voices sound
    ("E4 8 DCO6 Unison 4", (8, 0, 0), UNISON),
    ("E5 8 voices DCO6 heavy", (4, 2, 2), HEAVY),
)


def patch_bytes(axf, values: dict) -> bytes:
    """A DCO6 patch with these values, as the firmware saves it"""
    r = Rig(axf)
    r.part(8, None, tuple(values.items()))
    r.call(r.first("_ZN5Synth20savePatchToWorkspaceEi"), r.synth, 0, max_insns=200_000_000)
    if r.failures or "Workspace/0.patch" not in r.sd.files:
        raise RuntimeError(f"the patch wasn't saved: {r.failures[:1]}")
    return bytes(r.sd.files["Workspace/0.patch"])


def check_patch(axf, data: bytes, values: dict) -> None:
    """Load it back on a fresh rig as a project's open does, and compare"""
    r = Rig(axf)
    r.part(8)
    r.sd.files["Workspace/0.patch"] = bytearray(data)
    loaded = r.call(r.first("_ZN5Synth22loadPatchFromWorkspaceEi"), r.synth, 0, max_insns=200_000_000) & 0xFF
    wrong = [(k, round(r.get_param(0, P[k]), 4), v) for k, v in values.items()
             if abs(r.get_param(0, P[k]) - v) > 1e-3 * max(1.0, abs(v))]
    if loaded != 1 or wrong or r.failures:
        raise RuntimeError(f"the patch doesn't load back: loaded {loaded}, wrong {wrong[:4]}, {r.failures[:1]}")


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    axf, source, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    for name, voices, values in DCO6_LADDER:
        data = None
        if values:
            data = patch_bytes(axf, values)
            check_patch(axf, data, values)
        folder = maker.make(source, out, name, voices, engine="DCO6")
        if data:
            for slot in range(3):
                (folder / f"{slot}.patch").write_bytes(data)
        print(f"Copy written: {folder}" + (f" ({len(data)} B patch on every part, loads back)" if data else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
