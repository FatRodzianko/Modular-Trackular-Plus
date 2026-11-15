# Master FX Plugin SDK (`.ptmfx`)

Build effects for the Tracker+'s three master slots: the reverb, the delay and the limiter
(the dynamics slot), loaded from `/MasterFX/` on the card. A slot plays CE's own effect
(Stock) or one plugin made for it, only on the Modular-Trackular Plus firmware with the Master
FX mod on.

Every `.ptmfx` targets the Tracker+ (`-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
-mthumb`) as position-independent code. The firmware copies it into the slot's 8 KB code area
in ITCM and runs it in the audio interrupt, once per block of 128 samples, where CE's effect
would run.

**The guide is [docs/masterfx_plugins.md](../docs/masterfx_plugins.md):** the architecture, the
lifecycle, a tutorial, the Master page, the binary format, the host's functions, the guards,
the cost budget and how to check a plugin. Read it before writing one.

---

## SDK Layout

```text
masterfx-sdk/
├── ptmfx.h              # The format: PtmfxHeader, PtmfxParam, PtmfxHost, PtmfxBlock, CE's
│                        # knobs per slot, the entry macros
├── ptmfx_builder.py     # C/C++ -> .ptmfx; refuses a plugin that breaks the rules
├── ptmfx_support.c      # memset, memcpy and memmove, linked into every plugin
├── examples/
│   ├── room.c           # A small float reverb, both of the reverb slot's memory areas
│   └── ducker.c         # A sidechain ducker for the dynamics slot; can duck chosen tracks
└── pack/                # The default pack: Freeverb, Plate, Spring, Shimmer, "Gated",
                         # Reverse, Tape, BBD and Glue (pack.cpp builds the reverbs and delays)
```

The tools that go with it are in `tools/masterfx/`: `check_ptmfx.py` (runs a plugin in the
emulator with the firmware's runtime), `build_pack.py` (builds the default pack) and
`test_pack.py` (the pack against the effects it replaced).

---

## Quick Start

You need the Arm GNU toolchain (`arm-none-eabi-gcc`) and Python 3.

```
python masterfx-sdk/ptmfx_builder.py masterfx-sdk/examples/ducker.c -o Ducker.ptmfx --toolchain <prefix>
python tools/masterfx/check_ptmfx.py Ducker.ptmfx
```

Leave out `--toolchain` if `arm-none-eabi-gcc` is on the path. The checker needs a Tracker+
Modular-Trackular firmware image (`make plus`, or `--firmware`). Copy the `.ptmfx` file to
`/MasterFX/` on the card and pick it in the Master page's Limiter column (a reverb in Reverb, a
delay in Delay), or in Config > Project > Master FX.

---

## A Plugin in Brief

- **Header:** `PTMFX_INFO = { ... }` with the flags, the slot, the name, the author, labels for
  CE's knobs on the slot's page, the state size and up to 2 own knobs; the builder fills in
  the rest.
- **Entries:** `PTMFX_INIT` and `PTMFX_FORMAT` (optional) in the main loop;
  `PTMFX_SET_PARAM`, `PTMFX_PROCESS` and `PTMFX_RESET` in the audio interrupt.
- **CE's knobs** come in every block (`b->knobs`), as the slot's page left them.
- **State:** all of it in the state the host gives (up to 1 KB for the reverb, 512 bytes for the
  delay and dynamics, zeroed before `init`) and the slot's memory (CE's buffers). No globals
  that change, no heap, no files, single precision.
- **Flags:** `PTMFX_FLAG_FLOAT` gives float blocks instead of CE's 16-bit ones.
  `PTMFX_FLAG_DUCK` (a float dynamics plugin) lets it duck chosen tracks instead of the master.

---

## Licence

MIT. `ptmfx_builder.py` is based on Synth FX's `synthfx-sdk/ptsfx_builder.py`, itself adapted in
part from CE's `editor-fx-sdk/ptfx_builder.py`, under CE's MIT licence (`LICENSE` at the
repository's root).
