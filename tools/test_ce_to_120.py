"""Tests for ce_to_120.py on made-up projects. Needs only Python.

    python tools/test_ce_to_120.py

tools/emu/check_ce_to_120.py checks a repaired project on real 1.2.0 code.
"""
from __future__ import annotations

import filecmp
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ce_to_120 as t  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    failures += not ok


def header(id_: bytes, kind: int, version: int, size: int, fw=(1, 0, 0, 1)) -> bytearray:
    return bytearray(id_ + struct.pack("<H", kind) + bytes(fw) + bytes((version,)) * 4 + struct.pack("<H", size))


def v6_pattern(edit=None) -> bytes:
    data = header(b"KS", 2, 6, t.PATTERN_V6_SIZE)
    data += bytes(t.PATTERN_HEADER - len(data))
    for _ in range(t.TRACKS):
        data.append(31)
        for _ in range(t.STEPS):
            data += bytes((0xFF, 0, 0, 0, 0, 0, 0xFF, 0xFF))
    data += bytes(4)
    assert len(data) == t.PATTERN_V6_SIZE
    if edit:
        edit(data)
    return bytes(data)


def step_at(track: int, step: int, size=t.STEP_V6) -> int:
    return t.PATTERN_HEADER + track * (1 + t.STEPS * size) + 1 + step * size


def project_file(models=(0, 1, 2), name=b"Song") -> bytes:
    mt = header(b"MT", 0, t.PROJECT_VERSION, 0x914, fw=(1, 0, 0, 0xFF))
    mt += bytes(t.PROJECT_SIZE - len(mt))
    mt[t.PROJECT_NAME:t.PROJECT_NAME + len(name)] = name
    for slot, model in enumerate(models):
        record = t.SLOT_RECORDS + slot * t.SLOT_RECORD_SIZE
        struct.pack_into("<I", mt, record + t.SLOT_MODEL, model)
        mt[record + t.SLOT_NAME:record + t.SLOT_NAME + 4] = b"Lead"
        mt[record + t.SLOT_MACROS:record + t.SLOT_MACROS + 6] = bytes((63,)) * 6
    return bytes(mt)


def instrument(edit=None) -> bytes:
    data = header(b"TI", 1, 9, 372, fw=(1, 9, 2, 1))
    data += bytes(t.INSTRUMENT_CRC - len(data))
    data += b"\x11\x22\x33\x44" + b"SAMPLE"
    if edit:
        edit(data)
    return bytes(data)


def make_project(folder: Path, models=(0, 1, 2)) -> Path:
    folder.mkdir(parents=True)
    (folder / "patterns").mkdir()
    (folder / "instruments").mkdir()
    (folder / "project.mt").write_bytes(project_file(models))
    (folder / "patterns" / "patternsMetadata").write_bytes(b"names")
    for name, data in (("0.patch", b"\x08\x08zero"), ("1.patch", b"\x08\x07phase"), ("2.patch", b"\x08\x04vap"),
                       ("swing.bin", b"HSW1side"), ("mods.bin", b"MTP1")):
        (folder / name).write_bytes(data)
    (folder / "patterns" / "pattern_01.mtp").write_bytes(b"\0" * 8 + bytes((5,)) * 4 + bytes(t.PATTERN_V5_SIZE - 12))
    (folder / "instruments" / "1 Kick.pti").write_bytes(instrument())
    return folder


def snapshot(folder: Path) -> dict[str, bytes]:
    return {str(p.relative_to(folder)): p.read_bytes() for p in folder.rglob("*") if p.is_file()}


def test_pattern():
    print("Pattern conversion")
    def edit(d):
        at = step_at(2, 5)
        d[at:at + t.STEP_V6] = bytes((60, 7, 4, 150, 14, 40, 90, 55))
        at = step_at(15, 127)
        d[at:at + t.STEP_V6] = bytes((72, 64, 43, 10, 42, 9, 0xFF, 0xFF))
        d[t.PATTERN_HEADER + 3 * (1 + t.STEPS * t.STEP_V6)] = 127
        d[0x0E:0x10] = b"\xF0\xF1"
    report = t.Report()
    out = t.pattern_v6_to_v5(v6_pattern(edit), report)
    check(len(out) == t.PATTERN_V5_SIZE, "the result is 1.2.0's size")
    check(out[t.HEADER_VERSION:t.HEADER_VERSION + 4] == bytes((5,)) * 4, "the header says format 5")
    check(struct.unpack_from("<H", out, t.HEADER_SIZE)[0] == t.PATTERN_V5_SIZE, "the header has 1.2.0's size")
    check(out[0x0E:0x10] == b"\xF0\xF1", "the rest of the header is kept")
    at = step_at(2, 5, t.STEP_V5)
    check(out[at:at + t.STEP_V5] == bytes((60, 7, 4, 150, 14, 40)), "a step keeps its note, instrument and both FX")
    at = step_at(15, 127, t.STEP_V5)
    check(out[at:at + t.STEP_V5] == bytes((72, 64, 0, 0, 42, 9)), "the last step of the last track lands right; "
          "FX 43 is cleared, FX 42 is kept")
    check(out[t.PATTERN_HEADER + 3 * (1 + t.STEPS * t.STEP_V5)] == 127, "a track's length moves with the track")
    check((report.volumes, report.micromoves, report.ce_fx) == (1, 1, 1), "what's lost is counted")
    check((report.chords_above_29, report.chances_above_100) == (1, 1), "what stock will play differently is counted")


def test_instrument():
    print("Instruments")
    plain = instrument()
    same, notes = t.repair_instrument(plain)
    check(same == plain and not notes, "an instrument with nothing CE-only isn't changed")
    def edit(d):
        d[t.ENVELOPES + t.ENVELOPE_LOOP] = 2
        d[t.ENVELOPES + t.ENVELOPE_SIZE + t.ENVELOPE_LOOP] = 3
        d[t.LFOS + t.LFO_SIZE + t.LFO_SHAPE] = 90
        d[t.LFOS + t.LFO_SIZE + t.LFO_WT_BEND] = 0x55
        d[t.ENVELOPES + 3 * t.ENVELOPE_SIZE + t.ENVELOPE_ENABLE] = 3
    out, notes = t.repair_instrument(instrument(edit))
    check(out[t.ENVELOPES + t.ENVELOPE_LOOP] == 0, "WT Env becomes an envelope")
    check(out[t.ENVELOPES + t.ENVELOPE_SIZE + t.ENVELOPE_LOOP] == 1, "WT LFO becomes an LFO")
    check(out[t.LFOS + t.LFO_SIZE + t.LFO_SHAPE] == t.LFO_SHAPE_DEFAULT, "a wavetable shape becomes triangle")
    check(out[t.LFOS + t.LFO_SIZE + t.LFO_WT_BEND] == 0, "its warp is cleared")
    check(out[t.ENVELOPES + 3 * t.ENVELOPE_SIZE + t.ENVELOPE_ENABLE] == 1,
          "Granular Length automation goes off, Wavetable Position stays on")
    check(out[t.INSTRUMENT_CRC:t.INSTRUMENT_CRC + 4] == bytes(4), "the CRC is 0, as 1.2.0 writes it")
    check(out.endswith(b"SAMPLE"), "the sample is kept")
    try:
        t.repair_instrument(instrument(lambda d: d.__setitem__(t.HEADER_VERSION, 10)))
        check(False, "a newer instrument format is refused")
    except t.Refused:
        check(True, "a newer instrument format is refused")


def test_project(base: Path):
    print("Whole project")
    source = make_project(base / "a" / "Song", models=(6, 0, 5))
    def edit(d):
        at = step_at(0, 0)
        d[at:at + t.STEP_V6] = bytes((60, 1, 0, 0, 0, 0, 80, 0xFF))
    (source / "patterns" / "pattern_08.mtp").write_bytes(v6_pattern(edit))
    before = snapshot(source)
    out = source.with_name("Song 120")
    report = t.repair(source, out, "WTFM")
    check(snapshot(source) == before, "the original isn't touched")

    mt = (out / "project.mt").read_bytes()
    check(mt[t.PROJECT_NAME:t.PROJECT_NAME + t.PROJECT_NAME_SIZE] == b"Song 120".ljust(t.PROJECT_NAME_SIZE, b"\0"),
          "project.mt has the copy's name, so Save can't reach the original")
    models = [struct.unpack_from("<I", mt, t.SLOT_RECORDS + s * t.SLOT_RECORD_SIZE + t.SLOT_MODEL)[0] for s in range(3)]
    check(models == [3, 0, 3], "CHIP and DIRT become the chosen stock model; a stock slot stays")
    slot0, slot1 = t.SLOT_RECORDS, t.SLOT_RECORDS + t.SLOT_RECORD_SIZE
    check(mt[slot0 + t.SLOT_NAME:slot0 + t.SLOT_NAME + 5] == b"Init\0", "a changed slot asks for the Init patch")
    check(mt[slot1 + t.SLOT_NAME:slot1 + t.SLOT_NAME + 4] == b"Lead", "a stock slot keeps its patch name")
    check(not (out / "0.patch").exists() and not (out / "2.patch").exists(), "the CE synths' patches are left out")
    check((out / "1.patch").read_bytes() == b"\x08\x07phase",
          "a stock slot's patch is kept, even with an id only a 1.2.0 mod knows")
    converted = (out / "patterns" / "pattern_08.mtp").read_bytes()
    check(len(converted) == t.PATTERN_V5_SIZE and converted[t.HEADER_VERSION] == 5, "the CE pattern is converted")
    check(report.volumes == 1, "the report counts the Volume lost")
    for name in ("swing.bin", "mods.bin", "patterns/pattern_01.mtp", "patterns/patternsMetadata", "instruments/1 Kick.pti"):
        check(filecmp.cmp(source / name, out / name, shallow=False), f"{name} is copied byte for byte")

    fine = make_project(base / "b" / "Fine")
    report = t.repair(fine, fine.with_name("Fine 120"), "ACD")
    check(not report.anything() and not fine.with_name("Fine 120").exists(),
          "a project 1.2.0 can already read gets no copy")


def test_refusals(base: Path):
    print("Refusals leave nothing behind")
    source = make_project(base / "c" / "Song", models=(6, 0, 1))
    out = source.with_name("Song 120")

    def refused(what, target=out, model="ACD"):
        try:
            t.repair(source, target, model)
            check(False, what)
        except t.Refused:
            check(not target.exists() or target == source.with_name("Taken"), what)

    (source.parent / "Taken").mkdir()
    refused("the copy's folder already exists", source.parent / "Taken")
    refused("a name too long for the tracker", source.with_name("x" * 33))
    refused("a name that isn't plain text", source.with_name("Sóng"))
    bad = source / "patterns" / "pattern_09.mtp"
    bad.write_bytes(header(b"KS", 2, 7, 100) + bytes(100))
    refused("a newer pattern format")
    bad.write_bytes(v6_pattern()[:-1])
    refused("a pattern of the wrong size")
    bad.unlink()
    (source / "project.mt").write_bytes(project_file(models=(9, 0, 1)))
    refused("a synth model nobody knows")
    (source / "project.mt").write_bytes(b"MT")
    refused("a damaged project.mt")
    (source / "project.mt").unlink()
    refused("a folder with no project.mt")
    try:
        t.repair(base / "nowhere", base / "nowhere 120", "ACD")
        check(False, "a folder that doesn't exist")
    except t.Refused:
        check(True, "a folder that doesn't exist")


def main() -> int:
    test_pattern()
    test_instrument()
    with tempfile.TemporaryDirectory() as folder:
        base = Path(folder)
        test_project(base)
        test_refusals(base)
    print(f"{failures} failed" if failures else "All passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
