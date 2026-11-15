# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Humanize mod's mockups (docs/mods/humanize.md): the Pattern screen in CE's Micro-move/Volume
view, page 3 of the bottom bar with the Humanize button, a selection before and after a press.

The values after are what core/mods/humanize.h gives with the default settings (Timing range 10,
Volume/velocity On, Volume range 10, Sample volume 50) on steps with an empty m and V: m 0..10,
V 45..55 (a window of 10 around 50). They are picked by hand from those ranges: the mod's are random.

    python tools/docs/mockups_humanize.py
"""
from __future__ import annotations

from mockups_synths import write
from patternscreen import (CURSOR_ROW, OFFSET, POS_Y, SEL_OFFSET, STEP_H, TRACK_W, Pattern, Step, page3,
                           pattern_screen)
from svgscreen import COL


def example(humanized: bool) -> Pattern:
    p = Pattern()
    kick, hats, bass, keys = p.tracks
    for s in (0, 4, 8, 12):
        kick[s] = Step("C4", 1)
    for s in range(0, 16, 2):
        hats[s] = Step("F#4", 2)
    for s, n in ((0, "C2"), (3, "C2"), (6, "D#2"), (10, "G2"), (12, "A#1")):
        bass[s] = Step(n, 3)
    for s, n in ((0, "G4"), (8, "A#4")):
        keys[s] = Step(n, 4)
    if humanized:
        values = {(0, 4): (3, 52), (0, 8): (8, 47), (1, 2): (6, 49), (1, 4): (1, 54), (1, 6): (10, 46),
                  (1, 8): (4, 51)}
        for (t, s), (m, v) in values.items():
            p.tracks[t][s].m, p.tracks[t][s].v = m, v
    return p


SELECTION = (0, 1, 1, 8)       # Tracks 1-2, steps 2-9, the cursor at its end (Track 2, step 9)


def screen(title: str, humanized: bool):
    s = pattern_screen(title, example(humanized), cursor_step=8, cursor_track=1, column=2, mic_vol=True,
                       selection=SELECTION, right_label="Micro-move")
    slots = page3(s)
    s.mark(COL * slots["humanize"] + 3, 428, COL - 6, 48)
    return s


def main() -> None:
    s = screen("Humanize: before", False)
    write("humanize-before.svg", s, "The Pattern screen in the Micro-move/Volume view with steps 2-9 of Tracks 1 "
          "and 2 selected, m and V empty; page 3 of the bottom bar with the Humanize button")

    s = screen("Humanize: after", True)
    for t in (0, 1):    # the m and V columns of the selected tracks
        x = OFFSET + t * TRACK_W + 1 + SEL_OFFSET[2]
        s.mark(x, POS_Y + 1, 104, STEP_H * (CURSOR_ROW + 1) - 2)
    write("humanize-after.svg", s, "The same selection after Humanize: each note has an m of 0 to 10 and a V of "
          "45 to 55")


if __name__ == "__main__":
    main()
