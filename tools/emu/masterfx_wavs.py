#!/usr/bin/env python3
"""Each effect on a plucked note, as WAVs to listen to: build/wavs/masterfx/.

    python tools/emu/masterfx_wavs.py <mods.axf>

The objects run as in test_masterfx.py, without the synths, so this takes under a minute. The files
aren't committed. The pluck is made here, so nothing from a closed library is in them.
"""
import math
import random
import struct
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_masterfx import DelayRig, ReverbRig  # noqa: E402

RATE = 44100
BLOCK = 128
REPO = Path(__file__).resolve().parents[2]
REVERBS = ("Stock", "Freeverb", "Plate", "Spring", "Shimmer", "Gated", "Reverse")
DELAYS = ("Stock", "Tape", "BBD")


def pluck(seconds=0.25, hz=220.0, amp=14000):
    """Karplus-Strong, the input all the effects get."""
    random.seed(7)
    period = int(RATE / hz)
    line = [random.uniform(-1, 1) for _ in range(period)]
    out = []
    for i in range(int(seconds * RATE)):
        a = line[i % period]
        line[i % period] = 0.5 * (a + line[(i + 1) % period]) * 0.996
        out.append(int(amp * a * math.exp(-3.0 * i / RATE)))
    return out


def to_blocks(samples, tail_seconds):
    blocks = [samples[i:i + BLOCK] for i in range(0, len(samples), BLOCK)]
    blocks[-1] += [0] * (BLOCK - len(blocks[-1]))
    return blocks + [[0] * BLOCK for _ in range(int(tail_seconds * RATE / BLOCK))]


def write(path, left, right):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(struct.pack("<hh", a, b) for a, b in zip(left, right)))


def main():
    axf = Path(sys.argv[1])
    out = REPO / "build" / "wavs" / "masterfx"
    for which, name in enumerate(REVERBS):
        r = ReverbRig(axf)
        r.active("reverb", which)
        r.knobs(size=0.7, damp=0.4, pre=0.1, diff=0.625)
        r.reset()
        left, right = r.run(to_blocks(pluck(), 4.0))
        write(out / f"reverb_{which}_{name}.wav", left, right)
    for which, name in enumerate(DELAYS):
        for pingpong in (False, True):
            d = DelayRig(axf)
            d.active("delay", which)
            d.knobs(time=11025, fb=0.6, pp=pingpong, hz=6000.0)
            d.clear()
            left, right = d.run(to_blocks(pluck(), 3.0))
            write(out / f"delay_{which}_{name}_{'pingpong' if pingpong else 'mono'}.wav", left, right)
    print(f"Written: {out}")


if __name__ == "__main__":
    main()
