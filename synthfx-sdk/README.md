# Synth FX SDK

Tools and sources for building Synth FX plugins (`.ptsfx`): effects for the synth parts'
two slots, FX1 and FX2, loaded from `/SynthFX/` on the card. They run only on the
Modular-Trackular Plus firmware with the Synth FX mod on.

**The guide is [docs/synthfx_plugins.md](../docs/synthfx_plugins.md):** the rules, the file
format, the entries, the guards, the cost budget and how to check a plugin. Read it before
writing one.

## Quick start

You need the Arm GNU toolchain (`arm-none-eabi-gcc`) and Python 3.

```
python synthfx-sdk/ptsfx_builder.py synthfx-sdk/examples/tremolo.c -o Tremolo.ptsfx --toolchain <prefix>
```

Leave out `--toolchain` if `arm-none-eabi-gcc` is on the path. Copy the `.ptsfx` file to
`/SynthFX/` on the card, open a synth patch, press button 3 and pick it in FX1 or FX2.

To check a plugin in the emulator before the unit, see "Checking one" in the guide
(`tools/synthfx/check_ptsfx.py`).

## What's here

| File | What it is |
|---|---|
| `ptsfx.h` | The format: the header, the knobs, the entries and the host's functions |
| `ptsfx_builder.py` | Builds a plugin from C or C++ and refuses one that breaks the rules |
| `ptsfx_support.c` | `memset`, `memcpy` and `memmove`, linked into every plugin |
| `examples/tremolo.c` | The smallest useful plugin |
| `examples/delay.c` | A short stereo delay with a 64 KB state |
| `pack/` | The default pack's sources: Overdrive, Distortion, Bit Crusher, Chorus, Flanger, Phaser |

## Licence

MIT. `ptsfx_builder.py` is based on CE's `editor-fx-sdk/ptfx_builder.py`, under CE's MIT
licence (`LICENSE` at the repository's root).
