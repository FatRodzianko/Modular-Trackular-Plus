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
  `AudioPlayMemory::update` are in ITCM. `Sequencer::play_microStep`, `AudioPlayMemory::play` and `applyRowCustomFx` are
  flash.
- **Recorder** (`core/source/Recorder.cpp`): runs from `engine.update`. It records straight
  into `externalRamSampleMemory`, sized `SAMPLE_MEMORY_SIZE`, and overwrites the loaded
  samples (`wasSampleMemoryOverwritten`), which are reloaded afterwards
  (`SampleMemoryReload`). Longest stereo recording: 62,849,024 / 4 / 44,100 = 356 s in plain
  CE, 350 s with the mods' 1 MiB taken out.
- **Sample editor** (`core/effector`): its edit area starts at
  `EDIT_BUFF_OFFSET = SAMPLE_MEMORY_SIZE - 2 * 2 * EDIT_BUFF_SIZE`, the top 15 MB of the
  pool, so they move with the constant. `mtEffect` handles overlap with loaded samples.
- `.ptfx` custom effects load code and scratch into `externalRamScratchBuff`.

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

## 12. Display

FT812 over LPSPI4 with DMA. `cDisplay::update` has three steps. Controls render bitmaps into
RAM_G and `step3` appends each control's list from the highest slot down, stopping at the
first that doesn't fit the 8,172 bytes (`CLAUDE.md`). Themes are JSON (`ThemeManager`) and fonts
are decompressed into SDRAM. 15,684 lines in `core/libs/NewDisplay`.

## 13. Synth

`core/synth/Synth.cpp` wraps the closed `polysynth` libraries: 3 parts, 8 voices at most
(`kSynthParts`, `kSynthMaxVoices`). `synth.init` creates patch stores, a MIDI adapter and three
instruments on the heap. `core/synth/engines/ChiptuneSynthVoice.cpp` is open. Part of the
libraries is placed in ITCM by the linker script.

## 14. USB

Composite device (`platforms/imx/usb/composite.c`): MIDI, audio in, audio out, plus mass storage
in file-transfer mode. `handleFileTransferMode` swaps the class and resets the bus.

## 15. Memory budget

Exact, from the ELF, with `tools/mem_budget.py`. "Mods" is the mod build with Color Song and
the 1 MiB taken from sample memory.

| Region | Size | Plain CE free | Mods free | Floor | Notes |
| --- | --- | --- | --- | --- | --- |
| Flash | 16,580,608 | 14,916,380 | 14,902,916 | 1 MiB | 1.2.0 images up to 8 MB flashed whole |
| ITCM | 131,064 | 22,744 | 22,696 | 1,024 | closed libs and audio code |
| DTCM unreserved | gap | 21,776 | 21,120 | 4,096 | heap end to stack base |
| OCRAM | 32,768 | 9,556 | 9,556 | 1,024 | mods stay out |
| SDRAM unreserved | 67,108,864 | 8,108 | 1,037,868 | 256 KiB | the pool takes all the rest in plain CE |

Mods use 18,816 bytes of SDRAM so far: `projectBuffer` 6,144, `configBuffer` 512, Color Song
12,144 and alignment.

**DTCM layout** (mods): `.data` 6,120, `.bss` about 118.5 KB, `.noinit` 32,768 (reverb), heap
138,752 fixed (`_HeapSize`), free gap 21,120, stack 20,480 at 0x20053000–0x20058000. The
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

**Heap:** 138,752 fixed. A failed malloc hangs the unit, so this is the one with a hard edge.
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
limit, and builds with `MT_NO_MODS` stay byte-identical to upstream CE.

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
  hot loops must be chosen.
- **Display list.** 8,172 bytes per frame.
- **Heap.** A hard hang on failure, though 80 KB was free in the reading above.

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
| Interface, input | Module list, input path, `InputProcessor`; 13 modules not read inside |
| Display | `step3` budget and file list; individual controls not read |
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
