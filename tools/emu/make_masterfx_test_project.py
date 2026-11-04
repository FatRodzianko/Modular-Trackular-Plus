#!/usr/bin/env python3
"""A copy of a project made into masterfx's device test: 8 synth voices sounding, sending to both effects.

    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" [--name "MFX test"]

The project given is only read. In the copy:
- project.mt: the new name, 4 + 2 + 2 voices on the three synth parts, both sends at 60.
- every pattern: 8 tracks holding one note each, 4 on part 1, 2 on part 2, 2 on part 3.

Everything else stays byte for byte, the synth patches and mods files included. So a project with a
1.2.0 masterfx.bin still gets its effects through the importer, as any real project would.
"""
import argparse
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ce_to_120 as ce  # noqa: E402

# SynthPartSettings in core/source/mtStructs.h
SLOT_VOICES = 4
SLOT_REVERB_SEND = 0x30
SLOT_DELAY_SEND = 0x34
PROJECT_CRC = ce.PROJECT_SIZE - 4       # 0 means "no CRC", as older firmware wrote it

SYNTH_INSTRUMENTS = 64                  # SYNTH_INSTRUMENTS_START_IDX
VOICES = (4, 2, 2)
SEND = 60.0
# (track, part, note): spread out, so each voice is heard and none are the same note
HELD = tuple((track, part, note) for track, (part, note) in enumerate(
    ((0, 48), (0, 55), (0, 60), (0, 64), (1, 43), (1, 67), (2, 72), (2, 76))))
LAST_STEP = 63


def pattern() -> bytes:
    data = bytearray(b"KS" + struct.pack("<H", 2) + bytes((1, 0, 0, 1)) + bytes((6,)) * 4
                     + struct.pack("<H", ce.PATTERN_V6_SIZE))
    data += bytes(ce.PATTERN_HEADER - len(data))
    empty = bytes((ce.EMPTY, 0, 0, 0, 0, 0, ce.EMPTY, ce.EMPTY))
    for _ in range(ce.TRACKS):
        data.append(LAST_STEP)
        data += empty * ce.STEPS
    data += bytes(4)
    assert len(data) == ce.PATTERN_V6_SIZE
    for track, part, note in HELD:
        at = ce.PATTERN_HEADER + track * (1 + ce.STEPS * ce.STEP_V6) + 1
        data[at:at + 2] = bytes((note, SYNTH_INSTRUMENTS + part))
    return bytes(data)


def project_file(source: bytes, name: str) -> bytes:
    mt = bytearray(source)
    if len(mt) != ce.PROJECT_SIZE or mt[:2] != b"MT" or mt[ce.HEADER_VERSION] != ce.PROJECT_VERSION:
        raise ce.Refused(f"project.mt: {len(mt)} bytes. I only know version {ce.PROJECT_VERSION} "
                         f"at {ce.PROJECT_SIZE} bytes.")
    mt[ce.PROJECT_NAME:ce.PROJECT_NAME + ce.PROJECT_NAME_SIZE] = name.encode("ascii").ljust(ce.PROJECT_NAME_SIZE, b"\0")
    for slot, voices in enumerate(VOICES):
        record = ce.SLOT_RECORDS + slot * ce.SLOT_RECORD_SIZE
        struct.pack_into("<i", mt, record + SLOT_VOICES, voices)
        struct.pack_into("<f", mt, record + SLOT_REVERB_SEND, SEND)
        struct.pack_into("<f", mt, record + SLOT_DELAY_SEND, SEND)
    mt[PROJECT_CRC:PROJECT_CRC + 4] = bytes(4)
    return bytes(mt)


def make(source: Path, parent: Path, name: str) -> Path:
    out = parent / name
    if out.exists():
        raise ce.Refused(f"{out} already exists, and I won't write over it.")
    new_project = project_file((source / "project.mt").read_bytes(), name)
    parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, out)
    (out / "project.mt").write_bytes(new_project)
    (out / "patterns").mkdir(exist_ok=True)
    names = {p.name for p in (out / "patterns").glob("pattern_*.mtp")} | {"pattern_01.mtp"}
    for filename in names:
        (out / "patterns" / filename).write_bytes(pattern())
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--name", default="MFX test")
    args = parser.parse_args()
    try:
        print(f"Copy written: {make(args.project, args.out, args.name)}")
    except ce.Refused as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
