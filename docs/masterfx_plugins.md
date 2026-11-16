# Master FX Plugins (`.ptmfx`) - Developer Guide

A Master FX plugin is a master effect in a file on the card. The Tracker+ has three master
slots: the reverb, the delay and the limiter (called the dynamics slot here). Each slot plays
CE's own effect (Stock) or one plugin from `/MasterFX/` made for that slot. The firmware copies
the plugin's code into the slot's own fast memory and runs it in the audio interrupt, once per
block of 128 samples, where CE's effect would run.

Plugins run only on the Modular-Trackular Plus firmware for the Tracker+, with the Master FX mod
on. Stock CE doesn't load `.ptmfx` files.

The SDK is [`masterfx-sdk/`](../masterfx-sdk/); the firmware's side is
[`core/mods/masterfx/`](../core/mods/masterfx/) and [`core/mods/masterfx.cpp`](../core/mods/masterfx.cpp).

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [SDK Contents](#2-sdk-contents)
3. [Plugin Lifecycle & Audio Model](#3-plugin-lifecycle--audio-model)
4. [Step-by-Step Tutorial: Building a Plugin](#4-step-by-step-tutorial-building-a-plugin)
5. [On the Master Page](#5-on-the-master-page)
6. [The Header: Slot, Flags, Labels, State & Knobs (`PTMFX_INFO`)](#6-the-header-slot-flags-labels-state--knobs-ptmfx_info)
7. [Host API Reference (`PtmfxHost`)](#7-host-api-reference-ptmfxhost)
8. [The `.ptmfx` Binary Specification](#8-the-ptmfx-binary-specification)
9. [Firmware Architecture: Memory, Loading, Guards & Storage](#9-firmware-architecture-memory-loading-guards--storage)
10. [Real-Time Audio, CPU & Memory Guidelines](#10-real-time-audio-cpu--memory-guidelines)
11. [Checking a Plugin in the Emulator](#11-checking-a-plugin-in-the-emulator)
12. [Reference Implementations](#12-reference-implementations)
13. [How Master FX Relates to CE's Plugin Systems](#13-how-master-fx-relates-to-ces-plugin-systems)

---

## 1. Architecture Overview

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│                               Tracker+ firmware                              │
│                                                                              │
│  Main loop                                                                   │
│  ┌───────────────────────────────┐   ┌────────────────────────────────────┐  │
│  │ Master page: Reverb, Delay    │   │ core/mods/masterfx/                │  │
│  │ and Limiter lists, the slots' │──▶│  catalog of /MasterFX/*.ptmfx      │  │
│  │ pages; Config > Project >     │   │  loader: a code area per slot      │  │
│  │ Master FX                     │   │  (8 KB ITCM), state (DTCM)         │  │
│  └───────────────────────────────┘   │  mods.bin record 4                 │  │
│                                      └──────────────────┬─────────────────┘  │
│                                                         │ installs           │
│  Audio interrupt, in CE's own objects:                  ▼                    │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────────────────────┐  │
│  │ reverb send  │ │ delay send   │ │ the master (and the Sidechain key)   │  │
│  │ ─▶ reverb    │ │ ─▶ delay     │ │ ─▶ dynamics slot, in CE's limiter    │  │
│  │    slot      │ │    slot      │ │    (or the gain for ducked tracks)   │  │
│  └──────────────┘ └──────────────┘ └──────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────┘
                                  ▲
                    ┌─────────────┴──────────────┐
                    │ SD card /MasterFX/*.ptmfx  │
                    │  built with ptmfx_builder  │
                    │  default pack written by   │
                    │  the firmware if missing   │
                    └────────────────────────────┘
```

### Key facts

- **One plugin a slot, in CE's place.** A slot's plugin runs inside CE's own object (the
  reverb, the delay or the limiter), on the same blocks CE's effect gets. CE's effect stays in
  the firmware: Stock in a slot is CE's, unchanged.
- **CE's knobs stay CE's.** Size, Damping, Predelay and Diffusion; PingPong, Sync, Rate, Time,
  Feedback and Filter; Attack, Release and Threshold stay in the project as on CE. The plugin
  reads them from the block every time and may rename their labels on the slot's page. It can
  add up to two knobs of its own; a dynamics plugin can have five, three of them in place of
  Attack, Release and Threshold on the page (section 3.6).
- **Fast memory, given by the slot.** The code is copied into the slot's 8 KB area in ITCM, the
  state (up to 1 KB) is in DTCM, and the slot's buffers are CE's own: the reverb's 32 KB in fast
  RAM and 32 KB in SDRAM, the delay's three 512 KB lines, and 64 KB for the dynamics slot. A
  plugin has no globals that change and no heap.
- **Position-independent code.** The slots' code areas are fixed in one firmware but move
  between builds, so the builder compiles the code position independent and checks it by
  linking it twice at different addresses.
- **Settings follow the project.** Each slot's file name and own knobs' values are in the
  project's `mods.bin` (record 4), with the dynamics slot's Ducks or Tracks list. CE's fields
  aren't touched, so the project still opens on stock CE.
- **The default pack.** Freeverb, Plate, Spring, Shimmer, "Gated" and Reverse (reverbs), Tape and
  BBD (delays), and the compressors Glue and Compressor (dynamics) are plugins too. The firmware
  carries a copy and writes each pack file to `/MasterFX/` when it is missing there, so delete
  one to get the firmware's copy back. A pack file an earlier release wrote, still byte for byte
  as it was shipped, is replaced by the new version (section 9.6); any other file of that name,
  your own or one you changed, is left alone.

---

## 2. SDK Contents

| File | Purpose |
| :--- | :--- |
| [`masterfx-sdk/ptmfx.h`](../masterfx-sdk/ptmfx.h) | The format: `PtmfxHeader` (336 bytes), `PtmfxParam` (32 bytes), the slots, the flags, `PtmfxHost`, `PtmfxMemory`, CE's knobs for each slot, `PtmfxBlock`, the entry types, and the `PTMFX_INFO`, `PTMFX_KNOB` and entry macros. The firmware's copy, `core/mods/masterfx/ptmfx.h`, is the same file (a host test checks). |
| [`masterfx-sdk/ptmfx_builder.py`](../masterfx-sdk/ptmfx_builder.py) | Builds a plugin from C or C++ sources and refuses one that breaks the rules. Built on Synth FX's `ptsfx_builder.py`, itself adapted in part from CE's `editor-fx-sdk/ptfx_builder.py`. |
| [`masterfx-sdk/ptmfx_support.c`](../masterfx-sdk/ptmfx_support.c) | `memset`, `memcpy` and `memmove`, linked into every plugin (the code has no C library). |
| [`masterfx-sdk/examples/room.c`](../masterfx-sdk/examples/room.c) | A small reverb: float blocks, both of the reverb slot's memory areas, CE's four knobs and one own knob. |
| [`masterfx-sdk/examples/ducker.c`](../masterfx-sdk/examples/ducker.c) | A sidechain ducker for the dynamics slot: a key, a renamed label, a list knob shown by `format()`, the meter, and ducking chosen tracks (`PTMFX_FLAG_DUCK`). |
| [`masterfx-sdk/pack/`](../masterfx-sdk/pack/) | The default pack's sources. |
| [`tools/masterfx/check_ptmfx.py`](../tools/masterfx/check_ptmfx.py) | Runs a plugin in the emulator with the firmware's own runtime (section 11). |
| [`tools/masterfx/build_pack.py`](../tools/masterfx/build_pack.py) | Builds the default pack, and with `--embed` the firmware's copy of it. |

You need the Arm GNU toolchain, `arm-none-eabi-gcc` (Arm's 10.3 release builds the firmware),
and Python 3.

---

## 3. Plugin Lifecycle & Audio Model

### 3.1 The entries

| Entry | Runs in | When |
| :--- | :--- | :--- |
| `PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)` | main loop | once, on zeroed state, when the plugin is put in its slot. Keep `mem` and `host`; don't write to the memory yet |
| `PTMFX_SET_PARAM(void *st, uint32_t index, float value)` | interrupt | before a block: every own knob after `init`, then each one that changed |
| `PTMFX_RESET(void *st)` | interrupt | before the first block, and after every clear of the slot's memory: start over in it |
| `PTMFX_PROCESS(void *st, const PtmfxBlock *b)` | interrupt | each block; returns the gain it applied, for the Limiter page's meter (1.0 is none; the reverb and delay return 1.0) |
| `PTMFX_FORMAT(const PtmfxHost *host, uint32_t index, float value, char *out, uint32_t size)` | main loop | optional: an own knob's text for the screen |

`init`, `set_param`, `process` and `reset` are required; the builder refuses a plugin without
them. `format` is optional.

### 3.2 Lifecycle order

1. **Catalog.** At start with the Master FX mod on (or when it is first turned on, or a
   project that has it is opened), the firmware writes the missing pack files, lists every
   `.ptmfx` in `/MasterFX/` (up to 64) and checks each header and file size.
   A file that fails stays in the list with the reason. The folder is listed again each time a
   project opens and each time the Master page opens, so a file copied to the card since shows
   then, and a project's file that was missing plays.
2. **Read.** When a slot gets a plugin (picked on the Master page or in Config, or from a
   project), its file is read into a buffer in SDRAM and checked again. What plays keeps
   playing meanwhile.
3. **Install.** CE's object for the slot is stopped. The code is copied into the slot's code
   area and the instruction cache synced over it, the state is zeroed, `init` runs, and the own
   knobs are set to the project's values (or their defaults).
4. **Clear, then play.** CE's own clear empties the slot's memory; the effect is silent for the
   clear, the dry sound isn't touched. Then `reset` runs before the first block, `set_param` for
   each knob, and `process` every block from then on.
5. **Swap or remove.** Picking another plugin or Stock stops the slot and installs the next one
   the same way. The old plugin's code and state are simply overwritten.

`reset` also runs whenever CE clears the slot's buffers (a project load, an export, the sample
editor or recorder) and after a float plugin's output wasn't a number (section 9.4).

### 3.3 Blocks

`PtmfxBlock` holds 16-bit pointers (Q15, CE's own samples) or, with `PTMFX_FLAG_FLOAT`, float
pointers (±1.0); the others are null. `frames` is 128. `knobs` points at CE's knobs for the slot,
as its page left them.

| Slot | In | Out | `knobs` |
| :--- | :--- | :--- | :--- |
| Reverb | `left`/`right` (`fleft`/`fright`): the reverb send | the return, in place | `PtmfxReverbKnobs` |
| Delay | `in` (`fin`): the mono delay send | `left`/`right`: the return, to fill | `PtmfxDelayKnobs` |
| Dynamics | `left`/`right`: the master; `key_left`/`key_right` (`fkey_*`): the Sidechain key, or null when it is off | the master, in place | `PtmfxDynamicsKnobs` |

- **The reverb's return,** as CE makes it: `send + (wet - send) * amount`. `input_gain` is CE's
  gain into the reverb.
- **A key** is the Sidechain list's pick: a track after its fader, Line in L or R, or a synth
  part. With a key, CE's limiter turns down everything but the key; Compressor and the Ducker
  do the same.
- **Float blocks** cost the host about 6,900 instructions a block for the conversions and the
  guard (section 9.4), 10,600 with a key: under 1 % of a block's time. After `process` they are
  converted back to 16 bits, held at full scale: nothing limits the output after a plugin, so
  past ±1.0 it clips.

### 3.4 Ducking chosen tracks (`PTMFX_FLAG_DUCK`)

A float dynamics plugin with `PTMFX_FLAG_DUCK` can turn down chosen tracks instead of the
master. On the Limiter page (section 5), the Ducks list picks them: sample tracks 1 to 8 and
synth parts 1 to 3. While the list has any and a key is chosen:

- `b->gain` points at 128 floats for the plugin to fill: for each frame, the factor it would
  apply to the master there, its makeup included.
- `fleft`/`fright` and `fkey_left`/`fkey_right` all hold the key. What the plugin leaves in
  `fleft`/`fright` is thrown away; the master passes through untouched.
- The host multiplies each chosen track by the gain, after its volume and after its reverb and
  delay sends are taken, so its sends aren't ducked. The gain is held within 0 to 4; a value
  that isn't a number becomes 1 and the plugin is reset.
- The gain is worked out once a block. A track whose key is made after it (a synth part as the
  key: CE makes the synth parts after the sample tracks) gets the block before's gain, 2.9 ms
  late. A sample track or the Line in as the key reaches the synth parts and the later sample
  tracks in the same block.
- `process` still returns the meter's value; the meter shows the gain's turning down.

Without a key, or with no tracks in the list, the plugin runs on the master as usual and
`b->gain` is null.

### 3.5 A bus of chosen tracks (`PTMFX_FLAG_BUS`)

A float dynamics plugin with `PTMFX_FLAG_BUS` can work on a group of tracks instead of the
master, as a bus compressor on a drum group does. Its Limiter page has the Tracks list in the
Sidechain list's column, always up (section 5): Master, sample tracks 1 to 8 and synth parts 1
to 3. With Master, the plugin runs on the master as usual. With tracks in the list:

- It runs in CE's tracks mixer, where the tracks and synth parts are summed, not on the master.
  `fleft`/`fright` hold the chosen tracks' sum as that mixer gets them: after each track's volume
  and panning, without the reverb and delay returns. With several loud tracks it can be past
  ±1.0.
- The plugin works on it in place, as it would on the master. The host adds what the plugin left
  to the other tracks' sum, and CE's mixer saturates the total as it would its own sum (the
  project's Saturation, Master 1/3). So the other tracks, the sends and the returns aren't
  touched, and the result is the same whatever the Saturation.
- There is no key: `fkey_left`/`fkey_right` are null and the Sidechain list isn't shown. `BUS`
  and `DUCK` can't be set together.
- When the bus starts (the list goes from Master to tracks), the limiter passes the master through
  and the bus begins a block later. While it runs, the limiter passes the master through and its
  meter shows the plugin's gain.
- The output is guarded as any float block's (section 9.4), but held within ±11.0 (CE's mixer's
  11 inputs at full scale) instead of ±4.0.

### 3.6 Own Attack, Release and Threshold (`PTMFX_FLAG_OWN_DYNAMICS`)

A dynamics plugin with `PTMFX_FLAG_OWN_DYNAMICS` has three to five own knobs. Knobs 0, 1 and 2
take the Limiter page's first three columns, where CE's Attack, Release and Threshold are; knobs 3
and 4 take columns 5 and 6, before Back. CE's three knobs stay in the project and in the block
(`PtmfxDynamicsKnobs`), unchanged, and the page doesn't show them, so the plugin's `labels` for
them aren't used. Without the flag a plugin has at most two own knobs. Glue uses it with Attack,
Release and Threshold of its own ranges, Ratio and Makeup.

---

## 4. Step-by-Step Tutorial: Building a Plugin

### Step 1: Write the plugin (`ducker.c`)

The heart of [`examples/ducker.c`](../masterfx-sdk/examples/ducker.c), shortened:

```c
#include "ptmfx.h"

typedef struct
{
    const PtmfxHost *host;
    float depth;                            /* 0 to 1: how far down a full duck goes */
    uint32_t smooth;                        /* the curve: 0 linear, 1 smooth */
    float attackMs, releaseMs, threshold;   /* CE's knobs as last seen */
    float attack, release, inverse;         /* worked out from them */
    float level;                            /* the follower */
} Ducker;

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK, /* magic and version (the builder's), flags */
    PTMFX_SLOT_DYNAMICS, 2, {0, 0},         /* slot, own knobs, reserved */
    "Ducker", "MT+ examples",
    {"", "", "Trigger"},                    /* CE's Attack and Release as they are; Threshold renamed */
    sizeof(Ducker),
    0, 0, 0, 0, 0, 0, 0,                    /* the code's place and the entries (the builder's) */
    {0, 0, 0},
    {
        PTMFX_KNOB("Depth", 0, 100, 60, 1, "%", PTMFX_PARAM_INT),
        PTMFX_KNOB("Curve", 0, 1, 1, 1, "", PTMFX_PARAM_LIST),
    },
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    Ducker *d = (Ducker *)st;               /* state is zeroed */
    d->host = host;                         /* keep it for process() */
    d->attackMs = d->releaseMs = d->threshold = -1.0f;   /* worked out on the first block */
}

PTMFX_RESET(void *st)                       /* the slot starts over */
{
    ((Ducker *)st)->level = 0.0f;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    /* CE's Attack, Release and Threshold from b->knobs, when they change;
       then per frame: follow the key (or the master), and write the gain into b->gain when it
       is set, or turn the master down by it; return the block's mean gain for the meter */
}
```

The fields of `PTMFX_INFO` in order: magic and version (`0, 0`, the builder's), the flags, the
slot, the number of own knobs, two reserved bytes, the name, the author, the labels for CE's
knobs, the state size, seven zeros (the builder fills in the code's place and the entries'
offsets), three reserved words, and the own knobs.

### Step 2: Build it

```
python masterfx-sdk/ptmfx_builder.py masterfx-sdk/examples/ducker.c -o Ducker.ptmfx --toolchain <prefix>
```

`<prefix>` is the toolchain's path and name prefix, such as
`/opt/arm-gnu-toolchain/bin/arm-none-eabi-`; leave `--toolchain` out if `arm-none-eabi-gcc` is
on the path. Several sources can be given, C or C++. Other options:

| Option | What it does |
| :--- | :--- |
| `-o`, `--output` | The `.ptmfx` file to write (required) |
| `--toolchain` | The tools' prefix |
| `-I`, `-D` | Include folders and defines, as for the compiler |
| `--allow-double` | Accepts calls to double-precision helpers (slow; see section 10) |

The builder compiles for the Tracker+ (`-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard
-mthumb`, `-O2`, `-fPIC`), links the code twice at different addresses, and refuses the
plugin if:

- an entry is missing, or `PTMFX_INFO` is;
- it has writable globals (`.data`, `.bss` or a GOT);
- its code differs between the two links, so it wouldn't run where it is loaded;
- it calls double-precision helpers, unless `--allow-double`;
- its code and constants are over 8 KB, the slot's code area;
- the header's fields are out of range (section 6).

### Step 3: Check it in the emulator

```
python tools/masterfx/check_ptmfx.py Ducker.ptmfx
```

Section 11 says what it checks.

### Step 4: Put it on the card

Copy `Ducker.ptmfx` to `/MasterFX/` on the card. The file name is what a project stores, so
keep it once a project uses the plugin. The header's name is what the lists show. Then, with
the Master FX mod on, pick it in the Master page's Limiter column, or in Config > Project
Settings > Master FX.

---

## 5. On the Master Page

The user's guide, with pictures of each page: [docs/mods/masterfx.md](mods/masterfx.md). With the
Master FX mod on:

- **The Master page** has three lists, Reverb, Delay and Limiter, after Volume. Each has Stock
  (CE's effect), then every plugin in `/MasterFX/` made for that slot that loaded cleanly, by
  its header's name. A pick takes effect at once; the slot's effect is silent while its memory
  is cleared. Config > Project Settings > Master FX has the same three lists. Under each column the
  choice's name is cut to what fits in the column's 94 px, so keep names short. A project's file
  that isn't on the card reads `Missing` in the list and under the column; the slot's page title
  names the file.
- **The slot's page** (the Reverb, Delay or Limiter button) is CE's, with the plugin's labels in
  place of CE's where it gives them. Its title says what plays: `Reverb: Shimmer`,
  `Limiter: Glue (bypassed)`, `Limiter: Missing: Gone.ptmfx`.
- **Own knobs** sit in columns 5 and 6 of the Reverb and Limiter pages, and with
  `PTMFX_FLAG_OWN_DYNAMICS` in the Limiter page's columns 0 to 2 too: the label, the text (the
  plugin's `format()`, else the value and the unit), and a bar. The buttons under the columns
  pick them, the encoder and Up/Down move one step a tick, and Delete puts a knob back to its
  default. The Delay page has no free column: a delay plugin's own knobs stay at the project's
  values, or their defaults, so give a delay plugin defaults that work.
- **The Ducks list** (section 3.4): in column 3 of the Limiter page, always up while a plugin
  with `PTMFX_FLAG_DUCK` plays; CE's Sidechain list stays in column 4, and CE's Gain Reduce meter
  moves above Back (labelled `Gain Reduce` over `Back`). It lists Master, Track 1 to 8 and
  Synth 1 to 3. The button under the column selects it, then marks or clears the item under the
  cursor (Master clears them all), Delete goes back to Master, and the column's label says
  `Master`, the one track, or `3 tracks`.
- **The Tracks list** (section 3.5) is in column 4, in CE's Sidechain list's place, always up,
  while a plugin with `PTMFX_FLAG_BUS` plays (a bus plugin has no key). It lists the same items
  and works the same way; Gain Reduce stays in column 3.
  Picking another plugin in the Limiter slot sets the Ducks or Tracks list back to Master.
- **A project stores** each slot's file name and its own knobs' values, and the Ducks or Tracks
  list. A
  file that is missing or refused plays Stock meanwhile, and the slot's title says why; it is
  looked for again when the project is next opened or the Master page next opens.
- **A plugin stopped by the time guard** (section 9.4) reads `Bypassed` under the slot's column
  on the Master page, and its page's title says so. It stays bypassed until it is installed
  again: pick another choice in the slot and then the plugin, or turn the mod off and on.
  Opening a project that uses the same plugin keeps the plugin that is installed, bypassed.
- **Off,** every slot plays CE's effect, and the project's choices are kept.

A card that has the pack from an earlier release gets the new version of each file that is
still as that release shipped it (section 9.6). A file you changed, or your own of that name,
is kept.

---

## 6. The Header: Slot, Flags, Labels, State & Knobs (`PTMFX_INFO`)

### 6.1 Fields you fill in

| Field | Limits |
| :--- | :--- |
| `flags` | `PTMFX_FLAG_FLOAT`; `PTMFX_FLAG_DUCK` (with `FLOAT`, dynamics only); `PTMFX_FLAG_BUS` (with `FLOAT`, dynamics only, not with `DUCK`); `PTMFX_FLAG_OWN_DYNAMICS` (dynamics only, with 3 knobs or more); or 0 |
| `slot` | `PTMFX_SLOT_REVERB`, `PTMFX_SLOT_DELAY` or `PTMFX_SLOT_DYNAMICS` |
| `num_params` | 0 to 2 own knobs; 3 to 5 with `PTMFX_FLAG_OWN_DYNAMICS` |
| `name` | up to 31 characters; shown in the slot's list, under its column on the Master page (cut to the column's 94 px) and in the page's title |
| `author` | up to 15 characters |
| `labels` | one for each of CE's knobs on the slot's page, up to 11 characters; empty keeps CE's |
| `state_size` | 1 byte up to 1 KB (reverb) or 512 bytes (delay, dynamics) |
| `params` | the own knobs, `PTMFX_KNOB(label, min, max, default, step, unit, display)` |

The code and its constants may be up to 8 KB.

### 6.2 Flags (`PTMFX_FLAG_*`)

| Flag | Value | Meaning |
| :--- | :--- | :--- |
| `PTMFX_FLAG_FLOAT` | `0x0001` | Float blocks (±1.0) instead of CE's 16-bit ones. The host converts in and out and guards the output (section 9.4). |
| `PTMFX_FLAG_DUCK` | `0x0002` | A dynamics plugin with `FLOAT` that can duck chosen tracks: it fills `b->gain` when that is set (section 3.4). |
| `PTMFX_FLAG_OWN_DYNAMICS` | `0x0004` | A dynamics plugin whose own knobs 0 to 2 take the Limiter page's Attack, Release and Threshold columns (section 3.6). |
| `PTMFX_FLAG_BUS` | `0x0008` | A dynamics plugin with `FLOAT` that can work on a bus of chosen tracks, picked in the Tracks list (section 3.5). |

### 6.3 Labels

`labels[i]` is the slot page's column `i`, in the page's order:

| Slot | Columns |
| :--- | :--- |
| Reverb | Size, Damping, Predelay, Diffusion |
| Delay | PingPong, Sync, Rate, Time, Feedback, Filter |
| Dynamics | Attack, Release, Threshold |

A label past the slot's columns must be empty. A label only renames: the knob's value and its
text on the page stay CE's.

### 6.4 CE's knobs (`b->knobs`)

| Struct | Field | What it is |
| :--- | :--- | :--- |
| `PtmfxReverbKnobs` | `amount` | how much of the return is wet (see 3.3) |
| | `input_gain` | CE's gain into the reverb |
| | `size` | Size, 0 to 1 |
| | `diffusion` | Diffusion, 0 to 0.85 |
| | `lp` | 1 - Damping, 0 to 1 |
| | `predelay` | Predelay in samples, 0 to 5,000 |
| `PtmfxDelayKnobs` | `time` | the delay in samples (Time, or Sync and Rate) |
| | `filter_pole` | Filter: -pole x 1023 |
| | `feedback` | Feedback, 0 to 252 |
| | `pingpong` | 0 or 1 |
| `PtmfxDynamicsKnobs` | `attack`, `release` | Attack and Release in ms |
| | `threshold` | Threshold, 0 to 1 of full scale |
| | `key_on` | 1 when the Sidechain list has a key |

### 6.5 Own knobs (`PtmfxParam`)

- a label of up to 10 characters;
- a min, a max and a step over 0. Max - min must be a whole number of steps, at most 100,000
  of them;
- a default, on a step;
- a unit of up to 3 characters, shown after the value;
- a display:

| Display | Value | Shown as |
| :--- | :--- | :--- |
| `PTMFX_PARAM_FLOAT` | `'f'` | the value with one decimal |
| `PTMFX_PARAM_INT` | `'d'` | the value as a whole number |
| `PTMFX_PARAM_LIST` | `'l'` | whole steps from min to max, each shown by `format()` |

The host keeps every knob on its steps: its value is always `min + i * step` for a whole `i`,
rounded once to single precision (`fmaf(i, step, min)`). Out-of-range values are held at the
ends. These are Synth FX's rules; a knob is the same 32 bytes as a `PtsfxParam`.

---

## 7. Host API Reference (`PtmfxHost`)

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
| `print` | `int (*)(char *out, uint32_t size, const char *fmt, float value)` | `snprintf` with one float, such as `"%.1f:1"`; for `format()` only (the main loop) |
| `tempo` | `float (*)(void)` | the project's tempo in BPM |

The memory (`PtmfxMemory`, given to `init`):

| Slot | `area[0]` | `area[1]` | `area[2]` |
| :--- | :--- | :--- | :--- |
| Reverb | 32 KB, fast RAM (DTCM) | 32 KB, SDRAM | none |
| Delay | 512 KB, SDRAM | 512 KB, SDRAM | 512 KB, SDRAM |
| Dynamics | 64 KB, SDRAM | none | none |

`size[i]` gives each area's bytes; an unused area is null with size 0. The host clears every
area before `reset`. The reverb's and delay's areas are CE's own buffers for those effects,
which Stock uses when it plays.

The host's functions are calls into the firmware: use them once per block or per few samples,
not per sample, where that is enough. There is no file, screen, memory or MIDI access. A plugin
that needs a table brings it as constant data in its code.

---

## 8. The `.ptmfx` Binary Specification

A `.ptmfx` file is the 336-byte header, then the code. Everything is little-endian. Format
version 1's header was 240 bytes, with 2 knobs; the firmware still reads it.

### 8.1 Header (`PtmfxHeader`, 336 bytes)

| Offset | Size | Field | Set by | Meaning |
| ---: | ---: | :--- | :--- | :--- |
| 0 | 4 | `magic` | builder | `0x464D5450` (`"PTMF"`) |
| 4 | 2 | `format_version` | builder | 2 |
| 6 | 2 | `flags` | plugin | `PTMFX_FLAG_*` |
| 8 | 1 | `slot` | plugin | 1 reverb, 2 delay, 3 dynamics |
| 9 | 1 | `num_params` | plugin | 0 to 5 |
| 10 | 2 | `reserved` | plugin | 0 |
| 12 | 32 | `name` | plugin | ended by a 0 |
| 44 | 16 | `author` | plugin | ended by a 0 |
| 60 | 72 | `labels` | plugin | 6 labels of 12 bytes, each ended by a 0 |
| 132 | 4 | `state_size` | plugin | bytes of state |
| 136 | 4 | `code_offset` | builder | where the code starts in the file: 336 |
| 140 | 4 | `code_size` | builder | up to 8 KB |
| 144 | 4 | `init_offset` | builder | from the code's start, Thumb bit clear |
| 148 | 4 | `set_param_offset` | builder | |
| 152 | 4 | `process_offset` | builder | |
| 156 | 4 | `reset_offset` | builder | |
| 160 | 4 | `format_offset` | builder | `0xFFFFFFFF` (`PTMFX_NO_ENTRY`) if there is none |
| 164 | 12 | `reserved2` | plugin | 0 |
| 176 | 160 | `params` | plugin | 5 `PtmfxParam` of 32 bytes |

### 8.2 Knob (`PtmfxParam`, 32 bytes)

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

At listing and again when it reads a file (`checkHeader`): the magic and version 1 or 2; known
flags (version 1: `FLOAT` and `DUCK` only), `DUCK` and `BUS` only with `FLOAT` on a dynamics
plugin and not together, `OWN_DYNAMICS` only on a dynamics plugin with 3 knobs or more; a slot
from 1 to 3; a name and an author that end within their fields; every label ended, and none
past the slot's columns; at most 2 knobs (5 with `OWN_DYNAMICS`) and the reserved fields 0; a state size from 1 byte to the slot's most and a code size from 1
byte to 8 KB; the code right after the header and ending at the file's end; every entry inside
the code on an even offset; and every knob's label, display, range, step and default. A plugin
that fails is listed with the reason and can't be picked. A file in the wrong slot's list is
refused when read.

---

## 9. Firmware Architecture: Memory, Loading, Guards & Storage

### 9.1 Where it runs

| Part | Where | File |
| :--- | :--- | :--- |
| the reverb slot | audio interrupt, `AudioEffectPolyverb::update` | `core/libs/Audio/effect_polyverb.cpp`, `core/mods/masterfxDsp.cpp` |
| the delay slot | audio interrupt, CE's delay object | `core/libs/Audio/effect_shortDelay.cpp`, `masterfxDsp.cpp` |
| the dynamics slot | audio interrupt, `AudioEffectLimiter::update` (the left object runs the plugin once on both sides) | `core/libs/Audio/effect_limiter.cpp`, `masterfxDsp.cpp` |
| ducked tracks | audio interrupt, `AudioAmplifier::update` on the tracks' and synth parts' panning amplifiers | `core/libs/Audio/amplifier.cpp`, `masterfxDsp.cpp` |
| a bus of tracks | audio interrupt, `AudioMixer::update` of CE's tracks mixer (the left object runs the plugin once on both sides) | `core/libs/Audio/mixer.cpp`, `masterfxDsp.cpp` |
| catalog, loader, guards | main loop; `process` and `cleared` in the interrupt | `core/mods/masterfx/masterfxPlugins.cpp`, `masterfxPluginsCe.cpp` |
| choices, record, pages | main loop | `core/mods/masterfx.cpp`, hooks in `core/interface/masterParams/` |

The interrupt only reads what the main loop publishes. A slot's `active` byte is set last when
a plugin is installed and cleared first when it goes, and CE's object for the slot is stopped
meanwhile; on this single core the main loop never runs while the interrupt does. For an own
knob, the main loop stores the value and then sets the knob's dirty byte; the interrupt clears
the byte, reads the value and calls `set_param`.

### 9.2 Memory

| What | Size | Where |
| :--- | ---: | :--- |
| Code areas | 8 KB a slot | ITCM, executable, 32-byte aligned |
| State | reverb 1 KB, delay and dynamics 512 B | DTCM |
| The slot's memory | section 7's table | CE's reverb and delay buffers; the dynamics slot's own 64 KB in SDRAM |
| Float blocks | 2 KB, shared | DTCM |
| Load buffer | 8,432 B (a header and 8 KB of code) | SDRAM |
| Catalog, slots' records | 64 files | SDRAM |

Master FX uses no heap. Nothing moves while a slot plays: a slot has one plugin and fixed
memory, so there is no pool to share or evict.

### 9.3 Loading and the cache

The file is read from the card in the main loop into the load buffer while the slot plays on.
Then, with the slot stopped, the code is copied into the slot's code area and the data cache
cleaned and the instruction cache invalidated over it, the step CE's `.ptfx` and `.psyn`
loaders take. The code then runs from ITCM, the state from DTCM, as CE's own effects do.

### 9.4 Guards

A plugin is someone else's code in the audio interrupt, so the firmware watches it:

- **Time.** Each `process` call is timed with the cycle counter. Over an eighth of a block
  (about 217,000 cycles at 600 MHz) for 8 blocks in a row, the slot is bypassed and its page
  says so. A bypassed reverb or delay returns silence; a bypassed dynamics slot passes the
  master through. It stays bypassed, even if a knob makes the plugin faster, until it is
  installed again (section 5).
- **Output (float plugins).** NaN or infinity becomes 0, and the plugin is reset before its next
  block. Anything beyond ±4 is held at ±4 (±11 for a bus, section 3.5). Then the block goes back to 16 bits, held at full
  scale. A 16-bit plugin's output can't be either.
- **The ducking gain** is held within 0 to 4; NaN becomes 1, and the plugin is reset.
- **The meter's value** is held within 0 to 1; one that isn't a number reads as 1.
- **Start.** A marker file, `/MasterFX/.starting`, is on the card from when a plugin is first
  installed until it has run 5 seconds. If the unit stops in that time, from a hang or a crash,
  the marker is still there at the next start. Then the plugins a project asks for aren't
  loaded until you pick a plugin yourself and it runs 5 seconds.
- A plugin that crashes or never returns can't be stopped: the start marker is for that.

`tools/masterfx/test_guards.py` makes each guard act in the emulator, with the firmware's own
runtime and test plugins built to trip them (`tools/masterfx/device/`): a slow plugin bypassed after
exactly 8 blocks, bad output and gains fixed and the plugin reset, a hang with the marker on the card
and the next start in safe mode.

CE's 2.1 ms synth limit counts only the synth parts' rendering, so the master slots are outside
it. Their time still comes out of the same block.

### 9.5 Storage

Each slot's file name (up to 47 characters) and five own knobs' values, and the dynamics
slot's Ducks or Tracks list, are in the project's `mods.bin`, record 4, version 3 (208 bytes).
Version 2 (Master FX 2.0, two own knobs a slot) is read, its knobs as knobs 0 and 1. Version 1,
from builds with the effects inside the firmware, is read and its choices mapped to the pack's
files; so is 1.2.0's `masterfx.bin`. CE's knobs stay in CE's own fields.

### 9.6 The default pack on the card

At start the firmware goes through the pack it carries. A file missing from `/MasterFX/` is
written. A file there that is byte for byte one an earlier release shipped (its size and FNV-1a
hash, from the list in `masterfxPack.inc`), and isn't this release's, is replaced: the new file
is written as `<name>.new` and read back, then the old one is removed and the new one renamed.
If the read-back differs, the `.new` file is removed and the old file kept. A `.new` left by a
power cut is removed at the next start. Any other file of that name is left alone.

---

## 10. Real-Time Audio, CPU & Memory Guidelines

1. **Single precision.** Use `float`, `0.5f`, and the host's `sinf` and others. A `double`,
   or a literal like `0.5` without `f`, is done in software and is slow. The builder refuses
   double-precision helpers unless `--allow-double`.
2. **No globals that change.** All state is in the state and the slot's memory. Constant tables
   are fine.
3. **No heap, no files, no screen, nothing that waits.**
4. **`reset` starts over.** The memory has just been cleared; put back everything in the state
   that depends on it (positions, filters, followers). The checker checks the next blocks are a
   fresh plugin's.
5. **Don't write the memory in `init`.** It is cleared after `init`, before `reset`.
6. **Read CE's knobs every block,** and work out coefficients only when they change, as the
   examples do.
7. **Smooth what would click** when a knob moves.
8. **Hoist work out of the sample loop.** The host's functions are calls.
9. **16-bit blocks are cheaper.** Float blocks cost the host about 6,900 instructions a block,
   and a plugin's own float code needs no shifts. The pack's reverbs and delays use 16-bit
   blocks because the effects they replace did, bit for bit.
10. **Mind the clipping.** Nothing limits the master after the dynamics slot: a makeup gain
    past full scale clips.

### Cost

The three slots run beside the sample tracks and the synths, which can use most of the CPU.
Aim for a small fraction of a block.

The pack and the examples, from `check_ptmfx.py` (instructions in the plugin per block, the
most over every setting it tries; the share of the time guard's eighth of a block and of a
whole block, at 600 MHz):

| Plugin | Slot | Blocks | Code | State | Instructions | Guard | Block |
| :--- | :--- | :--- | ---: | ---: | ---: | ---: | ---: |
| Freeverb | reverb | 16-bit | 2,568 B | 292 B | 44,810 | 20.6 % | 2.6 % |
| Plate | reverb | 16-bit | 2,448 B | 388 B | 39,028 | 17.9 % | 2.2 % |
| Spring | reverb | 16-bit | 1,920 B | 408 B | 34,042 | 15.6 % | 2.0 % |
| Shimmer | reverb | 16-bit | 2,928 B | 352 B | 48,623 | 22.3 % | 2.8 % |
| "Gated" | reverb | 16-bit | 2,432 B | 752 B | 41,047 | 18.9 % | 2.4 % |
| Reverse | reverb | 16-bit | 2,416 B | 752 B | 41,038 | 18.9 % | 2.4 % |
| Tape | delay | 16-bit | 1,768 B | 100 B | 26,675 | 12.3 % | 1.5 % |
| BBD | delay | 16-bit | 2,368 B | 224 B | 34,680 | 15.9 % | 2.0 % |
| Glue | dynamics | float | 1,632 B | 48 B | 6,155 | 2.8 % | 0.4 % |
| Compressor | dynamics | float | 1,288 B | 52 B | 8,191 | 3.8 % | 0.5 % |
| Room (example) | reverb | float | 832 B | 132 B | 26,543 | 12.2 % | 1.5 % |
| Ducker (example) | dynamics | float | 784 B | 40 B | 6,825 | 3.1 % | 0.4 % |

The host adds about 6,900 instructions a block around a float plugin (10,600 for a dynamics
plugin with a key), counted in the emulator, and a few dozen around a 16-bit one. A bus
(section 3.5) adds about 500 more in CE's tracks mixer with one track, and about 1,000 for each
further track that plays (estimated). The time
guard doesn't count those.

Instructions aren't cycles: the caches and SDRAM add to them on the unit, where the Profiler
measures the load with all 8 voices sounding. A pass in the checker is not a pass on the unit.

---

## 11. Checking a Plugin in the Emulator

```
python tools/masterfx/check_ptmfx.py Ducker.ptmfx
```

The checker needs a Tracker+ Modular-Trackular firmware image with the plugins, as `make plus`
builds it (`platforms/imx/build-PLUS/TrackerPlus.axf`; `--firmware` names another), and Python
with `unicorn`. It puts the plugin on a fake card, and the firmware's own runtime lists it,
reads it and installs it. Then it runs blocks through the runtime's `process`, as the slot's
hook does, and reports:

- the header as the firmware reads it (a refused file says why), the labels, and each own knob's
  text at its min, default and max;
- the output on noise, a loud sine and silence, at several of CE's knob settings and the own
  knobs' defaults, ends and random steps, then each own knob swept while it plays; for the
  dynamics slot without a key, with one, and ducking tracks. A float plugin's output must be
  finite and within ±4, and a ducking gain within 0 to 4, before the firmware's guard changes
  them; output past ±1.0 is noted, since it clips;
- writes by the plugin's code outside its state, the slot's memory, its output blocks and its
  own stack (past the end of its state, or into the host's). A stray write that happens to land
  inside one of those can't be told apart;
- after CE's clear (the memory cleared, then `reset`), the same output as a fresh plugin;
- instructions per block in the plugin, the most and the mean, against the time guard's eighth
  of a block. It stops at the first block over the guard, or after 20 blocks of bad output.

A plugin's check takes a few seconds to half a minute. The checker was tried on a planted fault
of each kind (a write past the state, output that isn't a number, a gain left unfilled, a
`reset` that keeps a level, a slow loop, a state size of 0), and caught each one.

---

## 12. Reference Implementations

- **`examples/room.c`:** a small reverb, four combs and two all-passes a side. Float blocks; the
  combs in the reverb slot's fast area, the predelay in its SDRAM area; CE's Size, Damping,
  Predelay and Diffusion; Width as an own knob.
- **`examples/ducker.c`:** a sidechain ducker for the dynamics slot. It follows the key (or the
  master) with CE's Attack and Release, renames Threshold to Trigger, has a list knob shown by
  `format()`, returns the meter's value, and fills the gain to duck chosen tracks.
- **`pack/`:** the default pack. The reverbs and delays give the same output, bit for bit, as
  the effects that were inside the firmware before (`tools/masterfx/test_pack.py`), with
  16-bit blocks. `glue.cpp` is a bus compressor with float blocks, its own Attack, Release and
  Threshold (`PTMFX_FLAG_OWN_DYNAMICS`), Ratio and Makeup, a soft knee and a linked stereo
  detector, on the master or a bus of chosen tracks (`PTMFX_FLAG_BUS`). `compressor.cpp` (Master
  FX 2.0's Glue) is a compressor with CE's Attack, Release and Threshold, Ratio and Makeup, and it
  can duck tracks.

---

## 13. How Master FX Relates to CE's Plugin Systems

CE has two plugin formats of its own: `.psyn` synth engines (`docs/custom_synth_sdk.md`) and
`.ptfx` sample-editor effects (`docs/modular_effects_system.md`). Modular-Trackular Plus adds
Synth FX (`.ptsfx`, `docs/synthfx_plugins.md`) and Master FX. Master FX follows them where the
job is the same and differs where it isn't.

| | `.psyn` (CE) | `.ptfx` (CE) | `.ptsfx` (Synth FX) | `.ptmfx` (Master FX) |
| :--- | :--- | :--- | :--- | :--- |
| What it is | a synth engine | an offline sample-editor effect | a real-time effect on a synth part | a master reverb, delay or dynamics effect |
| Card folder | `/Synths/` | `/Effects/` | `/SynthFX/` | `/MasterFX/` |
| Header | 128 bytes, magic and API version | 472 bytes, magic and version | 476 bytes, magic and version | 336 bytes, magic and version |
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

- **Fixed, fast memory.** A master effect runs every block, on the master, in the slot CE's own
  effect used. So the plugin gets what CE's effect had: code in ITCM, state in DTCM and CE's
  buffers. Its code area is fixed, so a plugin has 8 KB of code and constants.
- **One plugin a slot.** There is one master: no instances, no pools, nothing to evict.
- **CE's knobs, not the plugin's.** The slots keep CE's pages and project fields, so a project
  opens on stock CE with its sound as close as CE can make it, and a plugin adds at most two
  knobs of its own where the page has room.
- **No file access.** Everything a plugin does happens in the audio interrupt or the screen's
  text, where the card can't be used.
- **The pack is in the firmware** so a fresh card has the effects that used to be built in,
  without a separate download.
