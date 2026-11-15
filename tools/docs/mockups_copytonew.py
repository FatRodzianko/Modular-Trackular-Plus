# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Copy to New mod's mockups (docs/mods/copytonew.md): page 3 of the Pattern screen's bottom bar
with the Copy to New button, and CE's popup with the mod's message after a copy
(core/mods/copypattern.cpp: "Copied to pattern %u", shown with mtPopups.show).

    python tools/docs/mockups_copytonew.py
"""
from __future__ import annotations

from mockups_humanize import example
from mockups_synths import write
from patternscreen import page3, pattern_screen
from svgscreen import COL


def screen(title: str):
    s = pattern_screen(title, example(False), cursor_step=8, cursor_track=0, column=0,
                       right_label="1. Kick.wav", right_kind="demo")
    slots = page3(s)
    s.mark(COL * slots["copypattern"] + 3, 428, COL - 6, 48)
    return s


def main() -> None:
    s = screen("Copy to New: the button")
    write("copytonew-button.svg", s, "Pattern 1 in edit mode; page 3 of the bottom bar with the Copy to New button")

    s = screen("Copy to New: copied")
    s.popup(["Copied to pattern 2"])
    s.mark(200, 200, 400, 80)
    write("copytonew-copied.svg", s, "After Copy to New: the popup says Copied to pattern 2, and Pattern 1 is still open")


if __name__ == "__main__":
    main()
