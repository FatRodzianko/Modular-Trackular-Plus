# CLAUDE.md

Fork of polyend/tracker-ce with our mods as runtime switches under `core/mods/`. Plan: `docs/mt/CE_PORT_PLAN.md`.

## What runs in the audio interrupt

`AudioOutputI2S::isr` → `updateBuffers` → `TrackerImpl::updateAudioBuffers` (`core/source/Tracker.cpp`) → `midiUpdate()`, `sequencer.tick()`, `engine.update()`. So every MIDI handler registered in `midiInit`, the whole sequencer and the audio engine run in the interrupt, on both platforms. So does the `scheduleMidiClockUs` callback, and on IMX `TMR4_IRQHandler`.

On those paths: no SD, heap, blocking or UI calls (`display.*`, `mtPopups.*`). Set a flag and do the work in a main-loop `update()`. Settings a hook needs must already be in RAM.

No new `double` there either. Both builds use single-precision FPU flags, so `double` and unsuffixed literals like `0.5` are software calls; use `0.5f` and `sinf`. CE has some already, such as `Sequencer::getTimerPeriod_us`; leave those alone.

`core/` builds with `-fno-builtin`, so `memset`, `memcpy` and `floorf` are real function calls.

Before hooking a function, grep its callers up to an entry point above or to main-loop code.

**CE breaks these rules itself, so a function can run in the interrupt even if it looks like main-loop code:**
- MIDI Start in Song mode → `playSongFromPosition` → `saveWorkspacePatternNow`, `loadWorkspacePatternNow`. Never hook slow work onto these.
- `Sequencer::stop` can call `loadWorkspacePatternNow`, and MIDI Stop reaches `stop`.
- `Sequencer::loadNextPattern` → `loadWorkspacePattern` → `stopAutosave` → `WorkspaceProjectSave::stop`, which spins on `update()` while the step is `saveInstrumentSample`. Keep hooks out of that step. `finishStep`, where `mods::onWorkspaceSaved` runs, isn't reached this way.
- MIDI note-off on the Sample Recorder screen → `hidePreviewValue` → `display.setControlText`.

## Display limit

Each frame has 8,172 bytes of display list. `cDisplay::step3` appends controls from the highest slot down and stops at the first that doesn't fit, so the rest silently don't draw. Count the bytes of anything added to a screen. `lastMemoryUsed` reports the total in DEBUG builds only.

`controlStyle*` bits 0–29 are taken; only 30 and 31 are free.

## Memory

Map and figures: `docs/mt/CE_FIRMWARE_MAP.md`. Run `python3 tools/mem_budget.py <axf>`: it gives the real free space per region and fails under a floor. Give each mod's cost in its plan section.

- State the audio interrupt touches per sample or per step: DTCM, and keep it small.
- Hot audio code: ITCM (`AUDIO_CORE_RAM`). About 22 KB is free, so pick the inner loops.
- Tables for the main loop: SDRAM with `__NOINIT(EXTERNAL_RAM)`. Startup doesn't zero it, so the owner must. Mod builds have 1 MiB for this, taken from `SAMPLE_MEMORY_SIZE`; don't lower that further without asking.
- Constants: flash.
- OCRAM is CE's, and mods stay out. Mods don't use the heap: after boot a failed malloc hangs the unit.

Heap and stack can only be read on the device: an empty `/.develop` file makes Config show free heap and max stack. See the map for the last reading.

## Formats

`strInstrument` (372 bytes), `strEnv` (20) and `strPattern` have size `static_assert`s because files store them byte for byte. A failing assert means the change is wrong, not the number.

## Line endings

Many `core/` files are CRLF in the blob and a few are mixed; keep each file's style. Check with `git ls-files --eol`, and check `git diff --stat` after edits. Don't test with `grep $'\r$'` in Git Bash: it has returned 0 on CRLF files here.

## Tests and the closed synth libraries

Run mod tests on a full build, with `libPolySynth.a` and `libPolyDSP.a` in. Their licence allows it, but section 2(c) bans reverse engineering, so they're a black box: no disassembly, no tracing, no hooks inside them, no reading their objects by offset, and no measuring an engine to work out how it works. Sound test plan, including when each part gets built: `docs/mt/SOUND_TEST_PLAN.md`. CPU cost is measured on the device with the Profiler, with all 8 synth voices sounding.

## PRs to polyend/tracker-ce

CI rejects AI `Co-authored-by:` trailers on PR commits, and `CONTRIBUTING.md` also requires human commit authors. This file and `core/mods/` stay off PR branches.
