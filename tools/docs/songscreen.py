# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""CE's Song screen as SVG, for the mods' user pages (Color Song, Song Loop).

The geometry is cSongPlayer's (core/libs/NewDisplay/songPlayer.cpp) and cSongEditor's
(core/interface/songEditor/gSongEdit.cpp, fSongEdit.cpp). With the cursor in the blocks (focus
"grid") there is no frame, the selection has a red outline and the list's frame is white; with the
cursor in the pattern column (focus "list") the list's frame is red with Rec on, green without.
"""
from __future__ import annotations

from svgscreen import COL, GREEN, GREY_12, GREY_33, GREY_44, GREY_77, RED, WHITE, Screen, text_width

PLAYER_X, PLAYER_Y, PLAYER_W, PLAYER_H = 1, 29, COL * 7 - 3, 394
TRACKS = 16
MARGIN, SPACING, BLOCK_H = 5, 4, 23
BLOCK_W = ((486 - MARGIN * 2) - (TRACKS - 1) * SPACING) // TRACKS     # 26
ROWS = 12                                                               # MAX_PATTERNS_VISIBLE
LIST_W, ROW_H = 210, 27
LIST_X = PLAYER_X + PLAYER_W - LIST_W                                   # 488
LIST_Y = PLAYER_Y + 62                                                  # 91
TRACK_NAMES = [f"Track {i + 1}" for i in range(8)] + [f"Midi {i + 1}" for i in range(8, 16)]

# Song Loop's repeat signs (core/mods/songloop.cpp kRepeatCols): 2 thick, 1 thin, 3 dots
REPEAT_COLS = (2, 2, 0, 1, 0, 3, 3, 0, 0, 3, 3, 0, 1, 0, 2, 2)


def block_x(track: int) -> int:
    return PLAYER_X + MARGIN + track * (BLOCK_W + SPACING)


def block_y(row: int) -> int:
    return PLAYER_Y + row * (BLOCK_H + SPACING) + 64


def list_row_y(position: int) -> int:
    return LIST_Y + position * ROW_H


def repeat_sign(s: Screen, x0: float, y0: float, y1: float, dy1: float, dy2: float, ncols: int,
                color: str = WHITE) -> None:
    """drawRepeat: one column per pixel; 16 columns are |: :|, 7 are |: alone"""
    for i in range(ncols):
        kind = REPEAT_COLS[i]
        if kind in (1, 2):
            s.rect(x0 + i, y0, 1, y1 - y0 + 1, color)
        elif kind == 3:
            s.rect(x0 + i, dy1, 1, 2, color)
            s.rect(x0 + i, dy2, 1, 2, color)


def song_screen(title: str, playlist: list[int], masks: dict[int, int], fill=None,
                selected: tuple[int, int] = (0, 0), cursor: int = 0, tempo: float = 120.0,
                muted: tuple[int, ...] = (), list_frame: tuple[int, int] | None = None,
                edit_mode: bool = False, focus: str = "grid", locked: bool = False,
                labels01: tuple[str, str, str, str] = ("Play", "Pattern", "Play", "Song"),
                timer: dict | None = None, playing: tuple[int, float] | None = None,
                list_icon: tuple[int, str] | None = None) -> Screen:
    """playlist: the song's patterns; masks: each pattern's used tracks; fill(position, track): the
    colour of a filled block (None: CE's white or grey); selected: (position, track) in the grid;
    cursor: the list's position; list_frame: (top, bottom) y of the list's frame, if not the cursor's
    row; locked: Song Loop greys Undo .. Pattern; timer: song_timer's arguments, with icon "stop",
    "play", "loop" or "armed"; playing: (position, fraction of the pattern) for the playhead line;
    list_icon: (position, "play" | "loop" | "armed")"""
    s = Screen(title)
    s.title_bar("Song")
    t = dict(pattern=playlist[cursor], icon="stop")
    t.update(timer or {})
    icon = t.pop("icon")
    s.song_timer(state=icon if icon in ("stop", "play") else "none", **t)
    if icon in ("loop", "armed"):
        # the two glyphs in place of the state's: the 16 columns start 10 px before the first's right
        lead = f"{t.get('minutes', 0):3d}m:{t.get('seconds', 0):02d}s "
        gx = 280 + text_width(lead, 4)
        repeat_sign(s, gx + 10 - 10 + 4, 13 - 10 + 4, 13 - 10 + 18, 13 - 10 + 8, 13 - 10 + 13,
                    16 if icon == "loop" else 7)

    # the player's background and shadow
    s.cover(PLAYER_X, PLAYER_Y, PLAYER_W, PLAYER_H)

    # track names, upwards from y 71 (posY + 42), white or the muted grey
    for tr in range(TRACKS):
        s.vtext(block_x(tr) + BLOCK_W / 2, PLAYER_Y + 42 + 12, TRACK_NAMES[tr], 5,
                GREY_33 if tr in muted else WHITE)

    # the blocks
    for row, pattern in enumerate(playlist[:ROWS]):
        mask = masks.get(pattern, 0)
        for tr in range(TRACKS):
            x, y = block_x(tr), block_y(row)
            if not mask & (1 << tr):
                s.rect(x, y, BLOCK_W, BLOCK_H, GREY_12)
            else:
                color = fill(row, tr) if fill else None
                if color is None:
                    color = GREY_44 if tr in muted else WHITE
                s.rect(x, y + 1, BLOCK_W - 1, BLOCK_H - 2, color)
    if focus == "grid":
        sp, st = selected
        s.outline(block_x(st), block_y(sp), BLOCK_W, BLOCK_H, RED, 1.5)

    # the playhead: a line across the row, red with Rec on
    if playing:
        pos, frac = playing
        y = block_y(pos) + int(frac * BLOCK_H + 0.5)
        x0 = PLAYER_X + MARGIN
        s.line(x0, y, x0 + (BLOCK_W + SPACING) * TRACKS - SPACING, y, RED if edit_mode else GREEN, 1.5)

    # the list: "Pattern", the frame, each position's pattern number (names are empty by default)
    s.text(600, PLAYER_Y + 30, "Pattern", 1, WHITE, "middle")
    w_bar = LIST_W - 6 if len(playlist) <= ROWS else LIST_W - 13
    top, bottom = list_frame or (list_row_y(cursor), list_row_y(cursor) + ROW_H)
    frame_color = WHITE if focus != "list" else (RED if edit_mode else GREEN)
    s.outline(LIST_X + 3, top, w_bar, bottom - top, frame_color, 1.5)
    for i, pattern in enumerate(playlist[:ROWS]):
        s.text(LIST_X + 7 + 25, list_row_y(i) + ROW_H / 2, str(pattern), 1, WHITE, kind="val")
    if list_icon:
        pos, kind = list_icon
        x, y = LIST_X + 7, list_row_y(pos) + ROW_H / 2 - 9
        if kind == "play":
            s.poly([(x + 5, y + 4), (x + 16, y + 10), (x + 5, y + 16)], WHITE)
        else:
            repeat_sign(s, x + 2, y + 3, y + 16, y + 6, y + 12, 16 if kind == "loop" else 7)

    # column 8: the tempo bar
    s.bar(COL * 7 + 1, PLAYER_Y, COL - 3, PLAYER_H, int(tempo * 100 / 800))

    # the bottom bar: bgLabel 191, labels 2-4 and 5 with their red lines; 2-5 grey with Rec off
    s.bottom_bar(191)
    s.button(0, labels01[0], labels01[1], 1)
    s.button(1, labels01[2], labels01[3], 1)
    c = GREY_77 if edit_mode and not locked else GREY_33
    s.button(2, "Undo", color=c)
    s.button(3, "Add", "Slot", 1, color=c, color2=c)
    s.button(4, "Delete", "Slot", 1, color=c, color2=c)
    s.button(5, "Pattern", x=COL * 6, color=c)
    double_arrow(s, COL * 5 + COL / 2 - 10, 445, left=True, color=c)
    double_arrow(s, COL * 6 + COL / 2 + 10, 445, left=False, color=c)
    s.button(7, "Tempo", f"{tempo:.1f} BPM", 1, color2=WHITE, font2=2, kind2="val")
    for cx, half in ((COL * 2.5, 40), (COL * 3.5, 40), (COL * 4.5, 40), (COL * 6, 90)):
        s.line(cx - half, 474, cx + half, 474, RED, 0.5)
    return s


def double_arrow(s: Screen, cx: float, y: float, left: bool, color: str = GREY_77) -> None:
    """displayDoubleArrowL / R, drawn as two chevrons"""
    d = -1 if left else 1
    for off in (-4, 4):
        x = cx + off * d
        s.parts.append(f'<polyline points="{x - 3 * d:g},{y - 5:g} {x + 3 * d:g},{y:g} {x - 3 * d:g},{y + 5:g}" '
                       f'fill="none" stroke="{color}" stroke-width="1.5"/>')
