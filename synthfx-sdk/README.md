# Synth FX Plugin SDK (`.ptsfx`)

Build effects for the synth parts' two slots, FX1 and FX2, loaded from `/SynthFX/` on the
card. They run on every synth engine (the `.psyn` engines in `/Synths/`, WAVE and PHASE too),
only on the Modular-Trackular Plus firmware with the Synth FX mod on.

Every `.ptsfx` targets the Tracker+ (`-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
-mthumb`) as position-independent code, and runs from the firmware's 96 KB SDRAM code pool in
the audio interrupt, once per block of 128 samples.

**The guide is [docs/synthfx_plugins.md](../docs/synthfx_plugins.md):** the architecture, the
lifecycle, a tutorial, the binary format, the host's functions, the guards, the cost budget
and how to check a plugin. Read it before writing one.

---

## SDK Layout

```text
synthfx-sdk/
├── ptsfx.h              # The format: PtsfxHeader, PtsfxParam, PtsfxHost, the entry macros
├── ptsfx_builder.py     # C/C++ -> .ptsfx; refuses a plugin that breaks the rules
├── ptsfx_support.c      # memset, memcpy and memmove, linked into every plugin
├── examples/
│   ├── tremolo.c        # The smallest useful plugin
│   └── delay.c          # A short stereo delay with a 64 KB state
└── pack/                # The default pack: Overdrive, Distortion, Bit Crusher,
                         # Chorus, Flanger, Phaser (pack.h is their shared code)
```

The tools that go with it are in `tools/synthfx/`: `check_ptsfx.py` (runs a plugin in the
emulator with the firmware's runtime) and `build_pack.py` (builds the default pack).

---

## Quick Start

You need the Arm GNU toolchain (`arm-none-eabi-gcc`) and Python 3.

```
python synthfx-sdk/ptsfx_builder.py synthfx-sdk/examples/tremolo.c -o Tremolo.ptsfx --toolchain <prefix>
python tools/synthfx/check_ptsfx.py Tremolo.ptsfx
```

Leave out `--toolchain` if `arm-none-eabi-gcc` is on the path. Copy the `.ptsfx` file to
`/SynthFX/` on the card, open a synth part's Edit Patch screen, press button 3 and pick it in
FX1 or FX2.

---

## A Plugin in Brief

- **Header:** `PTSFX_INFO = { ... }` with the flags, name, author, knobs (up to 12) and state
  size; the builder fills in the rest.
- **Entries:** `PTSFX_INIT` and `PTSFX_FORMAT` (optional) in the main loop;
  `PTSFX_SET_PARAM`, `PTSFX_PROCESS` and `PTSFX_RESET` in the audio interrupt.
- **State:** all of it in the memory the host gives each instance, zeroed before `init`. No
  globals that change, no heap, no files, single precision.
- **Flags:** `PTSFX_FLAG_WIDENS` makes stereo from a mono part. `PTSFX_FLAG_PRE` is kept for
  files built for earlier firmware and changes nothing: every slot runs on the part.

---

## Licence

MIT. `ptsfx_builder.py` is based on CE's `editor-fx-sdk/ptfx_builder.py`, under CE's MIT
licence (`LICENSE` at the repository's root).
