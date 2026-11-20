# CE firmware map (Tracker+)

How Tracker CE works on the Tracker+ hardware, from its source and from the linked builds.
This is the CE counterpart of `docs/FINDINGS.md` in Modular-Trackular. Written 2026-11-04,
from upstream CE plus our `modular` branch.

Numbers come from the built ELF of a Tracker+ build, not from documents. Where a number is
for this worktree (Color Song included) it says so. Section 16 says how much of the source
was read. Anything not listed there as read in full hasn't been checked line by line.

The closed synth libraries are a black box under their licence (section 2(c)), so they
appear here only as totals.

## 1. Chip and clocks

- i.MX RT1051, Cortex-M7, single-precision FPU. `CPU_FREQ` is 720 MHz
  (`platforms/imx/config/config.h`, applied by `mainCpu.setFrequency`). The developer guide
  says 600 MHz.
- Clock tree: AHB 528, SEMC 132, FlexSPI 66, IPG 132, USDHC1 198, LPSPI 57.6 MHz. SAI1 MCLK
  11.2896 MHz.
- FlexRAM is split by `ResetISR` (`IOMUXC_GPR17 = 0xAAAAABFD`): OCRAM 1 bank (32 KB), ITCM 4
  banks (128 KB), DTCM 11 banks (352 KB). This is our source, so it can be moved in 32 KB
  units if it ever has to be. Nothing here needs that today.
- MPU (`platforms/imx/board/board.c`): ITCM, DTCM, OCRAM and SDRAM are all Normal write-back
  cacheable; flash is read-only. The guide's "non-cacheable OCRAM for USB/SDMA" doesn't match
  the MPU.

## 2. Memory map

From `platforms/imx/linker/TrackerPlus/memory.ld`:

| Region | Address | Size | Speed | Used for |
| --- | --- | --- | --- | --- |
| Flash (QSPI) | 0x60030000 | 16,580,608 | slow, D-cache | code, constants |
| ITCM | 0x00000008 | 131,064 | fastest | audio code |
| DTCM | 0x20000000 | 360,448 | fastest | variables, heap, stack |
| OCRAM | 0x20200000 | 32,768 | fast | `mtProject`, audio play buffer |
| SDRAM | 0x80000000 | 67,108,864 | slow, D-cache | samples, buffers |

`.sample_memory` sits in `.noinit_RAM4` after scratch, delay and reverb. Nothing in `.noinit`
is zeroed at startup, so anything placed there must be initialised by its owner.

## 3. Boot

`ResetISR` (`platforms/imx/startup/startup_mimxrt1051.cpp`): FlexRAM split, `SystemInit`,
copy data sections, zero bss, fill the stack with the watermark 0xAA, `__libc_init_array`,
`main`.

`main` (`platforms/imx/source/PlatformTrackerIMX.cpp`):

1. `BOARD_ConfigMPU`, pins, clocks, peripherals, CPU frequency.
2. `usb->init`, debug console, `timeControl.init`.
3. `initHardware`: power, LPSPI4 display, I2C4, early start screen, I2C2 radio, buttons,
   SGTL5000 codec, audio hardware, pads and LEDs on I2C4, encoder.
4. `platform.init`, `trackerInstance->init`, then `enableMallocCheck = true`. **After this a
   failed malloc asserts and hangs.**
5. Loop: `usb->update`, `updateHardware` (I2C buses, SDRAM, power, encoder, bootloader-reset
   check), `platform.update`, `trackerInstance->update`.

`end_dummy[4096]` in `.end` works around a bootloader bug that doesn't erase a last sector
holding under 256 bytes.

`TrackerImpl::init`: wavetable envelopes, timers, `midiInit`, `synth.init`, `engine.init`,
`sequencer.init`, pads backlight, exporter, `Profiler::init`, `initConfig`,
`mtInterface.begin`, `newFileManager.init`, `mods::init`, then dev mode from `/.develop`.

## 4. Main loop and interrupts

`TrackerImpl::update`, in order: `engine.updateSdStream`, `display.update`, `updateSdStream`,
pads backlight, exporter, slice detector, `newFileManager.update`, `mtInterface.update`,
`updateSdStream`, `sequencer.update`.

Audio interrupt: DMA0_DMA16 → `updateBuffers` → `TrackerImpl::updateAudioBuffers`:
`midiSetAudioBlockStartUs`, `midiUpdate`, `sequencer.tick(len/44100)`, `engine.update`,
`engine.updateAudioBuffers` → `AudioStream::update_all`. One block is 128 samples at
44,100 Hz (2.9 ms).

Priorities (`interruptsConfig.h`, lower is more urgent): SysTick and USB device 0; MIDI UART
and TMR4 1; audio in and out 3; SDHC 6; GPIO and LCD SPI DMA 15. All share one stack.

`CLAUDE.md` lists the paths where CE does slow work in the interrupt.

## 5. Audio

A Teensy-style graph: `AudioStream` objects joined by `AudioConnection`s, 128-sample blocks
from a pool set by `AudioMemory(80)` (20,800 bytes in DTCM). The connections are heap
objects, created by the constructors.

- **Voice** (`AudioVoice`), per channel (stereo on IMX): `AudioPlayMemory` → `AudioBitDepth`
  → `AudioFilterStateVariable` → `AudioAmplifier` → panning amplifier, plus RMS. Eight
  `playerEngine`s (`instrumentPlayer`), each with envelope generators.
- **Mix** (`MixerAndFxs`): voices and synth parts → `tracksAndSynthMixer` →
  `tracksAndSendsMixer`, with sends through `preFxMixer` into `AudioEffectShortDelay` and
  `AudioEffectPolyverb`, RMS on each. Then limiter, final mixer, output and USB output.
- `playerEngine::update` runs per voice in the interrupt (`mtAudioUpdateHandle.cpp`): end of
  play, envelopes, LFOs, tune, loop points. `noteOn` is `mtAudioNoteOnHandle.cpp`.
- **Placement:** `AUDIO_CORE_RAM` puts a function in ITCM; `AUDIO_CORE_FLASH` is an explicit
  note that it stays in flash. `AudioStream::allocate`, `update_all` and
  `AudioPlayMemory::update` are in ITCM. `Sequencer::play_microStep` moved there upstream on 2026-11-05 (5,988 B).
  `AudioPlayMemory::play` and `applyRowCustomFx` are flash.
- **Recorder** (`core/source/Recorder.cpp`): runs from `engine.update`. It records straight
  into `externalRamSampleMemory`, sized `SAMPLE_MEMORY_SIZE`, and overwrites the loaded
  samples (`wasSampleMemoryOverwritten`), which are reloaded afterwards
  (`SampleMemoryReload`). Longest stereo recording: 62,849,024 / 4 / 44,100 = 356 s in plain
  CE, 350 s with the mods' 1 MiB taken out.
- **Sample editor** (`core/effector`): its edit area starts at
  `EDIT_BUFF_OFFSET = SAMPLE_MEMORY_SIZE - 2 * 2 * EDIT_BUFF_SIZE`, the top 15 MB of the
  pool, so they move with the constant. `mtEffect` handles overlap with loaded samples.
- `.ptfx` custom effects load code and scratch into `externalRamScratchBuff`.
- **Master effects.** `AudioEffectPolyverb::update` (1,264 B) and
  `AudioEffectShortDelay::update` and `process` (about 580 B) run in ITCM. Reverb buffers: the
  first, `internalRamReverbBuff`, is in DTCM (on 1.2.0, SDRAM) and holds the allpasses and the
  predelay; the second, `externalRamReverbBuff`, is in SDRAM. Per block, with a 0.5 s delay:
  reverb 33,399 instructions and 36 SDRAM lines, delay 11,661 and 51
  ([plan](MASTERFX_PORT_PLAN.md)). The exporter uses the delay's `isSilent()` to set the tail, and
  gives the reverb a fixed 300 ms. `reset()` and `clear()` run on project load, export, Sample
  Editor and Sample Recorder.

## 6. Streaming (the change that decides sample memory)

`core/AudioEngine/VoiceStreamManager`, 1,771 lines. CE plays large samples from SD.

- A sample of at least `kStreamThresholdBytes` (256 KB) streams if `isPlayModeStreamable`,
  which is every mode on IMX except Wavetable and Granular
  (`fileManagerInstrument.cpp:146`).
- A streamed sample keeps only `kStreamPreCacheFrames` (32,768 frames per channel, 128 KB
  stereo) in RAM. `used_memory` counts only that.
- Each voice has a ring of 16,384 frames per channel (`s_voiceRingBuffers`, 524,288 bytes).
  `kChunkFrames` = 4,096 async ADMA2 reads are pumped from the main loop; `getSample` and
  playhead updates run in the interrupt from ITCM.

So in the streamable modes, 48 instruments need at most about 48 × 256 KB = **12.3 MB** of
the ~60 MB pool. Only long Wavetable and Granular samples must fit whole. The pool is that
size because of recording length and 1.2.0 history, not because normal projects fill it.
`docs/developer_guide.md` section 1 says the same: about 29 MB of it is reclaimable, with a
floor of 31 MB (15 MB edit area and 16 MB resident).

## 7. Sequencer

`core/source/mtSequencer.cpp`. `tick` (double accumulator) → `tick_uStep` → `play_microStep`
per track. 1 step = 48 micro steps = 576 nanosteps. External clock comes through
`externalClockPulses`.

On micro step 1 a track copies its step, applies custom FX, checks for the end of the pattern
and calls `loadNextPattern`, which can reach file code from the interrupt (see `CLAUDE.md`).
Two RAM patterns `seq[2]` in SDRAM swap in `switchRamPatternsNow`.

`strPattern` format 6: 28-byte header, 16 tracks of (length + 128 × 8-byte steps), CRC:
16,432 bytes.

## 8. MIDI and timers

`midiUpdate` drains UART and USB inside the audio interrupt; handlers are registered in
`midiInit` (`mtMidi.cpp`). MIDI clock can be scheduled sub-block with
`scheduleMidiClockUs`, which fires from TMR4 at priority 1 (`TimerImpl.cpp`).

## 9. Files and projects

FatFs with LFN and exFAT, 512-byte sectors (`platforms/imx/config/ffconf.h`).
`FileSystemImpl` wraps it and adds async sector reads and fast seek for streaming.

`cFileManager::update` runs one `currentOperation` per main-loop pass; when idle,
`autoSaveProjectToWorkspace` saves every 10 s or on `save_asap`.

`/Workspace` is the working copy (`project.mt`, `patterns/pattern_%02d.mtp`, `instruments`).

| Operation | Steps |
| --- | --- |
| `ProjectOpen` | load project file, save it to Workspace, copy patterns, copy instruments, copy samples |
| `WorkspaceProjectLoad` | project file, per instrument struct and sample, pattern |
| `WorkspaceProjectSave` | pattern, instrument struct, instrument sample, project file |
| `ProjectSave` | copy project file, patterns, instruments to the project folder |

Instrument headers are copy-on-write (392 bytes, developer guide section 3). The load check
is `sampleDataSizeMax > max_memory - used_memory` (`fileManagerInstrument.cpp:281`);
`max_memory = SAMPLE_MEMORY_SIZE` is set at runtime (`fileManagerProject.cpp:199`), so changing
the constant doesn't change any file format.

Config is a sectioned struct kept by `ConfigManagerImpl`; `mods.cfg` and `mods.bin` are ours.

## 10. Firmware update

Config lists `.ptf` files (`updateBrowseFirmwares`). `PlatformImpl::runBootloader` writes the
chosen name to `kFirmwareInfoFilePath`, writes "start" to `kTurnOnFilePath`, and calls
`resetMCU`. The bootloader is outside the image at the bottom of flash, below 0x60030000.

## 11. UI

`Interface::modules[13]`: projectEditor 0, sampleImporter 1, patternEditor 2, samplePlayback 3,
songEditor 4, instrumentEditor 5, sampleEditor 6, configEditor 7, sampleRecorder 8,
performanceMode 9, imageViewer 10, nullptr 11 (the removed NES emulator), masterParams 12.

Each module is a `ModuleBase` with `start`, `stop`, `initDisplayControls`, `update`. Input goes
`Interface::buttonChange/padPressed/potChange` → `InputProcessor` → the callbacks the active
module registered with `setButtonObj`, `setPotObj`, `setPadsGlobal`. Popups and `mtPopups` are
main-loop only.

Biggest own files: `fPatternEdit` 5,320 lines, `fIinstrEdit` 3,571, `fSamplePlay` 3,425,
`fSampleRec` 3,230, `fMasterParams` 3,110.

**Master screen (`core/interface/masterParams`).** Columns are 100 px wide. Each screen's
`show*Screen` sets its own labels and hides or shows each control by name, so a new control must
be added to all of them. `initDisplayControls` runs at each module start, so controls return to
their created state. Hidden lists on the Master page: `delayPingpongEnableList` (column 0),
`delaySyncEnableList` (1), `delaySyncRateList` (2), `limiterSidechainList` (4). `selectedPlace`,
`selectedPlaceReverb` and `selectedPlaceDelay` are kept separately. Lists follow a pattern: first
press selects, second cycles. `shiftHandler` polls Shift every 50 ms, and it's what turns Line in
to Mute/Unmute. MIDI CC reaches only volumes (71–82), never reverb or delay knobs.

## 12. Display

FT812 over LPSPI4. Since CE's 9d6d10a (merged 2026-11-15) the MCU builds each control's display
list (`cDisplay::buildControl`, `core/libs/FT812/DisplayList.cpp` expands text, numbers, gradients
and bitmap set-up into the words the chip would make) into a 56 KB arena in SDRAM. `composeFrame`
adds the shown controls from the highest slot down, skipping any that doesn't fit the 8,172 bytes
(`CLAUDE.md`), and only the stretches that differ from the chip's back list are sent to RAM_DL. On
the IMX, stretches of 48 words or more go by eDMA channel 5 (LPSPI4 Tx request, no interrupt); audio
is on channels 0 and 1. `MCU_CSlow`, where every chip access starts, waits for that DMA. A pass of
the main loop spends at most about 1 ms building and 320 words sending. The coprocessor is left with
image decoding and screenshots. Themes are JSON (`ThemeManager`) and fonts are decompressed into SDRAM. 15,684 lines in `core/libs/NewDisplay`.

Each control's `ramSize` is now the MCU's own count of its list (before 9d6d10a it was what the
coprocessor wrote, which only the device knew).
Bytes from source: `cList` about 206 B (background 28, shadow with `CMD_GRADIENT`, selection frame
36) plus per line a 4 B colour and a `CMD_TEXT`. For planning, take at most 24 B per text plus 12 B
per glyph, and 120 B per gradient. The Master page comes to about 4.2 KB at those bounds. 65
control slots; `new` in `createControl` uses the heap, so reuse a hidden control where one fits.
A later-created control has a higher slot and draws first, so `blankSpaceCover` draws over lists
created after it.

## 13. Synth

`core/synth/Synth.cpp` wraps the closed `polysynth` libraries: 3 parts, 8 voices at most
(`kSynthParts`, `kSynthMaxVoices`). `synth.init` creates patch stores, a MIDI adapter and three
instruments on the heap. `core/synth/engines/ChiptuneSynthVoice.cpp` is open. Part of the
libraries is placed in ITCM by the linker script.

**Since CE's plugin synths (merged 2026-11-13, CE `9cb0964`).** The host is open now (MIT,
`synth/source`); the factory engines are closed `.psyn` files in `/Synths` on the card (`/Synth` as
well), loaded by `PsynLoader` into a 1,920 KB arena inside `s_psynRamPool` (6 MB of SDRAM). Their
code runs from SDRAM, through the cache, not from ITCM. Engines register with
`registerSynthEngine` (`InstrumentTypeRegistry.h`), `PsynLoader` for each `.psyn` it finds. WAVE and
PHASE are `.psyn` files too (`core/mods/synths/psyn/`, built by `make psyn`), with the type ids 64 and
65 in their headers. `synthTypes` is `kSynthTypes` filtered to what is
available, then the other ids, so a project's model index moves when `.psyn` files are missing.
`PolySynth::update` stops the synth for 32 blocks when its parts take more than 2,100 us in a
block (`kMaxSynthRealtimeBudgetUs`, about 72 % of 2,902 us), and skips a part while
`part.loading` is set. Synth FX runs inside that time.

## 14. USB

Composite device (`platforms/imx/usb/composite.c`): MIDI, audio in, audio out, plus mass storage
in file-transfer mode. `handleFileTransferMode` swaps the class and resets the bus.

## 15. Memory budget

Exact, from the ELF, with `tools/mem_budget.py`, on the merge of CE `422e0ed` into `modular`
(Synths 1.1, the heap trim below), 2026-11-19. "Mods" is the release mod build, every mod in, with the 1 MiB
taken from sample memory.

| Region | Size | Plain CE free | Mods free | Floor | Notes |
| --- | --- | --- | --- | --- | --- |
| Flash | 16,580,608 | 15,250,140 | 15,101,660 | 1 MiB | 1.2.0 images up to 8 MB flashed whole; CE 422e0ed's fmt without `std::locale` freed about 125 KB |
| ITCM | 131,064 | 68,176 | 37,984 | 1,024 | closed libs, audio code, masterfx's 5,584 |
| DTCM unreserved | gap | 14,932 | 8,936 | 4,096 | heap end to stack base; 4,616 before CE 422e0ed (its fmt build without `std::locale` gave back 4,320), 5,152 before CE 922bba0 (its stereo mirror in each voice and the menus' vectors took 536), 4,128 before the trim; the recorder (diag) build has 7,416 |
| OCRAM | 32,768 | 9,556 | 9,556 | 1,024 | mods stay out |
| SDRAM unreserved | 67,108,864 | 21,048 | 610,824 | 256 KiB | the pool takes all the rest in plain CE |

**Heap trim (mods builds only).** `platforms/imx/makefile` passes `--defsym=_MtHeapTrim=0x400` to
the link of a Tracker+ build without `MT_NO_MODS` (the same rule as `core/mods/modsBuild.h`), and
`linkerScript.ld` takes it off `_HeapSize`: the heap is 137,728 instead of 138,752, and the 1,024
bytes join the DTCM gap. Plain CE and Tracker Mini link exactly as before (the plain image is byte
for byte the same, but for the build date). The mods' own variables stay where they are; nothing
moves to the heap. The cost is 1,024 bytes less for CE's `malloc`, where running out hangs the unit. With
the trim, the lowest free heap on the unit is 60,336 B (3 PERC voices, the 2026-11-18 reading below).
Made for the Resample port (`docs/mt/RESAMPLE_PORT_PLAN.md` section 12).

Mods use 31,616 bytes of SDRAM so far: `projectBuffer` 10,496, `configBuffer` 512, Color Song
12,144, Variable Track Length 8,443 and alignment.

**DTCM layout** (mods, an older reading): `.data` 6,120, `.bss` about 123 KB, `.noinit` 32,768 (reverb), heap
138,752 fixed (`_HeapSize`; 137,728 in mods builds since the trim), free gap 15,480, stack 20,480 at 0x20053000–0x20058000. The
`.heap2stackfill` reservation of 20,480 is a linker check, not memory in use. About 1.8 KB
before `.bss` (0x200068F0–0x20007000) is alignment padding for the buffer `.bss` starts with.

Biggest in DTCM (plain CE): `internalRamReverbBuff` 32,768; audio pool 20,800;
`instrumentPlayer` 18,048; `engine` 9,792; MSC read and write buffers 4,096 each;
`interfaceGlobals` 2,980; `sequencer` 2,312; `triangleTab`, `gaussTab`, `squareTab` 2,000 each
(built at startup, could go to flash); `filterFreq`, `filterFmult` 1,912 each.

**Device readings, 2026-11-04** (`/.develop` makes Config show these once a second,
`fConfigEdit.cpp:103`), with hellish downtown open:

| Reading | swing build | colorsong2 build | Of |
| --- | --- | --- | --- |
| Free heap | 80,472 | 80,708 | 138,752 |
| Max stack | 7,777 | 7,105 | 20,480 |
| CPU min, avg, max | 20%, 29%, 40% | 16%, 33%, 55% | |

The colorsong2 reading was taken after the Sample Recorder, the Sample Editor and a few seconds
of song playback, with Color Song On. Free heap is a total, not the largest block. Max stack is
the most used since boot, so it only covers what had run by then. Hellish downtown is a small
project (6 MB).

**Device reading, 2026-11-07,** tracklen recorder build, across the ladder and a new project in one
boot: free heap 84,636 at `L1`, 78,892 at `L8`, 75,560 in a new project, then 73,852, the lowest logged.
It steps down at project and voice-count changes and stays flat in between. Variable Track Length
doesn't allocate; the steps haven't been looked into. Max stack 7,761.

**Device reading, 2026-11-07, after merging upstream's `main` (`bc3a758`),** recorder build: `L1`, `L5` and
`L8` slowest 34 %, 50 % and 80 %, `L8` averaging 63 % over 25 s, 1 to 2 points above the readings before the
merge. 23,747 blocks, none over time, no stall and no lockup record. Longest main loop gap 22 ms, lowest free
heap 78,624, max stack 7,721.

**Device reading, 2026-11-08, mod registry (`b772fa6`),** recorder build: a project with Global Swing and Master
FX On, Swing and Master FX changed while playing, saved, then the unit powered off and on. Both took effect at
once and came back after the restart. 7 plays, 25,711 blocks, none over time, no stall and no lockup record.
Audio interrupt average 37 to 63 %, slowest 78 %. Longest main loop gap 34 ms, lowest free heap 78,856, max
stack 7,713.

**Device reading, 2026-11-08, Copy to New (`1ff23e7`),** recorder build: `L1`, then a new project with copies
made stopped, playing, in song mode and while recording, with Humanize, Track Length and Color Song in turn.
50 plays, 71,397 blocks, none over time, no stall and no lockup record. Audio interrupt average 20 to 31 %,
slowest 49 %. Longest main loop gap 22 ms, lowest free heap 74,760, max stack 7,841. The mod allocates
nothing; the lowest heap figures are all from the new project, and `L1`'s play shows 84,344.

**Device reading, 2026-11-09, Song Loop (on `19a1f1a`),** recorder build: `L1`, then a new project with a
looped song, the block moved while playing, the font switched during the loop, and the mod switched Off
and On. 5 plays, 57,901 blocks, none over time, no stall and no lockup record. Audio interrupt average 20
to 38 %, slowest 50 %. Longest main loop gap 105 ms (likely the font loads), lowest free heap 76,120, max
stack 7,849.

**Device reading, 2026-11-10, Synth FX (on `1f510b7`),** recorder build, the Synth FX set: lowest free heap
21,072, on `F5` opened after `F2`. That project was out of spec, not a leak. PERC's patch allows 1 voice a
part (`Synth::getMaxVoiceCount`; every other engine allows 8), and `make_masterfx_test_project.py` wrote
4, 2 and 2 into `project.mt`, which the UI can't save. On open, `Synth::updateToSettings` sets the stored
count, then `setInstrumentType`, which clamps to the engine's limit only when the engine changes. Opened
after an ACD project, the parts were clamped to 1 each: 53,964 B free on the `MT_NO_MODS` build (53,132 B
with the mods). Opened after a PERC project, nothing clamped and 8 PERC voices ran: 23,636 B (22,804 B).
With a valid project (1, 1, 1), every order gives about 53 KB. So the real PERC case is 3 voices, about
52 KB free, as `N6` and `F4` read on the unit, and the corrected `F5` opened after `F2` read 51,928 B
on the same firmware. Neither Synth FX nor Master FX allocates.

A fix for CE, checked on a scratch `MT_NO_MODS` build only: in `updateToSettings`, after
`setInstrumentType`, clamp the part to `getMaxVoiceCount` whatever the engine was before. With it, the
out-of-spec `F5` opened after `F2` leaves 53,952 B. Only a hand-made `project.mt` reaches this.

**Device reading, 2026-11-18, heap trim (`modular` `5b40785` with the trim),** recorder build
`TrackerPlus_MT_heap1k_diag.ptf`, Master FX Off: 20 plays, no lockup or fault, 0 blocks over time,
the synth never over its limit, max stack 7,733. The projects are matched to the log lines by their
voices and pool peak, which the rig reads the same.

| Project | Average | Slowest | Main loop gap | Free heap | Rig, same image |
| --- | --- | --- | --- | --- | --- |
| `E3 8 voices DCO6 Init` | 29 % | 41-47 % | 9 ms | 72,636 B | 74,096 B |
| `N6 3 voices PERC` | 8-17 % | 22-28 % | 22 ms | 60,336 B | 61,864 B |
| `N7 8 voices DIRT` | 33-38 % | 45-56 % | 10 ms | 72,204 B | 73,840 B |

The lowest free heap on the unit is now 60,336 B, on PERC, with the trim's 1,024 B already taken
(51,936 B on 2026-11-11 for 3 PERC voices, before CE's 1620e85 rebuilt the factory engines). The rig
reads 1.5 to 1.6 KB high, as in `PAST_FAILURES.md`'s calibration. `N7` was 63-66 % average and 83 %
slowest on 2026-11-08 and is about half that now; that's CE's rebuilt DIRT or its silence work, not
looked into (the engine is a black box) and nothing to do with the trim.

**Heap:** 138,752 fixed (137,728 in mods builds, the trim above). At every boot CE's
`Profiler::init` allocates until `malloc` fails and frees it all, so `sbrk` reaches the limit and
the free heap Config and the recorder show (`mallinfo().fordblks`) is all of the heap's free bytes:
the trim reads about 1 KB lower. A failed malloc hangs the unit, so this is the one with a hard edge.
About 58 KB is in use with a project open, and 1.2.0 measured at least 42 KB free. In the
emulator the break pointer reached 23,968 after the static constructors and 67,112 by the time
`engine.init` stopped on a null codec. That's a high-water mark, not bytes in use.

**Stack:** 20,480, one stack shared by the main loop and every interrupt, with about 12.7 KB
never touched in the reading above. `-fstack-usage` is on
for `platforms/imx` only, whose largest frame is 592 bytes. `core/` has no `.su` data.
`Profiler::maxStackBytes` reads the watermark.

**OCRAM:** `mtProject` 21,676, `audioPlayBuff` 1,536.

**SDRAM**, in address order, plain CE:

| Item | Bytes |
| --- | --- |
| `externalRamScratchBuff` (shared scratch) | 786,432 |
| `externalRamDelayBuff` | 1,572,864 |
| `externalRamReverbBuff` | 32,768 |
| `externalRamSampleMemory` | 62,849,024 |
| `s_voiceRingBuffers` | 524,288 |
| `undoPatternBuffer` | 328,640 |
| `s_instrumentWaveformPeaks` | 230,400 |
| `undoSongBuffer` | 164,320 |
| `s_voiceLoopWrapBuffers`, `g_wtEnvelopeBank` | 65,536 each |
| `seq`, `songTrackCopy` | 32,864 each |
| file manager buffers (L, Src) | 32,640 each |
| everything else, under 30 KB each | the rest of the 1.86 MB these lines total |
| **Free** | **8,108** |

SDRAM reads as nearly full only because CE gives whatever is left to one constant. Its comment,
"~60 MB minus 64 KB for pattern buffers", shows Polyend already carving from it the same way.
Mod builds lower it by 1 MiB under `MT_MODS` (`mtStructs.h`); a project file doesn't store the
limit, and builds with `MT_NO_MODS` stay byte-identical to upstream CE. A new source file in
`platforms/imx/source` breaks that by changing the link order, even with its contents compiled out
(same size, 3,142 bytes differ), so flagged chip code goes in an existing file there. New files in
`core/mods` don't.

### What the whole 1.2.0 mod set needed

From the `patch/all` build map (`TrackerPlus_ch_all.map.json`, version 1.2.0-CX4), bytes of RAM
added by each mod:

| Mod | RAM | | Mod | RAM |
| --- | --- | | --- | --- |
| colorsong | 12,208 | | visualizer | 560 |
| synths | 8,352 | | copypattern, slicethru, resample | 48 each |
| tracklen | 4,128 | | condtrig | 48 |
| interpolate | 1,504 | | humanize, swing | 32 each |
| masterfx | 1,152 | | legato, songloop | 16 each |
| | | | chords | 0 |

**Total 28,192 bytes**, with 573,002 bytes of code in flash (synths 516,992, masterfx 20,008).
Color Song alone is 43% of that RAM. 15,920 bytes remain for the mods not yet ported.

The "big buffers" the port plan expected a pool for were never new memory on 1.2.0:

- masterfx uses the stock delay and reverb buffers (`patch/masterfx/*_dsp.h`).
- resample records into the free part of the sample bank.
- synths' wavetables are in flash.

These are 1.2.0 figures. CE's own framework adds `projectBuffer` 6,144 and `configBuffer` 512 in
SDRAM and about 660 bytes in DTCM, and each mod must still be sized when it's ported.

### Where the real limits are

Memory isn't the first limit. These are:

- **CPU.** 1.2.0's masterfx measured up to 10.4 points of CPU above stock (Gated), running from flash.
  `CLAUDE.md` has CPU measured on the device with all 8 synth voices sounding.
- **ITCM.** 22,696 bytes free, against masterfx's 20,008 bytes of code. It can't all move in, so
  hot loops must be chosen. The split across all mods is in
  [MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md#memory). `libPolyDSP.a` and parts of
  `libPolySynth.a` already live in ITCM, by linker script. CE's open CHIP engine runs from flash.
- **Display list.** 8,172 bytes per frame.
- **Heap.** A hard hang on failure, though 80 KB was free in the reading above.

### After CE's plugin synths (2026-11-13)

`tools/mem_budget.py` on the merged mod build, against `modular` before it (`d7a853c`):

| Region | Free after | Free before |
| --- | --- | --- |
| Flash | 14,468,772 | 14,232,540 |
| ITCM | 70,048 | 9,768 |
| OCRAM | 9,556 | 9,556 |
| SDRAM unreserved | 688,204 | 688,492 |
| DTCM unreserved | 9,676 | 14,248 |

ITCM freed up because the closed libraries left it (CE now places only `Amplifier.o` and
`Mixer.o` there). DTCM is 4.5 KB tighter. The sample pool is 55,508,992 bytes (61,800,448
before): the 6 MB `.psyn` pool comes out of it, and the mods' 1 MiB is taken on top.

**WAVE and PHASE as `.psyn` files (2026-11-13).** Out of the firmware, the same build reads:

| Region | Free |
| --- | --- |
| Flash | 15,025,756 |
| ITCM | 70,112 |
| OCRAM | 9,556 |
| SDRAM unreserved | 688,348 |
| DTCM unreserved | 12,516 |

In the `.psyn` arena (1,920 KB) WAVE takes 528,560 bytes (code 46,152, its wavetables 481,984 of
rodata) and PHASE 53,944. CE's 1 MB boot preload goes through the files in name order and skips
any that won't fit, so WAVE is read from the card (530,660 bytes) the first time it's picked, with
audio masked, CE's own path. Both run from SDRAM through the cache; as built-in engines they ran
from flash.

### After CE's display lists in the MCU (2026-11-15)

`tools/mem_budget.py` on the merged mod build (CE `ed51301`), against `modular` before it
(`250cd04`), both built the same way:

| Region | Free after | Free before |
| --- | --- | --- |
| Flash | 14,997,732 | 15,002,100 |
| ITCM | 48,320 | 48,320 |
| OCRAM | 9,556 | 9,556 |
| SDRAM unreserved | 610,844 | 603,196 |
| DTCM unreserved | 4,680 | 7,840 |

DTCM is 3,160 bytes tighter, all of it CE's (plain CE goes from 14,876 to 11,724 free): the DMA
descriptors `writeTcds` 1,024, `romFontMetrics` 512, the arena's table `fragments` 460, `display`
108 more, and smaller ones. That leaves 584 bytes above the 4,096 floor. The diagnostic build
(`MT_LOCKUP_RECORDER`) is at 3,160, under the floor. CE takes 96 KB of SDRAM out of sample memory
for its 56 KB arena, 8 KB build buffer, 8 KB frame and the two 8 KB copies of the chip's lists; the
mods build takes it as well as the mods' 1 MiB (`mtStructs.h`), so the sample pool is 55,410,688
bytes.

### After CE's audio optimisations (2026-11-16)

`tools/mem_budget.py` on the merged mod build (CE `cc53303`), against `modular` before it
(`2cbd759`), both built the same way:

| Region | Free after | Free before |
| --- | --- | --- |
| Flash | 14,976,188 | 14,997,732 |
| ITCM | 40,096 | 48,320 |
| OCRAM | 9,556 | 9,556 |
| SDRAM unreserved | 610,844 | 610,844 |
| DTCM unreserved | 4,496 | 4,680 |

ITCM is 8,224 bytes tighter. 256 of them are the mods' answers to CE's silence work: the delay
shortcut's `activeDelay` test, the Synth FX `holdsPart` test in `PolySynth::update`, and the
dynamics slot running its plugin on silence (`AudioEffectLimiter::modsUpdate`). The other 7,968
are CE's (7482cc8, 0238786). About 2.1 KB of CE's is new code there: the
mixer's `mix`, the delay's and filter's idle checks, the voice-sleep checks
(`InstrumentImpl::prepareSilentBlock`, `isIdle`), `AudioMux::update`, and `memset`, `memmove` and
`memcmp`. The other 6 KB is growth of code that was already there: the synth render, the reverb and
delay updates, the sample players, `Sequencer::tick` and `PolySynth::update`. Plain CE goes to 68,728
free. DTCM is 184
bytes tighter, again all CE's: plain CE goes from 11,724 to 11,540. That leaves 400 bytes above the
4,096 floor. The diagnostic build (`MT_LOCKUP_RECORDER`) is at 2,976, under the floor, with
40,064 of ITCM free.

Heap after boot and the open of the D1 project, from the simulator (`sim_boot.py --play 1.0`,
not the unit): 72,692 bytes free, against 79,536 before the merge. Max stack 8,360 against 8,344.
Heap and stack are only exact on the unit (Config with `/.develop`).

## 16. How much was read

| Area | Coverage |
| --- | --- |
| Linker scripts, startup, `main`, MPU, clocks, interrupt priorities | Read in full |
| `TrackerImpl`, `Profiler`, `AudioVoice`, `MixerAndFxs`, `AudioStream` | Read in full |
| Memory accounting | From the ELF, every region |
| `VoiceStreamManager` | Structure and decisions; not every line |
| `mtSequencer.cpp` | Structure, tick, start of `play_microStep`; not all 3,829 lines |
| `mtAudioEngine`, `mtAudioNoteOnHandle`, `mtAudioUpdateHandle` | Function list and main paths |
| `play_memory*` | Entry points, one play mode |
| `mtMidi`, `TimerImpl`, `Recorder` | Handlers and structure |
| File manager | Operation steps, autosave, load check; not import/export (`.mod`, `.it`) |
| `FileSystemImpl`, FatFs config | API and settings; not the SD driver |
| Interface, input | Module list, input path, `InputProcessor`; other 12 modules not read inside |
| Display | `composeFrame` budget and the arena, `cList`, `cLabel`, `cBar`, `cFrame` drawing; `DisplayList.cpp` expansions not checked against the chip; other controls not read |
| `masterParams` | Read except mixer solo functions in `fMasterParams.cpp` lines 1800–2300 |
| `AudioEffectPolyverb`, `Reverb`, `AudioEffectShortDelay` | Read in full |
| USB, pads, LEDs, power | Entry points only |
| Performance mode, exporter, themes, `.ptfx`, `mtEffect` | Purpose and memory use only |
| Closed synth libraries | Not read, by licence |

Read the code at a site before hooking it; this map doesn't replace that.

## 17. Corrections to earlier statements

- `docs/mt/CE_PORT_PLAN.md` section 1.1, "~21 KB DTCM ... megabytes of SDRAM free": SDRAM has
  8 KB unreserved in plain CE. Megabytes exist only by lowering the pool.
- Section 1.7, "Big buffers ... one SDRAM pool": see section 15. The mods' big buffers were
  CE's own.
- Decision 3 wasn't sized when step 0 asked for it. It's now 1 MiB.
- "1–2 MB out of ~60 MB, a project that fills nearly all sample memory wouldn't fit": this
  ignored streaming (section 6). The cost of lowering the pool is recording length and the
  largest Wavetable and Granular sets.
- The memory table in Color Song's first report added the heap to "used" and took the stack as
  free. The real free DTCM is the gap in section 15.
- `docs/mt/CE_PORT_PLAN.md` section 3.13, "audio objects in the master chain ... a selector routes
  the send" and `idleOnly`: neither is needed. See [MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md).
- This map's section 15, "1.2.0's masterfx measured up to 10.4 points" and "ITCM ... against
  masterfx's 20,008 bytes": both are 1.2.0 figures. On CE, with one reverb buffer in DTCM, the estimate is much lower,
  and the inner loops are 15.2 KB.
