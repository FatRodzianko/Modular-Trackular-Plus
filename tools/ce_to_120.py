"""Make a copy of a project saved on CE-based firmware that 1.2.0 firmware can open.

    python tools/ce_to_120.py "E:/Projects/User/my song"
    python tools/ce_to_120.py "E:/Projects/User/my song" --out "my song old" --synth-model WTFM

The project given is only read. The copy goes next to it, named "<name> 120" unless --out
says otherwise, and its project.mt carries the new name, so that Save on 1.2.0 writes to
the copy and not to the original.

What 1.2.0 can't read, and what the copy gets instead:
  - Pattern files in format 6 (8-byte steps). 1.2.0 reads them as 6-byte steps without
    checking. They become format 5: Volume and Micro-move columns are dropped, and FX
    only CE has (id 43 up: w, y, z) are cleared.
  - Synth model 5 (DIRT) or 6 (CHIP) in project.mt. 1.2.0 stops loading on a model above 4.
    The slot gets a stock model and that model's Init patch; its <slot>.patch is left out.
  - WT Env and WT LFO on an instrument, and Granular Length automation. They become a
    plain envelope or LFO, or are turned off.

Everything else is copied byte for byte, including every synth patch of a stock-model
slot: a patch with an id the record doesn't show can be a 1.2.0 mod's own engine.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# File headers: id[2], type u16, firmware[4], structure version[4], size u16.
HEADER_FW = 4
HEADER_VERSION = 8
HEADER_SIZE = 12

# Patterns. A 0x1c-byte header, then 16 tracks of a last-step byte and 128 steps, then a CRC.
PATTERN_HEADER = 0x1C
TRACKS = 16
STEPS = 128
PATTERN_V5_SIZE = 0x3030
PATTERN_V6_SIZE = 0x4030
STEP_V5 = 6
STEP_V6 = 8
EMPTY = 0xFF            # volume and micromove are int8, with -1 for "not set"
FIRST_CE_FX = 43        # 1.2.0's FX list has ids 0..42
FW_1_2_0_PATTERN = bytes((1, 9, 2, 1))

# project.mt. Offsets are the file's: RAM 0x202046c4 is file +0x110.
PROJECT_SIZE = 0x914
PROJECT_VERSION = 17
PROJECT_NAME = 0x810
PROJECT_NAME_SIZE = 33
FW_1_2_0_PROJECT = bytes((1, 9, 2, 0xFF))
SLOT_RECORDS = 0x74C
SLOT_RECORD_SIZE = 0x40
SLOTS = 3
SLOT_MODEL = 8
SLOT_NAME = 0x10
SLOT_NAME_SIZE = 0x20
SLOT_MACROS = 0x38
SLOT_MACRO_COUNT = 6
STOCK_MODELS = ("ACD", "FAT", "VAP", "WTFM", "PERC")
CE_MODELS = {5: "DIRT", 6: "CHIP"}

# Instruments: a 16-byte header, strInstrument, a CRC, then the sample.
INSTRUMENT_VERSION = 9
INSTRUMENT = 0x10
ENVELOPES = INSTRUMENT + 76
ENVELOPE_SIZE = 20
ENVELOPE_LOOP = 18      # 0 ADSR, 1 LFO; CE adds 2 WT Env, 3 WT LFO
ENVELOPE_ENABLE = 19
LFOS = INSTRUMENT + 196
LFO_SIZE = 8
LFO_SHAPE, LFO_WT_BEND, LFO_WT_SUST = 0, 2, 3
LFO_SHAPES = 5
LFO_SHAPE_DEFAULT = 2
ENV_SLOTS = 6
ENV_WT_POS = 3          # CE shares this slot with Granular Length, as enable bit 1
INSTRUMENT_CRC = INSTRUMENT + 372


class Refused(Exception):
    """The folder isn't something this tool knows how to repair."""


@dataclass
class Report:
    patterns: list[str] = field(default_factory=list)
    volumes: int = 0
    micromoves: int = 0
    ce_fx: int = 0
    synths: list[str] = field(default_factory=list)
    patches_left_out: list[str] = field(default_factory=list)
    instruments: list[str] = field(default_factory=list)
    chords_above_29: int = 0
    chances_above_100: int = 0

    def anything(self) -> bool:
        return bool(self.patterns or self.synths or self.instruments)


def pattern_v6_to_v5(data: bytes, report: Report) -> bytes:
    out = bytearray(data[:PATTERN_HEADER])
    out[HEADER_FW:HEADER_FW + 4] = FW_1_2_0_PATTERN
    out[HEADER_VERSION:HEADER_VERSION + 4] = bytes((5,)) * 4
    struct.pack_into("<H", out, HEADER_SIZE, PATTERN_V5_SIZE)
    at = PATTERN_HEADER
    for _ in range(TRACKS):
        out.append(data[at])
        at += 1
        for _ in range(STEPS):
            note, instrument, type1, value1, type2, value2, volume, micromove = data[at:at + STEP_V6]
            at += STEP_V6
            report.volumes += volume != EMPTY
            report.micromoves += micromove != EMPTY
            fx = []
            for fx_type, value in ((type1, value1), (type2, value2)):
                if fx_type >= FIRST_CE_FX:
                    report.ce_fx += 1
                    fx_type = value = 0
                report.chords_above_29 += fx_type == 14 and value > 29
                report.chances_above_100 += fx_type == 4 and value > 100
                fx += [fx_type, value]
            out += bytes((note, instrument, *fx))
    out += bytes(4)     # 1.2.0 writes 0 here and doesn't check it
    assert len(out) == PATTERN_V5_SIZE
    return bytes(out)


def repair_patterns(project: Path, report: Report) -> dict[str, bytes]:
    changed = {}
    for path in sorted((project / "patterns").glob("pattern_*.mtp")):
        data = path.read_bytes()
        version = data[HEADER_VERSION] if len(data) > HEADER_VERSION else None
        if version is not None and version <= 5:
            continue
        if version != 6 or len(data) != PATTERN_V6_SIZE:
            raise Refused(f"{path.name}: format {version}, {len(data)} bytes. I only know format 6 "
                          f"at {PATTERN_V6_SIZE} bytes, so I changed nothing.")
        changed[f"patterns/{path.name}"] = pattern_v6_to_v5(data, report)
        report.patterns.append(path.name)
    return changed


def repair_project_file(project: Path, name: str, synth_model: str,
                        report: Report) -> tuple[bytes, list[str]]:
    path = project / "project.mt"
    if not path.is_file():
        raise Refused(f"No project.mt in {project}, so this isn't a project folder.")
    mt = bytearray(path.read_bytes())
    if len(mt) != PROJECT_SIZE or mt[:2] != b"MT" or mt[HEADER_VERSION] != PROJECT_VERSION:
        raise Refused(f"project.mt: {len(mt)} bytes, version {mt[HEADER_VERSION] if len(mt) > 8 else '?'}. "
                      f"I only know version {PROJECT_VERSION} at {PROJECT_SIZE} bytes, so I changed nothing.")
    mt[HEADER_FW:HEADER_FW + 4] = FW_1_2_0_PROJECT
    encoded = name.encode("ascii")
    mt[PROJECT_NAME:PROJECT_NAME + PROJECT_NAME_SIZE] = encoded.ljust(PROJECT_NAME_SIZE, b"\0")

    patches_left_out = []
    for slot in range(SLOTS):
        record = SLOT_RECORDS + slot * SLOT_RECORD_SIZE
        model = struct.unpack_from("<I", mt, record + SLOT_MODEL)[0]
        if model < len(STOCK_MODELS):
            continue
        was = CE_MODELS.get(model)
        if was is None:
            raise Refused(f"project.mt: Synth {slot + 1} has model {model}, which I don't know, "
                          "so I changed nothing.")
        struct.pack_into("<I", mt, record + SLOT_MODEL, STOCK_MODELS.index(synth_model))
        mt[record + SLOT_NAME:record + SLOT_NAME + SLOT_NAME_SIZE] = b"Init".ljust(SLOT_NAME_SIZE, b"\0")
        mt[record + SLOT_MACROS:record + SLOT_MACROS + SLOT_MACRO_COUNT] = bytes(SLOT_MACRO_COUNT)
        report.synths.append(f"Synth {slot + 1}: {was} -> {synth_model}, Init patch")
        patches_left_out.append(f"{slot}.patch")
    report.patches_left_out = [p for p in patches_left_out if (project / p).exists()]
    return bytes(mt), patches_left_out


def repair_instrument(data: bytes) -> tuple[bytes, list[str]]:
    if len(data) < INSTRUMENT_CRC + 4 or data[:2] != b"TI":
        raise Refused(f"not an instrument file I know ({len(data)} bytes)")
    if data[HEADER_VERSION] > INSTRUMENT_VERSION:
        raise Refused(f"instrument format {data[HEADER_VERSION]}, newer than I know")
    out = bytearray(data)
    notes = []
    for slot in range(ENV_SLOTS):
        envelope = ENVELOPES + slot * ENVELOPE_SIZE
        lfo = LFOS + slot * LFO_SIZE
        loop = out[envelope + ENVELOPE_LOOP]
        if loop in (2, 3):
            out[envelope + ENVELOPE_LOOP] = loop - 2
            out[lfo + LFO_WT_BEND] = out[lfo + LFO_WT_SUST] = 0
            notes.append("WT LFO -> LFO" if loop == 3 else "WT Env -> envelope")
        if out[lfo + LFO_SHAPE] >= LFO_SHAPES:
            out[lfo + LFO_SHAPE] = LFO_SHAPE_DEFAULT
            notes.append("WT shape -> triangle")
    wt_pos_enable = ENVELOPES + ENV_WT_POS * ENVELOPE_SIZE + ENVELOPE_ENABLE
    if out[wt_pos_enable] & 0x02:
        out[wt_pos_enable] &= 0x01
        notes.append("Granular Length automation off")
    if not notes:
        return data, notes
    out[INSTRUMENT_CRC:INSTRUMENT_CRC + 4] = bytes(4)   # as 1.2.0 writes it
    return bytes(out), sorted(set(notes))


def repair_instruments(project: Path, report: Report) -> dict[str, bytes]:
    changed = {}
    for path in sorted((project / "instruments").glob("*.pti")):
        try:
            repaired, notes = repair_instrument(path.read_bytes())
        except Refused as e:
            raise Refused(f"{path.name}: {e}, so I changed nothing.") from e
        if notes:
            changed[f"instruments/{path.name}"] = repaired
            report.instruments.append(f"{path.name}: {', '.join(notes)}")
    return changed


def repair(project: Path, out: Path, synth_model: str) -> Report:
    """Work everything out before writing, so a refusal leaves nothing behind."""
    if not project.is_dir():
        raise Refused(f"Folder not found: {project}")
    if out.exists():
        raise Refused(f"{out} already exists, and I won't write over it. Use --out to pick another name.")
    try:
        fits = 0 < len(out.name.encode("ascii")) < PROJECT_NAME_SIZE
    except UnicodeEncodeError:
        fits = False
    if not fits:
        raise Refused(f'"{out.name}" won\'t work as a project name: it has to be plain text, '
                      f"at most {PROJECT_NAME_SIZE - 1} characters. Use --out to pick another name.")

    report = Report()
    project_file, left_out = repair_project_file(project, out.name, synth_model, report)
    files = repair_patterns(project, report)
    files.update(repair_instruments(project, report))
    if not report.anything():
        return report

    files["project.mt"] = project_file
    try:
        shutil.copytree(project, out, ignore=lambda folder, names: left_out if Path(folder) == project else [])
        for relative, data in files.items():
            (out / relative).write_bytes(data)
    except BaseException:
        shutil.rmtree(out, ignore_errors=True)
        raise
    return report


def print_report(report: Report, project: Path, out: Path) -> None:
    if not report.anything():
        print(f"{project.name} has nothing 1.2.0 can't read, so I made no copy.")
        return
    print(f"Copy written: {out}")
    print(f"The original wasn't changed: {project}")
    print()
    if report.patterns:
        print(f"Patterns converted: {len(report.patterns)} ({', '.join(report.patterns)})")
        print(f"Lost in those patterns: {report.volumes} Volume, {report.micromoves} Micro-move, "
              f"{report.ce_fx} CE-only FX")
    for line in report.synths:
        print(line)
    if report.patches_left_out:
        print(f"Patches left out: {', '.join(report.patches_left_out)}")
    for line in report.instruments:
        print(f"Instrument {line}")
    if report.chords_above_29:
        print(f"Kept, but stock 1.2.0 plays a single note and shows odd text: "
              f"{report.chords_above_29} chords above 29")
    if report.chances_above_100:
        print(f"Kept, but stock 1.2.0 always plays them: {report.chances_above_100} Chance values above 100")


def main() -> int:
    parser = argparse.ArgumentParser(description="Copy a CE-saved project into a form 1.2.0 can open.")
    parser.add_argument("project", type=Path, help="the project folder; it is only read")
    parser.add_argument("--out", help='name or full path of the copy (default: "<name> 120" next to it)')
    parser.add_argument("--synth-model", choices=STOCK_MODELS, default="ACD",
                        help="stock model for a synth on DIRT or CHIP (default: ACD)")
    args = parser.parse_args()
    project = args.project.resolve()
    if args.out is None:
        out = project.with_name(f"{project.name} 120")
    else:
        out = Path(args.out)
        if not out.is_absolute():
            out = project.parent / out
    try:
        report = repair(project, out, args.synth_model)
    except Refused as e:
        print(e, file=sys.stderr)
        return 1
    print_report(report, project, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
