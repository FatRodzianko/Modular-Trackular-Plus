# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""DCO6 prototype: per-voice instructions in CE's real synth host, beside WAVE and PHASE.

PROTOTYPE MEASUREMENT for docs/mt/VA_SYNTH_PLAN.md, not a test. The full build's start-up and audio
graph (sound_rig.py), CE's factory engines on the fake card, and our three engines (DCO6 from
core/mods/synths/psyn/build, WAVE and PHASE as the Synths mod ships them). One part gets all 8
voices; notes go in through Synth::triggerTestNote. Counted: every instruction inside the engine's
own .psyn image (its render, the bridge, isActive), per block, after the attack. The host's share
(InstrumentImpl, mixing) and the closed engines aren't counted.

Unison: the prototype has no host hook, so a voice takes its place in the stack from its own index
(index % Unison). Sending each note Unison times fills the stacks as the hook would: the render
work per voice is the same, the hook's note-on work (a few hundred instructions a note) isn't in it.

    python tools/emu/measure_dco6.py build/emu/TrackerPlus_MT_rig.axf [--wav out_dir]
"""
from __future__ import annotations

import struct
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sound_rig  # noqa: E402

REPO = sound_rig.REPO
DCO6_PSYN = sound_rig.OWN_PSYN_BUILD / "DCO6.psyn"
sound_rig.OWN_SYNTHS = ("WAVE", "PHASE", "DCO6")
sound_rig.SYNTH_OVERRIDES["DCO6.psyn"] = DCO6_PSYN.read_bytes()   # build/Synths needs a copy too (synth_files checks first)
from test_synths import Rig  # noqa: E402

WAVE, PHASE, DCO6 = 64, 65, 66
NAMES = {WAVE: "WAVE", PHASE: "PHASE", DCO6: "DCO6"}
CHORD = (48, 52, 55, 59, 62, 65, 69, 72)
# DCO6 parameter ids (core/mods/synths/d6Engine.h)
EP = dict(saw=3, pulse=4, sub=5, noise=6, pw=7, pwm=8, lfo_pitch=10, hpf=11, cutoff=12, res=13, env_amt=14,
          lfo_cut=15, key=16, attack=17, decay=18, sustain=19, release=20, lfo_wave=22, lfo_delay=24,
          unison=27, detune=28, width=29, vintage=30, glide=31, boost=34)
HEAVY = dict(saw=1.0, pulse=0.8, sub=0.6, noise=0.2, pw=0.3, pwm=0.5, lfo_pitch=0.1, hpf=80.0, boost=1, res=0.7,
             lfo_cut=0.3, vintage=1.0, lfo_wave=6, sustain=1.0)


def measure(axf, engine, notes, params=(), skip=12, blocks=40, wav=None, label=""):
    r = Rig(axf)
    for part in (1, 2):
        r.set_voices(part, 0)
    r.set_instrument(0, engine)
    r.set_voices(0, 8)
    for pid, v in params:
        r.set_param(0, pid, v)
    e = r.own[NAMES[engine]]
    renders = [0]
    r.watch(e.addr("voiceRenderAudio"), lambda mm: renders.__setitem__(0, renders[0] + 1))
    for n in notes:
        r.note(0, n, True)
    out_l, out_r = r.blocks(skip)
    count = [0]
    h = r.hook_range(e.base, e.base + e.header["image_size"], lambda uc, a, s, u: count.__setitem__(0, count[0] + 1))
    r.m.uc.ctl_flush_tb()
    per_block, voices = [], []
    for _ in range(blocks):
        count[0] = renders[0] = 0
        left, right = r.blocks(1)
        out_l += left
        out_r += right
        per_block.append(count[0])
        voices.append(renders[0])
    r.m.uc.hook_del(h)
    if wav:
        for n in set(notes):
            r.note(0, n, False)
        left, right = r.blocks(120)
        out_l += left
        out_r += right
        wav.mkdir(parents=True, exist_ok=True)
        with wave.open(str(wav / f"{label}.wav"), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(b"".join(struct.pack("<hh", a, b) for a, b in zip(out_l, out_r)))
    mean = sum(per_block) / len(per_block)
    v = max(voices)
    peak = max(max(abs(x) for x in out_l), max(abs(x) for x in out_r))
    side = max(abs(a - b) for a, b in zip(out_l, out_r))
    return dict(mean=mean, worst=max(per_block), voices=v, per_voice=mean / max(v, 1), peak=peak, side=side,
                failures=list(r.failures), heap=r.heap_brk - r.heap_base)


def main():
    axf = sys.argv[1]
    wav = Path(sys.argv[sys.argv.index("--wav") + 1]) if "--wav" in sys.argv else None
    cases = [
        ("WAVE Init, 8 notes", WAVE, CHORD, ()),
        ("PHASE Init, 8 notes", PHASE, CHORD, ()),
        ("DCO6 Init, 8 notes", DCO6, CHORD, ()),
        ("DCO6 heavy, 8 notes", DCO6, CHORD, tuple((EP[k], v) for k, v in HEAVY.items())),
    ]
    for u in (1, 2, 4, 8):
        notes = [n for n in CHORD[: 8 // u] for _ in range(u)]
        params = ((EP["unison"], float(u)), (EP["detune"], 12.0), (EP["width"], 0.5))
        cases.append((f"DCO6 Unison {u}, {8 // u} notes", DCO6, notes, params))
    cases.append(("DCO6 heavy, Unison 8", DCO6, [60] * 8,
                  tuple((EP[k], v) for k, v in HEAVY.items()) + ((EP["unison"], 8.0), (EP["detune"], 25.0))))
    print(f"{'case':34s} {'voices':>6s} {'insns/block':>12s} {'worst':>9s} {'per voice':>10s} {'peak':>6s} {'L-R':>6s}")
    for name, engine, notes, params in cases:
        res = measure(axf, engine, notes, params, wav=wav, label=name.split(",")[0].replace(" ", "_"))
        print(f"{name:34s} {res['voices']:6d} {res['mean']:12,.0f} {res['worst']:9,d} {res['per_voice']:10,.0f} "
              f"{res['peak']:6d} {res['side']:6d} {res['failures'][:1]}", flush=True)


if __name__ == "__main__":
    main()
