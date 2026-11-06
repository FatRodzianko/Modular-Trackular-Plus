#!/usr/bin/env python3
"""A note must not leak the track's last send into the effects.

Usage: python tools/emu/test_send_leak.py <mods.axf> <reference.axf> <project folder>

The recipe that showed it on the unit, on the real path: track 1 plays A with sends on, Stop, another
pattern, Play, and B with no sends plays. handleNoteOn*Send cleared only the left of a track's two send
channels, so B's first block went into the delay and the reverb on the right one, ramping down from A's
send. Each effect's input is read per block, as what the effect does with it isn't the question here.

Plain CE leaks, so the reference's figure is printed and not judged: upstream may fix it. The project needs
instruments numbered 1 and 2; only its instruments and project.mt are used.
"""
import shutil
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import make_masterfx_test_project as maker  # noqa: E402
from boot_rig import BootRig  # noqa: E402

ce = maker.ce
PLAY = 23
GAP = 8                 # steps between notes
TEMPO = 240.0           # so 21 blocks a step: a note's gap is about half a second
SEND = 60
A, B = 1, 2             # as the instruments' files are numbered
OTHER_PATTERN = 50
EFFECTS = {"delay": "_ZN21AudioEffectShortDelay6updateEv", "reverb": "_ZN19AudioEffectPolyverb6updateEv"}
failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what, flush=True)
    if not ok:
        failures += 1


def notes_of(instrument):
    data = bytearray(maker.pattern((0, 0, 0)))
    for step in range(0, 64, GAP):
        at = ce.PATTERN_HEADER + 1 + step * ce.STEP_V6
        data[at:at + 2] = bytes((48, instrument - 1))
    return bytes(data)


def make_project(source, folder):
    shutil.copytree(source, folder)
    for path in (folder / "instruments").glob("*.pti"):
        number = int(path.name.split()[0])
        if number not in (A, B):
            path.unlink()
            continue
        data = bytearray(path.read_bytes())
        data[maker.INSTRUMENT_DELAY_SEND] = data[maker.INSTRUMENT_REVERB_SEND] = SEND if number == A else 0
        path.write_bytes(data)
    for path in (folder / "patterns").glob("pattern_*.mtp"):
        path.write_bytes(notes_of(A))
    (folder / "patterns" / f"pattern_{OTHER_PATTERN}.mtp").write_bytes(notes_of(B))
    mt = bytearray((folder / "project.mt").read_bytes())
    struct.pack_into("<f", mt, maker.PROJECT_TEMPO, TEMPO)
    (folder / "project.mt").write_bytes(bytes(mt))
    (folder / "masterfx.bin").write_bytes(maker.masterfx_file(0, 1))    # Tape, where it was heard


def run(axf, project):
    """{effect: blocks with signal in, before and from B's start}, and what played."""
    r = BootRig(axf)
    m = r.m
    block = [0]
    b_start = [None]
    hits = {name: {"A": [], "B": []} for name in EFFECTS}
    notes = []

    def watcher(name):
        def on_update(mm):
            this = mm.arg(0)
            queue = next(v for v in (m.r32(this + off) for off in range(4, 64, 4)) if this < v < this + 0x800)
            input_block = m.r32(queue)
            if not input_block:
                return
            peak = max(map(abs, struct.unpack("<128h", m.read(input_block + 4, 256))))
            if peak:
                hits[name]["A" if b_start[0] is None else "B"].append((block[0], peak))
        return on_update

    for name, symbol in EFFECTS.items():
        r.watch(m.addr(symbol), watcher(name))
    r.watch(m.addr("_ZN12playerEngine6noteOnEhaahhhh"), lambda mm: notes.append((block[0], mm.arg(1) & 255)))
    screen = [0]
    r.watch(m.addr("_ZN14cPatternEditor6updateEv"), lambda mm: screen.__setitem__(0, screen[0] + 1))

    r.card_from(project, "Send leak", workspace=True)
    r.boot()
    for _ in range(3000):
        r.loop()
        r.interrupt()
        if screen[0] > 5:
            break

    def press_play():
        r.press(PLAY)
        r.loop()
        r.interrupt()
        r.release(PLAY)
        r.loop()
        r.interrupt()

    def blocks(n):
        peak = 0
        for i in range(n):
            block[0] += 1
            if i % 8 == 0:
                r.loop()
            left, right = r.interrupt()
            peak = max(peak, max(map(abs, left)), max(map(abs, right)))
        return peak

    for name in EFFECTS:
        hits[name]["A"].clear()
    press_play()
    blocks(120)
    press_play()                    # Stop
    blocks(400)                     # the tail dies away
    m.call(m.addr("_ZN12cFileManager24loadPatternFromWorkspaceEh"), m.addr("newFileManager"), OTHER_PATTERN,
           max_insns=50_000_000)
    m.call(m.addr("_ZN9Sequencer20switchRamPatternsNowEv"), m.addr("sequencer"), max_insns=50_000_000)
    b_start[0] = block[0]
    m.call(m.addr("_ZN9Sequencer11playPatternEh"), m.addr("sequencer"), 0, max_insns=50_000_000)
    b_peak = blocks(GAP * 21 * 3)
    return {"hits": hits, "notes": notes, "b_start": b_start[0], "b_peak": b_peak, "failures": r.failures}


def main():
    mods, reference, source = sys.argv[1:4]
    with tempfile.TemporaryDirectory() as tmp:
        project = Path(tmp) / "project"
        make_project(source, project)
        got = run(mods, project)
        plain = run(reference, project)

    b_notes = [n for n in got["notes"] if n[0] >= got["b_start"]]
    check(not got["failures"], f"no failures in the run ({got['failures']})")
    check(any(n[1] == A - 1 for n in got["notes"]) and len(b_notes) >= 3 and all(n[1] == B - 1 for n in b_notes),
          f"A played, then B three times: {got['notes']}")
    check(got["b_peak"] > 1000, f"B sounded: output peak {got['b_peak']}")
    for name in EFFECTS:
        check(len(got["hits"][name]["A"]) > 50, f"A's send reached the {name}: {len(got['hits'][name]['A'])} blocks")
        check(not got["hits"][name]["B"], f"nothing from B reached the {name}: {got['hits'][name]['B'][:4]}")
        print(f"     plain CE, the same: {len(plain['hits'][name]['A'])} blocks from A, "
              f"{plain['hits'][name]['B'][:4]} from B")
    print("failures:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
