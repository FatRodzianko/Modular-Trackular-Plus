# CLAUDE.md

Fork of polyend/tracker-ce with our mods as runtime switches under `core/mods/`. Plan: `docs/mt/CE_PORT_PLAN.md`.

## What runs in the audio interrupt

`AudioOutputI2S::isr` → `updateBuffers` → `TrackerImpl::updateAudioBuffers` (`core/source/Tracker.cpp`) → `midiUpdate()`, `sequencer.tick()`, `engine.update()`. So every MIDI handler registered in `midiInit`, the whole sequencer and the audio engine run in the interrupt, on both platforms. So does the `scheduleMidiClockUs` callback, and on IMX `TMR4_IRQHandler`.

On those paths: no SD, heap, blocking or UI calls (`display.*`, `mtPopups.*`). Set a flag and do the work in a main-loop `update()`. Settings a hook needs must already be in RAM.

No new `double` there either. Both builds use single-precision FPU flags, so `double` and unsuffixed literals like `0.5` are software calls; use `0.5f` and `sinf`. CE has some already, such as `Sequencer::getTimerPeriod_us`; leave those alone.

`core/` builds with `-fno-builtin`, so `memset`, `memcpy` and `floorf` are real function calls.

Before hooking a function, grep its callers up to an entry point above or to main-loop code.

**CE breaks these rules itself, so a function can run in the interrupt even if it looks like main-loop code:**
- MIDI Start or Continue in Song mode (`handleMidiInStart`) starts the song in the interrupt with `setSongPos` and `play()`. Since CE's 98468f0 it no longer goes through `playSongFromPosition`, whose callers are now all main-loop, but hooks on `setSongPos` or `play()` still run in the interrupt there.
- `Sequencer::stop()` from the main loop can call `loadWorkspacePatternNow`. Interrupt paths, MIDI Stop included, use `stop(StopContext::audioCallback)`, which leaves that to `update()`.
- `Sequencer::loadNextPattern` → `loadWorkspacePattern` only records the request (CE's 15c3d23). `cFileManager::update()` starts it in the main loop (`startRequestedPatternLoad`, which stops the autosave), so the interrupt no longer reaches `stopAutosave`.
- At a pattern end in song mode, and for a queued pattern change, the sequencer swaps patterns only if `isPatternPreloaded` says the buffer holds the one it is about to play, and otherwise stops (CE's c476ce1). A mod that changes which pattern comes next must check the pattern it will switch to without deciding again: Song Loop does it with `mods::songloop::expectedNext`. Emulator rigs that stub `loadWorkspacePattern` must answer `isPatternPreloaded` too.
- MIDI note-off on the Sample Recorder screen → `hidePreviewValue` → `display.setControlText`.

**CE skips silent work in the interrupt (7482cc8, 0238786).** With `AUDIO_NULL_IS_SILENCE` a null block is silence, and these skip work for it:
- `transmit` passes nothing to an inactive destination.
- The reverb skips a block while `reverb.IsQuiet()`.
- The delay stops processing, and zeroes its lines, once `quietSamplesCount` reaches its time.
- `PolySynth::update` skips a part whose voices `prepareSilentInstrument` calls idle.
- The limiter's sidechain mux is off while CE's Sidechain is 0.
- Objects downstream get no block at all while it's silent: a track with no sound sends none, and neither does the master into the limiter.

These are decided from CE's own state. A mod that keeps state on one of these paths must run before the skip, or switch it off while that state runs. Master FX's delay does this with `mods::masterfx::activeDelay` in `effect_shortDelay.cpp`, and Synth FX with `holdsPart` in `synth_PolySynth.cpp`. Otherwise the tail is cut and the old state plays on the next note. A mod that runs only when a block arrives must run on a zeroed block instead: the dynamics slot does in `AudioEffectLimiter::modsUpdate`, or Glue's level wouldn't release over silence.

## Display limit

Since CE's 9d6d10a, `cDisplay::buildControl` builds each control's display list in the MCU, and `composeFrame` puts the frame together. Count the bytes of anything added to a screen, against three limits:
- A frame takes at most `kFrameControlBytes`, 8,172 bytes of controls. `composeFrame` adds them from the highest slot down and skips any control that doesn't fit. That control silently doesn't draw, while smaller ones after it still can.
- A control's own list over 8 KB (`kFragmentWords`) is thrown away whole and draws nothing.
- The lists of every control that exists, shown or hidden, share a 56 KB arena in SDRAM. A list that doesn't fit draws nothing.

All three only add to `displayList::overflows`, which nothing shows; `lastMemoryUsed` is no longer set. Inside a control's `update()`, a coprocessor command with no MCU expansion is dropped (`displayList::droppedCommands`). The ones with an expansion are text, number, gradient, bitmap set-up and `API_BITMAP_ROTATE_270`. Draw with plain display-list words. The emulator rigs stub the `API_*` functions and count an upper bound per call; they don't run the MCU capture.

`controlStyle*` bits 0–29 are taken; only 30 and 31 are free.

## Memory

Map and figures: `docs/mt/CE_FIRMWARE_MAP.md`. Run `python3 tools/mem_budget.py <axf>`: it gives the real free space per region and fails under a floor. Give each mod's cost in its plan section.

- State the audio interrupt touches per sample or per step: DTCM, and keep it small.
- Hot audio code: ITCM (`AUDIO_CORE_RAM`). About 40 KB is free since CE's cc53303 (the map's section 15), and CE keeps moving its own audio code there, so pick the inner loops.
- Tables for the main loop: SDRAM with `__NOINIT(EXTERNAL_RAM)`. Startup doesn't zero it, so the owner must. Mod builds have 1 MiB for this, taken from `SAMPLE_MEMORY_SIZE`; don't lower that further without asking.
- Constants: flash.
- OCRAM is CE's, and mods stay out. Mods don't use the heap: after boot a failed malloc hangs the unit.

Heap and stack can only be read on the device: an empty `/.develop` file makes Config show free heap and max stack. See the map for the last reading.

## Formats

`strInstrument` (372 bytes), `strEnv` (20) and `strPattern` have size `static_assert`s because files store them byte for byte. A failing assert means the change is wrong, not the number.

## Line endings

Many `core/` files are CRLF in the blob and a few are mixed; keep each file's style. Check with `git ls-files --eol`, and check `git diff --stat` after edits. Don't test with `grep $'\r$'` in Git Bash: it has returned 0 on CRLF files here.

## Tests and the closed synth engines

Tests stay under 75 % of the CPU unless the user says otherwise. `tools/emu/cpu_limit.py` enforces it for anything that imports `machine.py` and for `tools/run_tests.py`; anything else long-running has to hold to it too.

Before planning a mod's tests, read `docs/mt/PAST_FAILURES.md`. Before any device test, `tools/run_tests.py --tier release`, then the steps in `docs/mt/DEVICE_TEST_LADDER.md`. A pass doesn't mean safe on the unit: say what ran, what didn't, and the risk.

Run mod tests on a full build, with CE's factory `.psyn` engines on the fake card (`build/Synths` or `synth/prebuilt`). Their licence (`synth/prebuilt/LICENSE-PROPRIETARY.txt`) allows it, but section 2(c) bans reverse engineering, so they're a black box: no disassembly, no tracing, no hooks inside them or in the `.psyn` pool they load into, no reading them by offset, and no measuring an engine to work out how it works. CE's host (`synth/`) is open, and so are our WAVE and PHASE: the rigs may hook those (`watch_own` in `sound_rig.py`). Sound test plan, including when each part gets built: `docs/mt/SOUND_TEST_PLAN.md`. CPU cost is measured on the device with the Profiler, with all 8 synth voices sounding.

## User pages and UI mockups

A mod that adds or changes anything on the screen (a page, button, column, label, list or picker) gets a user page, `docs/mods/<mod>.md`, linked from its row in the README's Mods table. The page shows a mockup of every screen the mod changes, and its steps refer to what the mockup shows. The user page is for users, so it follows the README's plain style; the plan doc in `docs/mt/` is separate.

- Mockups are SVG files in `docs/mods/img/`, named `<mod>-<screen>.svg`. Draw them at the Tracker+ screen's proportions (800 × 480), in CE's default theme colours.
- Every label in a mockup is copied from the firmware's own strings, not paraphrased. Mark what the mod adds or changes, for example with an outline, and say in the caption what's marked.
- One mockup for each state the steps describe, for example a list closed and open, or Off and On.
- Show the mod in use, with its effect visible, not just the screen it's on. How much depends on the mod: a mod with its own pages, controls or plugins (Synths, Synth FX, Master FX) shows every page and state; a mod set only in Project Settings (Global Swing) shows that settings page.
- The page ships with the mod: a mod that changes the screen doesn't merge into `modular` without it. A later change to the mod's screens updates its mockups in the same merge.
- A mod that ships plugins or other content (a default pack, presets, tables) lists every item on its user page: its name as the screen shows it, what it does in plain words, each control with its on-screen label, range and default, and its cost with what the figure measures. Take it all from the sources, and check that the list matches what the firmware ships.

## Merging into modular

Every merge into `modular`, from CE's `main` or from a mod branch, goes through all of these before it is committed. Conflicts and passing tests are not enough: git merges a change silently when it touches different lines, and the emulator fakes the SD card, the file manager and real timing.

1. List every incoming commit (`git log --no-merges <base>..<incoming>`).
2. For each one, write down the behaviour it changes: functions, shared state (file manager status, buffers, `requestedPattern`/`preloadedPattern`, sequencer state), what runs in the interrupt or the main loop, timing and SD access.
3. Check each change against every mod: `// MT:` hooks inside changed functions, mod calls to changed functions, and mods using the same state, buffers, SD card or timing (grep `core/mods/`). A mod merge is checked against the other mods the same way.
4. A test or rig that fails or needs changing after the merge: first find out what the real firmware does. Never change a stub to make it pass without that answer written down.
5. List what the emulator can't show for this merge (SD timing, interrupt races, USB, sound) and decide the hardware checks. For each check, make the test project it needs: the user doesn't build projects.
6. The merge report and the commit message say, in plain words: what changed, what each mod interaction is, what was fixed, what ran, and the hardware checks with their projects. Not one line in passing.
   They also list what was not checked, analysis included. A merge is not called done or tested while anything important is on that list, and "tested" covers only what ran.
7. For a mod that changes the screen, check its user page and mockups (above) against the merged firmware's labels and screens.
8. Then tag and release as below.

## Releases

Every merge into `modular` of a mod, a mod fix or CE's `main` gets an annotated tag on the merge, pushed with it. CI (`build.yml`) builds the Tracker+ firmware from the tag and publishes a public GitHub release named after it, with the tag's message as the notes and `TrackerPlus_MT_<tag>.ptf`, the `Effects`, `Synths` and `Words` zips attached.

- A mod merge: `<mod>-<version>`, e.g. `resample-1.0`; a fix to it later: `resample-1.1`.
- A CE merge: `CE_YYYYMMDD`, with `-2` for a second one that day.
- Anything else: `modular-<version>`.
- The message says what's new, lists every mod with its version, names the CE commit it's based on, and gives the install steps and the 1.2.0 warning. No `#` at line starts: git strips them.

## PRs to polyend/tracker-ce

CI rejects AI `Co-authored-by:` trailers on PR commits, and `CONTRIBUTING.md` also requires human commit authors. This file and `core/mods/` stay off PR branches.
