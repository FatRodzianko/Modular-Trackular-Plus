# Synth FX plugins (`.ptsfx`)

A Synth FX plugin is an effect for the synth parts, in a file on the card. Each synth part has
two effect slots, FX1 and FX2. Each slot picks a plugin from `/SynthFX/`. The firmware loads
the plugin's code into its own memory and runs it in the audio interrupt, once per block of 128
samples, on the part's audio.

The six effects that used to be built in (Overdrive, Distortion, Bit Crusher, Chorus, Flanger,
Phaser) are plugins too: the default pack. The firmware writes each pack file to `/SynthFX/`
when it is missing there, so delete one to get the firmware's copy back.

Status: the runtime (loading, the pools, the guards, the pack) is in the firmware. The two slots
on the synth page arrive with the next phase (`docs/mt/CE_PORT_PLAN.md` §3.17).

## What you need

- The Arm GNU toolchain, `arm-none-eabi-gcc` (Arm's 10.3 release builds the firmware), and
  Python 3.
- `synthfx-sdk/`: `ptsfx.h` (the format), `ptsfx_builder.py` (builds a plugin),
  `ptsfx_support.c` (memset, memcpy and memmove, linked into every plugin), the examples in
  `examples/` and the default pack's sources in `pack/`.

## Building one

```
python synthfx-sdk/ptsfx_builder.py synthfx-sdk/examples/tremolo.c -o Tremolo.ptsfx --toolchain <prefix>
```

`<prefix>` is the toolchain's path and name prefix, such as
`/opt/arm-gnu-toolchain/bin/arm-none-eabi-`; leave `--toolchain` out if `arm-none-eabi-gcc` is on
the path.

Copy the `.ptsfx` file to `/SynthFX/` on the card. The file name is what the firmware stores in a
project, so keep it once a project uses the plugin. The header's name is what the list shows.

The builder compiles for the Tracker+ (Cortex-M7, single-precision FPU) as position-independent
code, links it twice at different addresses, and refuses the plugin if:

- an entry is missing, or `PTSFX_INFO` is;
- it has writable globals (`.data`, `.bss` or a GOT);
- its code differs between the two links, so it wouldn't run where it is loaded;
- it calls double-precision helpers (slow in software), unless `--allow-double`;
- the header's fields are out of range (see below).

## The rules

The code runs in the audio interrupt, beside the synth engines:

- **Single precision.** Use `float`, `0.5f`, and the host's `sinf` and others. A `double`, or a
  literal like `0.5` without `f`, is done in software and is slow.
- **No globals that change.** All of a plugin's state is in the memory the host gives each
  instance: `state_size` bytes, zeroed before `init`. One plugin's code serves every slot and
  voice that uses it. Constant tables are fine.
- **No heap, no files, no screen, nothing that waits.**
- **Finish quickly.** See the guards and the cost below.

## The file

`ptsfx.h` has the full layout. You fill in the header with `PTSFX_INFO`; the builder sets the rest.

```c
PTSFX_INFO = {
    0, 0, flags, "Name", "Author", knobs, {0, 0, 0}, sizeof(State), 0, 0, 0, 0, 0, 0, 0,
    { PTSFX_KNOB("Rate", 0.1, 20, 4, 0.1, "Hz", PTSFX_PARAM_FLOAT), ... },
};
```

- **Name** up to 31 characters, **author** up to 15.
- **Flags:**
  - `PTSFX_FLAG_WIDENS`: the effect makes stereo from mono, as a chorus does. On a mono part
    the host gives `R` for output, with `in_channels` 1.
  - `PTSFX_FLAG_PRE`: the effect may run in each voice of WAVE, before the filter
    (Placement Pre). There it is mono and `R` is null.
  - A plugin can't have both.
- **State:** 1 byte to 64 KB per instance.
- **Code:** up to 32 KB.
- **Knobs:** up to 12. Each has:
  - a label of up to 10 characters;
  - a min, a max and a step over 0. Max - min must be a whole number of steps, at most
    100,000 of them;
  - a default, on a step;
  - a unit of up to 3 characters;
  - a display type: `PTSFX_PARAM_FLOAT` (one decimal), `PTSFX_PARAM_INT` (a whole number)
    or `PTSFX_PARAM_LIST` (whole steps, each shown by `format()`).

  The host keeps every knob on its steps: its value is `min + i * step`, rounded once to
  single precision (`fmaf(i, step, min)`).

## The entries

| Entry | Runs in | When |
|---|---|---|
| `PTSFX_INIT(void *st, const PtsfxHost *host)` | main loop | once per instance, on zeroed state |
| `PTSFX_SET_PARAM(void *st, uint32_t index, float value)` | interrupt | before a block: every knob after `init`, then each knob that changed |
| `PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)` | interrupt | each block, in place; `frames` is 128 |
| `PTSFX_RESET(void *st)` | interrupt | the part starts again, a new note on a Pre slot, or after bad output |
| `PTSFX_FORMAT(const PtsfxHost *host, uint32_t index, float value, char *out, uint32_t size)` | main loop | optional: a knob's text for the screen |

- **`process`:** `R` is null on a mono part (and in a voice). With `R` given, `in_channels`
  says whether `R` holds input (2) or is only for output (1, a widening plugin on a mono
  part).
- **`reset`** must leave the instance as a fresh one would be: `check_ptsfx.py` checks the next
  blocks are the same. Starting lazily on the first block, from the knobs as they are then,
  is the easy way. The pack and the Tremolo do it.
- **`format`:** leave `out` empty and the host shows the value and the unit.

The host table, `PtsfxHost`, gives:

- the sample rate (44,100) and the block (128);
- `sinf`, `cosf`, `tanf`, `tanhf`, `expf`, `logf`, `powf`, `sqrtf`, `fabsf`, `fminf`, `fmaxf`;
- `random`, 32 fast random bits;
- `print`, `snprintf` with one float, for `format()` only.

Keep the `host` pointer in your state at `init` if `process` needs it.

## The guards

A plugin is someone else's code in the audio interrupt, so the firmware watches it:

- **Time.** Each `process` call is timed with the cycle counter. Over a quarter of a block
  (about 435,000 cycles at 600 MHz) for 8 blocks in a row, the slot is bypassed and the page
  says so.
- **Output.** NaN or infinity becomes 0, and the plugin is reset before its next block.
  Anything beyond ±4 is held at ±4.
- **Start.** A marker file, `/SynthFX/.starting`, is on the card from when a new plugin is first
  made until it has run 5 seconds. If the unit stops in that time, from a hang or a crash,
  the marker is still there at the next start. Then the plugins a project asks for aren't
  loaded until you pick a plugin yourself and it runs 5 seconds.
- A plugin that crashes or never returns can't be stopped: the start marker is for that.

## Cost

Six slots can be running, two on each of three parts, beside the synths' own voices, and the
synths alone can use most of the CPU. Aim for a small fraction of a block:

Instructions per block, the checker's mean and most, over its settings, mono and stereo:

| Plugin | Mean | Most | Most, as a share of a block |
|---|---|---|---|
| Bit Crusher | 5,000 | 8,600 | 0.5 % |
| Chorus, Flanger, Phaser | 8,800 to 9,500 | 9,000 to 10,300 | 0.6 % |
| Overdrive | 32,700 | 50,100 | 2.9 % |
| Distortion (8x oversampled) | 53,600 | 78,800 | 4.5 % |
| Tremolo (example) | 1,400 | 1,600 | 0.1 % |
| Delay (example) | 8,500 | 8,700 | 0.5 % |

The most is on a stereo part, where a Distortion runs on each side.

Costs inside a plugin:

- the host's functions are calls: once per block or per 16 samples, not per sample, where
  that is enough (the Tremolo works out its gain every 16 samples);
- the state is in SDRAM, behind the data cache: a big state read all over the place (a long
  delay line) costs more on the unit than in the emulator.

## Checking one

```
python tools/synthfx/check_ptsfx.py Tremolo.ptsfx
```

The checker needs the repository's emulator rig: `tools/synthfx/rig/build.sh`, and Python with
`numpy` and `unicorn`. It loads the plugin with the firmware's own runtime and reports:

- the header as the firmware reads it, and each knob's text at its min, default and max;
- the output: finite and within ±4 at every knob's ends, every knob swept and random
  settings, on noise, a loud sine and silence, mono, stereo and widened;
- writes outside the plugin's state, blocks and stack (a global, or past the end of its state);
- `reset()` giving what a fresh instance gives;
- instructions per block, the most and the mean, against the quarter-block share.

Instructions aren't cycles: SDRAM and the caches add to them on the unit, where the Profiler
measures the load with all 8 voices sounding. A pass in the checker is not a pass on the unit.

## The examples

- `examples/tremolo.c`: the smallest useful plugin. It has a list knob shown by `format()`,
  uses the host's `sinf`, smooths Depth, and starts lazily so `reset` matches a fresh instance.
- `examples/delay.c`: a short stereo delay, up to 180 ms. It has a 64 KB state, widens a mono
  part, and reads a smoothed time between samples.
- `pack/`: the default pack. It shares its arithmetic with the firmware's old built-in effects
  (`core/mods/synthfx/synthFxDsp.h`), and gives the same output, bit for bit
  (`tools/synthfx/test_pack.py`).
