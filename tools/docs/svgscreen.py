# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Tracker+ screen as SVG, for the mods' user pages (docs/mods/).

Each primitive copies the geometry of the CE control that draws it on the unit (core/libs/NewDisplay,
core/interface/core), at the screen's 800 x 480, in the default theme's colours (core/source/Themes.cpp,
themeDefault). Text is drawn in Roboto Mono, the default ("Original") font; the unit's fonts are bitmaps
of it, so widths here are close, not exact. Bitmaps (icons, arrows) are drawn as simple shapes.

Every text element carries a class:
  lbl  a string the firmware has as a literal (tools/docs/check_mockups.py looks for it in the source)
  val  a value the firmware formats (numbers, times, names built at run time): not looked up
  demo the user's content (a project's names): not looked up
"""
from __future__ import annotations

from xml.sax.saxutils import escape

W, H = 800, 480
COL = 800 // 8

# themeDefault
BLACK = "#000000"
WHITE = "#FFFFFF"
RED = "#CD1818"          # one_true_red
GREEN = "#30CB3F"        # one_true_green
YELLOW = "#FEF749"       # instrYellow
GREY_08, GREY_0A, GREY_12, GREY_22 = "#080808", "#0A0A0A", "#121212", "#222222"
GREY_32, GREY_33, GREY_3F, GREY_44 = "#323132", "#333333", "#3F3F3F", "#444344"
GREY_57, GREY_77, GREY_90 = "#575757", "#777777", "#909090"

# what the mod adds or changes: drawn over the screen, not part of it
MARK = "#2F9BFF"

FONTS = {   # CE's fonts 1-5 (displayStructs.h), as fontsData1 (Roboto Mono) has them
    1: (14, 300),   # 14 Regular -> Roboto Mono Light 14
    2: (17, 300),   # 17 Regular -> Roboto Mono Light 17
    3: (14, 400),   # 14 Bold    -> Roboto Mono Regular 14
    4: (17, 700),   # 17 Bold    -> Roboto Mono Bold 17
    5: (12, 400),   # 12 Regular -> Roboto Mono Regular 12
}


def text_width(text: str, font: int) -> float:
    return len(text) * FONTS[font][0] * 0.6


class Screen:
    def __init__(self, title: str):
        self.title = title
        self.parts: list[str] = []
        self.marks: list[str] = []
        self.rect(0, 0, W, H, BLACK)

    # -- raw shapes ---------------------------------------------------------------------------
    def rect(self, x, y, w, h, fill, extra: str = "") -> None:
        self.parts.append(f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" fill="{fill}"{extra}/>')

    def outline(self, x, y, w, h, color, width: float = 1.0) -> None:
        self.parts.append(f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" fill="none" '
                          f'stroke="{color}" stroke-width="{width:g}"/>')

    def line(self, x1, y1, x2, y2, color, width: float = 1.0) -> None:
        self.parts.append(f'<line x1="{x1:g}" y1="{y1:g}" x2="{x2:g}" y2="{y2:g}" stroke="{color}" '
                          f'stroke-width="{width:g}"/>')

    def poly(self, points, fill) -> None:
        pts = " ".join(f"{x:g},{y:g}" for x, y in points)
        self.parts.append(f'<polygon points="{pts}" fill="{fill}"/>')

    def text(self, x, y, s: str, font: int = 1, color: str = WHITE, anchor: str = "start",
             kind: str = "lbl", middle: bool = True) -> None:
        """y is the text's centre (FT812's OPT_CENTERY) when middle, else its top."""
        if not s:
            return
        size, weight = FONTS[font]
        if not middle:
            y = y + size * 0.6
        self.parts.append(f'<text class="{kind}" x="{x:g}" y="{y:g}" font-size="{size}" font-weight="{weight}" '
                          f'fill="{color}" text-anchor="{anchor}" dominant-baseline="central" xml:space="preserve">{escape(s)}</text>')

    def vtext(self, x, y, s: str, font: int = 5, color: str = WHITE, kind: str = "lbl") -> None:
        """Text turned 270 degrees, reading upwards from (x, y): x is its centre line, y where it starts"""
        if not s:
            return
        size, weight = FONTS[font]
        self.parts.append(f'<text class="{kind}" x="{x:g}" y="{y:g}" font-size="{size}" font-weight="{weight}" '
                          f'fill="{color}" text-anchor="start" dominant-baseline="central" '
                          f'transform="rotate(-90 {x:g} {y:g})" xml:space="preserve">{escape(s)}</text>')

    # -- what the mod adds --------------------------------------------------------------------
    def mark(self, x, y, w, h, pad: float = 2) -> None:
        self.marks.append(f'<rect x="{x - pad:g}" y="{y - pad:g}" width="{w + 2 * pad:g}" height="{h + 2 * pad:g}" '
                          f'rx="3" fill="none" stroke="{MARK}" stroke-width="2.5" stroke-dasharray="7 4"/>')

    # -- CE's controls ------------------------------------------------------------------------
    def title_bar(self, title: str, right: str = "", right_kind: str = "val", title_kind: str = "lbl") -> None:
        """TitleBar (core/interface/core/titleBar.cpp): the bar, its line at y 28, two labels"""
        self.rect(0, 0, W, 28, GREY_08)
        self.line(0, 28.5, W, 28.5, WHITE)
        self.text(9, 13, title, 4, WHITE, kind=title_kind)
        if right:
            self.text(790, 13, right, 4, WHITE, anchor="end", kind=right_kind)

    def song_timer(self, minutes: int = 0, seconds: int = 0, pattern: int = 1, step: int = 0,
                   state: str = "stop") -> None:
        """cSongTimer at x 280: "%3im:%02is %2s P%i.%03i"; the state is two glyphs of the 17 Bold font"""
        x = 280
        lead = f"{minutes:3d}m:{seconds:02d}s "
        self.text(x, 13, lead, 4, WHITE, kind="val")
        gx = x + text_width(lead, 4)
        self.state_icon(gx, 13, state)
        self.text(gx + text_width("  ", 4) + 6, 13, f"P{pattern}.{step:03d}", 4, WHITE, kind="val")

    def state_icon(self, x, y, state: str) -> None:
        if state == "stop":
            self.rect(x + 4, y - 6, 12, 12, WHITE)
        elif state == "play":
            self.poly([(x + 4, y - 7), (x + 17, y), (x + 4, y + 7)], WHITE)

    def bottom_bar(self, separators: int = 255) -> None:
        """cBgLabel at y 425, h 54: the background and a line at each column set in the mask"""
        self.rect(1, 425, W - 3, 54, GREY_0A)
        for i in range(1, 8):
            if separators & (1 << i):
                self.line(COL * i + 0.5, 425 + 54 / 4, COL * i + 0.5, 479, GREY_77)

    def button(self, i: int, text: str = "", text2: str = "", lines: int = 0, x: float | None = None,
               color: str = GREY_77, color2: str = GREY_77, font2: int = 3, kind: str = "lbl",
               kind2: str = "lbl") -> None:
        """A bottom label (cLabel at y 424, h 55, centred on its column; value 1 adds text2 20 px lower)"""
        cx = COL * i + COL / 2 if x is None else x
        if lines == 0:
            self.text(cx, 424 + 27, text, 3, color, "middle", kind)
        else:
            self.text(cx, 424 + 27 - 10, text, 3, color, "middle", kind)
            self.text(cx, 424 + 27 + 10, text2, font2, color2, "middle", kind2)

    def arrow(self, cx: float, y: float, up: bool, color: str = GREY_77) -> None:
        """displayArrowU / displayArrowD, drawn as a small triangle"""
        if up:
            self.poly([(cx - 7, y + 10), (cx + 7, y + 10), (cx, y + 2)], color)
        else:
            self.poly([(cx - 7, y + 2), (cx + 7, y + 2), (cx, y + 10)], color)

    def cover(self, x, y, w, h, color: str = GREY_0A) -> None:
        """A label with controlStyleBackground and the bottom shadow (blankSpaceCover and the like)"""
        self.rect(x, y, w, h, color)
        self.parts.append(f'<rect x="{x:g}" y="{y + h - 10:g}" width="{w:g}" height="12" fill="url(#shadow)"/>')

    def frame(self, x, y, w, h) -> None:
        """cFrame: a red line, kRedFrameWidth 24 (1.5 px)"""
        self.outline(x, y, w, h, RED, 1.5)

    def popup(self, lines: list[str], kind: str = "lbl") -> None:
        """mtPopups.show (interfacePopups.cpp, textPopup.cpp): 400 x 80 at the centre, grey 22, a
        shadow under it, the lines in white, font 1, spread over its height"""
        widest = max(text_width(t, 1) for t in lines) + 20
        w = max(400, widest)
        x, y, h = 400 - w / 2, 240 - 40, 80
        self.parts.append(f'<rect x="{x:g}" y="{y + h + 1:g}" width="{w:g}" height="10" fill="url(#shadow)"/>')
        self.rect(x, y, w, h, GREY_22)
        for i, t in enumerate(lines):
            self.text(400, y + h * (i + 1) / (len(lines) + 1), t, 1, WHITE, "middle", kind)

    def bar(self, x, y, w, h, value: int | None, centre: bool = False) -> None:
        """cBar (bar.cpp) with its background, the container and the fill: 0-100, or -100-100 from the middle"""
        self.cover(x, y, w, h, GREY_0A)
        self.rect(x + 10, y + 11, w - 20, h - 25, GREY_08)
        self.parts.append(f'<rect x="{x + 10:g}" y="{y + 11:g}" width="8" height="{h - 25:g}" fill="url(#barShade)"/>')
        if value is None:
            return
        bw, bx, by, bh = w - 20, x + 10, y + 10, h - 25
        if centre:
            half = bh // 2
            top = by + 1 + (half * (100 - value) // 100 if value > 0 else half)
            bot = by + bh - (half * (100 + value) // 100 if value < 0 else half)
            if bot > top:
                self.rect(bx + 1, top, bw - 2, bot - top, WHITE)
        else:
            fill_y = bh - (bh * value) // 100
            self.rect(bx + 1, by + fill_y, bw - 2, bh - fill_y, WHITE)

    def new_list(self, x, y, w, h, rows: list[str], values: list[str] | None = None, selected: int = 0,
                 lines: int = 14, start: int = 0, font: int = 1, row_h: int = 27, kind: str = "lbl",
                 value_kind: str = "val", colors=None, bar: bool = True, row_colors=None) -> list[tuple]:
        """NewList (newList.cpp): the background, the selection bar (an outline), rows of 27 px from y + 4,
        names at x + 13, values right-aligned, a scroll bar when the list is longer than its lines.
        Returns each drawn row's box (x, y, w, h) for marking."""
        bg, frame_c, text_c = (GREY_0A, WHITE, WHITE) if colors is None else colors
        self.cover(x, y, w, h, bg)
        scroll = len(rows) > lines
        end_x = x + w - (3 * 2 + 3 if scroll else 3)
        top = y + 4
        visible = rows[start:start + lines]
        if bar and 0 <= selected - start < lines:
            by = top + (selected - start) * row_h
            self.outline(x + 3, by, end_x - x - 3, row_h, frame_c, 1)
        # the scissor: from x + 13 to the values' right edge
        clip = f"clip{len(self.parts)}"
        self.parts.append(f'<clipPath id="{clip}"><rect x="{x + 13:g}" y="{top:g}" '
                          f'width="{end_x - 7 - x - 13:g}" height="{row_h * lines:g}"/></clipPath>')
        self.parts.append(f'<g clip-path="url(#{clip})">')
        boxes = []
        for r, name in enumerate(visible):
            cy = top + r * row_h + row_h / 2
            color = text_c if row_colors is None else row_colors[start + r]
            self.text(x + 13, cy, name, font, color, kind=kind)
            if values is not None:
                self.text(end_x - 7, cy, values[start + r], font, color, "end", value_kind)
            boxes.append((x + 3, top + r * row_h, end_x - x - 3, row_h))
        self.parts.append("</g>")
        if scroll:
            sx = x + w - 3 - 3
            sy, sh = y + 8, h - 14
            self.rect(sx, sy, 3, sh, BLACK)
            th = sh * lines // len(rows)
            ty = sy + 2 + (sh - th) * (start / len(rows))
            self.rect(sx + 0.5, ty, 2, th, GREY_57)
        return boxes

    # -- out ----------------------------------------------------------------------------------
    def svg(self, desc: str = "") -> str:
        defs = ('<defs><linearGradient id="shadow" x1="0" y1="0" x2="0" y2="1">'
                f'<stop offset="0" stop-color="{GREY_0A}"/><stop offset="1" stop-color="{BLACK}"/></linearGradient>'
                '<linearGradient id="barShade" x1="0" y1="0" x2="1" y2="0">'
                f'<stop offset="0" stop-color="{BLACK}"/><stop offset="1" stop-color="{GREY_08}"/></linearGradient>'
                '</defs>')
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
                "font-family=\"'Roboto Mono', 'DejaVu Sans Mono', Consolas, monospace\">")
        title = f"<title>{escape(self.title)}</title>"
        d = f"<desc>{escape(desc)}</desc>" if desc else ""
        body = "\n".join(self.parts + self.marks)
        return f"{head}\n{title}{d}\n{defs}\n{body}\n</svg>\n"
