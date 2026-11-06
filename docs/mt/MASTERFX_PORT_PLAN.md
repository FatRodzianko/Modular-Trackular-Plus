# masterfx on CE: build plan

Status: steps 1 to 4 are built and pass in the emulator, 2026-11-04. Step 5, the first device test,
locked up the unit on Play. The stepped device test, 2026-11-05, found why: sustained overload of the
audio interrupt, which also happens with Master FX on Stock, so Master FX isn't the cause. The load was
CHIP: every other engine ran 8 voices, and so did Shimmer + BBD on ACD. CHIP is now fixed, and this branch has
upstream's `main` of 2026-11-06, CHIP fix included. On firmware before that merge the unit ran every effect, and on
both the load that locked up and a real project through the effects, with no problem. Section 8 has the readings. Sections 2, 3 and 4 give what was
built. Based on CE's source at `af01d47`, a reference build of it, and 1.2.0's
`patch/masterfx` (mf8) compiled with CE's FPU flags. Section 3.13 of
[CE_PORT_PLAN.md](CE_PORT_PLAN.md) points here.

## Summary

- **The port is feasible, and simpler than the port plan said.** No new audio objects and no
  change to `MixerAndFxs`. Our effects run inside CE's own `AudioEffectPolyverb::update` and
  `AudioEffectShortDelay::update`, so every route stays as it is: sends (synths included), RMS,
  USB and export.
- **All the effects fit CE's buffers.** Both have the sizes 1.2.0 had. No new buffers.
- **It should cost much less CPU than on 1.2.0.** CE keeps the first reverb buffer in DTCM, so
  most SDRAM misses go away. Worst pair: about 5–7 points over stock, against about 11 on 1.2.0.
  This is an estimate. One device reading with all 8 synth voices sounding settles it.
- **Recommended UI: your third idea.** Two lists on the Master page, above the Reverb and Delay
  labels, selected the way Saturation is. They reuse two list controls that already sit in
  those columns, so no new controls, slots or heap. Knob labels and the page title change with
  the effect. Project Settings stays as the fallback, and it's also how the CPU test switches.
- **The hardware ask is one firmware and one sitting**, section 8.

Open decisions are in [section 10](#10-open-decisions). Things added beyond the brief are in
[section 11](#11-added-beyond-the-brief).

## 1. Effects

Taken from the code, not the brief: mf8 moved Gated to the reverb list and has Reverse too.

| Slot | Choices |
|---|---|
| Reverb | Stock, Freeverb, Plate, Spring, Shimmer, "Gated", Reverse |
| Delay | Stock, Tape, BBD |

## 2. Audio port

### Hooks

Both `update()`s are `AUDIO_CORE_RAM` and run from `AudioStream::update_all` in the audio
interrupt. CE's receive, allocate, transmit and release stay as they are. Each `update()` gets an
`// MT:` check around CE's processing, behind `MT_HAS_MASTERFX`, and it calls
`mods::masterfx::reverbProcess` or `delayProcess` instead of it. `reverb.h` gets `knobsForMods()`,
so the effect reads CE's own knob values.

- **The stock object doesn't run when ours is chosen,** because the hook replaces CE's processing.
  That's what the CPU budget needs: CE's reverb drops from 32,233 instructions in `update()` to 57,
  and CE's delay processing from 11,555 to 0. Only one DSP per slot ever runs.
- **Off, or Stock chosen:** `activeReverb` is 0, one byte read and a branch. Same for the delay.
- `-DMT_NO_MODS` compiles the hooks out, and the reference build stays byte-identical.
- **Block handling** stays as stock's: receive, process, transmit, release. A missing block is
  handled the way stock handles it.

### What the DSP reads

On 1.2.0 the DSP read the objects by offset. On CE they're our build's own classes, so offsets
go. `Reverb` and `AudioEffectShortDelay` get small getters behind `MT_HAS_MASTERFX`, and the DSP
takes a params struct. CE's values mean what 1.2.0's did: `lp_` is 1 − Damping, predelay is
`length * 5000`, diffusion tops out at 0.85, feedback is a byte, `filterCoeffs.b1` holds the pole.

The DSP files move to `core/mods/masterfx/`, compiled as C++ into one file (`_Static_assert`
becomes `static_assert`, `void*` casts get written out, and the 1.2.0 chain words and firmware
addresses go). Compiled with CE's flags today, the DSP has no `f64` instructions and no undefined
symbols, so no `double` and no libc calls in the interrupt.

### Three things CE needs that 1.2.0 didn't

1. **`isSilent()`.** `mtAudioExport.cpp:639` uses it to decide how long the delay tail lasts in an
   export. Tape and BBD must keep `silentSamplesCount` up to date, or exports cut the tail at
   300 ms.
2. **CE clears the buffers behind us.** `AudioEffectPolyverb::reset` and
   `AudioEffectShortDelay::clear` run on project load, export, Sample Editor and Sample Recorder.
   Both get a hook that marks our effect's state invalid, so the next block re-initialises it.
3. **Output sides.** Stock on CE transmits `outputRight` on channel 0 and `outputLeft` on 1. Tape
   and BBD send left on 0. The emulator test compares the first ping-pong echo's side against
   stock, and we match stock if they differ.

### Switching: in the main loop, not per block

1.2.0 cleared the buffers a piece per block inside the interrupt: 32 KB per block for the delay,
which is 1,024 cold lines, or about 23 points of CPU for 0.1 s. That's the wrong place on CE, and
CE already has the right one.

Every change of choice comes from main-loop code (section 6), so `masterfx::apply()` runs there:

1. Block the object (`blockUpdate`, which both classes have).
2. Set `activeReverb` or `activeDelay`.
3. Clear through CE's own `reset()` or `clear()`, which unblocks. Hooks in both tell the effect its
   buffers are clean, so CE's own clears are covered too.
4. The next audio block lays out the effect's state in the clean buffers. That init is in flash.

The effect is silent for the length of the clear (estimate: under 20 ms for the delay's 1.5 MB)
and dry sound isn't touched. The interrupt carries no clearing code at all. On the Master page,
the switch waits until the encoder has stopped for about 150 ms, so turning through the list
doesn't clear 1.5 MB per click. `AudioEffectPolyverb` queues blocks while blocked, as CE does
already.

**Boot gate.** On 1.2.0, running Freeverb during boot hung the unit, and a saved project would
then hang every boot. So `activeReverb` and `activeDelay` stay Stock until the first Pattern
screen `update()`, one line in that screen. The emulator doesn't run boot, so the device test
checks it.

### Buffers

| Buffer | CE | 1.2.0 | Fit |
|---|---|---|---|
| `internalRamReverbBuff` | 32,768 B, **DTCM** | SDRAM | Same size |
| `externalRamReverbBuff` | 32,768 B, SDRAM | SDRAM | Same size |
| `externalRamDelayBuff` | 1,572,864 B, SDRAM, 3 × 2^18 samples | Same | Same size |

Every layout's `#error` checks carry over: Freeverb (lines in each buffer, predelay in the spare
ends), Plate and Shimmer (one 16,384-sample ring per buffer), Spring (all in the first), Gated
(tap line in the first, allpass ring in the second), Tape and BBD (first two delay buffers).

Spring keeps floats in the first buffer, so it must be word-aligned. It's a `uint16_t` array that
happens to be at `0x20023cf0` and `0x20023f80` in the two builds I checked. Mod builds add
`aligned(4)` to it, behind the flag.

### `masterfx.bin` import

Through the importer table in `modsProject.cpp`, as for the other mods:

- 12 bytes: magic `HMF1`, reverb, delay, two zeros, FNV-1a over the 4 data bytes (the same sum as
  swing's side file).
- Bad or missing file: Stock.
- Delay byte 3 (the old Gated): Stock delay, keep the reverb.
- The mod turns On only if the file picks something other than Stock + Stock.

## 3. Costs

### Measured in the emulator

Instructions per block and distinct SDRAM cache lines per block, 160 blocks of noise at default
knobs. Delay time 0.5 s. CE's stock objects are from the reference build, the rest is mf8's DSP
with CE's FPU flags. "1.2.0 layout" is the same code with both reverb buffers in SDRAM.

| Reverb | Instructions | Lines, 1.2.0 layout | Lines, CE layout |
|---|---|---|---|
| CE Stock | 33,399 | | 36 |
| Spring | 35,255 | 53 | **0** |
| Plate | 40,685 | 254 | 109 |
| "Gated" and Reverse | 42,500 | 341 | **66** |
| Freeverb | 46,527 | 238 | 120 |
| Shimmer | 49,639 | 304 | 152 |

| Delay | Instructions | Lines |
|---|---|---|
| CE Stock | 11,661 | 51 |
| Tape | 18,057 | 26 |
| Tape, PingPong | 27,657 | 36 |
| BBD | 22,734 | 25 |
| BBD, PingPong | 35,397 | 34 |

### CPU estimate

These are estimates, from 1.2.0's device model: extra instructions, plus about 350 cycles per
extra SDRAM line, plus about 350 per line of code fetched from flash. CE runs at 528 MHz like
1.2.0, so 1 point is about 15,300 cycles. The model ran 1–3 points low on 1.2.0 before the flash
term, which matches that term.

| Effect | Points over stock, in ITCM | Added if in flash | 1.2.0, measured |
|---|---|---|---|
| Spring | −0.7 | 1.3 | about +2 |
| "Gated", Reverse | 1.3 | 1.6 | about +10 |
| Plate | 2.1 | 1.5 | |
| Freeverb | 2.8 | 1.6 | |
| Shimmer | 3.7 | 2.0 | about +7 |
| Tape / with PingPong | −0.2 / 0.7 | 1.2 | |
| BBD / with PingPong | 0.1 / 1.2 | 1.7 | |

Worst pair, Shimmer + BBD PingPong: 4.9 with all code in ITCM, 8.6 with all in flash. With the ITCM
choice below it's 6.3, and the worst pair becomes Freeverb + Tape PingPong. It was about 11 on 1.2.0.

### Will it survive 8 voices?

Not known yet. The Config reading on `hellish downtown` was 33 % average and 55 % max, and that
isn't 8 voices sounding. On 1.2.0, 8 allocated voices took about 5 points each even when idle.
If CE's stock reading with 8 sounding voices peaks near 90 %, the worst pair leaves little room
and a project using it could crackle at full load. Stock adds nothing, and the choice is per
project, so that wouldn't block the mod. The device reading in section 8 answers it, and section
8 says what each result means.

### Memory

Measured on the build, with `tools/mem_budget.py`:

| Region | Cost | Free now | Floor |
|---|---|---|---|
| Flash | 19,384 B | 14,876,972 B | 1 MiB |
| ITCM | 5,584 B | 11,112 B | 1,024 B |
| DTCM | 1,144 B | 15,480 B gap | 4,096 B |
| SDRAM | 0: CE's own buffers | 1,042,508 B | 256 KiB |
| OCRAM, heap | 0 | 9,556 B OCRAM | Mods stay out |

**ITCM choice:** `shimmer_process` and its passes (2,832 B) and `bbd_process` and `bbd_coefs`
(2,420 B): the costliest reverb and delay. Moving all 15.2 KB of loops would save only about 1.4
more points and leave about 6 KB for everything else. Per-loop sizes: Tape 1,660, Spring 1,780,
Plate 2,092, Gated 2,176, Freeverb 2,250. `tools/mem_budget.py` gives the final figures.

**ITCM for the whole project.** 15,648 B is usable above the floor. It was 21,672 B until upstream moved
`Sequencer::play_microStep` (5,988 B) into ITCM on 2026-11-05. Hot code is my estimate, from each mod's 1.2.0
function sizes:

| Who | Code on 1.2.0 | Hot part, estimate |
|---|---|---|
| legato, tracklen, slicethru Loop, resample, songloop, visualizer's step path | 20 KB in total | 2.5 KB |
| interpolate (the port plan puts its microstep path in ITCM) | 3.5 KB | 1 KB |
| WAVE and PHASE voice loops: `wt_voice_render` 5,216, `ph_voice_render` 2,552, `wt_saw` 2,126 | 516 KB, mostly tables | 10 KB |
| masterfx, this plan | | 5.3 KB |
| **Total** | | **18.8 KB, about 3.2 KB over** |

- It no longer all fits: the synths' voice loops take the shortfall. They start in flash, as below, and move
  in as far as room allows. Upstream's move also saves CPU on every note, which a device reading can show.
- Engines don't have to be in ITCM: CE's own CHIP renders from flash (4 KB at `0x60156440`). So
  later engines start in flash.
- A loop that runs every block costs about 0.7 points per KB from flash, whichever mod owns it.
  Placement can change later with one attribute per function and no change to files, so nothing
  here locks us in. If synths need the room, BBD goes back to flash first (about 1.7 points on
  projects that pick BBD).
- The device reading in section 8 also gives the first real flash-against-ITCM figure on CE, which
  the synth port needs.

## 4. UI options, against the real code

Controls on `core/interface/masterParams` today:

- **Master page:** 8 labels and 8 buttons. Columns 1–4 are empty, with `blankSpaceCover[0]` over
  them. Buttons 1–4 open Reverb, Delay, Limiter and EQ. Left/Right stop on columns 0, 5, 6, 7.
- **Reverb page:** columns 0–4 used, column 5 and 6 free, button 7 Back.
- **Delay page:** all 7 columns used, button 7 Back.

### A. Type list on the Reverb page, Shift+Back on the Delay page (your first idea)

Reverb: column 5 is free, and `saturationList`, hidden there, could be reused. Fits well.

Delay: no free column. `shiftHandler` already polls Shift to change the Line in label to
Mute/Unmute, so Shift could turn Back into "Tape" and Shift+Back could cycle the delays. It costs
almost no bytes. The alternative, taking column 7 and moving Back to Shift, breaks a button people
press constantly.

Against it: two different mechanics, and the delay's one is a cycle you can't see until you hold
Shift. You do get to hear the effect and see its knobs while choosing.

### B. Lists on the Master page (your third idea): recommended

`delaySyncEnableList` is already in column 1 and `delaySyncRateList` in column 2, both hidden on
this page. On the Master page they show the reverbs and delays:

- **Selecting:** Left/Right go 0, 1, 2, 5, 6, 7. Encoder and Up/Down change the choice, Delete
  goes back to Stock. As Saturation does.
- **Buttons don't change:** 1 and 2 still open the Reverb and Delay pages.
- **Labels:** `label[1]` and `label[2]` get a second line with the name, as Saturation's does.
- **Cover:** `blankSpaceCover[0]` moves to columns 3–4. It has to, since it draws over these
  lists.
- **The lists' own data:** `setControlData` swaps it, with two `strList`s (32 B) and names in
  flash. `showDelayScreen` sets CE's data back. Controls are rebuilt on each module start.

It works the same for both effects, shows both at once, and needs no Shift. It takes no new
control, slot or heap. Its one cost is that you step back a page to change effect while adjusting
knobs.

### C. Renaming

A flash table per effect, applied in `showReverbScreen` and `showDelayScreen` with
`setControlText`. The Stock row keeps CE's words exactly. The title goes from "Master Reverb" to
"Reverb: Shimmer". It works with A or B.

| Effect | Column names (only those that differ from Size, Damping, Predelay, Diffusion) |
|---|---|
| Freeverb | None |
| Plate | Size → Decay |
| Spring | Size → Decay, Diffusion → Drip |
| Shimmer | Size → Decay, Damping → Tone, Diffusion → Shimmer |
| "Gated", Reverse | Size → Length, Damping → Tone, Diffusion → Release |
| Tape, BBD | None. They were Intensity and Tone until 2026-11-05: Tape's Intensity is the stock Feedback value, and Tone is the stock Filter, a real lowpass in Hz |

Values still show as CE shows them: Size is a percentage, not a time.

### Display bytes

Counted by `test_masterfx_rig.py` on the real controls, with the heaviest choices shown. Only API
calls are exact. The coprocessor expands `CMD_TEXT` and `CMD_GRADIENT`, so these are upper bounds:
24 B per text plus 12 B per glyph, 120 B per gradient.

| | Bytes, at most |
|---|---|
| Reverb list | 964 |
| Delay list | 456 |
| Second lines of two labels | 176 |
| **Total added** | **1,596** |
| Master page as CE | 3,688 |
| **Master page, against 8,172** | **5,284** |
| Reverb page, with names | 3,476 |
| Delay page, with names | 4,980 |

If it ever overflowed, the lowest slots would vanish first, the frame and labels, so it would be
obvious on the device.

### Off, and other paths

- **Mod Off:** every UI change is behind `if (masterfx::uiActive())`. Master page, Left/Right,
  labels and title are CE's. If a saved place is 1 or 2 when Off, it falls back to 0.
- **MIDI CC:** `changeMixerOnMidiCC` only handles CC 71–82, which are volumes. Nothing reaches the
  knobs or the type. No FX or performance path writes them either. So there's nothing to keep
  in sync. The only writers of a choice are this page, Config and project load, all main loop.

## 5. Recommendation

Option B plus C, with Project Settings as fallback. If you'd rather choose on the effect's own
page, A can be added later without undoing anything, since it uses the same `setReverb` and
`setDelay` calls ([decision 1](#10-open-decisions)).

## 6. Where the choice lives

- **`mods.bin`, record id 4** (`Mod::masterfx` + 1): 2 bytes, reverb and delay. Values out of range
  mean Stock. It fits the project buffer.
- **In RAM:** the two choices and the two `active` bytes in DTCM, since the audio interrupt reads
  the `active` ones.
- **Off:** plays stock but keeps the record, so On brings the choice back. Config > Mods has the
  default, Project Settings > Mods the project's switch, as every mod does.
- **Project Settings > Master FX > Reverb, Delay:** always available. `projectMenuGroupCount` grows
  by `MT_HAS_MASTERFX`.
- **Saving:** edits call `project::markChanged()`, so Discard drops them, as with swing.
- **Who calls `apply()`:** `onWorkspaceLoaded`, the switch and menu callbacks, the Master page.

## 7. Build steps

Each one is a commit, tested in the emulator. Effort overall: M.

| Step | What | Done when |
|---|---|---|
| 1 | Settings: record, import, Config items, switch, `MT_HAS_MASTERFX`, no audio | Host and emulator tests pass; `MT_NO_MODS` byte-identical |
| 2 | Effects: DSP in `core/mods/masterfx/`, hooks, `apply()`, boot gate, ITCM choice | Section 8's object-level tests; `mem_budget.py` passes |
| 3 | UI: Master page lists, renames, title | Emulator UI tests; byte count against the table above |
| 4 | Sound rig and stress, WAVs for you to listen to | Sound test stage 3, as far as decision 2 says |
| 5 | Device test | Section 8's readings; I report before merging |

## 8. Tests

### Host and emulator (`tools/emu/test_masterfx.py`, `make -C core/mods/tests`)

- **Settings:** record round trip, out-of-range values, import of good, bad and old-Gated files,
  a real `masterfx.bin` through CE's load, On only if not Stock. Off keeps the record.
- **Off = CE:** with the mod Off or Stock chosen, `update()` output equals the reference build's,
  sample for sample, both objects.
- **Same sound as 1.2.0:** each effect's output is bit-identical to mf8's DSP on the same input,
  if Modular-Trackular is beside the repo, the same way the tables in section 3 were measured.
- **Each effect, from 1.2.0's `test_masterfx.py`:** decay, Size, Damping, Predelay, echo times,
  feedback 0.99 with no NaN, missing blocks, no clipping.
- **On CE:** only the chosen effect runs (instruction count); `isSilent` tracks a tail; `clear()`
  or `reset()` mid-play re-initialises; a switch is silent until the clear ends; ping-pong side
  matches stock.
- **UI:** real `cMasterParams` with display commands caught, as Color Song's test does: Off draws
  the same commands as CE, On draws the lists and names, Left/Right stops, Delete, byte count.
- **Stress:** 1.2.0's randomized sessions with switches, and guard bytes around the buffers.

Limits of these two: they don't run boot, the cache isn't modelled (line counts are my stand-in),
and they can't see the screen. "What ran" below has what came after the lockup.

### Device test, first attempt: locked up on Play

On 2026-11-04 the unit locked up when Play was pressed in the Pattern screen, on the first test
project, and had to be unplugged. That project held 8 voices with Shimmer + BBD on from the first
Play, and no other test ran. Nothing in the emulator reproduces it. What the rig has shown so far:

| Suspect | Result |
| --- | --- |
| A fault or hang on the real path | None. `boot_rig.py`: real boot, Open, Play button, main loop and interrupt, also with the interrupt fired inside main-loop code. Mods and reference, 8 held voices and the worst-case project |
| Asserts, or writes over the code | None |
| Failed audio allocations | None. The pool peaks at 55 of 80 at 8 voices on the real path. The 80 of 80 first written here came from a rig that took a wrong path |
| Unaligned accesses in our code (the emulator doesn't fault on them) | None, Shimmer and BBD |
| Slow first block | No. After a clear, Shimmer and BBD cost about what any block does |
| Loops that could spin | The effects have no `while` or `do` |
| Power-up contents of the buffers | 0xA5 in all three before CE's clear gives the same output as zeros |
| Stack | Largest effect frame 1,184 B, against 7,777 of 20,480 read on a device |

That list only covers what the rig runs, and the rig has no clock. The cause is still open, and these records from 1.2.0
(Modular-Trackular) should have been read before the test project was made:

- **8 voices were already known to overload stock.** `docs/TIMING_HEADROOM_PLAN.md` section 14: with
  Polyphony 3/3/2, stock read 107-110 % on the device, and with Shimmer and "Gated" 130 %. With 8
  voices allocated and nothing playing it read about 80 %. So this test project was over the limit
  by design, and section 3's "Not known yet" missed that.
- **Overload is the leading suspect, not shown.** This section used to say overload alone didn't lock
  1.2.0 up, because those 130 % runs lasted about 3 minutes and Config stayed readable. That was too
  strong: 130 % is the worst block in each second, not an average, so those runs weren't sustained
  overload. This project sounds all 8 voices on every block with the heaviest pair. If that takes
  over 100 % block after block, the interrupt takes all the CPU, and the main loop, which reads the
  buttons and the power button, never runs. That would look just like what happened. The diagnostic
  firmware tells it apart from a fault or a stuck main loop.
- **A masterfx hang the emulator passed has happened before.** `docs/FINDINGS.md` section 19: the first
  Freeverb hung the unit at boot while every emulator test passed, and needed the pinhole to power
  on. Stress runs found no stray writes or runaway loops, which pointed at timing: heavier DSP in the
  interrupt at a moment the rest of the firmware couldn't take it. It was never fully explained.
  That is the nearest match to this lockup, at a different moment.

### Level against Stock

No effect adds level. Same project, same notes, 300 blocks through the Play path, RMS against Stock:
at most +0.2 dB (Freeverb) with 5 voices, -0.1 dB for Shimmer + BBD with 8. Each effect does hear
the sends, with an input peak of 2,275 and output that differs from Stock. The loud peaks came from
the first test project's held notes, and the reference build, which has no masterfx, gave the same
ones. The test notes now play at CE's step volume 30: peak 23,602 and RMS 2,386 at 5 voices, against
32,764 and 5,624 before.

### Measured cost of the effects' own calls

From `reverbProcess` and `delayProcess` called alone, so no closed code is under the hooks. Points
use the model above: instructions, plus 350 cycles per SDRAM or flash line.

| Effect | Instructions | SDRAM lines | Points, gross |
| --- | --- | --- | --- |
| Shimmer | 47,500 | 141 | 6.4 |
| BBD | 23,300 | 18 | 1.9 |

CE's stock reverb is 32,233 instructions, so these are close to the estimates in section 3.

### Device test: stepped, 2026-11-05

Run on `TrackerPlus_mfx_recorder.ptf`, from `/mfx_log.txt`. `L1` to `L6` play 1 or 4 voices of CHIP.
`L7` and `L8` add part 2's ACD (`AT-Pluck`, 2 voices) and part 3's VAP (2 voices). `project.mt`'s `synthType`
sets a part's engine, and `boot_rig.py` now prints each part's engine and patch after Open.

| Project | Average | Slowest | Blocks over time | Longest main loop gap | What the unit did |
| --- | --- | --- | --- | --- | --- |
| `L1` 1 voice, Stock + Stock | 35 % | 59 % | 0 | 11 ms | Fine |
| `L2` 1 voice, Stock + Tape | 37 % | 82 % | 0 | 11 ms | Fine |
| `L3` 1 voice, Spring + Stock | 35 % | 59 % | 0 | 11 ms | Fine |
| `L4` 1 voice, Shimmer + BBD | 38 % | 68 % | 0 | 11 ms | Fine |
| `L5` 4 voices, Stock + Stock | 87 % | 125 % | 22 of 6,988 | 147 ms | Sound right, screen choppy |
| `L6` 4 voices, Shimmer + BBD | 89 % | 114 % | 88 of 8,987 | 141 ms | Sound right, screen a little choppier |
| `L7` 8 voices, Stock + Stock | Over 100 % | 3,861 us of 2,902 | 914 of 952 | Stalled | Screen stopped, sound distorted, recorder restarted it |
| `L8` 8 voices, Shimmer + BBD | Over 100 % | 3,917 us of 2,902 | 897 of 934 | Stalled | The same |

All four stalls read the same: inside the audio interrupt, 1,023 or 1,024 of the last 1,024 ms in it,
and no fault (`cfsr` and `hfsr` 0). The plain firmware has no recorder to restart it, so it stayed that
way, with the buttons and power button unread. That is the lockup of 2026-11-04, on the same load as `L8`.

- **Master FX adds 2 to 3 points on average** (35 to 38 at 1 voice, 87 to 89 at 4), less than section 3's
  estimate of 5 to 7. `L7` shows the stall needs no Master FX.
- **CHIP costs about 17 points a voice** on this patch: 35 % at 1 voice, 87 % at 4. At 600 MHz that's about
  2,300 cycles a sample per voice. `renderVoiceAudio` calls `std::pow` and `std::sin` per sample for the
  filter, on top of what the model itself calls. 22 of the recorder's 32 address samples were in CHIP or
  the maths library, in flash, and the saved stacks show CHIP calling `powf` and `sinf`. That's a small
  sample, and the closed engines' share wasn't looked into.
- **What isn't known:** whether unmodified CE stalls the same way: the rig says the mods add 83 instructions a
  block on average, but it hasn't run on the unit.
- **Recorder bug:** its `voices` field prints `getMaxVoiceCount`, so `8 7 7` at 1 voice is what each part
  could take, not what it has. Project names above give the real count.

### Device test: each engine, 2026-11-05

The same firmware, with all three parts on one engine and its Init patch
([DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md)). Nothing stalled, and no block went over time except two
in `N1`. Each slowest figure agreed with the maximum read on the Config screen, to within the rounding
of that reading.

| Project | Voices | Average | Slowest | Longest main loop gap | Free heap |
| --- | --- | --- | --- | --- | --- |
| `N1` CHIP | 4 | 79 % | 100 % | 51 ms | 89,444 B |
| `N2` ACD | 8 | 54-58 % | 75 % | 17 ms | 80,736 B |
| `N3` FAT | 8 | 54-56 % | 73 % | 15 ms | 80,468 B |
| `N4` VAP | 8 | 58 % | 75 % | 17 ms | 63,716 B |
| `N5` WTFM | 8 | 48-51 % | 67 % | 16 ms | 62,740 B |
| `N6` PERC | 8 | 18-19 % | 49 % | 9 ms | 51,980 B |
| `N7` DIRT | 8 | 63-66 % | 83 % | 20 ms | 78,292 B |
| `N8` ACD, Shimmer + BBD | 8 | 57-60 % | 75 % | 17 ms | 80,428 B |

- **CHIP is the load behind the stall.** 4 voices of CHIP cost more than 8 of any other engine. `L5`, the same
  4 voices on the project's patch, read 87 %, so the patch adds about 8 points. PERC's 18 % puts the part
  of the figure that isn't voices at 18 % or less, which makes CHIP about 15 points a voice and ACD about 5.
  So `L7`, 4 CHIP on the project's patch plus 4 other voices, comes to about 107 %, and the recorder read
  109 % when it stalled. That is an estimate from averages.
- **PERC's low figure may be its held note decaying**, so don't read it as a voice's full cost.
- **Master FX adds 2 points at 8 voices of ACD,** with the same slowest block.
- **These are Init patches.** A real patch on the same engine can cost more or less, and 1.2.0's records
  show 8 voices over 100 % on real patches. Nothing here promises every project fits.

### Device test: after the CHIP fix, 2026-11-05

`TrackerPlus_mfx_fixed_recorder.ptf`, from `/mfx_log.txt`. 68,456 blocks, none over time, no stall and no lockup
record. The log's project names are from the order of the plays, and its `voices` field still has the old bug.

| Project | Length | Average | Slowest | Longest main loop gap | Free heap |
| --- | --- | --- | --- | --- | --- |
| `R1`, no sends, Shimmer + BBD | 21 s | 37 % | 55 % | 11 ms | 80,780 B |
| `L8 8 voices Shimmer BBD`, the load that locked up | 15 s | 64 % | 82 % | 16 ms | 79,244 B |
| `R2 Real project sends`, Shimmer + BBD | 31 s | 38 % | 54 % | 10 ms | 80,840 B |
| `A1 Master FX sound`, stepping through the effects | 120 s | 20 % | 30 % | 16 ms | 85,392 B |

- Before the fix, `L8` read about 109 % when it stalled.
- Lowest free heap 77,344 B, on a short Play of `L8` repeated by hand. Pool peak 42 of 80, max stack 7,825 of 20,480.
- The user read about 80 % on `L8`, which agrees with the log, and about 60 % on `R2`, a little over the log's 54 %.
- `R1` and `R2` differ by 1 point at most, so these effects cost about the same with or without input.
- By ear, `A1` with each reverb and delay chosen on the Master screen: all seemed to work. A line in the log gives only
  the pair active at Stop, so the log doesn't confirm each effect separately.

Not asked for on the unit: `L1`, `L6` and `L7`, lighter than `L8`, nor the Reverb and Delay pages' knobs, Delete and
the Off/On switch as set steps. Those three have the emulator's UI tests. No Export or Saving test.

### Device test: with upstream's `main` of 2026-11-06

`TrackerPlus_mfx_main_recorder.ptf`, from `/mfx_log.txt`. 36,910 blocks, none over time, no stall and no lockup record.
Lowest free heap 79,116 B, pool peak 42 of 80, max stack 7,729 of 20,480.

| Project | Length | Average | Slowest | Longest main loop gap | Free heap |
| --- | --- | --- | --- | --- | --- |
| `L8 8 voices Shimmer BBD` | 22 s | 60 % | 78 % | 14 ms | 79,116 B |
| `R2 Real project sends`, effects switched while playing | 45 s | 38 % | 54 % | 18 ms | 81,044 B |
| `R2`, Shimmer + BBD | 22 s | 39 % | 50 % | 13 ms | 81,044 B |

- `L8` is 4 points lower than before the merge, on both figures. Upstream's further work on CHIP is the likely reason,
  as this project sounds CHIP. That isn't shown separately.
- `R2` loaded and played normally with samples in RAM by default, and its figures didn't rise.
- The user read about 78 % on `L8`, as the log has. On `R2` the user read about 50 %, and up to about 66 % while switching
  effects, recalled from the Config screen. The log's slowest is 54 %, and the gap hasn't been looked into.

### The stepped device test

The four-step test written here on 2026-11-04 is withdrawn. It reused the firmware that locked up,
with no fix and no way to tell why it stopped. Its reading of a plain 8-voice result was also wrong:
over 100 % is what 1.2.0's records predict, so it wouldn't clear masterfx.

[DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md) replaces it: `L1` to `L8`, lightest first, stopping at
the first problem. The firmware adds only the recorder; with it off, the build is byte-identical to the
one that locked up. The rest of the mod hasn't changed, so this test finds the cause and doesn't fix it.

### What ran

`tools/emu/test_masterfx.py` covers the effects themselves, `tools/emu/test_masterfx_rig.py` the
page, graph, stress, encoder wait, test project and Play path, and `tools/emu/test_sound_rig.py` the
rig. After the lockup, on the firmware going to the device (`tools/run_tests.py --tier release`, all
passing, 2026-11-04):

| Test | Ran | Result |
| --- | --- | --- |
| `boot_rig.py`, real boot, Open, Play button, main loop | Mods and reference, 8 held voices and the worst-case project, 300 blocks | No fault, hang or failed malloc. Pool 55 of 80, heap steady, stack 7,280 |
| The same, interrupt fired inside main-loop code | 8 held voices, mods and reference: about 9,900 interrupts in the main loop. Worst case, mods, reference and diagnostic: about 4,500 | Same |
| Ladder projects on the diagnostic build | `L1` to `L8`, 200 main loops each, about 1,700 interrupts in the main loop | Same, with the effects and notes each should have. Pool 49 at 1 and 4 voices, 55 at 8 |
| `stress_masterfx.py` | 20,000 blocks with garbage settings and memory guards | No stray access; each effect recovers |
| `test_bundle_cost.py` | Own code only | Heaviest effects 48,379 (Shimmer) and 23,261 (BBD) instructions a block, about the same right after a clear. No unaligned word access. The mods add at most 1,753 instructions to the sequencer's worst block, 83 on average |
| `test_lockup_recorder.py` | Each recorder path on the diagnostic build | Passes |

The same 24 passed again, 0 failed, on the firmware for the fourth set in
[DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md): this branch with the CHIP fix and upstream's optimisations of
2026-11-05. Its reference build is byte-identical to upstream `main` with `ChiptuneSynthVoice.cpp` from
polyend/tracker-ce#52. `boot_rig.py` opened and played `A1` and `R2` with the right parts and effects, with no fault.
`R2`'s output is silent on the rig, on the reference build too, so the rig says nothing about how it sounds. On `A1`, 300 blocks
with Shimmer + BBD give different output from Stock, with the same peak and 0.4 dB less energy, so its sends reach
the effects on the real path and add no level. The rig has no model of the display's SPI bus, so upstream's rewrite of it is left to the device.

The same 24 passed again, 0 failed, with upstream's `main` of 2026-11-06 merged, which has the CHIP fix itself, samples
in RAM by default and the USB storage changes. Its reference build is byte-identical to `main`.

Still not covered, so left to the device: anything that takes real time (CPU, overload, what a long
interrupt does to the screen, card and buttons), the real screen, the card's timing, the cache, a real
fault reaching the recorder's handlers, and the record surviving a real reset.

On the worst-case project one main loop during Play is about 360,000 instructions, against about
11,000 with held notes, from the interrupts that fired inside it. It's the same on the reference build, and I don't know yet what share of a
device's time that is.

The first device test went ahead without the Play path, though
[SOUND_TEST_PLAN.md](SOUND_TEST_PLAN.md) called for `Sequencer::tick` on mod tests. A device test
shouldn't be asked for until a rig run has gone through it, Play included.

On the rig, part 1's patch (`1.patch`) doesn't load, on the reference build too, so that part plays
CE's init patch. Whether a device does the same isn't known, and it doesn't change these results.

The UI test found one bug the plan had missed, now fixed: the lists stayed on the Reverb page,
because CE's `showReverbScreen` never had to hide them.

## 9. Sound test plan

[SOUND_TEST_PLAN.md](SOUND_TEST_PLAN.md) stages 1 and 3 came with this mod, level C only: level A
waits for the synths. "Each switch gives its silent clearing blocks" changes, since clearing now
happens in the main loop. That line is updated.

**The rig's main unknown is gone.** A throwaway probe on the reference build ran CE's real start-up
under Unicorn on its first try, using only stubs on open functions:

| Check | Result |
|---|---|
| 116 static constructors, `Synth::init`, `audioEngine::init` | All ran, under 3 s |
| A test note through `updateAudioBuffers` | Silence before, sound after (peak 21,492) |
| Same note on two fresh machines | Identical samples |
| Note off | Decays to 0 |
| 8 voices on one part | Sound, 94 ms per block, block pool peak 65 of 80, heap peak 68,628 B |
| 1 voice | 40 ms per block |
| The 7 engines (ids 1, 2, 4–8) | All make sound; MIDI Out silent, as it should be |
| Synth reverb send, synth delay send | Output differs from no send; return not yet checked by itself |

Ids 3 and 9–11 stopped the emulator. Those were my own test values: they aren't engines
`registry.cpp` registers, and I didn't look further.

**What the rig is:**

| Part | Status |
|---|---|
| `sound_rig.py`: start-up, fake `Platform`, licence guard, note and block helpers (level C, with level B a small step from it) | Built |
| `test_sound_rig.py`: all 7 engines, same twice, note off, Off = CE | Built |
| `masterfx_wavs.py`: each effect on a pluck, into `build/wavs/masterfx/` | Built |
| Level A, host render of our own engines | Comes with synths |
| Stages 2, 4, 5: Macros, recorder tap, our engines | Come with interpolate, resample, synths |

No firmware change. A second of audio takes 15–32 s here, so stress runs stay short or use the
object level. It can't measure CPU, which stays on the device.

**A thing to know when using it:** the rig calls `audioEngine::updateAudioBuffers`, without
`engine.update`. So a mixer input stays at CE's unity default until the test sets it, and a part
whose sends aren't set is heard by the reverb and delay. `Graph` in `test_masterfx_rig.py` sets all
three parts' sends, twice, because `AudioMixer` ramps from the gain before the last one. Sends are 0
to 100, as in the project file.

## 10. Open decisions

Answered 2026-11-04:

1. **UI:** Master page only for now (options B and C). A stays possible later.
2. **Rig:** built with masterfx: stage 1 and stage 3's level C. Level A and later stages stay with
   their mods.
3. **Names:** no changes, so section 4's table stands. After the device test, Tape and BBD went back to Feedback and
   Filter, at the user's choice.
4. **ITCM:** left to me: 5.3 KB, Shimmer and BBD, with the project-wide table in section 3.

## 11. Added beyond the brief

- For the re-think of the tests after the lockup: `tools/run_tests.py`, `tools/emu/boot_rig.py`,
  `tools/emu/stress_masterfx.py`, `tools/emu/test_bundle_cost.py`, the lockup recorder
  (`MT_LOCKUP_RECORDER`, in no normal build), its log line for each Stop, and the test project maker's
  `--worst`, `--effects`, `--ladder` and `--sound`.
- Clearing in the main loop instead of per block, and the 150 ms wait on the encoder. This changes
  1.2.0's behaviour: the interrupt carries no clearing load, and a switch takes effect just after
  you stop turning.
- Page title showing the effect.
- The boot gate is 1.2.0's rule, carried over.

Left out, could follow later:

- Gate length in ms instead of a percentage.
- `tools/ce_to_120.py` writing the choice back into a `masterfx.bin`. Today a 1.2.0 copy keeps
  whatever old `masterfx.bin` the project had, and choices made on CE don't travel back.
