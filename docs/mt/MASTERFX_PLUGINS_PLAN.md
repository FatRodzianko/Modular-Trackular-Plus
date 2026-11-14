# Master FX as card plugins (`.ptmfx`): feasibility and plan

Status: feasibility done and the design approved, 2026-11-14 (section 9). Building. Branch `mod/masterfx-plugins`, off `modular` at
`d9a6795`. Nothing committed yet.

Where the build stands (section 8's steps):
- Steps 1 to 3 are written: the shared parts, the runtime, the pack, record version 2, the built-in DSP
  removed, the Master page and Config lists made from the card.
- What ran:
  - The host tests: all pass, with the new `test_masterfx_plugins` (121 checks) and `test_masterfx`
    rewritten for version 2 (70 checks).
  - Synth FX on the shared parts: its rig (78 checks) and its planted faults (20 of 20 caught); the
    quick tier on the step 1 firmware, 20 of 20.
  - `tools/masterfx/test_pack.py`: each of the eight pack plugins gives the built-in effect's output bit for
    bit (the `d9a6795` firmware's), knobs moved and a clear in the run, at two load addresses.
  - The plugin firmware's own path on the sound rig, by a one-off probe (not yet a test in the tiers): the
    pack written to the fake card at the start, a project's files chosen and `apply` run, then a note
    through CE's sends. Six reverb and delay pairs covering all eight plugins play sample for sample as the
    `d9a6795` firmware's built-in effects.
  - The firmware builds; `mem_budget` is ok: ITCM free 69,984 → 49,568, DTCM 12,516 → 9,136, SDRAM
    688,348 → 603,408.
- Not yet: the emulator tests of the Master FX firmware (`test_masterfx.py`, `test_masterfx_rig.py`,
  `stress_masterfx.py`) still choose effects by list index, so they don't run the plugins; they must
  load them through the real path (the card, `apply`). Also the runtime on the emulated Cortex-M7
  (install, the knobs, the guards) and its planted faults. Then steps 4 to 8.

Today's mod is in [MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md): eight effects built into the firmware,
run inside CE's own reverb and delay objects. This plan turns them into plugins loaded from the card,
adds a third slot for the limiter, and gives users an SDK to write their own.

## Summary

- **It is feasible.** All three slots (reverb, delay, limiter) can run card-loaded plugins. The audio
  interrupt calls them in the same places today's effects run: inside CE's `AudioEffectPolyverb::update`,
  `AudioEffectShortDelay::update` and, new, `AudioEffectLimiter::update`.
- **The cost is about zero, measured in the emulator.** Each of today's eight effects, built as a
  position-independent plugin and called through a pointer, runs within 1 % of the built-in effect's
  instructions (section 3). The host's side of a call is 9 instructions with CE's 16-bit blocks. A float
  block costs the host 1,553 more.
- **Placement matters more than the call.** A plugin's code can be loaded into ITCM. The MPU lets ITCM run
  code, and 69,984 B of it are free since CE took the closed libraries out. Each plugin's small state can go
  in DTCM. Then a plugin runs from the same memories as today's effects. On the unit, Synth FX's plugins
  from SDRAM already cost nothing visible on single instances (F1 to F6, 2026-11-12). Many instances' state
  in SDRAM did cost more: P2, 16 points.
- **Follow Synth FX's `.ptsfx` rules, not CE's `.psyn` or `.ptfx`.** The plugins are position independent
  with no `.data` or `.bss`, have a versioned host table, and are guarded in the interrupt. Master FX gets
  its own format, `.ptmfx` in `/MasterFX/`, and a smaller runtime: one instance a slot, each slot with the
  memory it owns. It shares Synth FX's generic parts: the pool, the knob model, the catalog, the start
  marker and the cache sync.
- **Old projects keep working.** CE's knob values stay where CE stores them. The record's effect index
  (0 to 6, 0 to 2) maps to a file name in the default pack, and the pack is built from today's DSP. A test
  can check it plays the same, sample for sample.
- **A glue compressor costs less than CE's limiter.** The prototype runs 6,824 instructions a block,
  against CE's limiter at 14,420 (20,834 with its sidechain). Every candidate is costed in section 5.
- **The sidechain needs nothing new for one track.** CE's limiter already keys from one source the user
  picks: tracks 1 to 8 after their faders, Line in L or R, or Synth 1 to 3. CE pays for those taps in
  every block (the two muxes: 410 instructions). A plugin gets the same key. Several tracks at once needs
  a summing mux, about 150 instructions per chosen source per side (estimated, not measured), and a new
  list control.

## 1. CE's plugin loaders, and ours

| | `.psyn` (CE) | `.ptfx` (CE) | `.ptsfx` (Synth FX, ours) |
| :--- | :--- | :--- | :--- |
| What it is | a synth engine | a sample-editor effect, offline | a real-time effect on a synth part |
| Folder | `/Synths/` | `/Effects/` | `/SynthFX/` |
| Code | relocated ELF (ABS32, MOVW/MOVT), `.data`, `.bss` and constructors allowed | position independent | position independent, no `.data`/`.bss` |
| Memory | 1,920 KB arena in CE's 6 MB pool, LRU eviction | the sample editor's 128 KB code and 384 KB scratch, one plugin | 96 KB code and 192 KB state pools, no eviction |
| Loads | 1 MB boot preload, then on selection; `ensurePluginLoaded` can read the card with audio masked (`setVoiceCount`) | when applied | in the main loop, once the encoder rests |
| Runs in | audio interrupt | main loop | audio interrupt |
| Host table | `PsynHostApi`, versioned, file reads | `PtfxHostApi`, file I/O | `PtsfxHost`, versioned, maths only |
| Guards | CE's 2.1 ms synth limit | none needed | time, output, start marker |

Master FX's job matches Synth FX's: someone else's code in the audio interrupt, on audio CE already
routes. So it takes Synth FX's rules. CE's two loaders don't fit:
- `.psyn` relocates a big image into an arena that can evict plugins, and it reads the card from the audio
  path.
- `.ptfx` runs one effect offline, with file access.

**Share the loader?** In part. Synth FX's runtime (`synthfxPlugins.cpp`) is one global catalog with 56
instances, fades and per-voice seats. Master FX needs one instance a slot and memory tied to each slot, so
a second copy of that runtime would be mostly unused. These parts are generic, and both mods would share
them:
- `synthfxPool.h`;
- the knob step maths and text helpers;
- the header checks' pattern;
- the catalog listing;
- the start marker and safe mode;
- the cache sync.

Moving them into `core/mods/plugins/` is a refactor of a released mod. Synth FX's tests cover it, and
`MT_NO_MODS` must stay byte-identical.

## 2. Can each type run as a plugin?

- **Reverb.** Yes. The plugin replaces `reverb.Process` inside `AudioEffectPolyverb::update`, with CE's
  two buffers as its memory: 32 KB in DTCM and 32 KB in SDRAM. That is where today's six reverbs already
  run.
- **Delay.** Yes, in `AudioEffectShortDelay::update`, with CE's 1.5 MB of SDRAM lines. The host keeps
  `silentSamplesCount` from the plugin's output, since Export uses `isSilent()` for the tail.
- **Limiter / dynamics.** Yes, with care. CE runs two mono `AudioEffectLimiter` objects, left then right,
  and a compressor needs both sides together. `audioEngine` builds `mixerAndFxs` before `output`, and
  `limiter[0]` before `limiter[1]`. So when `limiter[0]` updates, the right block is already queued on
  `limiter[1]`. The left object's hook takes both blocks and runs the plugin once. The right object's hook
  sends its processed block on. This order is read from the source, not yet checked in the emulator.
  - The key comes from CE's sidechain mux.
  - The Gain Reduce meter reads a gain the plugin returns.
  - `resetLimiter` (Sample Editor, Sample Recorder) resets the plugin.
  - Stock stays CE's limiter, untouched, including its sidechain, which ducks the mix but keeps the key
    track at its level (`g × main + (1 − g) × key`).
- **Gated mode.** "Gated" and Reverse share `gated_dsp.h` with a flag today. They become two pack files
  from the one source, so old projects' indices 5 and 6 still map. 1.2.0's old Gated delay, delay 3, still
  imports as the stock delay, as today.
- **Performance mode and Break FX.** Performance mode only changes the sends, and Break FX is the
  sequencer's. Neither touches a slot, so neither interacts with plugins beyond the sound they send in.
  Checked in the emulator only, never in the user's hardware list.

## 3. Performance, measured

All in the emulator, instructions per 128-frame block, on the real firmware built from `d9a6795`. 1 % of
the CPU is about 3,800 instructions (1.2.0's readings). The built-in effects were measured on the real
graph: 4 voices playing into both sends, the effect's arguments captured, then each call counted alone.
The plugins are today's DSP headers built with Synth FX's builder flags (`-fPIC
-mpic-data-is-text-relative`, no GOT), loaded at an SDRAM address, and checked to give the same bytes when
linked at two addresses. They ran on the same buffers and knobs.

| Effect | Built in | Plugin, 16-bit blocks | Plugin, float blocks (the DSP converts back to 16-bit) |
| :--- | ---: | ---: | ---: |
| Freeverb | 44,959 | 44,810 | 52,884 |
| Plate | 38,737 | 38,980 | 46,948 |
| Spring | 34,014 | 34,138 | 42,226 |
| Shimmer | 48,466 | 48,970 | 56,988 |
| "Gated" | 41,704 | 41,145 | 49,118 |
| Reverse | 41,516 | 41,034 | 49,086 |
| Tape | 18,866 | 18,100 + the silence scan (about 800) | 33,231 |
| BBD | 23,261 | 22,459 + the silence scan | 41,046 |

- **The call and its indirection:** the host's side with a do-nothing plugin is 9 instructions with
  16-bit blocks. A float block, converted in, NaN-guarded and converted back, costs the host 1,553. So
  16-bit blocks (Q15, CE's own) are the default ABI and the pack uses them. Float blocks are a flag for
  plugins that want them: about 0.4 % of the CPU.
- **Code and data placement, as lines touched per block:**
  - code: 49 to 94 32-byte lines, 1.6 to 3.0 KB each;
  - state in DTCM: as built in;
  - state in SDRAM (the measurement build kept it beside the code): up to 83 more SDRAM lines a block
    (BBD's).
  - **So state, constants and code don't go in SDRAM.** Code and constants load into an ITCM area for
    each slot; state goes in DTCM.
- **On the unit:** not measured. The emulator doesn't model the cache or the stalls, which is why the
  placement above copies today's. The unit's own evidence is in the summary.
- **Memory, from `tools/mem_budget.py` on `d9a6795`:** ITCM 69,984 free, DTCM 12,516, SDRAM 688,348,
  Flash 15,025,596.

  | Region | Change | Why |
  | :--- | ---: | :--- |
  | ITCM | +24 KB, less the 5,584 B of the built-in loops | an 8 KB code area for each slot |
  | DTCM | +2 KB, less today's 1,144 B of state | fast state: reverb 1 KB, delay and dynamics 512 B each |
  | SDRAM | about +80 KB of the mods' 1 MiB | the dynamics slot's 64 KB, a load buffer, the catalog |
  | Flash | about +13 KB | the built-in DSP (19 KB) goes; the embedded pack and the runtime come in |
  | Heap | 0 | no malloc |
- **Load from the card, and the audio meanwhile.** A pack plugin is 2 to 3.5 KB. It is read in the main
  loop into an SDRAM buffer while the old effect keeps playing. Then the switch runs as today: the object
  is blocked, its buffers cleared in the main loop, the code copied into the slot's ITCM area and synced,
  `init` called, and the slot published. The effect is silent for the clear. The dry sound isn't touched,
  and audio is never masked. Not measured: the card's read time (the emulator fakes the card) and the
  1.5 MB clear on the unit.

## 4. Old projects, and how a project names its plugins

- **Record 4 (`mods.bin`), version 2:** each slot stores a file name (as Synth FX does, up to 47
  characters) and the plugin's own knob values. Version 1 is still read: its two indices map to the pack's
  file names (`Freeverb.ptmfx` and so on; 0 is Stock), and the record is rewritten at the next save.
- **CE's knobs** (Size, Damping, Predelay, Diffusion; Time, Feedback, Filter, PingPong, Sync and Rate;
  Attack, Release, Threshold and Sidechain) stay in CE's project fields. A plugin reads them every block,
  as today's effects do, and its header may rename their labels.
- **1.2.0's `masterfx.bin`:** imported as today, into version 2's names.
- **A missing or refused file:** the slot keeps the name and the page reads `Missing: Shimmer`. Section 9
  asks what plays meanwhile. The firmware writes any missing pack file at start, as Synth FX does, so only
  a third-party plugin can be missing for long.
- **Mod Off:** CE's stock effect in each slot, and the record is kept.
- **The same sound:** the pack is built from the same DSP headers. A `test_pack`-style emulator test
  compares each one with today's firmware, sample for sample, over many blocks and knob settings. Today's
  effects then come out of the firmware.

## 5. A new dynamics type in the limiter slot

Prototypes written for the measurement (`dyn.c`): stereo, float blocks, built as plugins. Their cost is
against CE's limiter on the same graph, both sides together.

| Type | Instructions a block | Against CE's limiter | Memory | Notes |
| :--- | ---: | ---: | :--- | :--- |
| CE's limiter | 14,420 | 1.0 | 0 | 20,834 with its sidechain on; a float divide and a 5-sample shift per sample per side |
| Glue compressor | 6,824 (6,884 keyed) | 0.47 | about 50 B | stereo-linked peak detector with a key high-pass, soft knee, gain every 8 frames, ramped, makeup, mix |
| Opto-style compressor | 6,329 | 0.44 | about 40 B | detector on the output, two release envelopes |
| Tape saturation, 2× | 14,667 | 1.0 | about 60 B | pre- and de-emphasis, asymmetric soft clip, top loss |
| Clipper | 4,102 | 0.28 | 8 B | soft knee |
| Clipper, 4× polyphase | 27,679 | 1.9 | about 130 B | two 2× half-band stages |
| Pitch shifter | 13,714 | 0.95 | 32 KB of lines | two heads and a crossfade: plain quality; a better one costs more |

- Add 1,553 to each for float blocks, or nothing if the plugin uses 16-bit blocks.
- The pitch shifter fits the slot's 64 KB, but it isn't dynamics: it is a creative effect, and Shimmer has
  one inside it already. A clipper and tape saturation fit beside a compressor, as plugins of the same
  slot.
- **Proof of concept: the glue compressor only.** Its knobs:
  - CE's Attack, Release and Threshold;
  - its own Ratio and Makeup, in the page's free columns 5 and 6;
  - CE's Gain Reduce meter;
  - CE's Sidechain list as its key.
- **Several key tracks (the user's idea):**
  - **Cost:** the two muxes already receive every input each block. Summing a chosen set instead of
    passing one is about 150 instructions per source per side (estimated) and 2 more audio blocks from the
    pool (peak 55 of 80).
  - **Where:** CE's graph taps each track's amplifier after its fader (`connectVoiceToOutput`), Line in
    (8, 9) and each synth part after its fader (10 to 12). No other tap is needed.
  - **What it needs:** an MT: hook in `AudioMux::update`, which runs from flash, and a multi-select
    control on the Limiter page, whose display bytes must be counted.

### 5.1 A sidechain destination (the user's design, 2026-11-14)

What CE does today: the Sidechain list picks the key (a track after its fader, Line in L or R, or a synth
part). The limiter's gain comes from the key and is applied to the whole master, and then CE adds the key
back: `out = g × master + (1 − g) × key` (`effect_limiter.cpp`, `processSidechain`). So everything but
the key is turned down, and the key keeps its level.

What the user wants: a second choice, the **destination**, so the kick can duck only the bass.
- **Default:** Master, as today.
- **Other choices:** one sample track (1 to 8) or one synth part (1 to 3).

How it fits CE's graph (Tracker+, read from the source):
- A sample track is `memoryPlayer → bitDepth → filter → amplifier (volume) → panningAmplifier →` the
  tracks mixer.
- The reverb and delay sends are taken from the filter, before the volume.
- The key is taken from the amplifier, after the volume.
- A destination track's compressor runs on that track's stereo block, in place, between its `amplifier`
  and its `panningAmplifier`. That's an MT: hook in the panning amplifier's update for that one track.
  Its sends are taken earlier, so they aren't ducked. Section 9 asks whether they should be.
- A synth part as the destination runs after its Synth FX slots, in `PolySynth::update`.
- The plugin doesn't change: `process` gets a stereo block and a key, whether the block is the master or a
  track. The host decides where it runs. When the destination isn't the master, the master's limiter is
  CE's (Stock, with its sidechain off).

Two limits to check in the emulator before it's built:
- **Update order.** The objects run in the order they were made. If the key's track comes after the
  destination's in that order, the key reaching the destination is one block (2.9 ms) old. The attack
  then starts up to 2.9 ms late; the compressor itself is unchanged.
- **Cost.** One compressor instance on a track costs about the same as on the master: 6,824
  instructions measured on the master (a track's block is the same size), plus the hook's call.

## 6. Guards and the interrupt's time

CE's 2.1 ms limit counts only `PolySynth::update`, so Master FX runs outside it. Each slot gets Synth
FX's guards:

- **Time:** `process` timed with the cycle counter. Over its budget for 8 blocks running, the slot is
  bypassed, and the page says so. Proposed budget: an eighth of a block, about 218,000 cycles. Today's
  heaviest effect is about 49,000 instructions.
- **Output:** NaN and infinity become 0, then the plugin is reset.
- **Start:** a marker, `/MasterFX/.starting`, from a new plugin's first block until 5 s later. If it is
  found at start, the project's plugins aren't loaded until the user picks one.
- **Boot gate:** no plugin runs before the first Pattern screen update, the rule today's effects keep.

A crash or a hang inside a plugin can't be stopped. The marker is for that.

## 7. The ABI and the SDK

`masterfx-sdk/ptmfx.h`, version 1, little-endian, the same shape as `ptsfx.h`:

- **Header:** magic `PTMF`, version, flags, slot (reverb, delay or dynamics), name[32], author[16], and
  the sizes of state and code.
- **Labels:** each CE knob's label as the plugin wants it shown.
- **Own knobs:** up to 2, as `PtsfxParam`, 32 bytes each, with the same step rules.
- **Entry offsets.**

The entries:

| Entry | Runs in | What |
| :--- | :--- | :--- |
| `init(st, const PtmfxMemory* mem, const PtmfxHost* host)` | main loop | zeroed state in DTCM; `mem` is the slot's memory: reverb 32 KB DTCM + 32 KB SDRAM, delay 3 × 512 KB SDRAM, dynamics 64 KB SDRAM, all cleared |
| `set_param(st, index, value)` | interrupt | own knobs, before a block |
| `process(st, const PtmfxBlock* b)` | interrupt | `b`: L and R in place (the reverb's send in, return out), the delay's mono send, the key's L and R (dynamics), CE's knob values for the slot; returns the gain for the meter (dynamics) |
| `reset(st)` | interrupt | after CE clears the buffers (project load, Export, Sample Editor or Recorder) |
| `format(host, index, value, out, size)` | main loop | optional: a knob's text |

- **Flags:** `PTMFX_FLAG_FLOAT` for float blocks (±1.0). Without it, CE's Q15 16-bit blocks.
- **Host table:** Synth FX's maths, `random` and `print`, plus the tempo (for synced delays) and the
  sample rate.
- **Builder:** `ptmfx_builder.py`, from `ptsfx_builder.py`, with the same refusals: no `.data`, `.bss`
  or GOT, the same bytes at two link addresses, no double-precision helpers. Code is at most 8 KB, the
  size of a slot's ITCM area.
- **Checker:** `tools/masterfx/check_ptmfx.py`: the plugin in the emulator with the firmware's runtime.
- **Docs:** `docs/masterfx_plugins.md`, with the same 13 sections at the same depth as
  `docs/synthfx_plugins.md`, CE's `custom_synth_sdk.md` and `modular_effects_system.md`. The comparison
  table in Synth FX's doc gets a `.ptmfx` column.
- **The pack:** the eight effects and the glue compressor are in the firmware, written to `/MasterFX/`
  when missing. Their sources are in `masterfx-sdk/pack/`.

## 8. Build plan, after the go-ahead

Each step is a commit, with its tests passing and `MT_NO_MODS` byte-identical.

1. **The shared parts.** Move them into `core/mods/plugins/` and put Synth FX on them. Synth FX's host,
   emulator and page tests must pass unchanged.
2. **The runtime.** Master FX's catalog, loader, ITCM code areas, DTCM state, guards, marker and pack
   writer, with host tests.
3. **The pack.**
   - Port the eight effects, with the state reached through a pointer.
   - Test them sample for sample against today's built-in effects.
   - Record version 2, with version 1 and 1.2.0's file converted.
   - Remove the built-in DSP.
4. **The dynamics slot.** The limiter hooks, the stereo pair, the key, the meter, the reset; Stock as CE.
   The glue compressor plugin.
5. **The UI.**
   - A third list on the Master page (the Limiter column).
   - The plugins' labels.
   - Own knobs in the free columns.
   - The Missing and Bypassed text.
   - The display bytes counted against 8,172.
6. **The SDK and docs**, with the builder, the checker and two examples.
7. **Tests and the device.**
   - The release tier.
   - The sound rig with all 8 voices and CE's `.psyn` engines on the fake card.
   - CPU against today's Master FX, in instructions.
   - Planted faults.
   - A short device check: one project, 8 voices with Shimmer + BBD + Glue, on today's release and on the
     plugin firmware, read from the diag log.
8. **The merge.** `CLAUDE.md`'s checklist against every mod, then the `masterfx-2.0` tag on the user's
   OK.

## 9. Decisions for the user

Answered 2026-11-14, with the go-ahead to build:
- 1: (a), CE's pages with up to 2 own knobs.
- 2: a missing plugin plays CE's Stock.
- 3: yes. CE's reverb, delay and limiter stay built in. If this ever goes to CE, its maintainers can decide
  whether to convert them.
- 4: replaced by the sidechain destination (section 5.1). It comes after the master compressor works.

Still open: whether a destination track's sends are ducked too.

The questions as they were asked:

1. **Knobs on the pages:**
   - (a, recommended) CE's pages as they are, with the plugin's labels, plus up to 2 own knobs in the free
     columns. The reverb and Limiter pages have columns 5 and 6 free; the delay page has none.
   - (b) A generic knob list like Synth FX's, with up to 12 knobs.
2. **A missing plugin:** play CE's Stock in that slot (recommended), or nothing.
3. **"Stock" means CE's own reverb, delay and limiter,** built in as now. Only MT+'s effects become
   plugins. Is that the meaning of "the stock ones shipped as plugins"?
4. **Several key tracks for the sidechain:** now, or after the compressor proof of concept
   (recommended: after).

## 10. Not checked

- Anything on the unit: cycles, stalls, the cache, the card's read time, the clear's length, ITCM code
  copied at runtime.
- The limiter pair's update order in the emulator: read from the source only.
- The pack in the firmware's own path: one probe run only, outside the test tiers, with one knob setting.
- The display bytes of a third list.
- The multi-key mux's cost: estimated, not measured.
- The pitch shifter's and clipper's sound: these prototypes were only costed.

The measurement sources (`mfx_plug.cpp`, `host_glue.cpp`, `dyn.c`, `measure*.py` and the build scripts)
are in the session's shared notes folder, not in the repository.
