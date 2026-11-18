# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""DCO6's mockups for the Synths user page (docs/mods/synths.md): the synth picker and every section of
its patch editor, with the HPF, unison and Vintage controls in use.

Names, lists, defaults and sections are the engine's own (core/mods/synths/psyn/dco6Synth.cpp); values
are formatted as its formatters do. Voice Sleep is CE's row, which the host adds to the Voice section
of an engine packed with idle sleep (synth/source/synths/PsynLoader.cpp). The screens are CE's, drawn
by tools/docs/mockups_synths.py's helpers (the same layout as WAVE's and PHASE's pictures).

    python tools/docs/mockups_dco6.py
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mockups_synths import P, cents, hz, patch_editor, percent, seconds, semis, synth_picker, write  # noqa: E402

RANGES = ["16'", "8'", "4'"]
PWM_SRCS = ["LFO", "Env"]
VCAS = ["Env", "Gate"]
SHAPES = ["Triangle", "Sine", "Square", "Ramp Up", "Ramp Down", "Random S&H", "Random Smooth"]
OFF_ON = ["Off", "On"]
TRIGS = ["Note", "Free"]
UNISONS = ["Off", "2", "3", "4", "5", "6", "7", "8"]


def lfo_rate(v: float) -> str:
    """dco6Synth.cpp lfoHz with Sync Off: 0.05 Hz x 2^(v x 9.643856)"""
    return hz(0.05 * 2 ** (v * 9.643856))


def hpf(v: float) -> str:
    """dco6Synth.cpp HPF formatter: Off at the minimum (20 Hz), else Hz"""
    return "Off" if v <= 20.5 else hz(v)


def sections(unison=0, detune=0.0, width=0.5, vintage=0.0, hpf_hz=20.0, boost=0):
    return [  # metadata.layoutFlat: (name, shortName, params)
        ("Oscillator", "DCO", [
            P.pick("Range", RANGES, 1), P.num("Saw", percent(1, False), 1), P.num("Pulse", percent(0, False), 0),
            P.num("Sub", percent(0, False), 0), P.num("Noise", percent(0, False), 0),
            P.num("Pulse Width", percent(0, False), 0), P.num("PWM Amt", percent(0, False), 0),
            P.pick("PWM Source", PWM_SRCS, 0), P.num("LFO to Pitch", percent(0, False), 0),
            P.num("Tune", semis(0), 0, -12, 12), P.num("Fine Tune", cents(0), 0, -50, 50)]),
        ("Filter", "Filter", [
            P.num("Cutoff", hz(2400), 2400, 20, 20000), P.num("Resonance", percent(0.15, False), 0.15),
            P.num("Env Amt", percent(0.35, True), 0.35, -1, 1), P.num("LFO to Cutoff", percent(0, False), 0),
            P.num("Key Track", percent(0.5, False), 0.5), P.num("HPF", hpf(hpf_hz), hpf_hz, 20, 2000),
            P.pick("Bass Boost", OFF_ON, boost)]),
        ("Envelope", "Env", [
            P.num("Attack", seconds(0.005), 0.005, 0.001, 3), P.num("Decay", seconds(0.6), 0.6, 0.001, 12),
            P.num("Sustain", percent(0.6, False), 0.6), P.num("Release", seconds(0.35), 0.35, 0.001, 12),
            P.pick("VCA Mode", VCAS, 0)]),
        ("LFO", "LFO", [
            P.pick("LFO Wave", SHAPES, 0), P.num("LFO Rate", lfo_rate(0.693), 0.693),
            P.pick("LFO Sync", OFF_ON, 0), P.num("LFO Delay", seconds(0), 0, 0, 5), P.pick("LFO Trigger", TRIGS, 0)]),
        ("Voice", "Voice", [
            P.pick("Unison", UNISONS, unison), P.num("Detune", cents(detune), detune, 0, 50),
            P.num("Width", percent(width, False), width), P.num("Vintage", percent(vintage, False), vintage),
            P.num("Glide", seconds(0), 0, 0, 10), P.num("Volume", percent(1, False), 1),
            P.pick("Voice Sleep", OFF_ON, 1)]),
    ]


MODELS = ["ACD", "FAT", "VAP", "WTFM", "PERC", "DIRT", "CHIP", "WAVE", "PHASE", "DCO6"]


def main() -> None:
    write("synths-dco6-picker.svg", synth_picker(9, MODELS, (7, 8, 9)),
          "The Synth Parameters screen with DCO6 picked; WAVE, PHASE and DCO6 are marked")
    init = sections()
    for i, (f, sel) in enumerate((("dco", 0), ("filter", 5), ("env", 4), ("lfo", 0), ("voice", 0))):
        write(f"synths-dco6-{f}.svg", patch_editor("DCO6", init, i, sel),
              f"DCO6's {init[i][0]} section in the patch editor, Init patch")
    write("synths-dco6-hpf.svg", patch_editor("DCO6", sections(hpf_hz=240.0, boost=1), 1, 5),
          "DCO6's Filter section, HPF selected at 240 Hz, Bass Boost On")
    stack = sections(unison=3, detune=12.0, width=0.5)
    write("synths-dco6-unison.svg", patch_editor("DCO6", stack, 4, 0),
          "DCO6's Voice section, Unison 4 picked, Detune 12 ct, Width 50 %")
    write("synths-dco6-detune.svg", patch_editor("DCO6", stack, 4, 1),
          "DCO6's Voice section, Detune selected at 12 ct with Unison 4")
    write("synths-dco6-vintage.svg", patch_editor("DCO6", sections(vintage=0.25), 4, 3),
          "DCO6's Voice section, Vintage selected at 25 %")


if __name__ == "__main__":
    main()
