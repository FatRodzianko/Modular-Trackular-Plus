#!/usr/bin/env python3
"""Copies of a project made into Resample's device test (docs/mt/RESAMPLE_PORT_PLAN.md section 8).

    python tools/emu/make_resample_test_projects.py "<project>" "<out folder>"

The project given is only read. It writes two copies:

- `RS1 Resample test`: the project's own patterns, instruments and synth patches, every sample
  instrument and synth part sending to the delay and the reverb (40 and 40, as the Master FX sound
  set's `R2 Real project sends`), so a take with Trk Record shows they're recorded. Resample Off and
  never set (no resample.bin, mods.bin as for RS2), so it's turned On by hand with both buttons Off.
- `RS2 1.2.0 buttons`: the project as it is, renamed, with a resample.bin from 1.2.0's patch with
  both buttons On (settings byte 3), and mods.bin as if Resample had never been set there: its
  switch Off, no Resample record, its importer not yet run. Opening it on Tracker+ runs the importer
  (modsProject.cpp runImporters), which turns Resample On and both buttons On: the importer, on the
  unit's card.

Run it on a project with sample instruments and at least one synth part; hellish downtown is the one
the emulator tests use.
"""
import argparse
import shutil
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import make_masterfx_test_project as mfx  # noqa: E402
from make_masterfx_test_project import ce  # noqa: E402

RS1, RS2 = "RS1 Resample test", "RS2 1.2.0 buttons"
RESAMPLE = 11                   # Mod::resample: its switch in record 0, its bit in record 240
RESAMPLE_RECORD = RESAMPLE + 1  # resample::kRecord
IMPORTS_RECORD = 240            # modsProject.h kImportsRecord: the importers already run
SIDE_FILE = "resample.bin"


def side_file(settings: int) -> bytes:
    """resample.bin as 1.2.0's patch writes it: "RSM1", the settings byte, three zero bytes, and a
    little-endian FNV-1a over bytes 4 to 7 (core/mods/resample.cpp)."""
    data = bytes([settings, 0, 0, 0])
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return b"RSM1" + data + struct.pack("<I", h)


def copy(source: Path, parent: Path, name: str) -> Path:
    out = parent / name
    if out.exists():
        raise ce.Refused(f"{out} already exists, and I won't write over it.")
    parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, out)
    mt = bytearray((out / "project.mt").read_bytes())
    mt[ce.PROJECT_NAME:ce.PROJECT_NAME + ce.PROJECT_NAME_SIZE] = name.encode("ascii").ljust(ce.PROJECT_NAME_SIZE, b"\0")
    mt[mfx.PROJECT_CRC:mfx.PROJECT_CRC + 4] = bytes(4)
    (out / "project.mt").write_bytes(mt)
    return out


def without_resample(path: Path):
    """mods.bin as if Resample had never been set in the project: switch Off, no record, its importer
    not run."""
    if not path.exists():
        return
    records = mfx.store_records(path.read_bytes())
    if not records:
        raise ce.Refused(f"{path} isn't a mods.bin I can read.")
    out = []
    for i, v, d in records:
        if i == RESAMPLE_RECORD:
            continue
        if i == 0 and len(d) > RESAMPLE:
            d = d[:RESAMPLE] + bytes(1) + d[RESAMPLE + 1:]
        if i == IMPORTS_RECORD and len(d) == 4:
            d = struct.pack("<I", struct.unpack("<I", d)[0] & ~(1 << RESAMPLE))
        out.append((i, v, d))
    records = out
    payload = b"".join(struct.pack("<BBH", i, v, len(d)) + d for i, v, d in records)
    path.write_bytes(mfx.MODS_MAGIC + struct.pack("<HHI", 1, len(payload), zlib.crc32(payload)) + payload)


def make(source: Path, parent: Path) -> list:
    rs1 = copy(source, parent, RS1)
    (rs1 / SIDE_FILE).unlink(missing_ok=True)
    without_resample(rs1 / "mods.bin")
    mt = bytearray((rs1 / "project.mt").read_bytes())
    for slot in range(3):
        record = ce.SLOT_RECORDS + slot * ce.SLOT_RECORD_SIZE
        struct.pack_into("<f", mt, record + mfx.SLOT_REVERB_SEND, float(mfx.REAL_SEND))
        struct.pack_into("<f", mt, record + mfx.SLOT_DELAY_SEND, float(mfx.REAL_SEND))
    (rs1 / "project.mt").write_bytes(mt)
    for path in (rs1 / "instruments").glob("*.pti"):
        data = bytearray(path.read_bytes())
        data[mfx.INSTRUMENT_DELAY_SEND] = data[mfx.INSTRUMENT_REVERB_SEND] = mfx.REAL_SEND
        data[ce.INSTRUMENT_CRC:ce.INSTRUMENT_CRC + 4] = bytes(4)
        path.write_bytes(data)

    rs2 = copy(source, parent, RS2)
    (rs2 / SIDE_FILE).write_bytes(side_file(3))
    without_resample(rs2 / "mods.bin")
    return [rs1, rs2]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project", type=Path)
    ap.add_argument("out", type=Path)
    a = ap.parse_args()
    try:
        for folder in make(a.project, a.out):
            print(f"Copy written: {folder}")
    except ce.Refused as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
