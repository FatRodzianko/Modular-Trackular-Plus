# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Color Song mod's mockup (docs/mods/colorsong.md): a song's Song screen with its blocks coloured.

The colours are worked out as core/mods/colorsong.cpp does for a song seen the first time:
handOutColors gives each new instrument set (a family) the next hue slot and each new content the
family's next shade, in song order (positions, then tracks); blockColor turns them into HSL and keeps
the lightness 7 above the theme's empty block. The example's tracks are named by what they hold; the
mod only sees which blocks have the same instruments and the same content.

    python tools/docs/mockups_colorsong.py
"""
from __future__ import annotations

from mockups_synths import write
from songscreen import BLOCK_W, ROWS, block_x, block_y, song_screen

HUE_SLOTS, SHADES = 20, 6
SHADE_SAT = (78, 92, 70, 42, 88, 62)
SHADE_LIGHT = (50, 70, 34, 58, 80, 26)
THEME_DISTANCE = 7
EMPTY = 0x121212                       # songPlayerColors[8], themeDefault

# Each pattern's tracks: (instruments, content). Same instruments: same family (hue); same content
# too: the same colour, in any column.
PATTERNS = {
    1: {0: ("kick", "A"), 2: ("hats", "A"), 5: ("pad", "A")},                                     # intro
    2: {0: ("kick", "A"), 1: ("snare", "A"), 2: ("hats", "A"), 3: ("bass", "A"), 5: ("pad", "A")},  # verse
    3: {0: ("kick", "B"), 1: ("snare", "A"), 2: ("hats", "B"), 3: ("bass", "B"), 4: ("lead", "A"),
        5: ("pad", "B"), 6: ("chords", "A")},                                                       # chorus
    4: {3: ("bass", "C"), 5: ("pad", "A"), 6: ("chords", "B"), 7: ("pad", "A")},                    # break
    5: {0: ("kick", "A"), 5: ("pad", "C")},                                                         # outro
}
PLAYLIST = [1, 2, 2, 3, 2, 3, 4, 3, 3, 5]


def hsl_to_rgb(h: int, s: int, l: int) -> int:
    """hslToRgb, in the same integers"""
    c = (100 - (2 * l - 100 if 2 * l > 100 else 100 - 2 * l)) * s
    hp = h % 120
    d = hp - 60 if hp >= 60 else 60 - hp
    x = c * (60 - d) // 60
    m = l * 100 - c // 2
    r, g, b = [(c, x, 0), (x, c, 0), (0, c, x), (0, x, c), (x, 0, c)][h // 60] if h // 60 < 5 else (c, 0, x)
    r, g, b = (((v + m) * 255 + 5000) // 10000 for v in (r, g, b))
    return r << 16 | g << 8 | b


def lightness_of(rgb: int) -> int:
    parts = [(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255]
    return (max(parts) + min(parts)) * 100 // 510


def hand_out() -> tuple[dict[str, int], dict[tuple[str, str], int]]:
    """handOutColors for a song seen the first time: hue slots and shades in order of appearance"""
    slots: dict[str, int] = {}
    shades: dict[tuple[str, str], int] = {}
    next_shade: dict[str, int] = {}
    for pattern in PLAYLIST:
        for t in sorted(PATTERNS[pattern]):
            key = PATTERNS[pattern][t]
            family = key[0]
            if family not in slots:
                slots[family] = len(slots) % HUE_SLOTS
            if key not in shades:
                shades[key] = next_shade.get(family, 0) % SHADES
                next_shade[family] = shades[key] + 1
    return slots, shades


def block_color(slot: int, shade: int, muted: bool = False) -> str:
    hue = slot % HUE_SLOTS * 1375 // 10 % 360
    sat, light = SHADE_SAT[shade], SHADE_LIGHT[shade]
    if muted:
        sat //= 2
        light = light * 45 // 100
    empty = lightness_of(EMPTY)
    light = max(light, empty + THEME_DISTANCE) if empty <= 50 else min(light, empty - THEME_DISTANCE)
    return f"#{hsl_to_rgb(hue, sat, light):06X}"


def main() -> None:
    slots, shades = hand_out()
    masks = {p: sum(1 << t for t in tracks) for p, tracks in PATTERNS.items()}

    def fill(row: int, track: int) -> str:
        key = PATTERNS[PLAYLIST[row]][track]
        return block_color(slots[key[0]], shades[key])

    s = song_screen("Color Song: a song's blocks", PLAYLIST, masks, fill, selected=(1, 3))
    rows = min(len(PLAYLIST), ROWS)
    s.mark(block_x(0), block_y(0), block_x(15) + BLOCK_W - block_x(0), block_y(rows - 1) + 23 - block_y(0))
    write("colorsong-song.svg", s, "The Song screen with Color Song on: blocks with the same instruments share "
          "a hue, and the same content has the same colour")
    for key, shade in shades.items():
        print(f"  {key[0]:6} {key[1]}: slot {slots[key[0]]}, shade {shade}, {block_color(slots[key[0]], shade)}")


if __name__ == "__main__":
    main()
