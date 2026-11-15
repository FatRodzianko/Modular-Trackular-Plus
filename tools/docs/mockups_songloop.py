# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Song Loop mod's mockups (docs/mods/songloop.md): a block of song positions selected in the
Song screen's pattern column, the loop armed, and the loop playing.

From core/mods/songloop.cpp: the softkeys (labels(): "Play"/"Selection", "Continue"/"Song",
"Stop"), Undo .. Pattern greyed while a block exists (locked, recolorSoftkeys), the frame over the
whole block (cursorFrame), and the repeat signs in place of the play icon, in the status bar
(statusIcon: |: :| looping, |: armed) and in the list (drawListIcon).

    python tools/docs/mockups_songloop.py
"""
from __future__ import annotations

from mockups_colorsong import PATTERNS, PLAYLIST
from mockups_synths import write
from songscreen import LIST_X, ROW_H, list_row_y, song_screen
from svgscreen import COL, text_width

MASKS = {p: sum(1 << t for t in tracks) for p, tracks in PATTERNS.items()}
FIRST, LAST = 3, 5                      # the block: positions 4 to 6
BLOCK = (list_row_y(FIRST), list_row_y(LAST) + ROW_H)


def marks(s, timer: bool = False, icon_row: int | None = None) -> None:
    s.mark(LIST_X + 3, BLOCK[0], 204, BLOCK[1] - BLOCK[0])          # the block's frame
    s.mark(3, 428, COL - 6, 48)                                       # the left softkey
    if timer:
        x = 280 + text_width("  0m:38s ", 4)
        s.mark(x + 2, 4, 20, 18)
    if icon_row is not None:
        s.mark(LIST_X + 7, list_row_y(icon_row) + 2, 24, 23)


def main() -> None:
    # 1. stopped, Rec on, the block selected with Shift + Down
    s = song_screen("Song Loop: a block selected", PLAYLIST, MASKS, cursor=FIRST, list_frame=BLOCK,
                    edit_mode=True, focus="list", locked=True, labels01=("Play", "Selection", "Play", "Song"))
    marks(s)
    write("songloop-select.svg", s, "Song positions 4 to 6 selected in the pattern column; the left softkey reads "
          "Play Selection, Undo to Pattern are greyed out")

    # 2. playing before the block: armed
    s = song_screen("Song Loop: armed", PLAYLIST, MASKS, cursor=FIRST, list_frame=BLOCK, edit_mode=True,
                    focus="list", locked=True, labels01=("Continue", "Song", "Stop", ""),
                    timer=dict(minutes=0, seconds=12, pattern=PLAYLIST[1], step=9, icon="armed"),
                    playing=(1, 9 / 16), list_icon=(1, "armed"))
    marks(s, timer=True, icon_row=1)
    write("songloop-armed.svg", s, "Playing at position 2, before the block: the status bar and the list show |: "
          "where the play icon was")

    # 3. playing in the block: looping
    s = song_screen("Song Loop: looping", PLAYLIST, MASKS, cursor=FIRST, list_frame=BLOCK, edit_mode=True,
                    focus="list", locked=True, labels01=("Continue", "Song", "Stop", ""),
                    timer=dict(minutes=0, seconds=38, pattern=PLAYLIST[4], step=7, icon="loop"),
                    playing=(4, 7 / 16), list_icon=(4, "loop"))
    marks(s, timer=True)           # the list icon is inside the block's mark
    write("songloop-looping.svg", s, "Playing inside the block: the status bar and the list show |: :|, the left "
          "softkey reads Continue Song")


if __name__ == "__main__":
    main()
