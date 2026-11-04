#!/usr/bin/env python3
"""The sound rig itself: CE's start-up and the 7 engines, heard from outside.

Usage: python tools/emu/test_sound_rig.py <mods.axf> <reference.axf>

Note on gives sound, note off gives silence, the same twice, the mods build and the reference build
the same. Stage 1 of docs/mt/SOUND_TEST_PLAN.md. Nothing here looks inside an engine.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from sound_rig import ClosedCode, SoundRig, peak  # noqa: E402

ENGINES = (1, 2, 4, 5, 6, 7, 8)     # the ids core/synth/registry.cpp registers
failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def play(axf, engine, blocks=30):
    r = SoundRig(axf)
    r.set_instrument(0, engine)
    r.note(0, 60, True)
    return r, r.blocks(blocks)


def main():
    mods, ref = Path(sys.argv[1]), Path(sys.argv[2])

    r = SoundRig(ref)
    check(not r.failures, f"reference build starts up, no asserts ({r.failures[:2]})")
    silent = r.blocks(8)
    check(peak(silent[0]) == 0, "no note, no sound")
    try:
        closed = r.guard.ranges[0]
        r.watch(closed[0], lambda m: None)
        check(False, "the guard refuses a hook in a closed library")
    except ClosedCode:
        check(True, "the guard refuses a hook in a closed library")

    for e in ENGINES:
        rm, a = play(mods, e)
        rr, b = play(ref, e)
        check(peak(a[0]) > 100, f"engine {e}: note on, sound (peak {peak(a[0])})")
        check(a == b, f"engine {e}: mods build and reference build the same, sample for sample")
        _, again = play(mods, e)
        check(a == again, f"engine {e}: the same twice")
        rm.note(0, 60, False)
        rm.blocks(600)
        tail = rm.blocks(10)
        check(peak(tail[0]) <= 2, f"engine {e}: note off, silence (peak {peak(tail[0])})")
        check(not rm.failures, f"engine {e}: no asserts")

    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
