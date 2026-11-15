# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Variable Track Length mod's mockup (docs/mods/tracklength.md): a 16-step pattern whose Track 3
is 6 steps long and Track 4 is 4, with the cursor on Track 3 and page 3 of the bottom bar showing
Trk Len with the cursor's track's length.

Past a short track's length, a row shows the step the track plays there (step mod length) in the
inactive-track grey, and nothing where that step is empty (core/libs/NewDisplay/tracker.cpp and
fPatternEdit.cpp under MT_HAS_TRACKLEN; Trk Len's second line is trackLengthText).

    python tools/docs/mockups_tracklength.py
"""
from __future__ import annotations

from mockups_synths import write
from patternscreen import OFFSET, POS_Y, STEP_H, TRACK_W, Pattern, Step, page3, pattern_screen
from svgscreen import COL

LENGTHS = {2: 6, 3: 4}
CURSOR_STEP = 8          # rows show steps 2 to 16


def example() -> Pattern:
    p = Pattern()
    kick, hats, bass, keys = p.tracks
    for s in (0, 4, 8, 12):
        kick[s] = Step("C4", 1)
    for s in range(0, 16, 2):
        hats[s] = Step("F#4", 2)
    for s, n in ((0, "C2"), (3, "C2"), (5, "D#2")):
        bass[s] = Step(n, 3)
    for s, n in ((0, "G4"), (2, "A#4")):
        keys[s] = Step(n, 4)
    return p


def main() -> None:
    s = pattern_screen("Track Length: short tracks", example(), cursor_step=CURSOR_STEP, cursor_track=2, column=0,
                       right_label="3. Bass.wav", right_kind="demo", track_lengths=LENGTHS)
    slots = page3(s, track_length=str(LENGTHS[2]))
    s.mark(COL * slots["tracklen"] + 3, 428, COL - 6, 48)
    first = CURSOR_STEP - 7
    for t, own in LENGTHS.items():   # the rows past each short track's length
        top = POS_Y + (own - first) * STEP_H
        s.mark(OFFSET + t * TRACK_W + 3, top + 1, TRACK_W - 6, POS_Y + 15 * STEP_H - top - 3)
    write("tracklength-pattern.svg", s, "A 16-step pattern with Track 3 six steps long and Track 4 four: past their "
          "length they show their loop in grey; Trk Len shows 6 for the cursor's track")


if __name__ == "__main__":
    main()
