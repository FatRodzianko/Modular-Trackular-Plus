# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Distortion's and Overdrive's half-band filters (core/mods/synthfx/synthFx.cpp, hb_c and
hb2_c): their coefficients, from Laurent de Soras's polyphase IIR design (HIIR, WTFPL), and the
responses that matter.

The outer filter, 44.1 to 88.2 kHz and back, keeps 0-20 kHz flat and removes 24-44 kHz, which
would fold onto the audio band at 44.1 kHz. The inner one, 88.2 to 176.4 kHz and back (the
Distortion only), only has to remove what would fold onto 0-24 kHz at 88.2 kHz, so from 64.4 kHz
up: the outer filter takes out 24-44 kHz after it.

Usage: python tools/synths/halfband.py
"""
import math

import numpy as np

# (name, coefficients, transition, the rate it runs at, the band kept, where the stop band starts)
FILTERS = (
    ("hb_c", 8, 0.04, 88200.0, 20000.0, 24100.0),
    ("hb2_c", 3, 0.2, 176400.0, 24000.0, 64400.0),
)


def _transition(tw):
    k = math.tan((1 - tw * 2) * math.pi / 4) ** 2
    kk = (1 - k * k) ** 0.25
    e = 0.5 * (1 - kk) / (1 + kk)
    e4 = e ** 4
    return k, e * (1 + e4 * (2 + e4 * (15 + 150 * e4)))


def coefs(n, tw):
    """de Soras's compute_coefs_spec_order_tbw: n coefficients, transition tw (of the rate)"""
    k, q = _transition(tw)
    order = 2 * n + 1
    out = []
    for i in range(n):
        c = i + 1
        num = sum((-1) ** m * q ** (m * (m + 1)) * math.sin((2 * m + 1) * c * math.pi / order)
                  for m in range(40)) * q ** 0.25
        den = sum((-1) ** m * q ** (m * m) * math.cos(2 * m * c * math.pi / order) for m in range(1, 40)) + 0.5
        w2 = (num / den) ** 2
        x = math.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2)
        out.append((1 - x) / (1 + x))
    return out


def response(c, f):
    """|H| at f (of the rate): (A0(z^2) + z^-1 A1(z^2)) / 2, even coefficients on the first path"""
    z = np.exp(1j * 2 * np.pi * np.asarray(f))
    a = [np.ones_like(z), np.ones_like(z)]
    for i, ci in enumerate(c):
        a[i % 2] = a[i % 2] * (ci + z ** -2) / (1 + ci * z ** -2)
    return np.abs(0.5 * (a[0] + a[1] / z))


def main():
    for name, n, tw, rate, keep, stop in FILTERS:
        c = coefs(n, tw)
        rej = -20 * np.log10(response(c, np.linspace(stop / rate, 0.5, 4000)).max())
        flat = 20 * np.log10(response(c, np.linspace(1 / rate, keep / rate, 2000)).min())
        print(f"{name}: {n} coefficients, transition {tw}: " + ", ".join(f"{v:.10f}f" for v in c))
        print(f"  0-{keep / 1000:.0f} kHz within {flat:.5f} dB; {rej:.1f} dB down from {stop / 1000:.1f} kHz")


if __name__ == "__main__":
    main()
