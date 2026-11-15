# Synth FX Plugins (`.ptsfx`) - Developer Guide

A Synth FX plugin is an effect for the synth parts, in a file on the card. Each synth part has
two effect slots, FX1 and FX2, and each slot runs one plugin from `/SynthFX/`. The firmware
loads the plugin's code into its own memory and runs it in the audio interrupt, once per block
of 128 samples, on the part's audio. It works on every synth engine: the `.psyn` engines in
`/Synths/`, CE's and Modular-Trackular Plus's WAVE and PHASE alike.

Plugins run only on the Modular-Trackular Plus firmware with the Synth FX mod on. Stock CE
doesn't load `.ptsfx` files.

The SDK is [`synthfx-sdk/`](../synthfx-sdk/); the firmware's side is
[`core/mods/synthfx/`](../core/mods/synthfx/).

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [SDK Contents](#2-sdk-contents)
3. [Plugin Lifecycle & Audio Model](#3-plugin-lifecycle--audio-model)
4. [Step-by-Step Tutorial: Building a Plugin](#4-step-by-step-tutorial-building-a-plugin)
5. [On the Synth Page](#5-on-the-synth-page)
6. [The Header: Flags, State & Knobs (`PTSFX_INFO`)](#6-the-header-flags-state--knobs-ptsfx_info)
7. [Host API Reference (`PtsfxHost`)](#7-host-api-reference-ptsfxhost)
8. [The `.ptsfx` Binary Specification](#8-the-ptsfx-binary-specification)
9. [Firmware Architecture: Memory, Loading, Guards & Storage](#9-firmware-architecture-memory-loading-guards--storage)
10. [Real-Time Audio, CPU & Memory Guidelines](#10-real-time-audio-cpu--memory-guidelines)
11. [Checking a Plugin in the Emulator](#11-checking-a-plugin-in-the-emulator)
12. [Reference Implementations](#12-reference-implementations)
13. [How Synth FX Relates to CE's Plugin Systems](#13-how-synth-fx-relates-to-ces-plugin-systems)

---

## 1. Architecture Overview

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│                               Tracker+ firmware                              │
│                                                                              │
│  Main loop                                                                   │
│  ┌───────────────────────────────┐   ┌────────────────────────────────────┐  │
│  │ SynthPatchEditor, button 3:   │   │ core/mods/synthfx/                 │  │
│  │ the Synth FX rows (FX1, FX2,  │──▶│  catalog of /SynthFX/*.ptsfx       │  │
│  │ their knobs)                  │   │  loader: code pool 96 KB,          │  │
│  └───────────────────────────────┘   │  state pool 192 KB (SDRAM)         │  │
│                                      │  slots per part, mods.bin record 16│  │
│                                      └──────────────────┬─────────────────┘  │
│                                                         │ publishes          │
│  Audio interrupt (PolySynth::update), per synth part:   ▼                    │
│  ┌───────────────┐   ┌───────────────┐   ┌──────────────────────────────┐    │
│  │ engine renders│──▶│ FX1 ─▶ FX2    │──▶│ CE's budget check, sends,    │    │
│  │ (a .psyn:     │   │ (in place)    │   │ fader, soft clip             │    │
│  │  any engine)  │   └───────────────┘   └──────────────────────────────┘    │
│  └───────────────┘                                                           │
└──────────────────────────────────────────────────────────────────────────────┘
                                  ▲
                    ┌─────────────┴──────────────┐
                    │ SD card /SynthFX/*.ptsfx   │
                    │  built with ptsfx_builder  │
                    │  default pack written by   │
                    │  the firmware if missing   │
                    └────────────────────────────┘
```

### Key facts

- **One code copy, many instances.** A plugin's code is loaded once into the code pool and
  serves every slot and voice that uses it. Each instance has its own state in the state
  pool, zeroed before `init`. So a plugin has no globals that change.
- **Position-independent code.** The code runs wherever it lands in the pool. The builder
  compiles it position independent and checks it by linking it twice at different
  addresses.
- **On the part's output.** The slots run on what the part's engine has rendered, after
  `renderAudioBuffer` and before CE's synth time check, the sends and the fader. Engines are
  never touched from inside: CE's factory `.psyn` engines are closed (their licence, section
  2(c)), and Synth FX only hears their output.
- **Settings follow the part, not the patch.** A `.psyn` engine's patch can't hold extra
  settings, so each part's slots are stored in the project's `mods.bin` (record 16).
- **The default pack.** Overdrive, Distortion, Bit Crusher, Chorus, Flanger and Phaser are
  plugins too. The firmware carries a copy and writes each pack file to `/SynthFX/` when it
  is missing there, so delete one to get the firmware's copy back.

---

## 2. SDK Contents

| File | Purpose |
| :--- | :--- |
| [`synthfx-sdk/ptsfx.h`](../synthfx-sdk/ptsfx.h) | The format: `PtsfxHeader` (476 bytes), `PtsfxParam` (32 bytes), the flags, the knob displays, `PtsfxHost`, the entry types, and the `PTSFX_INFO`, `PTSFX_KNOB` and entry macros. The firmware's copy, `core/mods/synthfx/ptsfx.h`, is the same file (a host test checks). |
| [`synthfx-sdk/ptsfx_builder.py`](../synthfx-sdk/ptsfx_builder.py) | Builds a plugin from C or C++ sources and refuses one that breaks the rules. Based on CE's `editor-fx-sdk/ptfx_builder.py`. |
| [`synthfx-sdk/ptsfx_support.c`](../synthfx-sdk/ptsfx_support.c) | `memset`, `memcpy` and `memmove`, linked into every plugin (the code has no C library). |
| [`synthfx-sdk/examples/tremolo.c`](../synthfx-sdk/examples/tremolo.c) | The smallest useful plugin. |
| [`synthfx-sdk/examples/delay.c`](../synthfx-sdk/examples/delay.c) | A short stereo delay with a 64 KB state that widens a mono part. |
| [`synthfx-sdk/pack/`](../synthfx-sdk/pack/) | The default pack's sources. |
| [`tools/synthfx/check_ptsfx.py`](../tools/synthfx/check_ptsfx.py) | Runs a plugin in the emulator with the firmware's own runtime (section 11). |
| [`tools/synthfx/build_pack.py`](../tools/synthfx/build_pack.py) | Builds the default pack, and with `--embed` the firmware's copy of it. |

You need the Arm GNU toolchain, `arm-none-eabi-gcc` (Arm's 10.3 release builds the firmware),
and Python 3.

---

## 3. Plugin Lifecycle & Audio Model

### 3.1 The entries

| Entry | Runs in | When |
| :--- | :--- | :--- |
| `PTSFX_INIT(void *st, const PtsfxHost *host)` | main loop | once per instance, on zeroed state |
| `PTSFX_SET_PARAM(void *st, uint32_t index, float value)` | interrupt | before a block: every knob after `init`, then each knob that changed |
| `PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)` | interrupt | each block, in place; `frames` is 128 |
| `PTSFX_RESET(void *st)` | interrupt | the part starts again, or after bad output |
| `PTSFX_FORMAT(const PtsfxHost *host, uint32_t index, float value, char *out, uint32_t size)` | main loop | optional: a knob's text for the screen |

`init`, `set_param`, `process` and `reset` are required; the builder refuses a plugin without
them. `format` is optional.

### 3.2 Lifecycle order

1. **Catalog.** At start the firmware writes the missing pack files, lists every `.ptsfx` in
   `/SynthFX/` (up to 64) and checks each header and file size. A file that fails stays in
   the list with the reason.
2. **Load.** When a slot first picks a plugin, its code is read into the code pool and the
   instruction cache is synced over it. The code stays while any instance uses it.
3. **Instance.** State is taken from the state pool and zeroed, `init` runs, and every knob
   is set to its default (or to the project's values) through `set_param`.
4. **Publish.** The main loop makes the instance visible to the interrupt last. From then on
   the interrupt calls `set_param` for changed knobs and `process` every block.
5. **Swap or remove.** A new plugin fades in over its first block; the one it replaces fades
   out over its last block. Then the old instance is retired, and its state, and its code if
   no other instance uses it, go back to the pools.

### 3.3 Channels

- **Mono part:** `R` is null; the plugin works on `L` only.
- **Stereo part** (an engine whose metadata says stereo, such as CE's PERC and GRAIN):
  `R` is given and `in_channels` is 2.
- **Widening plugin on a mono part** (`PTSFX_FLAG_WIDENS`): `R` is given for output only and
  `in_channels` is 1. The part goes out as stereo (one more audio block) while such a plugin
  is in a slot.
- **WAVE and PHASE** go out as stereo, but both sides are the same, so the slots take them as
  mono until a widening plugin makes them stereo.
- **An engine that renders its own sends** (PERC) has only its main output in the slots.

A slot always runs on the part, after the engine, whatever the engine. Earlier builds, with
WAVE inside the firmware, could also run a slot in each WAVE voice before its filter
(Placement Pre). WAVE is a `.psyn` now and a slot can't run inside it, so Placement is gone
(section 6.2 has what that means for `PTSFX_FLAG_PRE`).

---

## 4. Step-by-Step Tutorial: Building a Plugin

### Step 1: Write the plugin (`tremolo.c`)

The whole of [`examples/tremolo.c`](../synthfx-sdk/examples/tremolo.c), shortened:

```c
#include "ptsfx.h"

typedef struct
{
    const PtsfxHost *host;
    float phase;                            /* the LFO, 0 to 1 */
    float rate, depth, depth_now;
    uint32_t shape, started;
} Tremolo;

PTSFX_INFO = {
    0, 0, 0, "Tremolo", "MT+ examples", 3, {0, 0, 0}, sizeof(Tremolo), 0, 0, 0, 0, 0, 0, 0,
    {
        PTSFX_KNOB("Rate", 0.1, 20, 4, 0.1, "Hz", PTSFX_PARAM_FLOAT),
        PTSFX_KNOB("Depth", 0, 100, 50, 1, "%", PTSFX_PARAM_INT),
        PTSFX_KNOB("Shape", 0, 2, 0, 1, "", PTSFX_PARAM_LIST),
    },
};

PTSFX_INIT(void *st, const PtsfxHost *host)
{
    ((Tremolo *)st)->host = host;           /* state is zeroed; keep host for process() */
}

PTSFX_SET_PARAM(void *st, uint32_t index, float value)
{
    Tremolo *t = (Tremolo *)st;
    if (index == 0) t->rate = value;
    else if (index == 1) t->depth = value * 0.01f;
    else t->shape = (uint32_t)value;
}

PTSFX_RESET(void *st)                       /* back to a fresh instance */
{
    Tremolo *t = (Tremolo *)st;
    t->phase = 0.0f;
    t->started = 0;
}

PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)
{
    /* the gain once per 16 frames, the host's sinf, L and R (if given) in place */
}

PTSFX_FORMAT(const PtsfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    /* "Sine", "Square" or "Triangle" for knob 2; leave out empty for the others */
}
```

The first fields of `PTSFX_INFO` (magic, version, flags) are `0, 0, flags`; then the name,
the author, the number of knobs, three reserved bytes, the state size, seven zeros (the
builder fills in the code's place and the entries' offsets) and the knobs.

### Step 2: Build it

```
python synthfx-sdk/ptsfx_builder.py synthfx-sdk/examples/tremolo.c -o Tremolo.ptsfx --toolchain <prefix>
```

`<prefix>` is the toolchain's path and name prefix, such as
`/opt/arm-gnu-toolchain/bin/arm-none-eabi-`; leave `--toolchain` out if `arm-none-eabi-gcc` is
on the path. Several sources can be given. Other options:

| Option | What it does |
| :--- | :--- |
| `-o`, `--output` | The `.ptsfx` file to write (required) |
| `--toolchain` | The tools' prefix |
| `-I`, `-D` | Include folders and defines, as for the compiler |
| `--allow-double` | Accepts calls to double-precision helpers (slow; see section 10) |

The builder compiles for the Tracker+ (`-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
-mthumb`, `-O2`, `-fPIC`), links the code twice at different addresses, and refuses the
plugin if:

- an entry is missing, or `PTSFX_INFO` is;
- it has writable globals (`.data`, `.bss` or a GOT);
- its code differs between the two links, so it wouldn't run where it is loaded;
- it calls double-precision helpers, unless `--allow-double`;
- the header's fields are out of range (section 6).

### Step 3: Check it in the emulator

```
python tools/synthfx/check_ptsfx.py Tremolo.ptsfx
```

Section 11 says what it checks.

### Step 4: Put it on the card

Copy `Tremolo.ptsfx` to `/SynthFX/` on the card. The file name is what a project stores, so
keep it once a project uses the plugin. The header's name is what the list shows. Then open a
synth part's Edit Patch screen, press button 3 and pick it in FX1 or FX2.

---

## 5. On the Synth Page

With the Synth FX mod on, button 3 on a synth part's Edit Patch screen shows the Synth FX rows:
FX1, its plugin's knobs, FX2, its plugin's knobs. Button 3 again, or picking a section, goes
back. On a row of a sample slot (an engine with sample slots, such as CE's GRAIN), button 3
is CE's Load WAV instead; its label says which.

- **A slot's row** picks the plugin: None, then every plugin in `/SynthFX/` that loaded
  cleanly, by its header's name, without the one in the part's other slot.
- **A pick is loaded once the encoder rests** for 0.4 s, so turning through the list loads
  none of the plugins it passes. Until then the old plugin plays on. Delete empties a slot
  at once.
- **While a plugin loads,** the row reads Loading. The page redraws when the plugin is ready.
- **A knob's row** shows its label and its text. A `PTSFX_PARAM_LIST` knob with up to 16
  steps is a list; any other knob is a bar, centred when its range is symmetric
  (min = -max).
- **The encoder** moves a knob one step a tick. A knob with more than 200 steps moves more
  per tick, so a turn still crosses its range. Delete puts a knob back to its default, or
  empties a slot.
- **A project stores** each slot's file name and its knobs' values, for each part. A slot
  whose file is missing or refused keeps its name and says why, such as `Missing: Delay`.
  It loads again when the page next opens, once the file is back.
- **A swap** fades the old plugin out over one block and the new one in over its first
  block, so a slot can change while the part plays.
- **A project saved with Placement Pre** opens with those slots as they were, run on the
  part.
- **An old WAVE or PHASE patch** (from a build where their effects were inside) has its
  Effect and Distortion moved into the part's slots once it has loaded: the Distortion type
  in FX1 (on the part, whatever its Placement was), the modulation effect in FX2, with the
  same values. The patch's own are then set to None and Off, so this happens once; save the
  project to keep that. It happens with the mod on or off, and is heard once the mod is on.
- **Off,** the slots are bypassed and kept.

A card that has the pack from an earlier build keeps those files: the firmware only writes
the pack files that are missing.

---

## 6. The Header: Flags, State & Knobs (`PTSFX_INFO`)

### 6.1 Fields you fill in

| Field | Limits |
| :--- | :--- |
| `flags` | `PTSFX_FLAG_WIDENS`, `PTSFX_FLAG_PRE`, or 0; not both |
| `name` | up to 31 characters; shown in the slot's list |
| `author` | up to 15 characters |
| `num_params` | 0 to 12 |
| `state_size` | 1 byte to 64 KB per instance |
| `params` | the knobs, `PTSFX_KNOB(label, min, max, default, step, unit, display)` |

The code may be up to 32 KB.

### 6.2 Flags (`PTSFX_FLAG_*`)

| Flag | Value | Meaning |
| :--- | :--- | :--- |
| `PTSFX_FLAG_WIDENS` | `0x0001` | Makes stereo from mono, as a chorus does. On a mono part, `process` gets `L` in and writes `L` and `R` (`in_channels` 1). |
| `PTSFX_FLAG_PRE` | `0x0002` | Kept for files built for earlier firmware: it let WAVE's Placement Pre run the plugin in each voice. No host runs a plugin in a voice now, so it changes nothing, and a plugin with it loads and runs on the part. Not with `WIDENS`. |

### 6.3 Knobs (`PtsfxParam`)

- a label of up to 10 characters;
- a min, a max and a step over 0. Max - min must be a whole number of steps, at most 100,000
  of them;
- a default, on a step;
- a unit of up to 3 characters, shown after the value;
- a display:

| Display | Value | Shown as |
| :--- | :--- | :--- |
| `PTSFX_PARAM_FLOAT` | `'f'` | the value with one decimal |
| `PTSFX_PARAM_INT` | `'d'` | the value as a whole number |
| `PTSFX_PARAM_LIST` | `'l'` | whole steps from min to max, each shown by `format()` |

The host keeps every knob on its steps: its value is always `min + i * step` for a whole `i`,
rounded once to single precision (`fmaf(i, step, min)`). Out-of-range values are held at the
ends.

---

## 7. Host API Reference (`PtsfxHost`)

`init` gets a pointer to the host's table; keep it in the state if `process` needs it. The
functions may be called anywhere, `process` included, except `print`.

| Member | Type | What it is |
| :--- | :--- | :--- |
| `version` | `uint32_t` | 1 |
| `sample_rate` | `float` | 44,100 |
| `block` | `uint32_t` | 128 |
| `sinf`, `cosf`, `tanf`, `tanhf`, `expf`, `logf`, `sqrtf`, `fabsf` | `float (*)(float)` | single-precision maths |
| `powf`, `fminf`, `fmaxf` | `float (*)(float, float)` | single-precision maths |
| `random` | `uint32_t (*)(void)` | 32 fast random bits |
| `print` | `int (*)(char *out, uint32_t size, const char *fmt, float value)` | `snprintf` with one float, such as `"%.1f kHz"`; for `format()` only (the main loop) |

The host's functions are calls into the firmware: use them once per block or per 16 samples,
not per sample, where that is enough.

There is no file, screen, memory or MIDI access. A plugin that needs a table brings it as
constant data in its code.

---

## 8. The `.ptsfx` Binary Specification

A `.ptsfx` file is the 476-byte header, then the code. Everything is little-endian.

### 8.1 Header (`PtsfxHeader`, 476 bytes)

| Offset | Size | Field | Set by | Meaning |
| ---: | ---: | :--- | :--- | :--- |
| 0 | 4 | `magic` | builder | `0x46535450` (`"PTSF"`) |
| 4 | 2 | `format_version` | builder | 1 |
| 6 | 2 | `flags` | plugin | `PTSFX_FLAG_*` |
| 8 | 32 | `name` | plugin | ended by a 0 |
| 40 | 16 | `author` | plugin | ended by a 0 |
| 56 | 1 | `num_params` | plugin | 0 to 12 |
| 57 | 3 | `reserved` | plugin | 0 |
| 60 | 4 | `state_size` | plugin | bytes per instance |
| 64 | 4 | `code_offset` | builder | where the code starts in the file |
| 68 | 4 | `code_size` | builder | up to 32 KB |
| 72 | 4 | `init_offset` | builder | from the code's start, Thumb bit clear |
| 76 | 4 | `set_param_offset` | builder | |
| 80 | 4 | `process_offset` | builder | |
| 84 | 4 | `reset_offset` | builder | |
| 88 | 4 | `format_offset` | builder | `0xFFFFFFFF` (`PTSFX_NO_ENTRY`) if there is none |
| 92 | 384 | `params` | plugin | 12 `PtsfxParam` of 32 bytes |

### 8.2 Knob (`PtsfxParam`, 32 bytes)

| Offset | Size | Field |
| ---: | ---: | :--- |
| 0 | 11 | `label` (ended by a 0) |
| 11 | 1 | `display_type` |
| 12 | 4 | `min_val` (float) |
| 16 | 4 | `max_val` (float) |
| 20 | 4 | `default_val` (float) |
| 24 | 4 | `step` (float) |
| 28 | 4 | `unit` (ended by a 0) |

### 8.3 What the firmware checks

At listing (`checkHeader`): the magic and version; known flags, not both; a name and an
author that end within their fields; at most 12 knobs and the reserved bytes 0; a state size
from 1 byte to 64 KB and a code size from 1 byte to 32 KB; the code right after the header and
ending at the file's end; every entry inside the code on an even offset; and every knob's
label, display, range, step and default. A plugin that fails is listed with the reason and
can't be picked.

---

## 9. Firmware Architecture: Memory, Loading, Guards & Storage

### 9.1 Where it runs

| Part | Where | File |
| :--- | :--- | :--- |
| `begin` (before a part's blocks are taken) and `run` (after the engine renders) | audio interrupt, `PolySynth::update` (`core/libs/Audio/synth_PolySynth.cpp`) | `synthfxMod.cpp` |
| catalog, loader, pools, guards | main loop; `process` and `reset` in the interrupt | `synthfxPlugins.cpp`, `synthfxPluginsCe.cpp` |
| slots, picks, project record | main loop | `synthfxSlots.cpp`, `synthfxSettings.cpp` |
| the page | main loop | `synthfxPage.cpp`, hooks in `SynthPatchEditor.cpp` |

The interrupt only reads what the main loop publishes. An instance's `active` byte is set
last when it is made and cleared first when it goes; on this single core the main loop never
runs while the interrupt does, so a cleared instance is never running. For a knob, the main
loop stores the value and then sets the knob's dirty byte; the interrupt clears the byte,
reads the value and calls `set_param`.

### 9.2 Memory

| Pool | Size | Where | Holds |
| :--- | ---: | :--- | :--- |
| Code pool | 96 KB | SDRAM (`EXTERNAL_RAM`), 32-byte aligned, executable | each loaded plugin's code, once |
| State pool | 192 KB | SDRAM, 32-byte aligned | each instance's state |
| Limits | | | 8 different plugins loaded, 56 instances (3 parts x 2 slots need 6; the table kept its size from Placement Pre), 64 catalog entries |

Neither pool moves a block once given, since the interrupt may be running it: first fit,
holes reused. Synth FX uses no heap. These pools are the mod's own and separate from CE's
6 MB `.psyn` pool (`PSYN_RAM_POOL_SIZE`).

### 9.3 Loading and the cache

The code is read from the card in the main loop, then the data cache is cleaned and the
instruction cache invalidated over it with CE's `PtfxLoader::syncInstructionCache`, the same
step CE's `.ptfx` and `.psyn` loaders take. Plugin code runs from SDRAM through the caches.

### 9.4 Guards

A plugin is someone else's code in the audio interrupt, so the firmware watches it:

- **Time.** Each `process` call is timed with the cycle counter. Over a quarter of a block
  (about 435,000 cycles at 600 MHz) for 8 blocks in a row, the slot is bypassed and the page
  says so. If CE's synth limit (below) trips in a block where the plugin was over that quarter,
  it is bypassed at once instead, so CE silences the synth only that once. It stays bypassed, even if a knob makes the plugin faster, until you pick the
  plugin again or open the project again.
- **Output.** NaN or infinity becomes 0, and the plugin is reset before its next block.
  Anything beyond ±4 is held at ±4.
- **Start.** A marker file, `/SynthFX/.starting`, is on the card from when a new plugin is
  first made until it has run 5 seconds. If the unit stops in that time, from a hang or a
  crash, the marker is still there at the next start. Then the plugins a project asks for
  aren't loaded until you pick a plugin yourself and it runs 5 seconds.
- A plugin that crashes or never returns can't be stopped: the start marker is for that.

CE's synth has its own guard on top: since CE's plugin-synth update, `PolySynth::update`
stops rendering the synth parts for 32 blocks (about 93 ms) when the parts together take more
than 2.1 ms of a block's 2.9 ms. The slots run before that check, so their time counts. A
heavy set of slots that stays under Synth FX's quarter-block guard can still trip CE's, and
then nothing is bypassed: CE's silences repeat until the load comes down.

### 9.5 Storage

Each part's two slots (the file name and every knob's value) are in the project's
`mods.bin`, record 16, version 3; versions 1 and 2 from earlier builds are read and
converted. Version 3 still has a Placement byte for each slot: it is written 0 and read as
Post. Nothing goes in the patch, so a patch keeps working on stock CE.

---

## 10. Real-Time Audio, CPU & Memory Guidelines

1. **Single precision.** Use `float`, `0.5f`, and the host's `sinf` and others. A `double`,
   or a literal like `0.5` without `f`, is done in software and is slow. The builder refuses
   double-precision helpers unless `--allow-double`.
2. **No globals that change.** All state is in the instance's memory. Constant tables are
   fine.
3. **No heap, no files, no screen, nothing that waits.**
4. **`reset` gives a fresh instance.** The checker checks that the next blocks are the same.
   Starting lazily on the first block, from the knobs as they are then, is the easy way.
5. **Smooth knob moves** that would click, as the Tremolo does with Depth.
6. **Hoist work out of the sample loop.** The host's functions are calls.
7. **Keep big state access local.** The state is in SDRAM behind the data cache: a big state
   read all over the place (a long delay line) costs more on the unit than in the emulator.

### Cost

Six slots can be running, two on each of three parts, beside the synths' own voices, and the
synths alone can use most of the CPU. Aim for a small fraction of a block.

Instructions per block, the checker's mean and most, over its settings, mono and stereo:

| Plugin | Mean | Most | Most, as a share of a block |
| :--- | ---: | ---: | ---: |
| Bit Crusher | 5,000 | 8,600 | 0.5 % |
| Chorus, Flanger, Phaser | 8,800 to 9,500 | 9,000 to 10,300 | 0.6 % |
| Overdrive (2x oversampled) | 32,700 | 50,100 | 2.9 % |
| Distortion (4x oversampled) | 53,600 | 78,800 | 4.5 % |
| Tremolo (example) | 1,400 | 1,600 | 0.1 % |
| Delay (example) | 8,500 | 8,700 | 0.5 % |

The most is on a stereo part, where a Distortion runs on each side. WAVE and PHASE parts
are mono, whatever their output: a slot runs once on them until a widening plugin has made
them stereo.

In the emulator, with all 8 voices on one WAVE part, the Distortion and the Bit Crusher cost
39,000 instructions a block between them (2.3 % of a block); three WAVE parts, 3 + 3 + 2
voices, each with the Distortion and the Phaser, cost 136,000 (8 %). WAVE's own cost as a
`.psyn` hasn't been measured on the unit yet; CE's 2.1 ms synth limit (section 9.4) applies
to the engine and its slots together.

Instructions aren't cycles: SDRAM and the caches add to them on the unit, where the Profiler
measures the load with all 8 voices sounding. A pass in the checker is not a pass on the
unit.

---

## 11. Checking a Plugin in the Emulator

```
python tools/synthfx/check_ptsfx.py Tremolo.ptsfx
```

The checker needs the repository's emulator rig (`tools/synthfx/rig/build.sh`, which builds
`build/emu/synthfx_rig.axf`; `--rig` names another), and Python with `numpy` and `unicorn`.
It loads the plugin with the firmware's own runtime and reports:

- the header as the firmware reads it, and each knob's text at its min, default and max;
- the output: finite and within ±4 at every knob's ends, every knob swept and random
  settings, on noise, a loud sine and silence, mono, stereo and widened;
- writes outside the plugin's state, blocks and stack (a global, or past the end of its
  state);
- `reset()` giving what a fresh instance gives;
- instructions per block, the most and the mean, against the quarter-block share.

---

## 12. Reference Implementations

- **`examples/tremolo.c`:** the smallest useful plugin. It has a list knob shown by
  `format()`, uses the host's `sinf`, smooths Depth, and starts lazily so `reset` matches a
  fresh instance.
- **`examples/delay.c`:** a short stereo delay, up to 180 ms. It has a 64 KB state, widens a
  mono part, and reads a smoothed time between samples.
- **`pack/`:** the default pack. It shares its arithmetic with the firmware's old built-in
  effects (`core/mods/synthfx/synthFxDsp.h`), and gives the same output, bit for bit
  (`tools/synthfx/test_pack.py`). Chorus, Flanger and Phaser widen. Overdrive, Distortion and
  Bit Crusher still set `PTSFX_FLAG_PRE`, which changes nothing now (section 6.2), so the
  files stay as earlier builds wrote them to the card.

---

## 13. How Synth FX Relates to CE's Plugin Systems

CE has two plugin formats of its own: `.psyn` synth engines (`docs/custom_synth_sdk.md`) and
`.ptfx` sample-editor effects (`docs/modular_effects_system.md`). Synth FX follows them where
the job is the same and differs where it isn't. Master FX (`.ptmfx`, `docs/masterfx_plugins.md`)
is in the last column; its guide says why it differs.

| | `.psyn` (CE) | `.ptfx` (CE) | `.ptsfx` (Synth FX) | `.ptmfx` (Master FX) |
| :--- | :--- | :--- | :--- | :--- |
| What it is | a synth engine | an offline sample-editor effect | a real-time effect on a synth part | a master reverb, delay or dynamics effect |
| Card folder | `/Synths/` | `/Effects/` | `/SynthFX/` | `/MasterFX/` |
| Header | 128 bytes, magic and API version | 472 bytes, magic and version | 476 bytes, magic and version | 240 bytes, magic and version |
| Host table | `PsynHostApi` (versioned) | `PtfxHostApi` | `PtsfxHost` (versioned) | `PtmfxHost` (versioned) |
| Code | relocated ELF (ABS32, MOVW/MOVT), `.data`/`.bss` and constructors allowed | position-independent | position-independent, no `.data`/`.bss` | position-independent, no `.data`/`.bss`, up to 8 KB |
| Instances | one engine, voices as objects | one run on a buffer | one code copy, state per instance (up to 56) | one plugin a slot, three slots |
| Memory | 1,920 KB arena in CE's 6 MB pool, LRU eviction | the sample editor's shared scratch | 96 KB code and 192 KB state pools, no eviction | a fixed ITCM code area, DTCM state and CE's own buffers per slot |
| Cache | data clean and instruction invalidate | `syncInstructionCache` | `syncInstructionCache` | `syncInstructionCache` |
| Runs in | audio interrupt | main loop | audio interrupt | audio interrupt, inside CE's effect objects |
| Loads | boot preload up to 1 MB, then on selection | when applied | on selection, after the encoder rests | on selection, or when the project opens |
| Guards | CE's 2.1 ms synth limit | none needed (main loop) | time, output and start marker, inside CE's limit | time, output and start marker |
| File I/O for the plugin | `read_file`, `load_wav_file` (outside the audio thread) | file parameters | none | none |
| Builder | `psyn_builder.py` from an ELF | `ptfx_builder.py` | `ptsfx_builder.py` (from `ptfx_builder.py`) | `ptmfx_builder.py` (from `ptsfx_builder.py`) |
| Shipped | CE's `polyend-tracker-ce-synths` build and `PolyendTracker_CE_Synths.zip` | CE's `polyend-tracker-ce-effects` build and `PolyendTracker_CE_Effects.zip` | the default pack is in the firmware, written when missing | the default pack is in the firmware, written when missing |

Why the differences:

- **No `.data`/`.bss`.** One `.psyn` engine is loaded once, and its voices are objects. A
  Synth FX plugin can have 56 instances at once from one code copy, so its state must be per
  instance. Relocating writable globals per instance would mean a code copy each.
- **No eviction.** The interrupt may be running any loaded plugin; a block is never moved or
  reused while an instance has it.
- **No file access.** Everything a plugin does happens in the audio interrupt or the
  screen's text, where the card can't be used.
- **The pack is in the firmware** so a fresh card has the six effects that used to be built
  in, without a separate download.
