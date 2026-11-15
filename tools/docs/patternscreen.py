# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""CE's Pattern screen as SVG, for the mods' user pages (Humanize, Copy to New, Track Length).

The geometry is cTracker's (core/libs/NewDisplay/tracker.cpp) in view mode 0 (four tracks, four
columns each) under the title bar (patternInfo 0: y 28, h 395), with the default numbering (steps
from 1) and step divider (4). The cells are filled as cPatternEditor::refreshPattern does
(core/interface/patternEditor/fPatternEdit.cpp); page 3 of the bottom bar is
showActionButtonPage3Labels (gPatternEdit.cpp) with the mods' buttons.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from svgscreen import COL, GREEN, GREY_08, GREY_22, GREY_33, GREY_57, GREY_77, RED, WHITE, Screen

POS_Y, HEIGHT = 28, 395
STEP_H = HEIGHT // 15                       # 26
ROWS = 15
OFFSET = 27                                 # the row numbers' columns, left and right
TRACK_W = (800 - 2 * OFFSET) // 4           # 186
CURSOR_ROW = 7

# patternTrackerColors, themeDefault
C_NAMES, C_BG, C_NOTE, C_INSTR, C_FX1, C_FX2 = "#000000", GREY_08, "#00E8BE", "#FEF749", "#BB58F1", "#57F1FF"
C_NOTE_OFF, C_INSTR_OFF, C_FX1_OFF, C_FX2_OFF = "#003933", "#453B1B", "#321F3A", "#183744"
C_DIVIDER, C_INACTIVE, C_SELTEXT, C_LINES, C_STEPNUM = "#0E0E0E", "#171717", WHITE, "#000000", GREY_57
C_TRACK = GREY_57
C_TRACKEND = "#FEF749"


def dim(color: str, background: str = GREY_08) -> str:
    """dimFx1Color / dimFx2Color: halfway between the empty FX colour and the background"""
    a, b = int(color[1:], 16), int(background[1:], 16)
    return f"#{((a & 0xFEFEFE) >> 1) + ((b & 0xFEFEFE) >> 1):06X}"


COLUMN_X = (8, 52, 86, 140)                 # fourParamsOffset
SEL_OFFSET = (1, 44, 78, 132)               # fourParamsSelOffset
SEL_WIDTH = (40, 30, 50, 50)                # fourParamsSelWidth
NORMAL = (C_NOTE, C_INSTR, C_FX1, C_FX2)
EMPTY = (C_NOTE_OFF, C_INSTR_OFF, C_FX1_OFF, C_FX2_OFF)
DIMMED = (None, None, dim(C_FX1_OFF), dim(C_FX2_OFF))


@dataclass
class Step:
    note: str = ""          # "C4", or "" for an empty step
    instr: int | str = 0    # 1-based sample instrument, or the text: "S1" a synth, "M01" MIDI
    m: int | None = None    # micromove, None: empty
    v: int | None = None    # volume, None: empty
    fx1: str = ""           # the FX columns' texts in the FX view, "" for ----
    fx2: str = ""


@dataclass
class Pattern:
    number: int = 1
    length: int = 16
    tracks: list[dict[int, Step]] = field(default_factory=lambda: [{} for _ in range(4)])
    names: list[str] = field(default_factory=lambda: [f"Track {i + 1}" for i in range(4)])


def cells(step: Step | None, mic_vol: bool) -> list[str]:
    """A row's four texts, as refreshPattern builds them"""
    if step is None or not step.note:
        note, instr = "---", "--"
    else:
        note = step.note
        instr = step.instr if isinstance(step.instr, str) else f"{step.instr:02d}"
    if mic_vol:
        m = "m---" if step is None or step.m is None else f"m{step.m:3d}"
        v = "V---" if step is None or step.v is None else f"V{step.v:3d}"
        return [note, instr, m, v]
    fx1 = (step.fx1 if step else "") or "----"
    fx2 = (step.fx2 if step else "") or "----"
    return [note, instr, fx1, fx2]


def cell_color(text: str, column: int) -> str:
    if text.startswith("-"):
        return EMPTY[column]
    if column >= 2 and text[1:3] == "--":
        return DIMMED[column]
    return NORMAL[column]


def pattern_screen(title: str, pattern: Pattern, cursor_step: int, cursor_track: int, column: int,
                   mic_vol: bool = False, selection: tuple[int, int, int, int] | None = None,
                   right_label: str = "", right_kind: str = "lbl", track_lengths: dict[int, int] | None = None,
                   names_kind: str = "lbl") -> Screen:
    """cursor_step: 0-based step on the cursor's row (row 7); column: 0 note .. 3 FX 2;
    selection: (first track, first step, last track, last step); track_lengths: Track Length's
    own lengths, for tracks shorter than the pattern; names_kind: "demo" for a project's own track names"""
    s = Screen(title)
    s.title_bar(f"Pattern {pattern.number}", right_label, right_kind=right_kind)
    s.song_timer(pattern=pattern.number, step=cursor_step + 1)
    first = cursor_step - CURSOR_ROW
    lengths = track_lengths or {}

    # background, the divider's rows, the bottom shadow
    s.rect(1, POS_Y, 797, HEIGHT - 2, C_BG)
    for j in range(ROWS):
        step = first + j
        if 0 <= step < pattern.length and step % 4 == 0:
            s.rect(1, POS_Y + j * STEP_H + 1, 797, 25, C_DIVIDER)
    s.parts.append(f'<rect x="1" y="413" width="798" height="10" fill="url(#shadow)"/>')

    # the vertical lines
    for x in (OFFSET, 799 - OFFSET) + tuple(OFFSET + TRACK_W * i for i in range(4)):
        s.line(x + 0.5, POS_Y, x + 0.5, POS_Y + STEP_H * 15, C_LINES)

    # the selection's frame and the selected column under it
    sel = None
    if selection:
        t1, s1, t2, s2 = selection
        x1 = OFFSET + t1 * TRACK_W + 1
        x2 = OFFSET + t2 * TRACK_W + TRACK_W - 2
        y1 = POS_Y + (s1 - first) * STEP_H
        y2 = POS_Y + (s2 - first) * STEP_H + STEP_H
        sel = (x1, y1, x2, y2)
        for t in range(t1, t2 + 1):
            sx = OFFSET + t * TRACK_W + 1 + SEL_OFFSET[column]
            s.rect(sx, y1 + 1, SEL_WIDTH[column], y2 - y1 - 1, RED)
    # the cursor's cell
    cx = OFFSET + cursor_track * TRACK_W + 2 + SEL_OFFSET[column]
    cy = POS_Y + STEP_H * CURSOR_ROW
    s.rect(cx, cy + 1, SEL_WIDTH[column] - 1, STEP_H - 3, RED)

    # row numbers, left and right
    for j in range(ROWS):
        step = first + j
        if 0 <= step < pattern.length:
            for x in (3, 799 - 23):
                s.text(x, POS_Y + j * STEP_H + 12, str(step + 1), 1, C_STEPNUM, kind="val")

    # the cells
    for t in range(4):
        own = lengths.get(t, pattern.length)
        for j in range(ROWS):
            step = first + j
            if not 0 <= step < pattern.length:
                continue
            past = step >= own
            data = pattern.tracks[t].get(step % own if past else step)
            texts = cells(data, mic_vol)
            for c, text in enumerate(texts):
                x = OFFSET + COLUMN_X[c] + t * TRACK_W
                y = POS_Y + 12 + j * STEP_H
                in_sel = sel is not None and c == column and sel[0] < x < sel[2] and sel[1] < y < sel[3]
                if in_sel or (j == CURSOR_ROW and t == cursor_track and c == column):
                    color = C_SELTEXT
                elif past:
                    if text.startswith("-") or (c >= 2 and text[1:3] == "--"):
                        continue        # Track Length: blank where it's empty
                    color = C_INACTIVE
                else:
                    color = cell_color(text, c)
                s.text(x, y, text, 2, color, kind="val")

    if sel:
        x1, y1, x2, y2 = sel
        s.outline(x1, y1, x2 - x1, y2 - y1, RED, 1.5)

    # track names over the first row: a grey box, the name in black
    for t in range(4):
        name = pattern.names[t]
        w = len(name) * 17 * 0.6
        x = OFFSET + t * TRACK_W + 1
        s.rect(x, POS_Y, w + 6, 27, C_TRACK)
        s.text(x + 3, POS_Y + 12, name, 4, C_NAMES, kind=names_kind)
    return s


def page1(s: Screen, pattern: Pattern, step_jump: int = 1) -> None:
    """Page 1 of the bottom bar in edit mode (showActionButtonPage1Labels, showPattern, showLength,
    showStep)"""
    s.bottom_bar(255)
    s.button(0, "Pattern", str(pattern.number), 1, kind2="val")
    s.button(1, "Length", str(pattern.length), 1, kind2="val")
    s.button(2, "Step jump", str(step_jump), 1, kind2="val")
    for i, text in ((3, "Fill"), (4, "Preview"), (5, "Undo"), (6, "Redo")):
        s.button(i, text)
    for i, color in ((0, GREEN), (1, GREEN), (2, GREEN), (3, RED), (4, GREEN), (5, RED), (6, RED)):
        cx = COL * i + COL / 2
        s.line(cx - 40, 474, cx + 40, 474, color, 0.5)
    s.button(7, "More", "", 1)
    for d in range(3):
        cx = COL * 7 + COL / 2 - 13 + 13 * d
        s.parts.append(f'<circle cx="{cx:g}" cy="463" r="3" fill="{GREY_77 if d == 0 else GREY_22}"/>')


def page3(s: Screen, edit_mode: bool = True, track_length: str = "--") -> dict[str, int]:
    """Page 3 of the bottom bar with the three mods on: Humanize, Copy to New, Track Length.
    Returns each mod button's column."""
    s.bottom_bar(255)
    s.button(0, "Pattern", "Name", 1)
    s.button(1, "Track", "Name", 1)
    lines = [(0, GREEN), (1, GREEN)]
    active = GREY_77 if edit_mode else GREY_33
    s.button(2, "Humanize", color=active)
    s.button(3, "Copy to", "New", 1, color=active, color2=active, kind2="lbl")
    s.button(4, "Trk Len", track_length, 1, color2=GREY_77, kind2="val")
    lines += [(2, RED), (3, RED), (4, GREEN)]
    for i, color in lines:
        cx = COL * i + COL / 2
        s.line(cx - 40, 474, cx + 40, 474, color, 0.5)
    s.button(7, "More", "", 1)
    for d in range(3):
        cx = COL * 7 + COL / 2 - 13 + 13 * d
        s.parts.append(f'<circle cx="{cx:g}" cy="463" r="3" fill="{GREY_77 if d == 2 else GREY_22}"/>')
    return {"humanize": 2, "copypattern": 3, "tracklen": 4}
