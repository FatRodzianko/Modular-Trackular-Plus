# Whole-firmware simulator: plan

Status: **narrowed on 2026-11-13.** Stage 1 (the virtual-time kernel) is built; its results are in
[SIMULATOR_STAGE1.md](SIMULATOR_STAGE1.md). The user then chose not to build stages 2 to 5: no
calibration session on the unit (D5), and instead one targeted test on the stage 1 kernel,
`tools/emu/sim_songstop.py`, which checks section 1's song stop with Color Song and Copy to New on
the reference, `modular` and `modular` with the fix. The rest of this file is the original plan,
kept for reference. Written 2026-11-13 against `modular` at `c32fee7`. Section 11 says what was read
and what wasn't.

## Summary

- **The gap.** The emulator runs firmware functions, or boots and loops with a clock that moves by a
  fixed 3 ms for each interrupt. It has no real interrupt cadence against main-loop work, no time
  for card access and no folder listings. So it couldn't show that Color Song and Copy to New could
  delay CE's pattern load past the step it has, which stops the song (section 1).
- **Recommendation: extend the Unicorn whole-image rig we already have** (`tools/emu/boot_rig.py`)
  into a simulator with deterministic virtual time. It runs the exact `.axf` we ship, with the
  closed synth libraries in. It can preempt the main loop at any instruction while honouring the
  interrupt masks, and its heap and stack readings already agree with the unit. It gets:
  - a virtual clock;
  - the audio interrupt at its real 2.90 ms cadence;
  - card, display and bus calls that cost modelled time;
  - a card backed by a PC folder;
  - scripted buttons, pads, encoder and MIDI;
  - an event log of what the sequencer and file manager do.
- **The community Linux port is not the base for this.** It builds the Original Tracker product, so
  none of the Tracker+ paths, synths or mods compile into it. It sits 110 CE commits behind
  `modular`, before the pattern-load changes the first tests are about. It runs the interrupt as a
  real thread alongside the main loop, which isn't how the device behaves. Parts of it would be
  worth taking if we later want a fast PC build as well (section 2.5). Its own files carry no
  licence statement, so that needs asking first.
- **A PC build can't have the closed synths.** `libPolySynth.a` and `libPolyDSP.a` are Cortex-M7
  object code, and x86 can't link them. Translating them would need disassembly, which 2(c) bans.
  Their licence allows a non-commercial PC project to use them, but only by running the ARM code,
  as our rigs already do. The synth host is inside them too, so a PC build loses every synth
  engine, CHIP and ours included (section 2.4).
- **Calibration:** one calibration firmware (the lockup recorder build plus timing probes), four
  projects we generate from one of the user's own, and one session on the unit of about 45 minutes
  (section 6).
- **First tests:** song playback across pattern ends, under card-latency, tempo and interrupt-phase
  sweeps, with Color Song, Copy to New and Song Loop. They must stop the song on today's committed
  `modular` (no fix) and pass with the fix that's waiting in the main checkout (section 8.3).
- **Stages 1 to 5, about 13 to 21 working sessions** before the existing tests are migrated
  (section 9). Open decisions are in section 10.

## 1. Why: the gap this has to close

How the pattern switch works since CE `15c3d23` and `c476ce1`, as merged in `7009114`:

1. In the audio interrupt, on track 0's first micro step of the last step of a pattern, the
   sequencer calls `loadNextPattern` (`core/source/mtSequencer.cpp:384-405`). That reaches
   `cFileManager::loadWorkspacePattern`, which now only records `requestedPattern`
   (`core/FileManager/fileManager.cpp:325-331`).
2. In the main loop, `cFileManager::update` first calls `startRequestedPatternLoad`
   (`fileManager.cpp:59-62, 333-363`). That starts `fmLoadWorkspacePattern`. Step 0,
   `savePatternToWorkspace`, writes the current pattern if it changed, and runs in the same pass.
   Step 1, in the next pass, reads the 16,432-byte pattern file and sets `preloadedPattern`
   (`fileManagerPattern.cpp:26-90`).
3. At the end of that step, the interrupt's `switchRowStep` checks `isPatternPreloaded` for the
   next pattern, and otherwise calls `stopForMissingNextPattern`, which stops the song
   (`mtSequencer.cpp:1987-2093`).

So the load has about one step: 125 ms at 120 BPM, and 18.75 ms at CE's maximum of 800 BPM
(`Sequencer::MAX_TEMPO`, 4 steps a beat). Out of that come the wait for the next main-loop pass, a
possible pattern write, and the pattern read.

`TrackerImpl::update` (`core/source/Tracker.cpp:169-198`) calls `newFileManager.update()`, then
`mtInterface.update()`, Song Loop's tick, `sequencer.update()`, then `mods::update()`. If the request
lands after `newFileManager.update()` in a pass, the mods still see `getStatus() == fmIdle`.
- Color Song's `readTracks` (`loadWorkspacePatternToTempBuffer`) or Copy to New's write then uses
  the card first.
- The load starts a pass later, behind that card access.

The fix waiting in the main checkout makes both mods also wait while `isPatternLoading()`
(`requestedPattern != 0`). One window stays open even with the fix: a mod access already under way
when the request lands. The simulator should measure how long that delays the load.

The emulator showed none of this:
- `test_swing.py` and `test_tracklen.py` stub `isPatternPreloaded` to 1.
- `test_songloop.py` models it, but its `loadWorkspacePattern` stub sets the preloaded pattern at
  once, inside the interrupt. That is CE's behaviour before `15c3d23`.
- `boot_rig.py` runs the real file manager, but its card costs no time. Its clock moves 3 ms for
  each interrupt, and the interrupt fires once a loop or every few hundred to few thousand
  instructions. That is about a thousand times more often than on the device, relative to
  main-loop work.

## 2. The community Linux port

Branch `github-desktop-scott-lepore/linux-port`, tag `linux-port-2026-11-05` (`8b3f1ca`). The
author in the commits is Scott Lepore, and the remote is `scott-lepore/tracker-ce` on GitHub. It is
22 commits on CE `02e15a5`, from 2026-11-02 to 2026-11-05: 75 files, +15,192/−141 lines, of which
7,988 are the vendored `stb_image.h`. The same remote also has branches that merge MT+'s `modular`
into an Original Tracker build (`mt-original`, `pr/mtplus-original`). They weren't read for this
plan.

### 2.1 What it covers

From `docs/linux_port.md` on the branch, checked against the source where noted:

| Device part | The port's stand-in |
|---|---|
| Product | **Original Tracker only**: `make linux` builds `PLATFORM=LINUX PRODUCT=ORIGINAL`. No `PLATFORM_IMX`, no `FEATURE_SYNTH`. 32-bit (`-m32 -fshort-enums -funsigned-char -malign-double`) so on-disk structs keep the ARM layout |
| Audio interrupt | `AudioThread`, a real thread calling `updateAudioBuffers` every 128 samples, paced by ALSA or the wall clock (`--no-pacing` runs flat out) |
| `CPSID i` sections | a recursive mutex shared with the audio thread (`PlatformGlue.cpp`) |
| `NVIC_DisableIRQ(audio)` | a flag, plus waiting for the block under way |
| MIDI clock timer | a one-shot timer thread (`TimerImpl.cpp`) |
| Time | `steady_clock` (wall time) |
| Display | `eve/EveEmu`: a software FT812, about 1,150 lines, calibrated against device screenshots; 0.1 to 0.6 % of pixels differ on nine screens |
| SD card | `FileSystemImpl` (732 lines): POSIX over a folder, listings sorted (FAT order isn't kept), a fake contiguous cluster chain for fast seek, `readSectorsAsync` done as a synchronous `pread` |
| Keys, pads, encoder | `SwitchGroupImpl` with the device's hold timing; X11 keyboard; a TCP "dev monitor" for scripted input |
| MIDI | ALSA sequencer ports |
| Codec, radio, battery, LEDs, USB audio | stubs |
| DSP intrinsics | `dspinst_portable.h`, bit-exact C |
| `.ptfx` plugins | can't run (ARM code); the loader does nothing |

The port says itself that it "does **not** model CPU load, RAM or timing".

### 2.2 How close it is to `modular`

- **Base:** CE `02e15a5`. `modular`'s CE base is `dbfa648` (merged in `7009114`), 110 commits later.
  Between them `core/` changes in 65 files, +2,402/−381.
- Those 110 commits include `15c3d23`, `c476ce1` and `98468f0`, none of which the port has. That is
  the exact behaviour the first tests are about.
- **The platform interface** changed little: `FileSystem::remount()` is a new pure virtual
  (`core/include/Platform.h`), which the port doesn't implement.
- **Trial merge** of the port into `modular`, in memory only (`git merge-tree`): one textual conflict,
  `core/makefile`. The port rewrote that file with LF endings where the blob is CRLF. A clean
  textual merge says nothing about behaviour, as `7009114` showed.
- **The port also changes CE's behaviour,** beyond what a simulator needs:
  - A developer monitor in `core/source/DevMonitor.cpp` (778 lines), with hooks in the sequencer,
    MIDI and Config.
  - Per-track MIDI-in routing (`2d61ad0`). This adds 8 bytes to `strMtConfig.midi` and changes note
    handling in `mtSequencer.cpp`.
  - An MK66 serial monitor and USB firmware upload.
  None of this belongs in MT+.
- **Mods:** `core/mods/modsBuild.h` switches mods on only for `PLATFORM_IMX` (not Mini) or
  `MT_HOST_TEST`, so a build of the port has no MT+ mods at all.

### 2.3 Its timing model isn't the device's

On the device the audio interrupt suspends the main loop and runs to the end, so main-loop code
never sees an interrupt half done. In the port the audio thread runs at the same time as the main
loop, on another core. They exclude each other only where the firmware takes
`DISABLE_GLOBAL_INTERRUPTS` (the mutex) or disables the audio interrupt.
- Interleavings the device can't produce become possible.
- Which interleaving happens depends on the host's scheduler, so a run can't be repeated.
- Time is wall time, and nothing in the main loop costs modelled time.

To be a simulator, a port would have to swap the audio thread for the cooperative scheduler in
section 4.

### 2.4 Licences and notices

Not legal advice: a plain reading of the texts in this repository.

- **CE's own code** (`LICENSE` Part 1): MIT, with Part 2's ban on commercial clones and ports. A
  free, non-commercial MT+ simulator is fine. The MIT notice and Part 2 have to travel with any
  copy.
- **The port's own new files** (`platforms/linux/`, `DevMonitor.cpp`, `dspinst_portable.h`): no file
  headers, and the branch's `LICENSE` is CE's, unchanged. The usual reading is that they come under
  the repository's licence, but the author hasn't said so in the files. **Before taking any of
  them, ask the author to confirm they are under CE's `LICENSE` Part 1 (MIT).** Then credit them in
  `NOTICE.md` and in each file taken, with the source commit (`8b3f1ca`).
- **`stb_image.h`:** MIT or public domain, at the user's choice. The port already adds it to
  `NOTICE.md` (section 10). Keep that entry if EveEmu comes over.
- **The closed synth libraries** (`synth/lib/LICENSE-PROPRIETARY.txt`):
  - 1(b) allows "personal, educational, research, hobbyist, or open-source DIY non-commercial …
    software projects", so off-hardware use is allowed.
  - The grant covers *unmodified* binaries, and 2(c) bans disassembly and reverse engineering.
  - Both archives are built for Cortex-M7 with FPv5. Their ELF attributes, read with `readelf -A`
    in an earlier session, say `7E-M` and `FPv5/FP-D16`. **An x86 or x86-64 link can't use them at
    all.**
    The only routes are an ARM emulator running them as they are, which is what our rigs do and
    what this plan keeps, or translating them, which needs disassembly and is out.
- **What a PC build without the libraries loses.** The libraries hold the synth *host*, not just
  the closed engines: `Synth::createSynth`, the patch store, patch pool, mixer, voice allocation
  and the instrument-type registry (`core/synth/Synth.cpp` calls `createSynth` and the `PatchStore`
  methods; `docs/custom_synth_sdk.md`). So `FEATURE_SYNTH` has to be off, and the build loses:
  - all synth sound, CHIP and our WAVE and PHASE included, since they register through the host;
  - synth patch loading and saving, and the card access it does at project open, Save and Save As
    (through `core/synth/FileSystemAdapter.cpp`);
  - the synth screens (`GUI_HANDLE_SYNTH`) and the code under `FILEMANAGER_HANDLE_SYNTH` (15 lines
    in `core/` mention it);
  - the Synths and Synth FX mods. Synth FX plugins are ARM code too, as the port notes for
    `.ptfx`.
  - The synths' share of the interrupt's CPU is lost as well, but on the device that's measured,
    not simulated.

  A clean-room stand-in host written from the public MIT headers is possible in principle. But it
  would have to guess how the real patch store uses the card, so its timing wouldn't be the real
  one.

### 2.5 Could MT+ use it, and what we'd take

**Not as the base of the simulator** (section 3 compares the options). If the user later wants a
fast PC build as well (stage 6, optional), and the author confirms the licence, these parts would
save work. Each would be adapted and credited, under MIT:

- `platforms/linux/source/FileSystemImpl.*`, the folder card, which is the biggest saving;
- `eve/EveEmu.*` and `EveImage.*` with `stb_image.h`, for screenshots. EveEmu could also render
  screenshots from command streams the Unicorn simulator captures, as a separate host tool;
- `dspinst_portable.h`, `sqrt_integer.h`;
- the `#ifdef PLATFORM_LINUX` changes in `config.h`, `cr_section_macros.h`, `Polyverb/util.h`,
  `Tracker.cpp` and `ptfx_loader.cpp`, and the two general fixes (`FileBrowser.cpp` include,
  `textBox.cpp` const);
- the 32-bit layout flags.

**Not taken:**
- the developer monitor;
- per-track MIDI in;
- the MK66 serial and USB changes;
- `AudioThread` and the mutex model (section 2.3);
- ALSA and X11, which tests don't need.

The port's snapshot fix (`core/libs/NewDisplay/snapshots.cpp`, "only visible controls are
redrawn") is a CE bug fix. It belongs in a PR to CE, not in our simulator.

### 2.6 Building it to see whether it compiles: needs your OK

Not done. What it would take, in WSL `Ubuntu` (26.04, g++ 15.2, checked with `dpkg -l`):
- Installs, which need your OK: `gcc-multilib g++-multilib` for `-m32`, plus `i386` runtime
  libraries. `libasound2-dev` and `libx11-dev` are there, but only for amd64. The port's makefile
  links them whenever the headers exist, and with no 32-bit libraries that link fails, so it needs
  either the i386 packages or a makefile edit.
- `M32=0` needs no install but fails CE's struct-size asserts by design. That would check only that
  most files compile.
- The port was tested with g++ 13. g++ 15 may add errors.
- A few minutes at 6 jobs, in a throwaway worktree.

**Recommendation: don't, for now.** It would only tell us that the *Original* build of an older CE
compiles on this machine. That says little about a Tracker+ PC build, which would be new work
either way. Revisit at stage 6.

## 3. The options compared

| | **A. Unicorn, whole image** (recommended) | **B. PC build with a simulated platform** | **C. QEMU or Renode with modelled peripherals** |
|---|---|---|---|
| What runs | The `.axf` we ship, ARM code, closed libraries included | The same source compiled for x86 (32-bit) | The `.ptf` from reset, through the real drivers |
| Where the fake starts | The `Platform` objects (`core/include/Platform.h`), as `boot_rig.py` does now | The same interface, in C++ | Chip registers: USDHC with ADMA2, SAI with eDMA, LPSPI and FT812, LPI2C devices, TMR4, SEMC, FlexSPI, USB |
| Synths | Yes, as a black box | No (section 2.4) | Yes |
| Preemption | At any instruction, honouring PRIMASK, BASEPRI and the audio mask (`loop_preempted` does this today) | Only at points we instrument (function entries, card calls, mask changes) | Any instruction |
| Determinism | Yes: Unicorn plus a seeded scheduler | Yes, if cooperative (not the port's threads) | Yes with `-icount` and record and replay |
| Speed | Not measured. Probably slower than real time, from 2× to 20× (section 4.7) | Probably many times faster than real time | Similar to A |
| Compiler and UB fidelity | Exact: our toolchain and flags | Different compiler and architecture: undefined behaviour and float contraction can differ | Exact |
| Keeping up with CE merges | Python stubs keyed to vtable slots and symbol names. A changed interface shows up as a wrong slot, so it needs a slot check (section 5.4) | The compiler catches interface changes, but about 10 `#ifdef` edits in CE files have to be carried through every merge, plus a Tracker+ host product the port doesn't have | Very large: an i.MX RT1051 board model. As far as this plan knows, QEMU has none; Renode wasn't checked |
| Cost to first useful test | Lowest: boot, main loop, interrupt, masks, USB mode and heap and stack agreement already exist | Medium to high: a Tracker+ host product, the folder card, the clock and scheduler, IMX paths on x86 | Months |
| Licence | Already the practice (`SOUND_TEST_PLAN.md`); the guard stays | Needs the port author's confirmation if we take their files | No issue, but no gain |

C's extra fidelity (drivers, DMA, boot below `Platform`) is in code the mods don't touch. A cut at
`Platform` keeps everything CE and the mods do. A cut lower, at FatFs's sector calls, is stage 6's
card option, still in A.

**Why A and not B first.**
- A runs the binary that goes on the unit, synths included.
- A can preempt between any two instructions, and B can't.
- About a third of A already exists and has calibration readings behind it.
- B's speed would matter for wide sweeps. Stage 1 measures A's speed before anything depends on it,
  and B stays open as stage 6.

## 4. Deterministic virtual time

### 4.1 The clock

One virtual clock `vt`, in nanoseconds, owned by a Python scheduler. Nothing reads wall time. The
fake `Timer` (`millis`, `micros`, `clockCycles`) and every `elapsedMillis`/`elapsedMicros` base
read `vt`. So CE's 10 s autosave, Color Song's 100 ms read gap and button hold times all follow
virtual time.

Three things move it:

1. **Main-loop instructions.** The main loop runs in slices,
   `emu_start(count=N)`. Each slice adds `N × c_main` to `vt`. `N` is chosen so a slice ends at the
   next event (the next interrupt, a timer, an input). `c_main` (ns per instruction) is calibrated
   (section 6). It is one value, and gets a seeded random factor per pass, for example ×0.8 to
   ×1.5, to stand in for cache and SDRAM stalls. Slices cross stage boundaries, so a value per
   stage of `TrackerImpl::update` would need extra stops. It's added only if calibration shows the
   stages differ a lot.
2. **I/O waits, charged by the platform models.** Every stubbed `Platform` call that waits on
   hardware returns after a modelled time: card (4.3), display coprocessor and SPI, the I2C work in
   `updateHardware`, `usb->update`. The real main loop also runs `usb->update`, `DevConsole_Update`,
   `updateHardware` and `platform.update` around `TrackerImpl::update`
   (`PlatformTrackerIMX.cpp:153-174`). The simulator charges one calibrated "platform part of a
   pass" for those.
3. **Interrupts.** The audio interrupt runs the real `TrackerImpl::updateAudioBuffers` and charges a
   *modelled* duration (4.2), not a counted one.

**Licence point for the clock (decision D2).**
- The rule is "no measuring an engine to work out how it works", and `PAST_FAILURES.md` says "the
  closed libraries aren't counted".
- So the interrupt's time never comes from instruction counts. It's dominated by the libraries,
  and counts don't predict device time anyway.
- The main loop rarely calls into the libraries during playback. When it does, for synth screen
  getters or patch load and save, a native `count=N` slice also counts the library's instructions
  as elapsed time. Nothing about them is recorded or reported.
- The alternative counts open code only, with block hooks over open ranges, and charges a flat cost
  per call into the libraries. It's cleaner and probably 10 to 50 times slower.
- The plan recommends native slices. You decide.

### 4.2 Interrupts

- **Audio:**
  - Deadlines at `φ + k × 128/44100 s` (2.9025 ms), with `φ` a phase chosen per run.
  - At a deadline the scheduler stops the main loop and runs the interrupt on the main stack below
    the current frame, as `loop_preempted` does now. Then it charges `d_isr` and resumes.
  - `d_isr` comes from a per-test model: a constant, a seeded distribution, or the device's average
    and slowest for that project from the recorder.
  - If `d_isr` goes past the next deadline, the next interrupt is pending at once and the main loop
    gets nothing. That's overload, modelled coarsely. Its real threshold stays a device question.
- **Masks:**
  - An interrupt due while PRIMASK, BASEPRI or `AudioNoInterrupts` holds it is *pending*.
  - The scheduler then runs the main loop in short slices of 16 instructions until the mask clears,
    and fires it there. Today's rig waits for the next slice end instead.
  - Enable and disable are a state, not a count (`PAST_FAILURES.md`).
- **MIDI clock (TMR4):** `scheduleMidiClockUs` puts an event at `vt + delay`. It has priority 1,
  above audio at 3 (`CE_FIRMWARE_MAP.md` section 4), so it may preempt the audio interrupt too.
  Later stage, with MIDI tests.
- **Blocking stubs:**
  - A card call takes `L` of virtual time, and interrupts falling due within `L` must run *during*
    it, as they would while the device polls the SD controller.
  - Unicorn can't tell how many instructions ran when a hook stops it, so a wait doesn't stop the
    emulator. It adds `L` to the slice, and the interrupts due in it run when the slice ends, at
    most one slice cap (a few thousand instructions) after the call returns.
  - That bound is the clock's resolution, and it's logged. The same mechanism serves
    `awaitCoProEmpty` and `delay`.
  - Details are in [SIMULATOR_STAGE1.md](SIMULATOR_STAGE1.md) section 1.1, which also runs the
    main loop as a small ARM trampoline so slices can cross pass ends with exact counts.

### 4.3 Card latency model

Per test, with sweeps. Two layers. Stage 2 builds the first, and stage 6 optionally adds the
second:

- **L1, per call** (the folder card at the `FileSystem` interface). Each method has a cost:
  - `t_open` per path component;
  - read and write: `t_cmd + bytes / bandwidth`;
  - close, flush, `exists`, `getFileSize`, `rename`, `remove`, `mkdir`, listings;
  - writes add a *stall* drawn from a seeded heavy-tailed distribution. SD cards pause for wear
    levelling, often for tens and sometimes hundreds of milliseconds.
  - Async sector reads (`readSectorsAsync`, `pollAsyncRead`) complete at `start + t_cmd +
    sectors × t_sector`. So streaming underruns show up when the card is slow.
- **L2, per sector:** stage 6, section 5.1.

Sweep controls:
- a global scale (×0.5 to ×50);
- separate read and write scales;
- stall probability and size;
- targeted stalls: "the Nth write takes 300 ms", or "every write between t1 and t2 takes X".

Defaults come from calibration (section 6). Until then, the defaults are placeholders, and a test
can't be called a pass on them alone.

### 4.4 Other waits

- **Display:** `awaitCoProEmpty` and `isCoProEmpty` model a coprocessor that is busy for a time
  proportional to the bytes in the frame.
- **Pads, LEDs and encoder:** I2C in `updateHardware`, one calibrated constant a pass.
- **USB:** a constant a pass while attached.

### 4.5 Preemption at chosen and random points

- **Phase sweep:** `φ` in N steps across one block period, so pattern ends land at different points
  in the main loop.
- **Random:** a seeded jitter on each deadline (±x µs), and the per-pass `c_main` factor. Different
  seeds give different interleavings.
- **Chosen:** fire the interrupt:
  - at virtual time `T`;
  - at entry to an open function on its Nth call (for example `cFileManager::update` returning,
    `mods::update` starting);
  - before or after card call k.
  The interrupt fires only if it's unmasked, since a device can't do otherwise.
- **Placement search for a pattern end:** run the pattern's last-step interrupt at each stage
  boundary of one `TrackerImpl::update` pass in turn. That covers before and after `display.update`,
  `newFileManager.update`, `mtInterface.update`, `sequencer.update` and `mods::update`. Check each
  outcome. This finds section 1's window directly, without waiting for a sweep to hit it.

### 4.6 Determinism and replay

The same `.axf` hash, card folder hash, script, model parameters and seed give the same event log,
byte for byte. Every failure prints those inputs and a one-line command to replay it. A
determinism check runs each scenario twice in the quick tier and compares log hashes.

### 4.7 Speed: unknown until stage 1

- Today's release tier gives one data point. Preempting every 100 to 1,500 instructions on the
  worst project made a 300-block run about 9,500 blocks and took about an hour
  (`tools/run_tests.py`). That's about 2.6 blocks a second, but almost all of it is rendering a
  full synth block per interrupt, at about a thousand times the device's rate.
- With interrupts at the real cadence, a sample-only project and card waits skipped instantly, the
  guess is 2 to 20 times slower than real time. That's 1 to 7 minutes for a 20 s song.
- Stage 1 measures it. If sweeps then take too long under the 75 % CPU cap (6 of the 8 cores),
  that's the case for stage 6's PC build.

## 5. Card, input, observation

### 5.1 The simulated card

- **L1, folder (stage 2).** A folder on the PC holds the card: `Projects/`, `Workspace/`,
  `Samples/` and the rest, real CE files.
  - The simulator copies it into a temporary folder per run, so the source is never written. It
    serves every `FileSystem` method from the copy, with the latency model.
  - Today's `FakeSd` is a dictionary and can't list folders (`PAST_FAILURES.md`). L1 adds
    `createFilesList`, `createInstrumentsList`, `countContainedFiles` and the filters, with the
    semantics of `platforms/imx/source/FileSystemImpl.cpp`. Those weren't read in detail for this
    plan; stage 2 reads them first.
  - Fast seek gets a contiguous fake cluster chain per open file, as the port does.
  - After a run, the copy is kept on failure, so a test can open the Workspace the firmware wrote.
- **L2, FAT image (stage 6, optional).** The image's own `FileSystemImpl` and FatFs run, and the
  stub sits at `sd_disk_read`, `sd_disk_write`, `sd_disk_status` and `sd_disk_initialize`
  (`platforms/imx/fatfs/source/fsl_sd_disk`), and at `SD_ReadBlocksAsync` and its poll. These are
  open NXP code.
  - That gives exact FatFs behaviour: directory order, FAT and directory-entry writes,
    and fast seek. Latency becomes per sector command, which is how a card really behaves.
  - It needs a tool to build a FAT32 image from a folder and read one back. The firmware doesn't
    call `f_mkfs`, so it probably isn't linked.
  - The choice is our own small Python FAT32 writer (no download) or `mtools` or `dosfstools` in
    WSL (an install; decision D4).

### 5.2 Scripted input

Inputs are delivered at the start of the first main-loop pass at or after their time, as
`updateHardware` does on the device before `TrackerImpl::update`.
- **Buttons and pads:**
  - `onButtonPush`, `onButtonRelease` and `onButtonHold`, and the pad equivalents, with the fake
    `SwitchGroup`'s `isButtonPressed` kept consistent.
  - Hold events come by themselves after the time CE set with `setHoldTime`.
- **Encoder:** `onPotChange`.
- **MIDI in:** bytes queued in the fake UART and USB MIDI objects, drained by `midiUpdate` in the
  interrupt, as on the device.

Scripts are Python:

```python
sim.at_ms(500, press(SONG))
sim.wait_until(lambda: sim.screen_has("SONG"))
sim.tap(PLAY)
sim.run_ms(20_000)
```

Navigation goes through the real buttons and screens, not internal calls. That's the "rig the real
path first" rule.

### 5.3 Observing state

- **Event log**, with `vt` and context (interrupt or main loop), from watch hooks on open functions.
  These are allowed by the guard, as today.
  - The pattern request, `startRequestedPatternLoad`, `loadPatternFromWorkspace`, `preloadedPattern`
    set, `switchRamPatternsNow`, `stopForMissingNextPattern`, `Sequencer::stop`, `play`.
  - `sendNoteOn`, with track and step.
  - The mods' card work: Color Song's `readTracks`, Copy to New's write, Song Loop's
    `expectedNext`.
  - Every card call, with path, bytes, and start and end `vt`.
- **State:**
  - Through public calls between slices: `getStatus`, `isPatternLoading`, `isPatternPreloaded`,
    `isPlay`.
  - Through symbols: `mtProject.song.playlistPos`, `mtProject.values.actualPattern`.
  - Private members like `requestedPattern` and `currentOperation` by offsets read from the `.axf`'s
    debug info with the WSL toolchain's `arm-none-eabi-gdb` (`ptype /o`). That needs no download.
    The offsets are cached per `.axf` hash, and only open types are read.
  - This replaces the probe-and-bisect offset finding in `test_colorsong.py` and
    `test_synthfx.py`.
- **Screen:**
  - Capture the words and strings sent through `DisplayDriver::WriteData32` and `sendString`, and
    decode `CMD_TEXT` (position, font and string) per frame.
  - Count each frame's display-list bytes against CE's 8,172-byte limit. That's a check `CLAUDE.md`
    asks for and nothing does today.
  - A PNG renderer (EveEmu, if its licence is confirmed) is optional, for people to look at.
- **Audio:** peak and silence per block, and notes, as `boot_rig.py` does now.
- **Invariants checked on every run:**
  - no card, display or heap call while in an interrupt;
  - stack watermark;
  - memory outside the unit's map;
  - no open file left in USB storage mode.

### 5.4 Keeping the stubs honest across CE merges

`boot_rig.py` writes stubs into vtable slots by number ("slot 11 is isCoProEmpty",
`platform_vtable + 4 * 14`). The simulator finds each slot by reading the `.axf`'s vtable symbols
and matching method names, as `_complete_card` already does for `FileSystemImpl`. It refuses to
start if a `Platform` method has no model. A CE merge that adds a method then fails loudly instead
of running a wrong stub.

## 6. Calibration on the unit

Device time doesn't follow instruction counts, because the unit is limited by cache, SDRAM and
flash stalls (`SOUND_TEST_PLAN.md`). The card, display and buses are I/O. So the model's numbers
have to come from the unit, once, with the user's card.

### 6.1 What to measure

1. **Main-loop pass time**, as a distribution: per stage of `TrackerImpl::update` (each
   `updateSdStream`, `display.update`, `newFileManager.update`, `mtInterface.update`,
   `sequencer.update`, `mods::update`), and the platform part outside it. Measured on the Pattern
   and Song screens, stopped and playing. This gives `c_main` per stage: device time minus the I/O
   measured inside it, divided by the simulator's instruction count for the same stage on the same
   project and screen.
2. **Pattern-load timing for each pattern end:** request time (interrupt), load start, save step
   end, preloaded, and the switch deadline. So the slack, and whether any stop happened. This is
   also the check that the simulator agrees.
3. **Card calls:** each `FileSystemImpl` method's duration and bytes, as histograms per method and
   size bucket, with the slowest writes kept individually. This covers autosave, Save, the pattern
   load, and streaming's async sector reads (start to done).
4. **Display:** `display.update` time and bytes a frame, and the wait in `awaitCoProEmpty`.
5. **Audio interrupt:** average and slowest per Play. The lockup recorder already logs this.

### 6.2 The calibration firmware

- The lockup recorder build (`DEVICE_TEST_LADDER.md`) plus `-DMT_SIM_CALIBRATION=1`. Other builds
  compile none of it.
- Timing comes from the DWT cycle counter (`TimeControl.cpp` enables `CYCCNT`).
- The interrupt writes only a few timestamps, in DTCM. Histograms live in SDRAM with
  `__NOINIT(EXTERNAL_RAM)`, zeroed by their owner. No heap is used, and nothing extra is done in the
  interrupt.
- Written from the main loop to `/simcal_log.txt` at each Stop, as the recorder writes
  `/mfx_log.txt`. It writes nothing while the card is lent over USB.
- Probes cost tens of cycles, against millisecond-scale readings.
- Its flash, DTCM and SDRAM cost gets measured with `tools/mem_budget.py` before it goes to the
  unit.
- It goes through `run_tests.py --tier release`, then ladder step `L1`, before the calibration
  steps.
- **No synthetic card benchmark:** it measures only what CE does in normal use, so it writes nothing
  CE wouldn't. That's the lower card risk.

### 6.3 The projects we prepare

Generated by a tool like `make_masterfx_test_project.py`, from one of the user's own projects, read
only. That's the source `--project` already uses. The user builds nothing.

- `SIM1 song 120`: 8 different patterns of 16 steps, one short sample instrument from the source
  project, notes on track 1, song slots 1 to 8, 120 BPM.
- `SIM2 song 800`: the same at 800 BPM, the shortest step CE allows, so the least slack.
- `SIM3 song 800 mods`: `SIM2` with Color Song On and Song Loop On over slots 3 to 5, set in its
  `mods.bin`.
- `SIM4 stream`: one long sample (over 256 KB, so it streams) on 4 tracks, 120 BPM, for the async
  read figures.

### 6.4 The user's steps (sent as one message at stage 4)

1. Back up the card (copy it to the PC).
2. Copy `SIM1 song 120`, `SIM2 song 800`, `SIM3 song 800 mods` and `SIM4 stream` into the card's
   `Projects` folder.
3. Flash `TrackerPlus_MT_simcal.ptf`.
4. Open `SIM1 song 120`. Go to the Song screen and press Play. Wait 30 seconds. Press Play to stop.
5. Do the same with `SIM2 song 800`.
6. Open `SIM3 song 800 mods`. Song screen, Play, 30 seconds. Then, still playing, go to the Pattern
   screen and press Copy to New once. The exact presses are written out from
   `COPYPATTERN_PLAN.md` when the message is sent. Wait 10 seconds and stop.
7. Open `SIM4 stream`. Press Play on the Pattern screen, wait 30 seconds, stop.
8. Save the open project (Project screen, Save), so a Save's writes are timed.
9. Turn the unit off normally, put the card in the PC and send `simcal_log.txt` and `mfx_log.txt`.
10. Flash the normal firmware back.

About 45 minutes. If the unit stalls or needs the pinhole at any step, stop there and send the logs.
The recorder writes what it saw at the next start.

### 6.5 Using the numbers

- They are fitted into the model's defaults and kept in `docs/mt/SIM_CALIBRATION.md`, with the
  firmware hash and the card's make and size.
- Then a calibration table in `PAST_FAILURES.md`'s style, unit against simulator, for:
  - the slack distribution at each pattern end on `SIM1` and `SIM2`;
  - main-loop pass time;
  - card call times;
  - display frame time.
- If they stop agreeing, the simulator is wrong, not the unit.
- A different card means a new card section, not new code.

## 7. What stays hardware-only

The list is kept as narrow as the simulator allows:
- **Absolute CPU time and overload:**
  - how near 100 % a project is, glitches, and the overload lockup;
  - the closed engines' cost, which isn't counted, by licence;
  - cache, SDRAM and flash stalls, and their worst-case alignment.

  The simulator shows only what follows *if* the interrupt costs X. The ladder and recorder stay.
- **The card's physics beyond the measured model:** another card's stalls, wear, a full card, power
  loss or removal mid-write, and what a computer writes over USB storage.
- **Codec and sound:** analogue output, levels, and what it sounds like.
- **USB with a real host:** MIDI timing, mass storage, USB audio.
- **The physical controls:** encoder bounce, pad pressure, I2C errors.
- **The display hardware:** FT812 timing, real pixels, SPI faults. The simulator decodes commands
  only.
- **Power:** battery, low-power mode, the power button, brown-out.
- **Below `Platform`:** `ResetISR`, clocks, MPU, the SD and DMA drivers, the bootloader and
  firmware update.
- **Exact nesting latencies of priority-0 interrupts (SysTick, USB).**

**What moves off the hardware list into the simulator:**
- song stops at pattern ends;
- card contention between the mods and CE (autosave, loads, Save, Copy to New);
- streaming underruns against card latency, at model level;
- UI flows (Open, Save As, New Project, Copy to New) on the real screens;
- display-list overflow;
- the "nothing slow in the interrupt" rules, checked on every run.

## 8. Migration

### 8.1 Which tests move and which stay

| Test | Where | Why |
|---|---|---|
| `test_step0`, `test_humanize` | Stay | Function-level logic; fast and exact |
| `test_swing`, `test_tracklen` | Stay, fixed | Their timing maths stays function-level. The always-yes `isPatternPreloaded`, `switchNextPatternInSong` and `getSongPattern` stubs go: pattern-end cases either use the CE model of 8.2 or move to the simulator |
| `test_songloop` | Split | `expectedNext` and frame logic stay. The song-mode cases across pattern ends move, since its load stub preloads inside the interrupt (pre-`15c3d23` CE) |
| `test_colorsong` | Split | Colours, state and file parsing stay. The `mayReadFile` gating while playing moves |
| `test_copypattern` | Split | The empty-step rules stay. The write while playing moves |
| `test_masterfx` (objects, graph, DSP), `stress_masterfx`, `test_synths*`, `test_sound_rig`, the Synth FX runtime and pack tests | Stay | Sound and DSP, no system timing |
| `test_masterfx_rig` `ui`, `project`, `play`; `test_synthfx_page` | Move | Screen flows and Play: the real main loop and interrupt cadence |
| `boot_rig.py` boot, preempt and expect-usb jobs, the hang and second start | Move | They become simulator scenarios with virtual time. `--preempt`'s instruction gaps become phase and jitter sweeps plus placement search |
| `test_send_leak` | Move | Real Play path |
| `test_bundle_cost` | Stays | Own-code instruction counts as a regression number, not time |
| `test_lockup_recorder` | Stays, plus one scenario | The recorder unit test stays. Add a simulator overload scenario that checks the recorder's stall record fires |
| `core/mods/tests` (host) | Stay | Pure mod logic |

### 8.2 The rule for the stubs that remain

1. **A stub of CE code models CE; it never short-circuits it.** It returns every outcome the real
   function can return, including "not yet" and failure. Its answers are driven by state that
   changes when CE's would. For example, `isPatternPreloaded` answers yes only after a modelled
   main-loop load has finished, never at once and never always.
2. **A stub of hardware is a model with parameters** (latency, failure), with calibrated defaults
   and the option to sweep.
3. **Every stub is registered** in `tools/emu/STUBS.md`: what it replaces, why, which outcomes it can
   give, and which simulator test runs the real function on the same path. A stub with no
   simulator counterpart can't be a pass's only evidence.
4. **A stub that must answer a constant** says so in the test's name and output. It doesn't count as
   covering that path.
5. **At every merge into `modular`,** the register is checked against the incoming changes. That is
   step 4 of the merging checklist: what the real firmware does is found and written down before
   any stub changes.

### 8.3 The first tests

All on `SIM`-style projects generated in the test, from the folder card, driven through the Song
screen. The same seeds and sweeps run against three builds:
- **R**, the CE reference (`MT_NO_MODS`);
- **N**, `modular` as committed (`c32fee7`, without the fix);
- **F**, `modular` with the fix.

Sweeps:
- tempo: 120, 400, 800 BPM;
- card read and write scale: ×1 to ×50, in about 8 steps;
- write stall: none, 50, 150, 300 ms;
- interrupt phase: 8 values;
- 3 seeds;
- placement search on every pattern end.

The quick tier runs a small corner set, and the release tier runs the grid.

- **T1, CE alone:** R, N and F with every mod Off.
  - The song plays two full loops with no stop wherever the load fits its step.
  - Where it doesn't fit, CE itself stops. That's CE's behaviour, recorded as R's threshold per
    tempo, not a failure.
  - N and F must match R exactly, with identical event logs, which also checks the mods' Off
    switch.
- **T2, Color Song On,** on the Song screen while playing (it reads every 100 ms or more).
  - On N, placement search must find a stop that R doesn't have at the same settings: the request
    landing between `newFileManager.update` and `mods::update`, with Color Song's read before the
    load.
  - On F, no stop where R has none.
  - F also reports the worst extra delay to the load start from a read already under way. That's
    section 1's remaining window, as a number.
- **T3, Copy to New** pressed while the song plays, scripted at each placement before a pattern
  end. Same criteria as T2.
- **T4, Song Loop On,** with a loop block over slots 3 to 5, across the block's end (where the next
  pattern is the block's start) and normal ends. On N and F, no stop where R has none at the same
  slot changes. This covers `expectedNext` against the real file manager.
- **T5, all three On together.** Same criteria.
- **Control:** the simulator's own test. If T2 and T3 don't fail on N, the simulator can't see
  section 1's problem, and nothing else in this list counts as evidence until that's explained.

**Pass criterion (decision D6):** relative to R. No stop with a mod On at settings where R doesn't
stop, plus the measured extra delay. The alternative is an absolute card latency figure, which only
means something after calibration.

## 9. Stages

Costs are guesses in working sessions (one agent session with your review), plus compute.

1. **Virtual-time kernel in the Unicorn rig.**
   - Work: the scheduler, the clock, interrupts at cadence, pending under masks, blocking stubs,
     chosen and random preemption, seeds and replay, and vtable slots found by name.
   - Done when:
     - Boot and Play on the existing test projects give today's heap and stack figures (the
       `PAST_FAILURES.md` calibration table).
     - Two runs with one seed give identical logs.
     - The speed is measured and written down.
   - About 3 to 5 sessions. The detailed plan is [SIMULATOR_STAGE1.md](SIMULATOR_STAGE1.md).
2. **Card, input and observers.**
   - Work: the folder card with listings and the latency model (L1), scripted input, the event log,
     gdb offsets, screen text and display-list bytes, and the invariants.
   - Done when:
     - A real CE project in a folder opens through the Project screen by button presses.
     - The Workspace written back to the copy is valid.
     - The screen text shows the project's name.
     - The card trace is complete and deterministic.
   - About 3 to 5 sessions.
3. **The first tests (8.3).**
   - Work: generate the projects, script T1 to T5, build R, N and F, and run the sweeps.
   - Done when T2 and T3 fail on N with a replayable seed and a log excerpt showing the window, and
     everything passes on F and R.
   - About 2 to 3 sessions, plus a few hours of compute under the 75 % cap.
   - **This stage is where the simulator earns trust.** If it can't reproduce N's problem, stop and
     find out why before going on.
4. **Calibration (section 6).**
   - Work: the calibration firmware, then the release tier, then ladder `L1`, then the user's steps.
     Fit the model, rerun stage 3 at calibrated values, and write the agreement table.
   - Done when the simulator's slack and pass-time distributions agree with the unit's within the
     limits written beforehand.
   - About 2 to 3 sessions, plus about 45 minutes on the unit.
5. **Migration (section 8).**
   - Work: move the tests, fix or register the stubs, and add a simulator group to
     `run_tests.py`'s tiers. Quick: corners and the determinism check. Release: the sweeps. Deep:
     wide sweeps and more seeds.
   - Done when no stub answers a CE query with a constant unless it's registered and has a
     simulator counterpart.
   - About 3 to 5 sessions.
6. **Optional, decided after stage 5:**
   - L2 FAT-image card: about 3 to 4 sessions.
   - Or a Tracker+ PC build (B), from the port's platform layer if its licence is confirmed, without
     synths, for fast wide sweeps: about 6 to 10 sessions, plus upkeep at every CE merge.

## 10. Open decisions for the user

- **D1. Approach:** A, the Unicorn whole image (recommended); B, a PC build; or A now and B later.
- **D2. The clock:**
  - Native instruction slices, recommended. The main loop's rare calls into the closed libraries
    count as elapsed time, and nothing about them is recorded.
  - Or open-code-only counting, which is slower.
  - Either way the interrupt's time is modelled, never counted.
- **D3. The Linux port:**
  - Take nothing now.
  - If B is chosen later, ask the author to confirm MIT for their files first.
  - Building it to see whether it compiles needs `gcc-multilib`, `g++-multilib` and i386 libraries
    installed in WSL (section 2.6). Recommendation: not now.
- **D4. Card layer:** L1 folder now. L2 FAT image later or never. For L2, our own Python FAT32 writer
  or an install of `mtools`/`dosfstools`. **Decided 2026-11-13: L1 now, as recommended.**
- **D5. Calibration:** OK to build the calibration firmware and run section 6.4's steps on your unit
  with your usual card, after the release tier and ladder `L1`. Measurements are of normal use
  only, with no synthetic card writes. **2026-11-13: the user turned down the 45-minute session as
  too much. Stage 4 is to be re-planned smaller or dropped; until then the simulator makes no
  absolute timing claims.**
- **D6. Pass criterion for 8.3:** relative to CE's reference (recommended), or absolute latencies
  after calibration. **Decided 2026-11-13: relative to CE, as recommended.**
- **D7. The release:** does `modular-1.0` wait for stage 3 (the fix shown in the simulator), stage 4
  (calibrated), or stage 5?
- **D8. The waiting fix:** stage 3 needs it built (F) next to the committed `modular` (N). Committing
  it is separate and needs your OK as usual. **Decided 2026-11-13: build F for stage 3, as
  recommended.**

## 11. What this plan checked and what it didn't

**Read:**
- `CLAUDE.md`, both the committed one and the main checkout's uncommitted "Merging into modular"
  and "Releases" sections.
- `docs/mt/PAST_FAILURES.md`, the first half of `DEVICE_TEST_LADDER.md`, and parts of
  `SOUND_TEST_PLAN.md` and `CE_FIRMWARE_MAP.md` (chip, boot, main loop, streaming, files, the
  device readings).
- `LICENSE` and `synth/lib/LICENSE-PROPRIETARY.txt`.
- `core/include/Platform.h` and `DisplayDriver.h`.
- `TrackerImpl::update`.
- The file manager's request, start, load and `isPattern*` code, and the sequencer's request and
  switch code.
- `platforms/imx/source/PlatformTrackerIMX.cpp`'s loop, `hardware.cpp`'s `updateHardware`, and
  `FileSystemImpl`'s async methods.
- `tools/emu/machine.py`, `boot_rig.py`, the start of `sound_rig.py`, `run_tests.py`, and the
  file-manager stubs in `test_swing.py`, `test_tracklen.py`, `test_songloop.py` and
  `test_colorsong.py`.
- `core/mods/tests/Makefile`.
- The fix's diff in the main checkout.
- The sibling notes on the synth licence and the CE review.

**On the Linux port:**
- Read `docs/linux_port.md`, `main.cpp`, `AudioThread.cpp`, `TimerImpl.cpp`, `PlatformGlue.cpp`,
  `PlatformImpl.h`, `Stubs.h`, the makefiles, and the diffs to CE files.
- Ran an in-memory trial merge (`git merge-tree`). It adds only unreferenced git objects. No ref,
  branch or file changed.

**Checked in WSL, without running or building anything:**
- the ARM toolchain has `arm-none-eabi-gdb`;
- g++ 15.2, no multilib, no `mtools` or `dosfstools`, 8 cores;
- the venv has Unicorn 2.1.4, Capstone and NumPy, and no ELF or FAT libraries.

**Not checked:**
- Nothing was built or run, so every speed figure is a guess, including whether native slices are
  fast enough.
- `FileSystemImpl`'s listing and filter semantics on IMX.
- The port's `FileSystemImpl.cpp` and `EveEmu.cpp` beyond their descriptions.
- The `mt-original` branches.
- Whether QEMU or Renode has any i.MX RT model. The table's "none in QEMU" is from memory.
- Copy to New's exact presses.
- How `test_swing.py` and `test_tracklen.py` would behave with a CE model of `isPatternPreloaded`.
- The legal standing of the port author's files: a reading, not advice.
- Whether `f_mkfs` is linked: inferred from no caller, not from the map file.
