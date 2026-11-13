# Simulator stage 1: the virtual-time kernel

Status: **kernel built; 2.5 partly checked.** Written 2026-11-13 against `modular` at `c32fee7`. It is stage 1
of [SIMULATOR_PLAN.md](SIMULATOR_PLAN.md), option A (Unicorn, whole image), which the user chose on
2026-11-13. The read-up (2.1) and both spikes are done; their results are at the end. Section 1
was revised after spike 2: the interrupt runs in a second Unicorn instance (1.1), idle main-loop
passes are skipped (1.10), and the models say which calls are pure (1.5).

## What stage 1 delivers

The existing whole-image rig (`tools/emu/boot_rig.py`) runs on a virtual clock:
- The audio interrupt fires every 128/44100 s against main-loop work.
- Waits take modelled time, with interrupts still firing during them. That covers card calls,
  display waits and `Timer::delay`.
- Interrupt masks are honoured exactly, and preemption can happen at chosen or random points.
- Every run can be replayed from its seed.

Out of stage 1, left for stage 2:
- the folder card, with its listings and calibrated latency;
- scripted input beyond what boot and Play need;
- reading the screen;
- the gdb offsets.

Stage 1 keeps today's `FakeSd`, and gives each card call a placeholder latency.

`boot_rig.py` and the release-tier jobs that use it stay unchanged until stage 5. The simulator
is new code beside them, so the two can be compared.

## 1. Design

### 1.1 Exact instruction counts: the main loop as ARM code

Unicorn can stop after a given number of instructions (`emu_start(count=N)`). If it stops for
any other reason, it can't tell us how many it ran. That covers a hook calling `emu_stop`, or
reaching the `until` address. The installed Unicorn 2.1.4 has no counter we can read either: its
`UC_CTL_*` options were checked.

Today's rig returns to `STUB_RET` at the end of every main-loop pass, so every pass would end
with an unknown count. So:

- **The main loop runs as a small Thumb trampoline** in the stub region. It is the device's
  `while (1)` loop:
  ```
  loop:  bl  pass_start        @ Python stub: may set r12 = an input handler and r0..r3 its args
         cbz r12, run
         blx r12               @ onButtonPush and the like, called as hardware.cpp would
         b   loop
  run:   ldr r0, =tracker
         bl  TrackerImpl::update
         b   loop
  ```
  `pass_start` doesn't stop the emulator. A pass boundary is just a call to it, so slices can
  run straight through pass ends.
- **Slices end only by count** in normal running, so their instruction counts are exact, to
  within one IT block: Unicorn never stops inside one, so a slice that ends in an IT block runs
  the rest of it (2.2). The same slicing always overruns the same way, so runs stay
  deterministic. The kernel charges the instructions it asked for.
  - The slice size is the distance to the next event, capped at `max_slice` instructions:
    1,000, about 2 µs of device time. Per-slice overhead is negligible at that size (2.2).
  - The cap is the clock's resolution. A timer read in the middle of a slice sees the slice's
    start time plus any waits so far in that slice, and may be up to one cap behind.
- **Two Unicorn instances.** `count=` is a hook on every instruction, and switching it off and
  on in one instance flushes the translated code. So:
  - the main loop runs in the *main instance*, always with `count=`;
  - the interrupts run in the *interrupt instance*, never with `count=`, about 16 times faster
    (7 to 17 ms an interrupt against 120 to 150 ms);
  - both map the same host memory (`mem_map_ptr`), so they see one RAM;
  - every hook the rig adds goes on both, except the main loop's own (the write watch of 1.10
    and `pass_start`);
  - before each interrupt the main instance's R0 to R12, S0 to S31 and FPSCR are copied into the
    interrupt instance, and SP is set below the interrupted SP with the CPU's 0x68-byte frame, so
    the ISR saves the interrupted code's registers on the same stack, as on the device.

  Spike 2 checked that this gives the same audio and the same final state as one instance.
- **Waits don't stop the emulator.** A card call, `delay`, or the display coprocessor being busy
  calls `wait(L)`, which adds `L` to the slice's waiting time and returns.
  - The slice runs on to its count. Then the clock moves by `count × c_main` plus the waits.
  - Interrupts that fell due during the waits run then, at their own deadlines, which are logged.
  - So the main-loop code after a wait can run up to one slice before those interrupts, which on
    the device would come first. The error is bounded and the same on every run, and is written
    in the log as "resolution".
- **The only stops with an unknown count:**
  - a chosen preemption point (1.4);
  - the stop at a pass start after an idle pass (1.10);
  - an assert, after which the run is over anyway.

  The stop sets the slice's count to its lower bound, 0. The log records "clock uncertain by up to
  N instructions here", so a forced-placement result can't hide it.

This counts every instruction the main loop executes, including the rare main-loop calls into the
closed libraries. That is decision **D2**. Nothing about those instructions is recorded or
reported, and interrupt time is never counted. The alternative counted open code only, with a
block hook over the open ranges; the spike found it about 6 times slower, and it would still need
a flat charge per library call. **D2: native counting**, as recommended after spike 2, taken with
the go-ahead on 2026-11-13.

### 1.2 The clock and the events

- `vt` is an integer in nanoseconds.
- **The event queue** is a heap ordered by time, then by insertion order.
- **Event kinds:**
  - audio-out deadline;
  - audio-in deadline, run just before audio-out. Both DMA interrupts are at priority 3
    (`interruptsConfig.h`), so neither preempts the other, and both are clocked by the same SAI
    frame clock. The input interrupt only hands `audioEngine` its buffer (`setInBuffer`); the
    kernel calls `TrackerImpl::updateInputAudioBuffers` with a silent buffer, in the interrupt
    instance. Its time, `d_isr_in`, is a placeholder (1 % of a block). `--no-input-interrupt`
    leaves it out, as `boot_rig.py` does;
  - input delivery;
  - run end;
  - (stage 2 and later) the MIDI clock timer.
- **Audio deadlines** are `phase + k × 2,902,494 ns`. That's 128/44100 s, rounded once per
  deadline from an exact rational, so no drift builds up.
- `jitter_ns` adds a seeded offset to each deadline, kept within ±1/4 of a block. So the order
  never changes.
- **Interrupt time:** a fired audio interrupt runs the real `TrackerImpl::updateAudioBuffers`
  (and `updateInputAudioBuffers` with silence). Then the clock moves by `d_isr`, drawn from the
  model:
  - `const`: a fraction of the block;
  - `table`: per run, from the recorder's average and slowest on the unit;
  - `seeded`: between the average and the slowest.

  The interrupt's instructions are never used for time.
- **An interrupt always delays the main loop by its `d_isr`**, also when its deadline fell in a
  wait. That's right for a wait the CPU spends polling, and an overestimate of up to `d_isr` per
  interrupt for `delay` and the display waits, which run on the wall clock. It's part of the
  resolution until stage 4 says which card waits poll.
- **Several overdue interrupts** (after a long wait) run back to back, each at its own deadline or
  when the one before it ends, whichever is later. Only an interrupt that starts after its deadline
  counts as late.
- **Overload:** if an interrupt ends after the next deadline, that one is due at once and the
  main loop gets no slice. The log counts late interrupts and the longest main-loop gap, the
  figures the lockup recorder keeps on the unit.

### 1.3 Masks

Before each slice the kernel reads PRIMASK, BASEPRI and the audio NVIC mask. The last is the
state that the existing `disableAudioInterrupt` and `enableAudioInterrupt` stubs keep, and it is
a state, not a count.

When an interrupt is due and masked:
- it is *pending*;
- slices shrink to `mask_step` instructions until the mask clears, then it fires. `mask_step` is 1:
  with 16, as first planned, an unmasked gap shorter than 16 instructions between a `cpsie` and the
  next `cpsid` could be stepped over, and the interrupt held past a point where the device takes it.
  If long NVIC-masked stretches make single steps too slow, the alternative is to stop at the
  `enableAudioInterrupt` stub, which is an uncertain stop;
- these slices still end by count, so the count stays exact;
- the log records how long it was held: the masked time per block, which the device has no other
  way to show.

### 1.4 Preemption

- **Physical:** the phase sweep and the seeded jitter, plus a seeded per-pass factor on `c_main`
  (default ×0.8 to ×1.5) standing in for stalls. All of it moves only *when* interrupts fire
  relative to the code. None of it can make an interrupt fire where the device couldn't.
- **Chosen:** `fire_at_entry(function, nth, which_interrupt)`.
  - A one-address hook on an open function (the guard refuses closed code) stops the emulator.
  - If the interrupt is unmasked there, the next due audio interrupt fires at that point.
  - If firing it there means bringing it earlier, or holding it later, than its deadline, the shift
    is limited to one pass's time and logged as **forced**, with its size.
  - A forced result is a lead. Stage 3 confirms it with a physical run (a phase near it) before it
    counts as a failure the device can have.
- **Placement search:**
  1. Run once.
  2. Find interrupt k, the one that issues a pattern's load request.
  3. Rerun once per stage of `TrackerImpl::update`, with interrupt k placed at that stage's entry.
     The stages are each `updateSdStream`, `display.update`, `padsBacklight.update`,
     `exporter.update`, `sliceDetector.update`, `newFileManager.update`, `mtInterface.update`,
     Song Loop's tick, `sequencer.update` and `mods::update`.

  Stage 1 builds the mechanism. Stage 3 uses it on pattern ends.

### 1.5 The platform, found by name

`boot_rig.py` writes stubs into vtable slots by number. For example, slot 14 of `Platform` is
the display driver, and slot 11 of the driver is `isCoProEmpty`. A CE change to an interface
would move a slot and silently run the wrong stub.

- A new resolver reads each IMX implementation class's vtable symbol from the `.axf`:
  - `_ZTV12PlatformImpl` and `_ZTV14FileSystemImpl`;
  - the timer's, the display driver's and the buttons' and pads' (`TimerImpl`,
    `DisplayDriverImpl`, `ButtonsImpl`, `PadsImpl`);
  - and the codec's, radio's, LEDs', battery's, encoder's, USB and USB audio's, MIDI's, config
    manager's and CRC's.
- It names every slot by its function symbol, as `FakeSd` already does for `FileSystemImpl`.
- Every model is registered by method name, with its behaviour: a value, a wait, or a call into
  another model.
- **Every model also says whether it is pure:** its answer depends only on the clock and the
  inputs, and it changes nothing. Only pure calls let a pass count as idle (1.10). This is the one
  list of pure calls; spike 2's hand-made list becomes these flags:
  - the timer reads (`millis`, `micros`, `clockCycles`, `clockCyclesPerUs`), but not `delay`;
  - the display driver's `isCoProEmpty`;
  - the buttons' `isButtonPressed`;
  - the card's `isAsyncReadBusy` and `isSdCardInserted`;
  - the Platform getters, which return fixed objects.

  A model with no flag is not pure, which is the safe default: a pass that calls it is work.
- **The simulator refuses to start** if any slot has no registered model. A slot that should do
  nothing is registered as `idle`, with a one-line reason. So a CE merge that adds a method stops
  the run until someone decides what it does.

### 1.6 Waits and time in stage 1

| Call | Model in stage 1 |
|---|---|
| `Timer::millis`, `micros` | `vt` at the slice's start plus its waits so far. `millis` and `micros` agree |
| `Timer::clockCycles`, `clockCyclesPerUs` | `vt × cpu_mhz / 1000`, with `cpu_mhz` 720 (`CPU_FREQ`, 2.1). `boot_rig.py`'s 528 stays there |
| A main-loop instruction, `c_main` | 2 ns, a placeholder on the slow side: 720 MHz at one instruction a cycle would be about 1.39 ns |
| `Timer::delay(ms)` | `wait(ms)`. Interrupts keep firing, as on the device |
| Display `isCoProEmpty`, `awaitCoProEmpty`, `beginCoProList` | The coprocessor is busy from each `endCoProList` for `t_copro` (placeholder, 100 µs; bytes aren't counted in stage 1). `isCoProEmpty` is pure and answers from the clock: `display.update` returns early while it's busy, as on the device, and the idle skip jumps to the moment it's free. `awaitCoProEmpty` and `beginCoProList` (which awaits in `API_LIB_BeginCoProList`) wait out the rest |
| Card calls (`FakeSd`) | `wait(t_call + bytes × t_byte)`, separate for read and write (placeholders) |
| One pass's platform part (`usb->update`, `DevConsole_Update`, the blink-LED check, `updateHardware`, `platform.update`) | `wait(t_platform)` in `pass_start` (placeholder, 0.5 µs). The trampoline doesn't run this code, which drives real peripherals |
| `elapsedMillis`, `elapsedMicros` and their audio versions | Their `timer` pointer goes to the fake timer, as now, so they follow `vt` |

**Placeholder values are marked as such.** Every run prints "uncalibrated" while any is in use,
and no stage 1 result says anything about the unit's timing.

### 1.7 Boot as the device boots

`platforms/imx/source/PlatformTrackerIMX.cpp`:
1. sets `trackerInstance` (line 143);
2. calls `initHardware`, which shows the start screen through `startDisplay` and its 200 ms delay,
   then starts the audio output DMA (`AudioOutputI2S::begin`);
3. calls `trackerInstance->init`.

`updateBuffers` calls into the tracker whenever `trackerInstance` is set. So on the unit the
audio interrupt probably already runs *during* `TrackerImpl::init`. Today's rig only starts
interrupts after `init`. Whether the NVIC line is enabled at that point is checked in 2.1, in
the EDMA handle setup.

If it is, the simulator boots that way by default, and keeps `--interrupts-after-init` for
comparison with `boot_rig.py`. Anything that differs between the two is a finding to explain, not
something to tune away.

### 1.8 Determinism and replay

- **Separate seeded random streams** for jitter, the `c_main` factor and `d_isr`. A new draw in
  one doesn't shift the others, so a sweep changes only what it means to change.
- **Every run writes:**
  - its parameters (one dataclass, serialised);
  - the seed;
  - the `.axf` SHA-256;
  - the card's contents hash;
  - an event log (JSON lines) and its hash.
- `--replay <run.json>` reruns it exactly.
- Two runs that should be identical are compared by log hash. A difference is a determinism bug
  in the simulator.

### 1.9 The event log in stage 1

Each entry carries `vt`, the context (main loop, audio or input interrupt), and the pass number.
Logged events:
- pass starts, sampled every N passes unless asked for;
- interrupts: deadline, fire time, lateness, `d_isr`, and held-by-mask time;
- waits, with their kind;
- forced placements and uncertain clock stops;
- the sequencer's `play` and `stop`, and `sendNoteOn`;
- the file manager's pattern request, load start and preload (the functions in
  SIMULATOR_PLAN.md section 5.3, all open code);
- heap and stack readings at chosen points;
- idle skips: a count and the skipped time per log interval, not one line per skip.

### 1.10 Idle passes are skipped

The main loop spends nearly all its time in passes that do nothing: 99.5 % of passes in spike 2.
Emulating each one at the device's speed was what made a song take an hour (2.2).

- **A pass is idle when all of these hold:**
  - it wrote nothing outside the main stack. A write hook on the main instance only watches ITCM,
    DTCM below the main stack, OCRAM, SDRAM and the rig's scratch memory. The peripheral ranges
    aren't watched: the rig's peripherals read as 0 whatever is written, so a write there can't
    change what a later pass does;
  - every model it called is pure (1.5);
  - no interrupt fired and no input arrived during it.
- **Why the next pass would do the same:** the same code, the same RAM, and pure calls whose
  answers change only with the clock and the inputs. So passes until the next clock tick, input
  or interrupt would repeat it.
- **The skip:** the next call to `pass_start` stops the emulator. The clock jumps to the earliest
  of the next interrupt deadline, the next millisecond tick (`millis` resolution), the next input
  and the run's end. Then one whole pass runs and is judged again.
- **Costs, all logged:**
  - the stop has an unknown instruction count. It's charged as 0 and counted as uncertain; the
    error is at most one `max_slice`, about 2 µs;
  - the clock's resolution inside a skipped stretch is 1 ms. Work that waits on `micros` or
    `clockCycles` can start up to 1 ms late. Skipped passes that read either are counted
    separately, so a run shows whether it relied on that;
  - an interrupt that falls in a skipped stretch fires at the pass boundary. An idle pass that
    masked interrupts for a few instructions can't hold one back there.
- **Off switch:** `--no-skip` runs every pass, for checking the skip against a full run on short
  scenarios. 2.4 and 2.5 compare the two.
- Passes that do work run instruction by instruction, and so do the card loads behind the
  pattern-load race.

## 2. Steps

### 2.1 Read first (no code)

- The EDMA handle setup for SAI1 TX and RX (`platforms/imx/3rdparty/Audio/output_i2s.cpp`,
  `input_i2s.cpp`, `fsl_edma`): when the NVIC line is enabled, and whether the half and major
  interrupts each hand over one 128-sample block.
- `initHardware`'s order (`hardware.cpp`) against `startDisplay` and `init`.
- `cpu_mhz` from `platforms/imx/config/config.h` and `mainCpu.setFrequency`.
- `DisplayDriverImpl` on IMX: which calls wait, and on what.
- All 39 `DISABLE_GLOBAL_INTERRUPTS` uses in `core/`. Does anything use BASEPRI? `grep` found
  none, so check `platforms/imx/source` too.
- `PlatformImpl`'s vtable in a current `.axf`: every slot named.

Done when each point has a one-line answer in this file's results section.

### 2.2 The spike (measure, then decide)

On a scratch branch, nothing kept unless it's good. Measure on today's `held` and `worst` test
projects, with the mods and reference builds:

1. **Exactness** on synthetic Thumb code:
   - a loop of known length;
   - an IT block;
   - `cpsid`/`cpsie`;
   - a `bl` to a stub.

   Does `count=N` run exactly N instructions every time? This must hold, or 1.1 changes.
2. **Speed** of the trampoline main loop, with `max_slice` at 1,000, 4,000 and 16,000:
   - instructions a second;
   - per-slice overhead;
   - virtual seconds per wall second, for boot, Open and Play, with interrupts at cadence.
3. **Open-code-only counting:** the same run with a block hook over the open ranges, for D2.
4. **Snapshot cost:** `context_save` plus a copy of the mapped memory, in case placement search
   would rather branch from a checkpoint than rerun from boot.

Output: a table here, and a recommended `max_slice`.

**Decision point.** If a 20-second song takes over 10 minutes at the chosen `max_slice`, stop
and bring the numbers to the user before 2.3. That much would push stage 6's PC build forward.

### 2.3 The kernel

Files, all new, in `tools/emu/`:
- `sim_kernel.py`: `Params`, the random streams, the event queue, the clock, `wait`, and the log
  writer. No Unicorn in it, so it can be unit-tested alone.
- `vtables.py`: the resolver from 1.5.
- `sim_rig.py`: `Sim`, built on `BootRig`'s start-up pieces (newlib stubs, static constructors,
  `FakeSd`, the guard), with its own timer, platform, trampoline, scheduler loop, mask handling,
  waits, chosen points and replay. It also holds the two instances (1.1) and the idle skip (1.10).
  The shared memory and mirrored hooks are set up when the main instance is made, through a
  `Machine` hook point, not by patching Unicorn's class as the spike did.
- `sim_boot.py`: the command line. It boots, opens a project, presses Play, runs N virtual seconds,
  and prints `boot_rig.py`'s figures (heap, stack, notes, pool, peak, failures) plus the clock's:
  interrupts fired, late, held by masks, longest main-loop gap, resolution and speed.
- `test_sim_kernel.py`: the checks in 2.4.

Every file imports `machine.py`, so `cpu_limit` applies (75 %).

### 2.4 Checks of the kernel itself

On synthetic Thumb code, no firmware:

- **Cadence:**
  - 10 virtual seconds give exactly `floor(10 s / block)` audio interrupts.
  - Deadlines match the rational formula to the nanosecond.
  - Jitter never reorders them.
- **Masks:**
  - an interrupt due inside `cpsid`…`cpsie` fires on the first instruction after `cpsie`, and its
    hold time is logged;
  - the same with the audio NVIC mask;
  - nested disable and enable behave as a state.
- **Waits:**
  - a 10 ms wait fires 3 or 4 interrupts, depending on phase, with their logged deadlines inside
    the wait;
  - the resolution bound holds;
  - `millis()` read after the wait shows it.
- **Overload:** `d_isr` above one block starves the main loop, and the counters show it.
- **Chosen points:** firing at the Nth entry of a synthetic function happens there, and an
  impossible placement is refused or marked as forced.
- **Determinism:** the same seed gives the same log hash, and different seeds give different
  interleavings.
- **Resolver:** a vtable with a slot that has no model is refused.
- **Idle skip:** a synthetic loop that polls `millis` until a deadline gives the same result, at
  the same millisecond, with and without the skip; a pass that writes memory or calls an impure
  model is never skipped.
- **Two instances:** a synthetic interrupt handler that reads the interrupted registers gives the
  same result in the interrupt instance as in one instance.

### 2.5 Checks on the firmware

Mods and reference builds, on `held`, `worst`, `tracklen` and `synthfx` from
`run_tests.py --project`:

- Boot, Open and Play for 10 virtual seconds. Notes go out, and there are no failures, no
  invariant breaks and no hooks in closed code.
- **Heap after boot** matches `boot_rig.py` (80,620 B in `PAST_FAILURES.md`) within the margin
  the two paths explain. Today they differ only in timing, so any difference gets explained.
- **Max stack** is at least `boot_rig.py`'s 7,280 B, and compared with the unit's 7,333 to 7,777
  B. Interrupts during `init` and in waits may raise it towards the unit's figure. If so, that's
  agreement improving.
- **CE's Profiler** (Config's CPU line, read from its variables) shows the modelled `d_isr`. This
  checks that the clock and the Profiler agree with each other.
- The same seed twice gives the same log hash, on every project.
- **The skip against a full run:** on `held` and `worst`, boot, Open and 0.3 s of Play give the
  same audio with and without `--no-skip`.
- **Speed** in virtual seconds per wall second, per project.

### 2.6 Into the test runner

- `run_tests.py` quick tier: `test_sim_kernel.py`.
- Release tier: `sim boot <project> <build>` jobs *beside* today's boot and preempt jobs, not
  instead of them.
- `docs/mt/PAST_FAILURES.md`: a calibration row for the simulator next to `boot_rig.py`'s.
- This file: the results section, filled in.

## 3. Done when

- 2.1's answers and 2.2's measurements are written here.
- D2 is decided.
- Every check in 2.4 passes.
- Every check in 2.5 passes or has a written explanation.
- Determinism holds on every project.
- The speed figures are recorded.
- Nothing in it claims anything about the unit's timing: all timing parameters are still
  placeholders until stage 4.

## 4. Cost

About 3 to 5 working sessions. That's one more than SIMULATOR_PLAN.md first guessed, because of
the trampoline and the resolver. Compute is small apart from 2.5, which is roughly one release
run's worth of boot jobs.

Nothing here needs a download or an install. Unicorn 2.1.4, Capstone and the WSL toolchain are
already there. Capstone is used only on the synthetic test code and on open code, never on the
closed libraries.

## 5. Risks

- **Unicorn's `count` may not be exact** in some Thumb cases (IT blocks, exceptions). The spike
  checks first. If it isn't, 1.1 falls back to the block-hook counter, which is slower.
- **Too slow.** That's the decision point in 2.2.
- **Booting with interrupts during `init`** may show a CE start-up race that `boot_rig.py` never
  saw. That's a finding, reported as one, not hidden behind `--interrupts-after-init`.
- **Placeholder timing** could make a stage 1 run look like it says something about the unit. The
  "uncalibrated" banner and section 3's last point are there to stop that.

## 6. Questions for the user before the code starts

- **D2:** native counting, including any main-loop instructions spent in the closed libraries, or
  open code only? Decided after spike 2: native (1.1).
- **Going ahead with the code:** stage 1 runs firmware under the emulator and builds the mods and
  reference `.axf` files in WSL as usual. That needs your go-ahead, since testing is on hold.
- **The scratch branch for the spike:** its code is thrown away or kept by your call. Nothing
  from it is committed without your OK.

## Results

### 2.1 Read-up answers

- **Audio interrupts.** The output interrupt is the SAI1 TX DMA, channel 0, on `DMA0_DMA16_IRQn`. Its TCD
  covers two blocks, `i2s_tx_buffer[kAudioBlockSamples * 2]`, with the half and major interrupts both on
  (`output_i2s.cpp` `begin`). So each interrupt hands over one 128-sample block, and `isr` picks the
  half from the DMA's `SADDR`. `EDMA_CreateHandle` enables the NVIC line early, in `SAI1_init`, which
  `BOARD_InitBootPeripherals` calls (`board/peripherals.c`). The transfer, and so the interrupts,
  start in `AudioOutputI2S::begin`, inside `initHardware`. That's before `TrackerImpl::init`, so the
  interrupt runs during `init` on the device.
  - There's a second interrupt: input, SAI1 RX on channel 1, `DMA1_DMA17_IRQn`, also half plus major,
    at `kAudioInputInterruptLevel`. It calls `updateInputBuffers`. Stage 1 must at least decide whether
    to model it. The plan only has the output interrupt.
- **Boot order** (`PlatformTrackerIMX.cpp`):
  1. Board pins, clocks and peripherals.
  2. `mainCpu.setFrequency(CPU_FREQ)`.
  3. `usb->init`, then `timeControl.init`.
  4. `initHardware(platform, updateBuffers, updateInputBuffers, startDisplay)`. Inside that,
     `audioHardware.init` starts the codec and the DMA.
  5. `platform.init`, then `trackerInstance->init`.
  6. The main loop.
- **The device's main loop** isn't only `TrackerImpl::update`. Each pass runs `usb->update`,
  `DevConsole_Update`, a blink-LED `millis` check, `updateHardware(&trackerInstance)`, `platform.update`,
  then `trackerInstance->update` (plus `mtLockupBeat` in the recorder build). The trampoline runs only
  `update`, because the rest is platform code that drives real peripherals. Their time per pass has to
  come from calibration as a flat per-pass charge. It's part of `c_main`'s per-pass factor.
- **CPU clock.** `CPU_FREQ` is 720,000,000 (`platforms/imx/config/config.h`), set through `SetArmClock`. That's
  720 MHz, not the 528 MHz I assumed in the plan. One instruction a cycle would make `c_main` about
  1.39 ns, so the spike's 2 ns placeholder is on the slow side.
- **Display waits.** `DisplayDriverImpl::awaitCoProEmpty` and `isCoProEmpty` go to the FT812 API layer
  (`EVE_WaitCmdFifoEmpty`, `EVE_IsCmdFifoEmpty`), which polls the chip over SPI. Those are the waits for
  the waits table. The rig's driver answers "empty" at once.
- **Masks.** `DISABLE_GLOBAL_INTERRUPTS` is `cpsid i` (PRIMASK), and `core/` uses it in 13 places.
  `AudioNoInterrupts` disables the audio NVIC line. Nothing in `core/` or `platforms/imx/source` uses
  BASEPRI. The rig already handles both kinds of mask.
- **`PlatformImpl`'s vtable** in `TrackerPlus_MT_c32fee7_base.axf` has 23 slots:
  - 0 `setPowerMode`, 1 `resetMCU`, 2 `runBootloader`, 3 `setPowerButtonCallback`
  - 4 `getPowerButtonState`, 5 `readForceStartUp`, 6 `getCodecAudio`, 7 `getRadio`
  - 8 `getButtons`, 9 `getPads`, 10 `getLeds`, 11 `getBattery`
  - 12 `getTimer`, 13 `getEncoder`, 14 `getDisplayDriver`, 15 `getUsbAudio`
  - 16 `getUartMidi`, 17 `getUsbMidi`, 18 `getConfigManager`, 19 `getFileSystem`
  - 20 `getCrcCalculator`, 21 `getSystemInfo`, 22 `getUsb`

  These match the slots `boot_rig.py` hard-codes (8, 12, 14, 19).

### 2.2 Spike results (2026-11-13)

Throwaway code in the session's scratch folder, nothing in the repo. Builds: `TrackerPlus_MT_c32fee7_base.axf`
(mods) and `TrackerPlus_ref_cemerge.axf` (reference). Projects: `held` and `worst` from `hellish downtown`.
Placeholders: `c_main` 2 ns an instruction, `d_isr` 30 % of a block. Unicorn 2.1.4 on this PC, inside
the 75 % CPU limit.

**1. Exactness.**
- Synthetic Thumb code: a known loop, `cpsid`/`cpsie`, and a `blx` to a Python stub. One run, single
  steps and random slices, with or without a context-saved "interrupt" between slices, all end in the
  same state.
- **IT blocks:** Unicorn never stops inside an IT block. A slice that ends in one runs the whole
  block, so `count=N` can run a few instructions over N. The state is still the same at the same
  program point: 40 of 40 random slicings, half with an interrupt after every slice, matched at
  iteration 700 of a loop with IT blocks and FPU instructions.
  - So the design has to say that slice counts are exact to within one IT block, not exactly N.
  - Determinism isn't affected, because the same slicing always overruns the same way.
- **Firmware:** from a point after Open, 3,000,000 main-loop instructions as one slice and as three
  different random slicings (1 to 48,000 instructions each) gave the same registers, RAM and card at
  the next pass boundary. Time was moved per pass for this check, so all runs read the same clock.

**2. Speed.** Boot, then Open (to the Pattern screen), then 0.3 virtual seconds of Play. Each row was
one of 5 runs in parallel; one run alone was about 1.3 times faster.

| Build, project, `max_slice` | Open: virtual s / wall s | Play: virtual s per wall s | Main-loop instructions a wall s | Interrupt, wall ms each |
|---|---|---|---|---|
| mods, held, 1,000 | 2.29 / 427 | 0.0049 | 2.6 M | 200 |
| mods, held, 4,000 (alone) | 2.29 / 256 | 0.0065 | 3.4 M | 150 |
| mods, held, 16,000 | 2.29 / 425 | 0.0050 | 2.6 M | 197 |
| mods, worst, 4,000 | 2.29 / 457 | 0.0051 | 3.0 M | 234 |
| reference, held, 4,000 | 2.29 / 459 | 0.0054 | 2.9 M | 182 |

- **Per-slice overhead is negligible.** 105,000 slices against 6,600 for the same work cost 40.5 against
  39.9 s, which is about 6 µs a slice. So `max_slice` can be 1,000, about 2 µs of device time.
- **The main loop is the bulk of it.** It runs about 425,000 passes a virtual second (509,000 on the
  reference), about 825 instructions each: 350 M instructions a virtual second at 2 ns. At 720 MHz
  the real figure may be higher.
- **Unicorn's `count=` is the other cost.** It counts with a hook on every instruction. Measured on
  the same state with `held` playing:
  - the interrupt takes about 9 ms without `count=` and 130 to 150 ms with it;
  - the main loop runs about 3.2 times as many passes a second without it.
  - Turning `count=` off and on in the same Unicorn instance flushes its translated code, so the
    two can't simply alternate per interrupt. A second Unicorn instance for the interrupt, sharing
    the same memory (`mem_map_ptr`), would avoid that. Not tried yet.

**3. Open-code-only counting (D2).** A Python block hook over the open ranges (all of ITCM and flash except the closed libraries' symbols), which stops the slice
when its budget is spent, with no `count=`. It ran at about 0.55 M main-loop instructions a second
and about 300 ms an interrupt, because the hook also fires on the interrupt's open code. That's about
6 times slower than `count=` on the main loop, and it still needs a flat charge per call into the
libraries. Run stopped after 0.74 virtual seconds of Open.

**4. Snapshot.** `context_save` plus a copy of the 80 MB of mapped RAM takes 76 ms, and restoring it
9 ms. A snapshot also has to copy the rig's Python state (heap pointer, card files, clock, held
buttons), which is small.

**Decision point: not met.** A 20-second song would take 51 minutes alone at 0.0065 virtual seconds a
wall second, and about 67 minutes in a parallel run. The limit is 10 minutes. Per virtual second of
Play, about 100 s goes to the main loop and about 50 s to the interrupts. 2.3 waits for your decision.

## Rethink after the spike (2026-11-13)

You chose to stop and rethink rather than go on with short scenarios. Nothing below is built or measured
yet, except where it says so.

### What the spike says about the problem

- **Almost all the cost is the main loop doing nothing, at the device's speed.** About 425,000 passes
  a virtual second, nearly all of them idle polls. Emulating every one of them is what makes a
  second of Play cost about 100 wall seconds. A faster clock model doesn't change that, and neither
  does a bigger `max_slice`.
- **The rest is `count=` slowing the interrupt about 16 times** (about 150 ms against about 9 ms),
  even though the interrupt's instructions are never counted.
- **The plan's test grid (SIMULATOR_PLAN.md 8.3) can't fit at any of these speeds.** That's 3 tempos ×
  8 card scales × 4 stalls × 8 phases × 3 seeds × placement search, times three builds. It has to
  shrink whatever is chosen.

### The options

1. **Skip idle passes, and run the interrupt in a second Unicorn instance.** Recommended, if spike 2
   below supports it.
   - **The interrupt:** a second Unicorn instance with no `count=` hook, mapping the same RAM
     (`mem_map_ptr`). The main loop keeps `count=` in the first instance, so the clock is unchanged.
     Neither instance ever flushes its translated code.
   - **Idle skip:** a pass is idle if it wrote nothing outside the main stack, checked with a write
     hook on the main loop's instance only. Same code, same RAM, same time, so the next pass would do
     the same nothing. After an idle pass, the clock jumps to the next event: an interrupt deadline,
     an input, a card completion, or the next millisecond tick (`millis` resolution). Then one pass
     runs and is checked again.
   - **What it costs in fidelity:** time reads inside skipped stretches can't happen. A `micros`-timed
     check can run up to 1 ms late, and the log says so. Passes that do work still run instruction
     by instruction at device speed, and so do the card loads behind the pattern-load race.
   - **Estimate, not measured:** 344 interrupts a virtual second at about 9 ms is about 3 s. Up to
     1,344 probe passes is under 0.1 s. The work passes are unknown, and are what spike 2 measures.
     If the work comes to about 10 M instructions a virtual second, a 20 s song takes about 2 minutes.
   - **The risk:** if some idle pass writes something every time (a pass counter, a timestamp, a
     profiler field), no pass is ever idle. A whitelist would weaken the "same next pass" argument, and
     each entry would need a reason.
2. **Time dilation** (the main loop runs slower than the device on purpose). Simple, and a 20 s song in
   about 5 minutes. Rejected: it stretches the pattern load itself, which is exactly what the race
   tests measure, so it would report stops the unit doesn't have.
3. **Pass-level time without `count=`**, with interrupts only between passes and at card calls. That's
   about 3 times faster per pass, and the interrupt is about 9 ms. On its own it still emulates every
   idle pass, so it's about 30 s a virtual second. It also loses preemption inside long passes. Only
   worth it combined with option 1's idle skip, and then option 1 is better.
4. **Stage 6's PC build, brought forward.** Native speed. It loses the closed libraries, preemption
   at any instruction, and our compiler. It also needs the port author's confirmation for any of
   their files. The race tests (T1 to T5) don't need synth sound, so this is the fallback if idle
   passes turn out never to be idle.
5. **Measure on the unit first.** The calibration firmware (SIMULATOR_PLAN.md 6.2) could also time
   request → load start → load end with DWT on real projects. That gives real slack figures for the
   pattern-load race without any simulator. It's complementary rather than a replacement: it can't
   sweep or place interrupts. It needs device time from you.

### Spike 2, if you want option 1 checked (about one session, throwaway code)

1. **Second instance:** map the RAM once with `mem_map_ptr` into two instances, and run the
   interrupt in the uncounted one. Check that the output blocks and RAM match the single-instance run
   on `held` and `worst`, and time it.
2. **Idle passes:** after Open, and during Play, record the addresses each pass writes outside the
   stack, for a few thousand passes. Is there a write-free pass at all, and how often?
3. **Speed:** if 2 finds idle passes, prototype the skip in `spike_fw.py` and measure a virtual second of
   Play on `held` and `worst`, mods and reference.

Gate: a 20 s song in 10 minutes or less, with work passes still counted instruction by instruction.
If it fails, the choice is between options 4 and 5.

### Spike 2 results (2026-11-13)

Throwaway code (`spike2.py` in the session's scratch folder). Same builds, projects and placeholders as 2.2,
`max_slice` 1,000. Five runs in parallel at a time, inside the 75 % CPU limit.

**1. The interrupt in a second Unicorn instance.**
- Both instances map the same host memory (`mem_map_ptr`). Every hook added to the main instance is
  added to the second. The second never uses `count=`.
- Before each interrupt, the main instance's R0 to R12, S0 to S31 and FPSCR are copied over, so the
  ISR saves the interrupted code's registers on the stack, as it would on the device.
- **Same result:** on `held`, through boot, Open and 0.3 s of Play (891 interrupts), the audio of
  every block and the whole final state (registers, all RAM, the card and the clock) are identical
  to the single-instance run. An earlier try that didn't copy the registers differed only in that
  dead stack.
- **Cost:** 7 to 17 ms an interrupt, against 120 to 150 ms with `count=`.

**2. Idle passes.**
- After Open on the Pattern screen, 8,315 of 8,356 passes (99.5 %) wrote nothing outside the main
  stack. In Play it was 21,273 of 21,290.
- The writes that did happen were display state, `s_lastCell`, the heap and the pattern editor.
- Every pass calls some Python stubs. Those that only answer a question whose answer depends on
  nothing but time and held buttons count as pure:
  - the timer reads;
  - `isCoProEmpty` (always 1), `isAsyncReadBusy` (always 0) and `isSdCardInserted` (always 1);
  - `isButtonPressed`;
  - the Platform getters that return fixed objects.

  Start-up polls `isSdCardInserted` on every pass. Any other stub call, `delay` included, makes the
  pass count as work.

**3. The skip.**
- **The rule:** a pass is idle if it wrote nothing outside the main stack (a write hook on the main
  instance only, over DTCM below the stack, OCRAM, SDRAM and the rig's scratch memory), called only
  pure stubs, and had no interrupt or input during it.
- **What happens:** at the next pass start, the emulator stops and the clock jumps to the next
  millisecond tick or the next interrupt deadline, whichever comes first. Then one whole pass runs
  and is judged again.
- **The cost:** the stop has an unknown instruction count. It's charged as 0 and counted as
  "uncertain". The error is at most one `max_slice`, about 2 µs.

| Build, project | Open: virtual s / wall s | Play: wall s per virtual s | 20 s song | Main-loop instructions emulated per virtual s of Play |
|---|---|---|---|---|
| mods, held | 2.29 / 28 | 4.4 | about 1.5 min | 1.6 M |
| mods, worst | 2.29 / 26 | 13.2 | about 4.4 min | 21.8 M |
| reference, held | 2.29 / 26 | 5.9 | about 2.0 min | 1.5 M |
| reference, worst | 2.29 / 26 | 13.1 | about 4.4 min | 21.5 M |

- Before the skip, Open took 350 to 460 wall seconds, and a 20 s song about 51 to 67 minutes.
- About 70 % of Play's virtual time is skipped (0.70 s in each second), in about 1,040 skips.
- **Same audio:** with the skip on through boot, Open and 0.3 s of Play, every audio block matched
  the run without it (same hash over 891 blocks). The main loop's own state differs, as expected,
  because idle-time work lands up to 1 ms later.
- Open still takes 2.29 virtual seconds with the skip, the same as without it.

**Gate: met.** A 20 s song takes about 1.5 to 4.4 minutes, under the 10-minute limit, and passes that
do work are still counted instruction by instruction.

**What this spike didn't check:**
- No race test (T1 to T5) has run.
- Card latency is still instant: card calls don't wait yet, so the work passes are shorter than
  they will be once the waits table exists.
- Only `held` and `worst` were measured, not a sample-heavy project.
- The write hook doesn't cover ITCM. The kernel must add it, or show that the main loop never
  writes there.
- The list of pure stubs was made by hand from the rig's stubs. A new stub counts as work until
  it's added to the list, which is the safe default.
- Audio equality over 0.3 s on one project isn't proof that the skip never changes behaviour. It
  only shows that it didn't here.

**D2, recommended: native counting** (`count=`, including the main loop's few instructions in the
closed libraries). The open-code-only block hook was about 6 times slower and would still need a
flat charge per library call. With the skip, counting costs little anyway, because few instructions
are emulated.

**What changes in the design (1.1 to 1.6) if you go ahead:**
- the second instance for the interrupt, with registers copied in;
- the idle skip, with its rule, its 1 ms resolution, and the "uncertain" stop logged;
- the list of pure stubs, kept in one place;
- the write hook, including ITCM;
- `c_main`'s per-pass charge for the device's other per-pass calls (2.1 above).

### 2.3 to 2.5 results: the kernel (2026-11-13)

The kernel is built (2.3): `sim_kernel.py`, `vtables.py`, `sim_rig.py`, `sim_boot.py` and
`test_sim_kernel.py` in `tools/emu/`, and a quick-tier job in `run_tests.py`. Nothing is committed.
All timing values are still placeholders (1.6), so none of the times below says anything about the
unit.

**2.4, checks of the kernel itself: 37 of 37 pass** (`test_sim_kernel.py`, under a second). They
cover cadence and the input interrupt, masks (on synthetic Thumb too), waits, overload, chosen
points, the idle skip, determinism, the two instances and the resolver.

**2.5, checks on the firmware.** `held` and `worst`, mods (`c32fee7`) and reference builds, boot,
Open and 2 virtual seconds of Play. Five runs in parallel, so the wall times include contention.

| Build, project | Result | Notes in 2 s | Open: virtual s / wall s | Play: wall s per virtual s | 20 s song |
|---|---|---|---|---|---|
| mods, held | ok | 8 | 2.735 / 30 | 9.0 | about 3 min |
| mods, worst | ok | 6,216 | 2.737 / 32 | 17.2 | about 5.7 min |
| reference, held | ok | 8 | 2.733 / 33 | 9.0 | about 3 min |
| reference, worst | ok | 6,216 | 2.734 / 33 | 16.8 | about 5.6 min |

- No failures, no broken invariants and no unexpected card calls in any run.
- **Speed:** the gate (a 20 s song in under 10 minutes) is still met. Open is 0.45 virtual s
  longer than in spike 2, because card calls now wait (about 4,050 calls).
- **Contention, not the skip, made interrupts slower:** an output interrupt cost about 18 ms of
  wall time in the five-way runs, and 11.4 ms with two runs at a time. With two at a time, `held`
  Play took 5.5 wall s per virtual s.
- **The skip against a full run:** on mods `held`, boot, Open and 0.3 s of Play give the same audio
  hash with and without `--no-skip`. Without the skip, Open takes 119 wall seconds and Play 51 wall
  s per virtual s.
- **Determinism:** `--replay` of the mods `held` run gave the same log hash (`0b08ce2c1cb26104`).
  Checked on that one run only, not on every project.
- **Heap after boot:**
  - with interrupts during `init`, as the device does: 80,264 B on mods; with
    `--interrupts-after-init`: 80,268 B, the same as `boot_rig.py` on the same Workspace project.
    The 4 B difference isn't explained yet.
  - reference: 81,096 B.
  - `PAST_FAILURES.md`'s 80,620 B is a different setup, so it isn't compared.
- **Max stack:** 7,280 B after Open, the same as `boot_rig.py`, against the unit's 7,333 to
  7,777 B. Interrupts during `init` haven't raised it. During boot alone it's 5,824 B on mods and
  3,504 B on the reference.
- **One interrupt held by a mask (mods only).** Interrupt 432, during Open at about 1.254 virtual s,
  was held 255 µs by the audio NVIC mask. The mask was set by a call to `disableAudioInterrupt` from
  inside the closed libraries, about 99 µs before the deadline. As a black box, that's all that's
  known. The reference run had none, perhaps only because no deadline fell inside the same window.
  With placeholder timing this isn't a finding about the unit.

**What the kernel work found about Unicorn (2.1.4), for later stages:**
- With `count=`, Unicorn checks every `UC_HOOK_CODE` hook on every instruction, even one-address
  hooks. 0 hooks: 254 M instructions a second; 1: 64 M; 30: 9 M; 400: 1 M.
- Memory-write hook ranges cost every store: 1 range 27 M/s, 291 ranges 2.7 M/s. The ITCM watch
  merges gaps of up to 32 bytes into 6 ranges.
- Block hooks are nearly free under `count=` but cost the uncounted instance a check per block.
  So the main instance uses block hooks and the interrupt instance code hooks.
- `emu_start(timeout=…)` costs about 15 ms a call. A watchdog thread stops a hung interrupt instead.
- `mask_step` is 1, so a held interrupt fires on the first instruction after the unmask.

**Not done or not checked:**
- `tracklen` and `synthfx` haven't run. Neither has 10 s of Play: only 2 s.
- No race test (stage 2).
- CE's Profiler check isn't done.
- Determinism on more than one run.
- The calibration row in `PAST_FAILURES.md` and the release-tier jobs (2.6).
- The 4 B heap difference with interrupts during `init`.

## Targeted test: does a mod stop the song? (2026-11-13)

After stage 1 the user chose one targeted test instead of stages 2 to 5 of the plan:
`tools/emu/sim_songstop.py`, on the stage 1 kernel. It checks [SIMULATOR_PLAN.md](SIMULATOR_PLAN.md)
section 1: since CE's `15c3d23` and `c476ce1`, the next pattern is read in the main loop after the
audio interrupt asks for it, and must be in memory by the end of that step, or CE stops the song.

**Setup:**
- **Builds:**
  - R, the CE reference (`TrackerPlus_ref_cemerge.axf`; no core change between it and `c32fee7`);
  - N, `modular` at `c32fee7`;
  - F, `c32fee7` plus the waiting fix (both mods also wait while `isPatternLoading()`), built from a
    clean copy for this test. The fix itself isn't committed.
- **Project:** `held`'s sounds, with a song of 48 patterns of 16 steps at 800 BPM (a step is 18.75
  ms, a pattern 300 ms). Every pattern is marked as used, and no Color Song colours are saved, so
  Color Song reads every pattern file once.
- **Color Song:** switched On after Open, Song screen, Play 40 ms later. It reads a pattern file
  every 100 ms while the song plays.
- **Copy to New:** the song started from the Song screen, then the Pattern screen. The job the button
  starts is called every 137 ms, so each press checks the card and writes a 16 KB pattern.
  That's far more often than anyone presses it; it's a stress case.
- **The card:** `--card-scale` multiplies the placeholder card times (100 µs a call, 50 ns a byte).
  At scale 1 a whole pattern load takes about 2.4 ms; at scale 8, about 17 ms. **No scale is known to
  match a real card.**
- **A stop** is `Sequencer::stop` called in the audio interrupt while playing.
  `stopForMissingNextPattern` is inlined; nothing else stops the song in this scenario.

**Results** (the first stop, if any, in 6 virtual seconds; "margin" is the smallest time between the
pattern landing in memory and the step's end):

| Card scale | R (CE) | N, mods Off | N, Color Song | F, Color Song | N, Copy to New | F, Copy to New |
|---|---|---|---|---|---|---|
| 4 | plays | | | | plays, margin 0.9 ms | |
| 6 | | | stops at 1.51 s | stops at 1.51 s | plays, margin 0.58 ms | plays, margin 0.58 ms |
| 7 | | | stops at 0.32 s | stops at 0.32 s | | |
| 8 | plays, margin 0.39 ms | plays, same load times as R | stops at 0.32 s | stops at 0.32 s | stops at 2.41 s | stops at 2.41 s |
| 9, 10 | stops | stops (9) | | | | |

Blank cells weren't run.

**What it shows:**
1. **Both mods can stop the song at card speeds where CE alone plays.** CE alone stops from scale
   9. Color Song stops from scale 6 or lower; Copy to New, pressed this often, at 8.
2. **The waiting fix made no difference in any run.** F's results equal N's to the 0.01 ms. In every
   delayed load, the mod's card access had started before the request (Color Song 6 to 15 ms
   before; Copy to New about 7 ms before) and was still running when it came. The load started only
   after that access ended, 2 to 7 ms late instead of 0.9 ms. The fix stops a mod from *starting* an
   access once a load is requested. It can't stop one already under way, which is the window
   SIMULATOR_PLAN.md section 1 already said stays open, and here it's the one that matters.
3. **How much it costs:** a mod's access already under way delays the load by up to its own length,
   about one pattern read. CE alone has room for one load inside a step. With a mod access in front
   of it, it needs room for about two.
4. **Why Color Song hit it so often:** its reads are 100 ms apart, and a 300 ms pattern is exactly 3
   of those, so once a read overlaps a pattern end it overlaps every one. Other tempos and pattern
   lengths drift in and out of it.

**What it doesn't show:**
- **Whether a real card is ever this slow.** The card times are placeholders, and no calibration is
  planned. At 120 BPM a step is 125 ms, so the same overlap needs a load about 7 times slower than
  at 800 BPM. The risk grows with tempo and with slower or busier cards.
- **The fix's own case,** a mod access starting between the request and the load start, wasn't hit.
  That window is the part of a pass before the mod's update, much shorter than an access.
- Only `held`'s sounds, 16-step patterns, one seed, one interrupt phase and one Play delay.
- Copy to New's presses were made by calling the job directly, not through the menu.
- Color Song reads only while it builds its colours: the first visit to the Song screen, or after
  edits. Once the colours are known it doesn't read.

**What would close it:** a mod could avoid starting a card access while a pattern end is near in
song mode (for example, not in the last steps before the end, with a margin of a pattern read or
more). Then nothing of the mod's is under way when the request lands. The same test checks it:
the mod's stops should move to CE's own threshold (scale 9 here). This is a design note, not
built.

Each run takes about 1 to 1.5 minutes of wall time. Run five at a time to stay inside the 75 % CPU
limit. Example:

    python tools/emu/sim_songstop.py <axf> <held project> --patterns 48 --card-scale 8 --mod colorsong
