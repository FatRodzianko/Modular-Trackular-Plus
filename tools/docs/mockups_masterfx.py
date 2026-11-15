# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Master FX mod's mockups (docs/mods/masterfx.md): the Master page's three lists, a plugin's
labels on the Reverb page, a plugin's own knobs and the Ducks list on the Limiter page, and the
Bypassed and Missing states.

The screens are CE's (core/interface/masterParams/gMasterParams.cpp: showMasterScreen,
showReverbScreen, showLimiterScreen and the framesPlaces), with Master FX's additions from the same
file (showMasterfxLists, showMasterfxOwnKnobs, showMasterfxDucks). The lists are CE's cList
(core/libs/NewDisplay/list.cpp) in activeListColors: rows of 27 px, a white outline on the choice,
marked rows in red. The lists' rows are masterfx::refreshChoices' (Stock, the pack in its order, then
the card's files), the labels and knobs the plugins' own (masterfx-sdk/pack/pack.cpp, glue.cpp), and
the values CE's defaults as its show functions print them.

    python tools/docs/mockups_masterfx.py
"""
from __future__ import annotations

from pathlib import Path

from svgscreen import COL, GREY_33, GREY_57, GREY_77, RED, WHITE, Screen

OUT = Path(__file__).resolve().parents[2] / "docs" / "mods" / "img"

# masterfx::refreshChoices: Stock, then the slot's plugins of the pack, in masterfxPack.inc's order
REVERBS = ["Stock", "Freeverb", "Plate", "Spring", "Shimmer", '"Gated"', "Reverse"]
DELAYS = ["Stock", "Tape", "BBD"]
LIMITERS = ["Stock", "Glue"]

# The Limiter page's Ducks list (masterfx.cpp kDuckNames) and CE's Sidechain list (masterParams.h
# limiterSidechainListTxt: the tracks' names are the project's, "Track 1" to "Track 8" in a new one)
DUCKS = ["Master"] + [f"Track {n}" for n in range(1, 9)] + ["Synth 1", "Synth 2", "Synth 3"]
SIDECHAIN = ["Disable"] + [f"Track {n}" for n in range(1, 9)] + ["Line in L", "Line in R",
                                                                 "Synth 1", "Synth 2", "Synth 3"]
SIDECHAIN_KINDS = ["lbl"] + ["demo"] * 8 + ["lbl"] * 5

LIST_Y, LIST_H = 29, 394        # the lists and bars of the Master pages
FRAME_Y, FRAME_H = 31, 391      # framesPlaces


def write(name: str, screen: Screen, desc: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(screen.svg(desc), encoding="utf-8", newline="\n")
    print("wrote", OUT / name)


# -- CE's parts of the Master pages -----------------------------------------------------------
def column_x(i: int) -> int:
    return COL * i + 2


def bar(s: Screen, i: int, value) -> None:
    s.bar(column_x(i), LIST_Y, COL - 3, LIST_H, value)


def cover(s: Screen, first: int, columns: int) -> None:
    """blankSpaceCover[0]: the columns with nothing in them"""
    s.cover(column_x(first), LIST_Y, COL * columns - 3, LIST_H)


def clist(s: Screen, i: int, rows: list[str], at: int, kinds=None, marked=()) -> list[tuple]:
    """A cList in column i: the outline on row `at`, marked rows red (its selectTab)"""
    colors = [RED if r in marked else WHITE for r in range(len(rows))]
    if kinds is None:
        return s.new_list(column_x(i), LIST_Y, COL - 3, LIST_H, rows, selected=at, lines=len(rows),
                          row_colors=colors)
    # rows of different kinds: the list drawn empty, then each row's text as its kind
    boxes = s.new_list(column_x(i), LIST_Y, COL - 3, LIST_H, [""] * len(rows), selected=at,
                       lines=len(rows))
    for r, (text, kind) in enumerate(zip(rows, kinds)):
        bx, by, bw, bh = boxes[r]
        s.text(column_x(i) + 13, by + bh / 2, text, 1, colors[r], kind=kind)
    return boxes


def label(s: Screen, i: int, text: str, text2: str = "", kind: str = "lbl", kind2: str = "val",
          dim: bool = False) -> None:
    """A bottom label: value 1 is two lines, the second in font 2 and colors[3]"""
    if text2:
        s.button(i, text, text2, 1, color=GREY_33 if dim else GREY_77, color2=GREY_57 if dim else WHITE,
                 font2=2, kind=kind, kind2=kind2)
    else:
        s.button(i, text, kind=kind)


def frame(s: Screen, place: int) -> None:
    s.frame(COL * place + 1, FRAME_Y, COL - 3, FRAME_H)


def mark_column(s: Screen, i: int, columns: int = 1, label_too: bool = True) -> None:
    s.mark(column_x(i), LIST_Y, COL * columns - 3, LIST_H + (55 if label_too else 0))


# -- the pages ---------------------------------------------------------------------------------
def master_page(title: str, choices=(0, 0, 0), texts=None, lists=None, place: int = 1) -> Screen:
    """showMasterScreen with the mod On: the Reverb, Delay and Limiter lists in columns 1 to 3"""
    s = Screen(title)
    s.title_bar("Master 1/3")
    s.bottom_bar()
    bar(s, 0, 81)                                           # MASTER_VOLUME_DEFAULT
    lists = lists or (REVERBS, DELAYS, LIMITERS)
    for slot in range(3):
        clist(s, 1 + slot, lists[slot], choices[slot])
    cover(s, 4, 1)
    s.new_list(column_x(5), LIST_Y, COL - 3, LIST_H,
               ["Hard Clip", "Soft Clip", "Saturate", "Overdrive", "Drive+"], selected=0, lines=5)
    bar(s, 6, 0)
    bar(s, 7, 0)
    texts = texts or [lists[slot][choices[slot]] for slot in range(3)]
    label(s, 0, "Volume", "0.00 dB")
    for slot, word in enumerate(("Reverb", "Delay", "Limiter")):
        label(s, 1 + slot, word, texts[slot], kind2="lbl")
    label(s, 4, "EQ")
    label(s, 5, "Saturation", "Hard Clip", kind2="lbl")
    label(s, 6, "Bass Boost", "0")
    label(s, 7, "Space", "0")
    frame(s, place)
    return s


def reverb_page(title: str, words, place: int = 0) -> Screen:
    """showReverbScreen: Size, Damping, Predelay, Diffusion (or the plugin's words), Line in, Back"""
    s = Screen(title)
    s.title_bar(words[0], title_kind=words[1])
    s.bottom_bar()
    values = (50, 50, 50, 80)                               # DEFAULT_REVERB_*: 0.5, 0.5, 0.5, 0.68 of 0.85
    for i, v in enumerate(values):
        bar(s, i, v)
        label(s, i, words[2][i], str(v))
    bar(s, 4, 0)                                            # the Line in send, muted in a new project
    label(s, 4, "Line in", "-inf dB", dim=True)
    cover(s, 5, 3)
    label(s, 7, "Back")
    frame(s, place)
    return s


def limiter_page(title: str, title_kind: str, words, column4, own=(), place: int = 5) -> Screen:
    """showLimiterScreen: Attack, Release, Threshold (or the plugin's words), Gain Reduce, column 4
    (CE's Sidechain list or Master FX's Ducks list), the plugin's own knobs in 5 and 6, Back"""
    s = Screen(title)
    s.title_bar(title, title_kind=title_kind)
    s.bottom_bar()
    for i, (v, text) in enumerate(((10, "0.100s"), (0, "0.001s"), (92, "-6.00 dB"))):  # CE's defaults
        bar(s, i, v)
        label(s, i, words[i], text)
    bar(s, 3, None)
    label(s, 3, "Gain Reduce")
    rows, at, kinds, marked, word, text, text_kind = column4
    clist(s, 4, rows, at, kinds, marked)
    label(s, 4, word, text, kind2=text_kind)
    for k, (name, v, text) in enumerate(own):
        bar(s, 5 + k, v)
        label(s, 5 + k, name, text)
    cover(s, 5 + len(own), 3 - len(own))
    label(s, 7, "Back")
    frame(s, place)
    return s


CE_LIMITER = ("LimiterAtt", "LimiterRls", "LimiterThr")
GLUE_KNOBS = (("Ratio", 15, "4.0 :1"), ("Makeup", 0, "0.0 dB"))   # glue.cpp: 4 of 1-20 by 0.5, 0 of 0-12
SIDECHAIN_OFF = (SIDECHAIN, 0, SIDECHAIN_KINDS, (), "Sidechain", "Disable", "lbl")


def main() -> None:
    # 1. the Master page with the mod On: Shimmer, BBD and Glue picked, the Reverb list selected
    s = master_page("Master FX: the Master page", (4, 2, 1))
    mark_column(s, 1, 3)
    write("masterfx-master.svg", s, "The Master page with Master FX On: the Reverb, Delay and Limiter "
                                     "lists, with Shimmer, BBD and Glue picked")

    # 2. the Reverb page with Shimmer: its words for Size, Damping and Diffusion
    s = reverb_page("Master FX: a plugin's words on the Reverb page",
                    ("Reverb: Shimmer", "val", ("Decay", "Tone", "Predelay", "Shimmer")))
    s.mark(0, 0, 800, 28, pad=0)
    for i in (0, 1, 3):
        s.mark(COL * i + 3, 428, COL - 6, 48)
    write("masterfx-reverb.svg", s, "The Reverb page with Shimmer: the title names it, and its words "
                                     "Decay, Tone and Shimmer replace Size, Damping and Diffusion")

    # 3. the Limiter page with Glue: its own knobs in columns 5 and 6, Ratio selected
    s = limiter_page("Limiter: Glue", "val", CE_LIMITER, SIDECHAIN_OFF, GLUE_KNOBS, place=5)
    s.mark(0, 0, 800, 28, pad=0)
    mark_column(s, 5, 2)
    write("masterfx-knobs.svg", s, "The Limiter page with Glue: its own knobs Ratio and Makeup in "
                                    "columns 5 and 6")

    # 4. the Ducks list in column 4 (Shift + its button): Track 3 and Synth 1 marked
    ducks = (DUCKS, 9, None, (3, 9), "Ducks", "2 tracks", "val")
    s = limiter_page("Limiter: Glue", "val", CE_LIMITER, ducks, GLUE_KNOBS, place=4)
    mark_column(s, 4)
    write("masterfx-ducks.svg", s, "The Limiter page's Ducks list: Track 3 and Synth 1 marked, so "
                                    "the key turns those two down instead of the master")

    # 5. the time guard has stopped Glue: the Limiter column says Bypassed
    s = master_page("Master FX: a bypassed plugin", (4, 2, 1), texts=["Shimmer", "BBD", "Bypassed"], place=3)
    s.mark(COL * 3 + 3, 428, COL - 6, 48)
    write("masterfx-bypassed.svg", s, "The Master page with the Limiter slot's plugin stopped by the "
                                       "time guard: its column says Bypassed")

    # 6. the project's Limiter file isn't on the card: Missing in the list and the column
    s = master_page("Master FX: a missing plugin", (4, 2, 2),
                    lists=(REVERBS, DELAYS, LIMITERS + ["Missing"]), place=3)
    mark_column(s, 3)
    write("masterfx-missing.svg", s, "The Master page with the project's Limiter plugin missing from "
                                      "the card: Missing in the list and under the column")


if __name__ == "__main__":
    main()
