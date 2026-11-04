# masterfx on CE: build plan

Status: steps 1 to 4 are built and pass in the emulator, 2026-11-04. Step 5, the device test in
section 8, is waiting on its readings. Sections 2, 3 and 4 give what was built; the CPU figures are
still estimates until then. Based on CE's source at `af01d47`, a reference build of it, and 1.2.0's
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
| Flash | 19,384 B | 14,883,532 B | 1 MiB |
| ITCM | 5,584 B | 17,112 B | 1,024 B |
| DTCM | 1,144 B | 19,824 B gap | 4,096 B |
| SDRAM | 0: CE's own buffers | 1,037,868 B | 256 KiB |
| OCRAM, heap | 0 | 9,556 B OCRAM | Mods stay out |

**ITCM choice:** `shimmer_process` and its passes (2,832 B) and `bbd_process` and `bbd_coefs`
(2,420 B): the costliest reverb and delay. Moving all 15.2 KB of loops would save only about 1.4
more points and leave about 6 KB for everything else. Per-loop sizes: Tape 1,660, Spring 1,780,
Plate 2,092, Gated 2,176, Freeverb 2,250. `tools/mem_budget.py` gives the final figures.

**ITCM for the whole project.** 21,672 B is usable above the floor. Hot code is my estimate, from
each mod's 1.2.0 function sizes:

| Who | Code on 1.2.0 | Hot part, estimate |
|---|---|---|
| legato, tracklen, slicethru Loop, resample, songloop, visualizer's step path | 20 KB in total | 2.5 KB |
| interpolate (the port plan puts its microstep path in ITCM) | 3.5 KB | 1 KB |
| WAVE and PHASE voice loops: `wt_voice_render` 5,216, `ph_voice_render` 2,552, `wt_saw` 2,126 | 516 KB, mostly tables | 10 KB |
| masterfx, this plan | | 5.3 KB |
| **Total** | | **18.8 KB, leaving about 2.9 KB** |

- Everything fits, including the synths' voice loops. Moving all the synths' hot helpers too (LFOs,
  envelopes, about 4 KB more) wouldn't fit.
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
| Tape | Feedback → Intensity, Filter → Tone |
| BBD | Filter → Tone |

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

Emulator limits: it doesn't run boot, it doesn't model the cache (line counts are my stand-in),
and it can't see the screen.

### Smallest hardware test: one firmware, one sitting

The firmware and generated test project come with the step 5 report. The project is a copy of
`hellish downtown` with three synth parts holding 8 voices in total, sending to both effects, and
every pattern holding those 8 notes. Its 1.2.0 `masterfx.bin` brings in Shimmer + BBD.

1. **Boot:** open the test project, so it's the last project. Power off and on. It reaches the
   Pattern screen.
2. **Master page (title Master 1/3):** Reverb and Delay lists show, with all labels and the frame.
   Left/Right stops on both. Turn the encoder on Reverb to Plate: the sound changes. Press the
   Reverb button: labels read Decay, Damping, Predelay, Diffusion.
3. **CPU:** with an empty `/.develop` on the card, open Config and note the CPU average and max at
   each of these, changing them under Project Settings > Master FX. CE's max and average run from
   the last Play pressed on the Config screen, so after each change press Play to stop and Play to
   start, and read after 15 seconds:
   - Reverb Stock, Delay Stock
   - Reverb Shimmer, Delay BBD, PingPong On (both in ITCM)
   - Reverb Freeverb, Delay Tape, PingPong On (both in flash)

   Also note free heap and max stack once. No crackle at any of them is the other half of the
   check.
4. **Off:** Project Settings > Mods > Master FX Off. The Master page looks as it did before the mod.

What the readings mean:

- **Max under about 90 % on all three:** done.
- **Second or third row over, first row under:** I move the next loops to ITCM and you repeat
  step 3 only.
- **First row already over:** that's CE with 8 voices and not this mod, and I report it.

### What ran

`tools/emu/test_masterfx.py` covers the effects themselves, `tools/emu/test_masterfx_rig.py` the
page, graph, stress, encoder wait and test project, and `tools/emu/test_sound_rig.py` the rig. Not
covered, and left to the device: boot itself, CPU, the real screen, and the sequencer's own note
path (`tools/emu/make_masterfx_test_project.py`'s pattern is read back through CE's reader, and its
notes are played through `Synth`'s calls).

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
3. **Names:** no changes, so section 4's table stands.
4. **ITCM:** left to me: 5.3 KB, Shimmer and BBD, with the project-wide table in section 3.

## 11. Added beyond the brief

- Clearing in the main loop instead of per block, and the 150 ms wait on the encoder. This changes
  1.2.0's behaviour: the interrupt carries no clearing load, and a switch takes effect just after
  you stop turning.
- Page title showing the effect.
- The boot gate is 1.2.0's rule, carried over.

Left out, could follow later:

- Gate length in ms instead of a percentage.
- `tools/ce_to_120.py` writing the choice back into a `masterfx.bin`. Today a 1.2.0 copy keeps
  whatever old `masterfx.bin` the project had, and choices made on CE don't travel back.
