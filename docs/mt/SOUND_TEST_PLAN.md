# Sound tests on CE: plan

Status: plan only, 2026-11-04. No rig or test exists yet. It's based on CE's source at
`b4a95ca` and the 1.2.0 sound tests in Modular-Trackular (`tools/emu/synth_rig.py`,
`reverb_rig.py`, `test_synths.py`, `test_masterfx.py`, `test_legato.py`,
`test_interpolate.py`). [What's checked and what isn't](#whats-checked-so-far) lists how
far each part has been confirmed.

The licence reading below is careful, but it isn't legal advice.

## Summary

- **Emulator tests move to full builds, with the synth libraries in.** Your call,
  2026-11-04: synths are a large part of the firmware and its biggest resource use, and
  the licence no longer stops it. `MT_NO_SYNTH` builds can't test engines at all, because
  they compile out all of `core/synth/`.
- **The same rig serves your engines, master FX and every other audio mod.** It runs CE's
  real host and audio graph, plays notes, keeps the audio and checks it with numpy, as the
  1.2.0 tests do.
- **Closed engines are a black box.** Notes go in and audio comes out. We can compare and
  measure that audio, but we don't look inside. Our own engines get the full 1.2.0 set of
  checks.
- **The code gets written in stages, each with the mod that first needs it.** This plan is
  written now. The first stage comes with legato, or sooner if engine work comes first.
- **Some things stay hardware-only.** The emulator has no device timing. The 1.2.0 WAVE
  screech appeared only on the device, and CPU cost is measured there with CE's Profiler.

## What carries over from 1.2.0

| 1.2.0 | On CE |
|---|---|
| Fake platform object, then the app's two synth setup calls | The same idea, now readable: `Synth::init`, then `audioEngine::init`, through a fake `Platform` built from `core/include/Platform.h` |
| Notes through the MIDI host, found by address | `Synth::triggerTestNote(part, note, on)`, and the sequencer's real note path for mod tests |
| One block through the whole graph (`0x6003061c`) | `audioEngine::updateAudioBuffers(left, right, 128)`, the interrupt's own entry, which calls `AudioStream::update_all` |
| Synth output captured at `transmit` | `AudioStream::transmit` stubbed or watched, around CE's open `PolySynth::update` |
| Each voice's float output, from a hook on its render | **Our engines only.** On closed engines this is hooking inside the library |
| Slot and voice fields read by offset (`SLOT_VOICES`, `VOICE_RECORD`) | **Dropped.** These are the library's internals. Use `Synth`'s public calls and the public headers |
| `Prof`: instruction counts per function | **Our code only.** Device cost comes from the Profiler ([Resources](#resources)) |
| Heap: sbrk extent minus `mallinfo` | The same: `_sbrk` and `mallinfo` are newlib's, not Polyend's |
| Write watch over all RAM during stress | **Dropped for closed code.** Use guard bytes around our own buffers |
| Reverb and delay driven through `update()`, block helpers stubbed | The same, by symbol: `AudioEffectPolyverb::update` is open source |
| WAV files for listening | The same, written to `build/`, never committed |
| `--quick` for what can differ between builds | Comes back when run times call for it |

**Don't bring 1.2.0's findings about the synth internals across.** The 1.2.0 work wasn't
done under this licence, and whether the licence reaches 1.2.0's copy of the code is
unclear. The CE rig is built only from CE's source and public headers.

## Licence rules for the rig

From `synth/lib/LICENSE-PROPRIETARY.txt`: section 1(b) lets a free, non-commercial project
use the unmodified libraries, and section 2(c) bans disassembling them, reverse engineering
them, or trying to derive their source code.

**Allowed:**
- Running the libraries, and calling them through CE's source or the public headers in
  `synth/include/PolySynth/`.
- Measuring what comes out: level, silence, whether two builds match, whether a note ends.
- Comparing a closed engine's output between two of our builds.

**Not allowed:**
- Disassembling them, or stepping through or logging their instructions.
- Hooks at addresses inside them, or patching them in memory.
- Reading their private objects by offset.
- Measuring an engine's output to work out how its algorithm works. "Did this build change
  it?" is fine. "How does this filter work?" isn't, so spectrum and filter-response checks
  are for our own engines only.

**Enforced in the rig, not by memory:**
- `sound_rig.py` gets its list of closed symbols from `arm-none-eabi-nm` on the two `.a`
  files (names only).
- Its hook helpers refuse any address that belongs to one of those symbols.
- It installs no hook without an address range.

**When something fails inside a closed engine:** treat it as a black box. Check what we fed
it, vary the inputs, or try it on hardware. Note the failure and don't dig further. This is
the real cost of the rule, compared with 1.2.0.

## The rig: `tools/emu/sound_rig.py`

Three levels, using the cheapest that can show the problem.

| Level | Runs | Good for |
|---|---|---|
| **A. Host render** | Our engine plus the SDK headers, compiled for the PC, no closed libraries | Pitch, filters, envelopes, aliasing, thousands of renders. Seconds. Our engines only |
| **B. Synth layer** | Emulator: `Synth` and `PolySynth::update`, with block helpers stubbed | All 7 engines through the real allocator and patches, per part |
| **C. Whole graph** | Emulator: `audioEngine` and `update_all` | Sends, reverb and delay, limiter, output, export, block pool |

**Builds**, both full, kept in `build/emu/` (ignored by git), and both from the same tree:
- `TrackerPlus_MT_full.axf`: the normal build.
- `TrackerPlus_ref_full.axf`: the same with `CUSTOM_DEFINES=-DMT_NO_MODS`, as the reference.

**Setup:**
1. Load the ELF. Run the static constructors between `__init_array_start` and
   `__init_array_end` (117 entries), skipping any that touch hardware.
2. Set up the heap, keeping the device's real limit so running out shows up: give `_sbrk`
   what the boot code would, or stub it with that limit.
3. Make `__assert_func` a test failure.
4. Stub `enableAudioInterrupt` and `disableAudioInterrupt`, and count them to catch
   unbalanced pairs (a real 1.2.0 bug).
5. Build the fake `Platform`. `getFileSystem` returns `FakeSd` from `test_step0.py`, extended
   with `listDirContents`. Other getters return fake objects.
6. Level B: construct a `Synth` and call `Synth::init` with a `std::function` that returns
   no tempo, or a fixed one. Then `PolySynth::init`.
7. Level C: `engine` is a global, so call `audioEngine::init(platform, &synth)`.

**Controls:**
- Engine and voices: `Synth::setInstrumentType`, `Synth::setVoiceCount`.
- Notes: `Synth::triggerTestNote`; for mods, `Sequencer::tick` once per block, as the
  interrupt does, so the real note path runs.
- Parameters: `audioEngine::setSynthParam`, MIDI CCs through `Synth::getVirtualMidiPort`, and
  `Synth::getPatch` with the public `Patch` header.
- Export: set `AudioStream::renderMode` and call `artificial_isr`. `PolySynth::update` takes a
  different path for an idle part in render mode, so it needs its own check.

**Memory:** `machine.py`'s `SCRATCH` is in a region of its own outside the chip's map
(`0x90000000`), so it can't overlap the image.

## The checks

### Any mod that touches audio

| Check | How | From |
|---|---|---|
| **Same twice** | One note script run twice gives identical samples | `test_synths` "the rig" |
| **Off = CE** | With the mod Off, every engine's output equals the reference build's, sample for sample | "Stock output", masterfx section 1 |
| **Mod On but unused** | A mod On but not used on a part leaves that part identical | "WAVE replacing ACD" |
| **No bad samples** | No NaN or infinity at the float stage of our code, no stuck full scale at the output, silence after notes end | Stress runs |
| **Block pool** | `AudioStream::memory_used_max` stays under the 80 blocks of `AudioMemory(80)`, with all 8 voices sounding | New on CE |
| **Interrupt balance** | Enable and disable counts match | The sy1 bug |
| **Heap** | Same `mallinfo` use after 50 engine switches as before | "No heap leak" |

### Closed engines (ACD, FAT, VAP, WTFM, PERC, DIRT), black box

- A note gives sound, a note off leads to silence, and more notes than voices don't crash.
- The Init patch's level at 1 voice and at 8 stays within limits, as a regression check
  against the reference build.
- Build-to-build comparison only. No analysis of how they sound.

### Our engines (WAVE, PHASE, later ones) and CHIP

CHIP is open source and written against the same SDK, so it proves the checks work before
ours are ported. Everything in `test_synths.py`'s list applies, measured the same way:

- **Level A:**
  - pitch within 1 cent, notes 24–108;
  - saw aliasing below -60 dB at notes 96–108;
  - filter cutoff, slopes and stability at Resonance 1.5 and 18 kHz;
  - envelope times within 5 %;
  - chorus taps, Drive, wavetable banks;
  - the render against 1.2.0 reference values.
- **Level B:**
  - voices stop when the amp envelope ends, which frees them for the allocator;
  - more notes than voices;
  - the Init patch's level for one note and for a four-note chord;
  - Macros reach `onParamChange` and move the sound;
  - patch save and load through `FakeSd`, which 1.2.0 never tested;
  - own-code instructions, idle and playing, as a regression number (1.2.0 limits: under
    500 idle, 16,000 playing per block).
- **Level C:** stereo through the graph, sends, export.
- **Stress** (2,000 blocks): random notes, parameters including garbage and NaN, engine
  switches, voice changes, then the Init values again. Guard bytes around our buffers.
- **SDK rules:** a stub on `malloc` during `renderVoiceAudio`, `onNoteOn`, `onNoteOff` and
  `onParamChange` fails the test, since those run in the interrupt.

### Master FX

Taken from `test_masterfx.py`:
- **Stock:** identical to the reference build with Stock chosen; each switch gives its silent
  clearing blocks, then the chosen effect.
- **Each effect, at object level (`update()`):**
  - bypass and missing-block handling, no clipping;
  - decay, Size, Damping, Predelay, echo times, feedback at 0.99 with no NaN.
- **New on CE, at level C with synths playing:** a synth's reverb and delay sends reach the
  chosen effect; only the chosen effect runs, which the device's budget depends on.
- **Stress and listening:** `stress_masterfx.py`'s randomized sessions; WAVs of the same
  test sounds through each effect, for you to listen to.

## Resources

You said synths should be weighed when testing mods. Each resource goes where it can be
measured honestly:

| Resource | Where | How |
|---|---|---|
| CPU | **Device** | Profiler, Config's CPU lines, with all 8 synth voices sounding. Compare the mod On and Off. Emulator counts don't predict device time, since the device is limited by memory stalls |
| Flash and fixed RAM | Build | The linker's memory report on the full build, mod by mod. An `MT_NO_SYNTH` build's numbers would be misleading |
| Heap | Emulator | `mallinfo`, with engines loaded at 8 voices |
| Audio blocks | Emulator | `memory_used_max` |
| Stack | Device | `Profiler::maxStackBytes` |
| Own-code instructions | Emulator | Our code's address range only, as a regression number, never as a budget |

## What stays on the hardware checklist

- Chords on 8 voices for every new engine. The 1.2.0 screech came from two held notes and
  never showed in the emulator.
- CPU with all 8 voices sounding, and audio dropouts.
- How it sounds: the WAV files help, but the verdict is yours.
- SD streaming under load.

As usual, Break FX and Performance mode stay emulator-only.

## Mod by mod

| Mod | Needs | Checks |
|---|---|---|
| swing, humanize, copypattern, tracklen, songloop, colorsong, visualizer | Their usual tests, run on the full build | No sound test: they change notes, screens or lights, not audio |
| legato | B, plus the sequencer | With G, the note-on arrives before the note-off; envelopes don't retrigger on engines with Glide Mode Legato, and do without G; Off = CE |
| interpolate | B, plus the sequencer | A Macro ramp reaches the synth each microstep and moves the sound; Off = CE |
| slicethru Loop | C | Off = CE; loop position stays on 1.2.0-style sample-position checks; streaming on hardware |
| masterfx | Object level and C | [Master FX](#master-fx) |
| resample | C | Recorded blocks equal the master's or the track's blocks, synth tracks included |
| synths and new engines | A, B and C | [Our engines](#our-engines-wave-phase-later-ones-and-chip) |

## Build order

Each stage is written with the mod that first needs it, and reused after that.

| Stage | When | What | Done when |
|---|---|---|---|
| 0 | Now | This plan. Mod tests run on a full build too | `test_step0.py` and `test_humanize.py` pass on a full build (they do) |
| 1 | legato, or the first engine work | `sound_rig.py` levels A and B, the symbol guard, reference build | All 7 engines: note gives sound, note off gives silence, same twice, Off = CE |
| 2 | interpolate | Parameters and Macros at level B | A ramp moves the sound on CHIP and on a closed engine |
| 3 | masterfx | Level C, object-level effect rig, WAV output | The Master FX checks, and the block pool at 8 voices |
| 4 | resample | Recorder tap at level C | Recorded blocks equal the source |
| 5 | synths | Everything under [Our engines](#our-engines-wave-phase-later-ones-and-chip), ported from `test_synths.py` | WAVE and PHASE match their 1.2.0 references |

**If stage 1 fails:** the biggest unknown is whether `Synth::init` and the library's start-up
run in Unicorn without a problem we aren't allowed to investigate. If they don't, level A
still covers our engines' DSP, and everything else moves to the hardware checklist. Stop and
report rather than dig.

**Test time:** run the level that fits. The 1.2.0 synth test took about 22 minutes in full,
with `--quick` for repeat runs. On CE, level A and most of the checks above should run in
seconds to a few minutes; the slow parts are stress and level C. Those are aims, not
measurements.

## What's checked so far

**Confirmed on 2026-11-04:**
- A full Tracker+ build links, and `test_step0.py` and `test_humanize.py` pass on it with 0
  failures. Neither exercises synth audio.
- The symbols named here exist in that build: `Synth::init`, `Synth::triggerTestNote`,
  `audioEngine::init`, `audioEngine::updateAudioBuffers`, `audioEngine::setSynthParam`,
  `AudioStream::update_all`, `artificial_isr`, `Sequencer::tick`, `mallinfo`, `_sbrk`,
  `__assert_func`, `AudioEffectPolyverb::update`, `Profiler::maxStackBytes`.
- `ChiptuneSynthVoice.cpp` passes a syntax-only compile with host `g++` against the SDK
  headers. It hasn't been linked or run on the PC.
- The `SCRATCH` overlap, from the full image's symbol table.

**Not tried yet:**
- Any of this rig: `Synth::init`, a closed engine, or the graph under Unicorn.
- Whether output is the same twice on CE. It was on 1.2.0.
- Which static constructors touch hardware, what `_sbrk` needs, and which further `Platform`
  and `FileSystem` methods get called.
- Whether the output buffers of `updateAudioBuffers` are easy to read as plain samples.
- Whether Level A links without the closed libraries. The SDK's base class looks header-only,
  but `InstrumentTypeRegistry` and the host are in the library, so level A would call the
  voice directly.
