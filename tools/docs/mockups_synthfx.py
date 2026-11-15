# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Synth FX mod's mockups (docs/mods/synthfx.md): the patch editor's Synth FX button, the slots'
plugin picker and a plugin's knobs.

The rows are Synth FX's (core/mods/synthfx/synthfxPage.cpp: "FX1", the plugin's knobs, "FX2", ...),
shown in CE's patch editor (core/interface/instrumentEditor/SynthPatchEditor.cpp, mtFxShowList and
mtFxShowValue). The plugins are the default pack (synthfx-sdk/pack/), in the catalog's order (the pack
first, synthfxPlugins.cpp scan), with their knobs' labels, defaults and texts.

    python tools/docs/mockups_synthfx.py
"""
from __future__ import annotations

from mockups_synths import COL, WAVE, P, patch_editor, write

PACK = ["Overdrive", "Distortion", "Bit Crusher", "Chorus", "Flanger", "Phaser"]


def picker_rows(fx1: str, fx2: str) -> list[P]:
    """A slot's choices: None, then every usable plugin but the other slot's (synthfxPage.cpp choices)"""
    rows = []
    for slot, own, other in (("FX1", fx1, fx2), ("FX2", fx2, fx1)):
        items = ["None"] + [n for n in PACK if n != other]
        rows.append(P.pick(slot, items, items.index(own)))
        rows += KNOBS.get(own, [])
    return rows


# The knobs at their defaults, as the plugins' format() prints them
KNOBS = {
    # overdrive.cpp: Gain "+%.0f dB" of 6.0206 x (1 + 0.5 x 5.882643) = 23.7; Tone "%.0f %%"; Level "%+.1f dB"
    "Overdrive": [P.num("Gain", "+24 dB", 50, 0, 100), P.num("Tone", "50 %", 50, 0, 100),
                  P.num("Level", "+0.0 dB", 30, 0, 36)],
    # chorus.cpp: Rate as hz_text(rate_hz(0.5)) = 0.05 x 2^(0.5 x log2 100) = 0.50 Hz; Depth, Mix "%.0f %%"
    "Chorus": [P.num("Rate", "0.50 Hz", 50, 0, 100), P.num("Depth", "50 %", 50, 0, 100),
               P.num("Mix", "50 %", 50, 0, 100)],
}


def main() -> None:
    # 1. the button, on one of WAVE's sections
    s = patch_editor("WAVE", WAVE, 0, 0, title="Patch editor: the Synth FX button", mark_synth=False)
    s.mark(COL * 3 + 3, 428, COL - 6, 48)
    write("synthfx-button.svg", s, "The patch editor on WAVE's Osc section; the Synth FX button is marked")

    # 2. the rows of a part with no plugins, FX1 selected: its choices in column 8
    rows = picker_rows("None", "None")
    s = patch_editor("WAVE", WAVE, 0, 0, rows=rows, list_name="Synth FX", title="Synth FX: FX1's plugins",
                     mark_synth=False)
    synthfx_marks(s)
    write("synthfx-picker.svg", s, "Synth FX's rows, FX1 selected: None and the six plugins of the pack")

    # 3. FX1 Overdrive, FX2 Chorus, Overdrive's Gain selected
    rows = picker_rows("Overdrive", "Chorus")
    s = patch_editor("WAVE", WAVE, 0, 1, rows=rows, list_name="Synth FX", title="Synth FX: a plugin's knobs",
                     mark_synth=False)
    synthfx_marks(s)
    write("synthfx-knobs.svg", s, "Synth FX's rows: FX1 Overdrive and its knobs, FX2 Chorus and its knobs")

    # 4. FX2 selected: its choices leave out FX1's Overdrive
    s = patch_editor("WAVE", WAVE, 0, 4, rows=rows, list_name="Synth FX", title="Synth FX: FX2's plugins",
                     mark_synth=False)
    synthfx_marks(s)
    write("synthfx-fx2.svg", s, "Synth FX's rows, FX2 selected: its choices without FX1's Overdrive")


def synthfx_marks(s) -> None:
    s.mark(COL * 4 + 2, 30, COL * 4 - 6, 392)          # the rows and column 8
    s.mark(COL * 3 + 3, 428, COL - 6, 48)              # button 3
    s.mark(COL * 5 - 30, 428, 160, 48)                 # the list's name under it


if __name__ == "__main__":
    main()
