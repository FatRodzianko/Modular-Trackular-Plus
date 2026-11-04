"""Check a project on real 1.2.0 code: read every pattern with 1.2.0's own reader and set
the synth slots with 1.2.0's own project load.

    python tools/emu/check_ce_to_120.py <1.2.0.ptf> <Modular-Trackular/tools> <project> [<original>]

Run it on a repaired copy; it should pass. Run it on the CE-saved original; it should fail,
which shows the check can see the problem. With <original>, each pattern's notes, lengths and
FX are also compared with that project's.

The slot setters are stubbed: this checks the model index, and reads no synth code. A pass
doesn't cover the rest of the project load, so opening the copy on the device is still the
final check.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

PATTERN_READ = 0x600C2350
APPLY_SLOTS = 0x600B2080
SET_POLYPHONY = 0x600B13CC
SET_MODEL = 0x600B1440
SET_NAME = 0x600B1EE8
THROW_OUT_OF_RANGE = 0x60175DC0
PROJECT_BLOCK = 0x202046C4
SEQUENCER = 0x20017274
PATTERN_SIZE = 0x3030
HEADER = 0x1C
TRACKS = 16
STEPS = 128


class Stop(Exception):
    pass


def steps_of(data: bytes, size: int) -> list:
    """Each track's length and its steps' note, instrument and FX."""
    tracks = []
    at = HEADER
    for _ in range(TRACKS):
        tracks.append((data[at], [data[at + 1 + s * size:at + 1 + s * size + 6] for s in range(STEPS)]))
        at += 1 + STEPS * size
    return tracks


def main() -> int:
    ptf, tools, project = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    original = Path(sys.argv[4]) if len(sys.argv) > 4 else None
    sys.path[:0] = [str(tools), str(tools / "emu")]
    from fakefs import FILEMAN, FakeFS
    from machine import Machine

    failures = 0

    def check(ok, what):
        nonlocal failures
        print(("PASS " if ok else "FAIL ") + what)
        failures += not ok

    m = Machine(ptf)
    FakeFS(m, project)
    buffer = m.alloc(PATTERN_SIZE)
    m.w32(SEQUENCER + 0x18, buffer)
    patterns = sorted((project / "patterns").glob("pattern_*.mtp"))
    for path in patterns:
        name = m.alloc(64)
        m.write(name, f"Workspace/patterns/{path.name}".encode() + b"\0")
        ok = m.call(PATTERN_READ, FILEMAN, name, buffer)
        read = bytes(m.uc.mem_read(buffer, PATTERN_SIZE))
        file = path.read_bytes()
        lengths_fine = all(read[HEADER + t * 0x301] < STEPS for t in range(TRACKS))
        check(ok == 1 and lengths_fine and read == file,
              f"{path.name}: 1.2.0 reads the file as it is, track lengths in range")
        if original is not None:
            source = (original / "patterns" / path.name).read_bytes()
            wanted = steps_of(source, 8 if source[8] == 6 else 6)
            if source[8] == 6:      # the tool clears FX only CE has
                def clear(s):
                    fx1 = (0, 0) if s[2] >= 43 else (s[2], s[3])
                    fx2 = (0, 0) if s[4] >= 43 else (s[4], s[5])
                    return bytes((s[0], s[1], *fx1, *fx2))
                wanted = [(length, [clear(s) for s in steps]) for length, steps in wanted]
            check(steps_of(read, 6) == wanted, f"{path.name}: same lengths, notes, instruments and FX as the original")

    mt = (project / "project.mt").read_bytes()
    m.write(PROJECT_BLOCK, mt[0x110:0x810])
    models, thrown = [], []
    m.stub_function(SET_POLYPHONY, lambda mm: None)
    m.stub_function(SET_MODEL, lambda mm: models.append(mm.arg(2)))
    m.stub_function(SET_NAME, lambda mm: None)

    def on_throw(mm):
        thrown.append(mm.arg(1))
        raise Stop

    m.stub_function(THROW_OUT_OF_RANGE, on_throw)
    try:
        m.call(APPLY_SLOTS, m.alloc(0x200), PROJECT_BLOCK, 1)
    except BaseException:
        if not thrown:
            raise
    check(not thrown, f"1.2.0's project load accepts the synth slots (model ids {models}"
          + (f"; stopped on model index {thrown[0]})" if thrown else ")"))
    name = mt[0x810:0x831].split(b"\0")[0].decode("ascii", "replace")
    check(name == project.name, f'project.mt has the folder\'s name ("{name}")')
    print(f"{failures} failed, {len(patterns)} patterns" if failures else f"All passed, {len(patterns)} patterns")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
