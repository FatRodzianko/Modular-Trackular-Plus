# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Global Swing mod's mockups (docs/mods/globalswing.md): Config -> Project Settings -> Global Swing,
and its Swing amount being edited.

The Config screen is cConfigEditor's (core/interface/configEditor/gConfigEdit.cpp, configMenu.cpp):
the base groups in columns 1-2, the open group's items and values in columns 3-5 with the red frame,
the bottom labels (bgLabel 0b11000100). The item editor covers columns 3-4 with the item's name and
its description, and lists its values in columns 5-6. The group and its items are the mod's
(configMenu.cpp, MT_HAS_SWING).

    python tools/docs/mockups_swing.py
"""
from __future__ import annotations

import textwrap

from mockups_synths import write
from svgscreen import COL, GREY_77, WHITE, Screen

BASE = ["Project Settings", "General", "MIDI", "USB", "Metronome", "Firmware", "Mods", "Info"]
ITEMS = ["Global swing", "Swing amount"]
AMOUNT_DESC = ("25 to 75, as the Swing FX: 50 is no swing, above 50 the off-beat steps come later. "
               "Used when Global swing is On. ")


def config_screen(title: str, values: list[str], selected: int) -> Screen:
    s = Screen(title)
    s.title_bar("Config")
    s.new_list(1, 29, COL * 2 - 3, 394, BASE, selected=0)
    s.new_list(COL * 2 + 1, 29, COL * 4 - 3, 394, ITEMS, values=values, selected=selected)
    s.cover(COL * 6 + 1, 29, COL * 2 - 2, 394)             # blankSpaceCover[0]
    s.frame(COL * 2 + 1, 31, COL * 4 - 3, 391)
    labels(s, "Back", "Enter")
    return s


def labels(s: Screen, l6: str, l7: str) -> None:
    s.bottom_bar(0b11000100)
    s.button(0, "Project Settings", x=COL)
    s.arrow(COL / 2 - 30, 444, up=True)
    s.arrow(COL * 1.5 + 30, 444, up=False)
    s.button(3, "Global Swing", x=COL * 4)
    s.arrow(COL * 2.5, 444, up=True)
    s.arrow(COL * 5.5, 444, up=False)
    s.button(6, l6, color=GREY_77)
    s.button(7, l7, color=GREY_77)


def main() -> None:
    # 1. the group, Swing amount selected
    s = config_screen("Global Swing: Project Settings", ["On", "50"], selected=1)
    s.mark(COL * 2 + 4, 33, COL * 4 - 9, 2 * 27)
    s.mark(COL * 3 + 3, 428, COL * 2 - 6, 48)
    write("globalswing-settings.svg", s, "Config, Project Settings, Global Swing: Global swing On, Swing amount 50")

    # 2. Swing amount open: its description and the values 25 to 75, 58 picked
    s = Screen("Global Swing: Swing amount")
    s.title_bar("Config")
    s.new_list(1, 29, COL * 2 - 3, 394, BASE, selected=0)
    s.cover(COL * 2 + 1, 29, COL * 2 - 2, 394)              # blankSpaceCover[1]
    s.text(COL * 2 + 15, 27 + 18, "Swing amount", 3, WHITE)
    for i, line in enumerate(textwrap.wrap(AMOUNT_DESC, 24)):
        s.text(COL * 2 + 10, 60 + 9 + i * 18, line, 5, WHITE, kind="val")
    values = [str(25 + i) for i in range(51)]
    pick = 58 - 25
    s.new_list(COL * 4 + 1, 29, COL * 2 - 3, 394, values, selected=pick, start=pick - 12, kind="val")
    s.cover(COL * 6 + 1, 29, COL * 2 - 2, 394)
    labels(s, "Cancel", "Apply")
    s.mark(COL * 2 + 4, 32, COL * 2 - 9, 386)
    s.mark(COL * 4 + 4, 32, COL * 2 - 9, 386)
    write("globalswing-amount.svg", s, "Swing amount open: its description, and the values 25 to 75 with 58 picked")


if __name__ == "__main__":
    main()
