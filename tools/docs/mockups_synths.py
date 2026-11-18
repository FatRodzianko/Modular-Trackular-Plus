# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Synths mod's mockups (docs/mods/synths.md): the synth picker and every section of WAVE's and
PHASE's patch editor.

Names, lists, defaults and sections are WAVE's and PHASE's own (core/mods/synths/psyn/waveSynth.cpp,
phaseSynth.cpp, synthsSdk.inc). The values are formatted as their formatters and the SDK bridge's
fmt::format do (synthsSdk.inc, synth-plugin-sdk/include/psyn/psyn_cpp_bridge.h). The screens are CE's
(core/interface/instrumentEditor/SynthEditor.cpp, SynthPatchEditor.cpp, gInstrEdit.cpp).

    python tools/docs/mockups_synths.py
"""
from __future__ import annotations

import math
import struct
from pathlib import Path

from svgscreen import COL, GREY_08, GREY_0A, GREY_32, GREY_77, WHITE, Screen

OUT = Path(__file__).resolve().parents[2] / "docs" / "mods" / "img"


# -- the plugins' value displays --------------------------------------------------------------
def f32(v: float) -> float:
    return struct.unpack("f", struct.pack("f", v))[0]


def bridge_float(val: float, precision: int, plus: bool = False) -> str:
    """psyn_cpp_bridge.h appendFloat: scaled = (uint64)(val * 10^p + 0.5)"""
    sign = "-" if val < 0 else ("+" if plus else "")
    val = abs(val)
    scale = 10 ** precision
    scaled = int(val * scale + 0.5)
    whole, frac = divmod(scaled, scale)
    return f"{sign}{whole}" + (f".{frac:0{precision}d}" if precision else "")


def percent(v: float, signed: bool) -> str:
    v = f32(v)
    pc = int(f32(v * 100.0) + (-0.5 if v < 0 else 0.5))
    return f"+{pc} %" if signed and pc > 0 else f"{pc} %"


def hz(v: float) -> str:
    v = f32(v)
    if v < 10:
        return bridge_float(v, 2) + " Hz"
    if v < 100:
        return bridge_float(v, 1) + " Hz"
    if v < 1000:
        return bridge_float(v, 0) + " Hz"
    return bridge_float(f32(v * f32(0.001)), 1) + " kHz"


def seconds(v: float) -> str:
    v = f32(v)
    return bridge_float(v, 2 if v < 1.0 else 1) + " s"


def semis(v: float) -> str:
    n = int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))
    return f"+{n} st" if n > 0 else f"{n} st"


def cents(v: float) -> str:
    n = int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))
    return f"+{n} ct" if n > 0 else f"{n} ct"


def lfo_rate(v: float) -> str:
    """lfoRatio with Sync Off: 0.01 Hz x 2^(v x 13.287712)"""
    return hz(f32(0.01 * 2 ** (f32(v) * 13.287712)))


class P:
    """One parameter: its name, value, and what column 8 shows for it"""

    def __init__(self, name, value, *, items=None, at=0, lo=0.0, hi=1.0, v=0.0):
        self.name, self.value, self.items, self.at = name, value, items, at
        self.lo, self.hi, self.v = lo, hi, v

    @staticmethod
    def pick(name, items, at):
        return P(name, items[at], items=items, at=at)

    @staticmethod
    def num(name, text, v, lo=0.0, hi=1.0):
        return P(name, text, lo=lo, hi=hi, v=v)


OFF_ON = ["Off", "On"]
LFO_WAVES = ["Triangle", "Sin", "Ramp Up", "Ramp Down", "Square", "Random S&H"]
RETRIG = ["Note", "One Shot", "Free Running"]
POLARITY = ["Bipolar", "Unipolar"]
GLIDE = ["Always", "Overlap", "Legato", "Legato O."]


def lfo_section() -> list[P]:
    """kLfoSection: 30, 27, 28, 29, 60, 34, 31, 32, 33, 61 (sharedSpecs' defaults)"""
    out = []
    for k, (rate, wave) in enumerate(((0.5, 0), (0.349485, 1))):
        n = k + 1
        out += [P.pick(f"LFO {n} Sync", OFF_ON, 0),
                P.num(f"LFO {n} Ratio", lfo_rate(rate), rate),
                P.pick(f"LFO {n} Wave", LFO_WAVES, wave),
                P.pick(f"LFO {n} Retrigger", RETRIG, 2),
                P.pick(f"LFO {n} Polarity", POLARITY, 0)]
    return out


def matrix(srcs, dests, defaults) -> list[P]:
    out = []
    for k in range(6):
        s, d = defaults.get(k, (0, 0))
        out += [P.pick(f"Mod {k + 1} Src", srcs, s), P.pick(f"Mod {k + 1} Dest", dests, d),
                P.num(f"Mod {k + 1} Amt", percent(0, True), 0, -4, 4)]
    return out


def voice() -> list[P]:
    return [P.pick("Glide Mode", GLIDE, 0), P.num("Glide Time", seconds(0), 0, 0, 10),
            P.num("Volume", percent(1, False), 1)]


def envelope(names, defs) -> list[P]:
    a, d, s, r = defs
    return [P.num(names[0], seconds(a), a, 0, 10), P.num(names[1], seconds(d), d, 0.001, 10),
            P.num(names[2], percent(s, False), s), P.num(names[3], seconds(r), r, 0.001, 10)]


# -- WAVE ------------------------------------------------------------------------------------
WAVES = ["Formant", "Resonant", "Harmonic", "Square/PWM", "Sin/Tri/Saw/Sqr", "Sync", "FM", "Super Saw", "Saw"]
TYPES = ["LP Ladder 24", "LP OB 24", "LP OB 12", "LP SVF 24", "LP SVF 12", "HP OB 24", "HP OB 12",
         "HP SVF 24", "HP SVF 12", "BP OB 24", "BP OB 12", "BP SVF 24", "BP SVF 12", "Notch 24", "Notch 12"]
W_SRCS = ["Amp Env", "Filter Env", "Mod Env", "LFO 1", "LFO 2", "Velocity", "Key"]
W_DESTS = ["Position", "Position 1", "Position 2", "Pitch", "Pitch 1", "Pitch 2", "Mix", "Cutoff",
           "Resonance", "Volume", "Noise"]

WAVE = [  # metadata.layoutFlat: (name, shortName, params)
    ("Oscillators", "Osc", [
        P.pick("Wave 1", WAVES, 4), P.num("Position 1", percent(0.6667, False), 0.6667),
        P.pick("Wave 2", WAVES, 4), P.num("Position 2", percent(0.6667, False), 0.6667),
        P.num("Mix", percent(0, True), 0, -1, 1), P.num("Tune 1", semis(0), 0, -36, 36),
        P.num("Tune 2", semis(0), 0, -36, 36), P.num("Finetune", cents(0), 0, -100, 100),
        P.num("Detune", cents(8), 8, 0, 100), P.num("Noise", percent(0, False), 0),
        P.pick("Digital", OFF_ON, 0), P.pick("Reset 1", OFF_ON, 0), P.pick("Reset 2", OFF_ON, 0)]),
    ("Filter", "Filter", [
        P.num("Cutoff", hz(2400), 2400, 20, 20000), P.num("Resonance", percent(0.2, False), 0.2, 0, 1.5),
        P.pick("Type", TYPES, 0), P.num("Env Amt", percent(0.15, True), 0.15, -1, 1),
        P.num("Key Track", percent(0.5, True), 0.5, -2, 2), P.num("Drive", percent(0, False), 0)]),
    ("Amp Envelope", "Amp Env",
     envelope(("Amp Attack", "Amp Decay", "Amp Sustain", "Amp Release"), (0.005, 0.5, 0.8, 0.4))),
    ("Filter Envelope", "Flt Env",
     envelope(("Flt Attack", "Flt Decay", "Flt Sustain", "Flt Release"), (0.005, 0.8, 0.35, 0.5))),
    ("Mod Envelope", "Mod Env",
     envelope(("Mod Attack", "Mod Decay", "Mod Sustain", "Mod Release"), (0.5, 1.0, 0.0, 0.5))),
    ("LFOs", "LFO", lfo_section()),
    ("Modulation Matrix", "Matrix", matrix(W_SRCS, W_DESTS, {0: (3, 0), 1: (2, 0)})),
    ("Voice", "Voice", voice()),
]

# -- PHASE -----------------------------------------------------------------------------------
SHAPES = ["Saw", "Square", "Pulse", "Double Sine", "Saw-Pulse", "Reso I", "Reso II", "Reso III"]
PAIRS = ["Off"] + SHAPES
P_SRCS = ["DCA Env", "DCW Env", "DCO Env", "LFO 1", "LFO 2", "Velocity", "Key"]
P_DESTS = ["Pitch", "Pitch 1", "Pitch 2", "DCW", "DCW 1", "DCW 2", "Mix", "Volume", "Detune"]

PHASE = [
    ("DCO Oscillators", "DCO", [
        P.pick("Wave 1", SHAPES, 0), P.pick("Wave 1+", PAIRS, 0), P.pick("Wave 2", SHAPES, 0),
        P.pick("Wave 2+", PAIRS, 0), P.num("Mix", percent(0, True), 0, -1, 1),
        P.num("Tune 1", semis(0), 0, -36, 36), P.num("Tune 2", semis(0), 0, -36, 36),
        P.num("Finetune", cents(0), 0, -100, 100), P.num("Detune", cents(8), 8, 0, 100),
        P.pick("Reset 1", OFF_ON, 0), P.pick("Reset 2", OFF_ON, 0), P.pick("Ring Mod", OFF_ON, 0),
        P.num("Noise Mod", percent(0, False), 0)]),
    ("DCW Shaper", "DCW", [
        P.num("DCW 1", percent(0.5, False), 0.5), P.num("DCW 2", percent(0.5, False), 0.5),
        P.num("Env Amt 1", percent(0.3, True), 0.3, -1, 1), P.num("Env Amt 2", percent(0.3, True), 0.3, -1, 1),
        P.num("Key Follow", percent(0, True), 0, -1, 1)]),
    ("DCA Envelope", "DCA",
     envelope(("DCA Attack", "DCA Decay", "DCA Sustain", "DCA Release"), (0.005, 0.5, 0.8, 0.4))),
    ("DCW Envelope", "DCW Env",
     envelope(("DCW Attack", "DCW Decay", "DCW Sustain", "DCW Release"), (0.005, 0.6, 0.3, 0.5))),
    ("DCO Envelope", "DCO Env",
     envelope(("DCO Attack", "DCO Decay", "DCO Sustain", "DCO Release"), (0.005, 0.3, 0.0, 0.3))
     + [P.num("DCO Env Amt", semis(0), 0, -48, 48)]),
    ("LFOs", "LFO", lfo_section()),
    ("Modulation Matrix", "Matrix", matrix(P_SRCS, P_DESTS, {0: (3, 0), 1: (4, 3)})),
    ("Voice", "Voice", voice()),
]

# Six user macros and Velocity (M7); Pressure has no tile (showSynthPatchSection)
MACRO_TILES = [f"M{i}" for i in range(1, 8)]


# -- the screens ------------------------------------------------------------------------------
def synth_picker(model: int, models: list[str], marked: tuple[int, ...]) -> Screen:
    """showSynthParamsScreen: Synth Parameters 1/2, the Model list in column 1"""
    s = Screen("Synth Parameters 1/2, " + models[model] + " picked")
    s.title_bar("Synth Parameters 1/2", f"Synth 1 ({models[model]})")
    s.song_timer()
    # bars 2-6: Polyphony 4 of 8, Gain -9 dB (kGainDefault), Panning 0, the sends 0
    for i, (v, centre) in enumerate(((50, False), (80, False), (0, True), (0, False), (0, False)), start=1):
        s.bar(COL * i + 1, 29, COL - 3, 394, v, centre)
    s.cover(COL * 6 + 1, 29, COL * 2 - 3, 394)                                # blankSpaceCover[0]
    s.text(625, 45 + 15 - 10, "Patch:", 4, GREY_77)
    s.text(625, 45 + 15 + 10, "Init", 2, GREY_77)
    boxes = s.new_list(1, 29, COL - 3, 394, models, selected=model, lines=min(len(models), 14), kind="val")
    s.frame(1, 31, COL - 3, 391)
    s.bottom_bar(255)
    s.button(0, "Model", color=GREY_77)
    for i, (t, v) in enumerate((("Polyphony", "4"), ("Gain", "-9.00 dB"), ("Panning", "0 "),
                                ("Reverb send", "-inf dB"), ("Delay send", "-inf dB")), start=1):
        s.button(i, t, v, 1, color2=WHITE, font2=2, kind2="val")
    s.button(6, "Load", "Patch", 1)
    s.button(7, "Edit", "Patch", 1)
    for r in marked:
        x, y, w, h = boxes[r]
        s.mark(x, y, w, h, pad=1)
    return s


def patch_editor(model: str, sections, section: int, param: int, rows: list[P] | None = None,
                 list_name: str | None = None, title: str | None = None, mark_synth: bool = True) -> Screen:
    """showSynthPatchEditorScreen with one section open (layoutFlat), a parameter selected. With rows,
    the parameter list shows those instead (Synth FX's) and label 6 says list_name."""
    name, _, params = sections[section]
    if rows is not None:
        params = rows
    if list_name is not None:
        name = list_name
    s = Screen(title or f"{model}: {name}")
    s.title_bar("Edit Patch: Init", f"Synth 1 ({model})", title_kind="lbl")
    s.cover(1, 29, COL * 4 - 3, 394)                                          # blankSpaceCover[0]

    # ImgGrid: x 50, y 97, 300 x 308, 4 x 4 boxes of 73 x 75, 40 x 40 squares; the tiles' text in font 1
    tiles = [t[1] for t in sections] + MACRO_TILES
    bw, bh = (300 - 5) // 4, (308 - 5) // 4
    for idx in range(16):
        row, col = divmod(idx, 4)
        bx = 50 + bw // 2 + bw * col - 20
        by = 97 + bh // 2 + bh * row - 20
        s.rect(bx, by, 40, 40, WHITE if idx == section else GREY_32)
        if idx < len(tiles):
            icon(s, bx, by, idx, len(sections), GREY_32 if idx == section else WHITE)
            s.text(50 + bw * col + bw // 2, 97 + bh * row + bh - 17 + 3, tiles[idx], 1, WHITE, "middle",
                   "lbl", middle=False)
    s.text(50, 45 + 20, model, 4, GREY_77, "middle", "val")                  # noteLabel: the model

    # the parameter list: columns 5-7
    s.new_list(COL * 4 + 1, 29, COL * 3 - 3, 394, [p.name for p in params], [p.value for p in params],
               selected=param, lines=min(len(params), 14), start=max(0, param - 12))
    s.frame(COL * 4 + 1, 31, COL * 3 - 3, 391)

    # column 8: the value list for a list, else a bar
    p = params[param]
    if p.items is not None:
        s.new_list(COL * 7 + 1, 29, COL - 3, 394, p.items, selected=p.at, lines=min(len(p.items), 14),
                   start=max(0, p.at - 12), kind="lbl")
    else:
        rng = abs(p.hi - p.lo)
        centre = rng > p.hi and p.hi == -p.lo
        value = int(p.v * 100 / p.hi) if centre else int((p.v - p.lo) * 100 / rng)
        s.bar(COL * 7 + 1, 29, COL - 3, 394, value, centre)

    s.bottom_bar(158)
    s.button(0, "Save")
    s.button(1, "Save As")
    s.button(2, "Init", "Patch", 1)
    s.button(3, "Synth FX")
    s.button(5, name, x=COL * 5 + COL / 2)
    s.arrow(COL * 4 + COL / 2, 450, True)
    s.arrow(COL * 6 + COL / 2, 450, False)
    s.button(7, "Back")

    # what WAVE or PHASE brings: the tiles' names, the list, the values
    if mark_synth:
        s.mark(52, 99, 296, 304)
        s.mark(COL * 4 + 2, 30, COL * 4 - 6, 392)
    return s


def icon(s: Screen, x, y, idx: int, sections: int, color: str) -> None:
    """The tiles' bitmaps (kBitmapOsc, kBitmapEnv, ...) are not drawn; a dot marks a tile with one"""
    s.rect(x + 17, y + 17, 6, 6, color)


def write(name: str, screen: Screen, desc: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(screen.svg(desc), encoding="utf-8", newline="\n")
    print("wrote", OUT / name)


# the Model list: CE's kSynthTypes order (core/synth/Synth.h), WAVE, PHASE and DCO6 at 7, 8 and 9; the
# release's Synths zip has no other engines (CE dropped GRAIN); each row is the engine's code name
MODELS = ["ACD", "FAT", "VAP", "WTFM", "PERC", "DIRT", "CHIP", "WAVE", "PHASE", "DCO6"]

# which parameter each page selects: a list shows its choices in column 8
WAVE_SELECT = [0, 2, 2, 2, 1, 2, 1, 0]
PHASE_SELECT = [0, 0, 2, 2, 1, 2, 1, 0]
FILES = ["osc", "filter", "ampenv", "fltenv", "modenv", "lfo", "matrix", "voice"]
P_FILES = ["dco", "dcw", "dca", "dcwenv", "dcoenv", "lfo", "matrix", "voice"]


def main() -> None:
    write("synths-picker.svg", synth_picker(7, MODELS, (7, 8, 9)),
          "The Synth Parameters screen with WAVE picked; WAVE, PHASE and DCO6 are marked")
    for i, f in enumerate(FILES):
        write(f"synths-wave-{f}.svg", patch_editor("WAVE", WAVE, i, WAVE_SELECT[i]),
              f"WAVE's {WAVE[i][0]} in the patch editor")
    for i, f in enumerate(P_FILES):
        write(f"synths-phase-{f}.svg", patch_editor("PHASE", PHASE, i, PHASE_SELECT[i]),
              f"PHASE's {PHASE[i][0]} in the patch editor")


if __name__ == "__main__":
    main()
