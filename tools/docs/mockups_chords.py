# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""CE's MIDI Chord FX (FX letter 0) mapped to chords and their inversions in C (docs/mods/chords.md).

The chords come from CE's table (core/source/mtChordDefs.h): chordDef[i] is a bitmask, bit n a note n
semitones above the step's note (Sequencer::sendMidiChordOn), and chordDefName[i] the value the FX
column shows after the letter 0. Each chord's inversions are found by rotating its notes and looking
the new shape up in the same table, so each value printed here is one CE has.

Each screen is a 16-step pattern: one chord per row from step 2 (the track names cover step 1), the
tracks named Root, 1st inv, 2nd inv and 3rd inv. Root position starts on C4; an inversion starts on that chord note in the octave below, so
the chord still reaches C4. The step's instrument is synth 1 (the FX plays on synth and MIDI
instruments).

    python tools/docs/mockups_chords.py           # the SVGs, and the Markdown tables on stdout
"""
from __future__ import annotations

import re
from pathlib import Path

from mockups_synths import write
from patternscreen import Pattern, Step, page1, pattern_screen

ROOT = Path(__file__).resolve().parents[2]
DEFS = ROOT / "core" / "source" / "mtChordDefs.h"
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]   # mtNotes
LETTERS = "CDEFGAB"
NATURAL = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
C4 = 48                       # mtNotes index of "C4"

# (CE's value for root position, the chord's name, its notes spelled in C)
TRIADS = [
    (" 27", "Sus2", "C D G"),
    (" 28", "Sus2#5", "C D G#"),
    (" 36", "Diminished", "C Eb Gb"),
    (" 37", "Minor", "C Eb G"),
    (" 47", "Major", "C E G"),
    (" 48", "Augmented", "C E G#"),
    (" 57", "Sus4", "C F G"),
    (" 5A", "Stacked 4ths", "C F Bb"),
    (" 5C", "Open4", "C F C"),
    (" 67", "Sus#4", "C F# G"),
    (" 7C", "Open5", "C G C"),
    (" 7E", "Open sus2", "C G D"),
]
SEVENTHS = [
    ("47B", "Maj7", "C E G B"),
    ("37A", "Min7", "C Eb G Bb"),
    ("47A", "Dom7", "C E G Bb"),
    ("369", "Dim7", "C Eb Gb Bbb"),
    ("36A", "m7b5", "C Eb Gb Bb"),
    ("37B", "mMaj7", "C Eb G B"),
    ("48A", "Aug7", "C E G# Bb"),
    ("48B", "AugMaj7", "C E G# B"),
    ("57A", "7sus4", "C F G Bb"),
    ("27A", "Sus2b7", "C D G Bb"),
    ("27B", "Sus2maj7", "C D G B"),
    ("57B", "Sus4maj7", "C F G B"),
]
SIXTHS = [
    ("479", "Maj6", "C E G A"),
    ("379", "Min6", "C Eb G A"),
    ("279", "6sus2", "C D G A"),
    ("378", "m(b6)", "C Eb G Ab"),
    ("489", "Aug(add6)", "C E G# A"),
]
SCREENS = [("chords-triads.svg", "3-note chords", TRIADS),
           ("chords-sevenths.svg", "7th chords", SEVENTHS),
           ("chords-sixths.svg", "6th chords and others", SIXTHS)]
INVERSIONS = ["Root", "1st inv", "2nd inv", "3rd inv"]


def table() -> tuple[list[int], list[str]]:
    text = DEFS.read_text()
    defs = [int(b, 2) for b in re.findall(r"0b([01]+),", text)]
    names = re.findall(r'"(.{3})"', text.split("chordDefName")[1])
    assert len(defs) == len(names) == 68, (len(defs), len(names))
    return defs, names


def intervals(mask: int) -> list[int]:
    return [n for n in range(16) if mask & (1 << n)]


def mask_of(notes: list[int]) -> int:
    return sum(1 << n for n in notes)


def semitone(name: str) -> int:
    n = NATURAL[name[0]] + name.count("#") - name.count("b")
    return n % 12


def spell_octaves(spelled: list[str], start: int) -> list[str]:
    """The chord's notes as written, with octaves, from the mtNotes index start upwards"""
    out, pitch = [], start
    for i, name in enumerate(spelled):
        if i:
            pitch += (semitone(name) - pitch) % 12 or 12
        octave = (pitch - name.count("#") + name.count("b")) // 12     # the letter's own octave: Bbb3 is A3
        out.append(f"{name}{octave}")
    return out


def inversions(root_name: str, spelled: str, defs: list[int], names: list[str]) -> list[dict | None]:
    """Root position and each inversion: the step's note, CE's value, the notes as written"""
    index = names.index(root_name)
    notes = intervals(defs[index])
    tones = spelled.split()
    assert len(tones) == len(notes)
    assert [semitone(t) for t in tones] == [n % 12 for n in notes], (root_name, spelled)
    out: list[dict | None] = [{"step": C4, "index": index, "tones": spell_octaves(tones, C4)}]
    for k in range(1, 4):
        if k >= len(notes) or notes[-1] >= 12:      # no inversion: a 3-note chord, or one an octave wide
            out.append(None)
            continue
        shape = [n - notes[k] for n in notes[k:]] + [n + 12 - notes[k] for n in notes[:k]]
        if mask_of(shape) not in defs:
            out.append({"step": C4 + notes[k] - 12, "index": None, "tones": []})
            continue
        found = defs.index(mask_of(shape))
        rotated = tones[k:] + tones[:k]
        out.append({"step": C4 + notes[k] - 12, "index": found, "tones": spell_octaves(rotated, C4 + notes[k] - 12)})
    return out


def note_text(index: int) -> str:
    return f"{NOTES[index % 12]}{index // 12}"


def fx_cell(names: list[str], index: int) -> str:
    return "0" + names[index]                         # the FX letter, then the value right-aligned


def fx_ce(names: list[str], index: int) -> str:
    return "0" + names[index].strip()                 # as CE's FX description writes it


def main() -> None:
    defs, names = table()
    used: set[int] = set()
    for svg, heading, chords in SCREENS:
        p = Pattern(names=list(INVERSIONS))
        rows = []
        for r, (root_name, chord, spelled) in enumerate(chords):
            entries = inversions(root_name, spelled, defs, names)
            rows.append((chord, entries))
            for t, e in enumerate(entries):
                if e and e["index"] is not None:
                    p.tracks[t][r + 1] = Step(note_text(e["step"]), "S1", fx1=fx_cell(names, e["index"]))
                    used.add(e["index"])
        cursor = min(7, len(chords))
        s = pattern_screen(f"Chords: {heading}", p, cursor_step=cursor, cursor_track=0, column=2,
                           right_label="0 - MIDI Chord", names_kind="demo")
        page1(s, p)
        write(svg, s, f"The MIDI Chord FX, {heading}: one chord per step, root position and inversions in C")

        print(f"\n### {heading}\n")
        print("| Step | Chord | Root position | 1st inversion | 2nd inversion | 3rd inversion |")
        print("|---|---|---|---|---|---|")
        for r, (chord, entries) in enumerate(rows):
            cols = []
            for e in entries:
                if e is None:
                    cols.append("")
                elif e["index"] is None:
                    cols.append("not in CE")
                else:
                    cols.append(f"`{note_text(e['step'])}` `{fx_ce(names, e['index'])}` ({' '.join(e['tones'])})")
            print(f"| {r + 2} | {chord} | " + " | ".join(cols) + " |")
    missing = [i for i in range(1, 68) if i not in used]
    print("\nvalues on no screen:", [(i, names[i]) for i in missing])


if __name__ == "__main__":
    main()
