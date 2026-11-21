# resample on CE: build plan

Status: on `modular` `CE_20261120` (`ff9b46a`) since 2026-11-20 ([section 14](#14-catch-up-to-ce_20261120)), on
`CE_20261119` (`71b4a8c`) from 2026-11-19 ([section 13](#13-catch-up-to-ce_20261119)), rebased
onto `5b40785` on 2026-11-17 before that ([section 12](#12-rebase-onto-5b40785)).
Step 1 is committed (2026-11-19, on `modular` `5c331c1`, which adds the 1 KB heap trim). Step 2 is committed (2026-11-20, on `CE_20261119`). Step 3 is committed (2026-11-20, on `CE_20261120`). Step 4 is committed (2026-11-20). Step 5 is committed (2026-11-20). Step 0's results ([section 11](#11-step-0-results)) and
every test run before the rebase come from `c32fee7` and the old rigs, and are to be run again.
Your decisions are in section 9, 10 and 11 (from step 0) included. Based on the
source of `modular` at `5b40785` (rebased 2026-11-17 from `c32fee7`, section 12), which holds upstream's `main` up to `f25fb89` (the Sample Recorder
fixes of upstream PR 97 included), and on what Modular-Trackular shipped as `rs8` and `rs9` (in
`patch/all` up to `1.2.0-MR9`). Section 3.14 of [CE_PORT_PLAN.md](CE_PORT_PLAN.md) points here. Line
numbers are for `5b40785` and only show where to look.

## Summary

- **CE can't record its own output anywhere except Export.** The Sample Recorder records the Line In,
  mic or USB input only, and it still records over the loaded samples, as 1.2.0 did. Upstream has no
  PR, branch or issue for it (section 1).
- **The port is simpler than on 1.2.0.** CE's source lets each piece be a one- or two-line `// MT:`
  call into `core/mods/resample.cpp`, where the 1.2.0 patch had to reach the same places with pool
  words and thunks. 17 hooks (18 call sites) in five CE files, hook 1b added in step 3. No heap, no SDRAM.
- **What moves is the behaviour of `rs8` and `rs9`,** with the changes you chose (section 2.1): the Trk
  Audio and Trk Record buttons, saved with the project; any instrument in the recorder with Trk Audio
  on, played on the cursor track's rule with the ended-sample-track fallback. Nothing of 1.2.0's
  addresses, pool words or `resample.bin` writing moves; `resample.bin` is only read once, by the
  importer.
- **The tracker's sound is taken just before the metronome joins the mix** (decision 2), so the clicks
  are heard and never recorded. 1.2.0 had to take it after the metronome and mute it.
- **With Trk Audio on, the tracker is audible for the whole recorder visit** (decision 3), so a pattern
  started over MIDI, Play, Shift+Play or a live instrument all sound the same way.
- **Per-project settings live in `mods.bin`,** so the New Project and Save As bugs that `rs6` and `rs7`
  fixed can't happen.
- **Steps:** a fact-finding pass on stock CE in the emulator, then seven patches, each with its own
  emulator test, then the release tier, the ladder and a short hardware checklist (sections 6 to 8).

Choices I made myself, and anything beyond the brief, are in [section 10](#10-small-choices-made-and-extras).

## 1. Does CE already have it?

**No.** What exists, and how close it is:

| What | Where | Close to MT's feature? |
|---|---|---|
| The recorder's sources | `Recorder::record`, `core/source/Recorder.cpp` L46–81; `Recorder::Config::Source` has `kUsb` and `kI2s` only | No. The I2S queues carry Line In, Radio or the mics; nothing carries the engine |
| The input taps | `AudioInput.cpp` L5–39: the I2S stereo-to-mono object and the USB amplifiers feed `recordingQueueI2s`/`recordingQueueUsb` | No output tap there |
| Master export queues | `audioEngine::connectOutputToExportQueue`, `mtAudioEngine.cpp` L784–801: the final mixers (L and R) feed `exportQueues` `stem_master` | The tap 1.2.0 used. After the metronome, so not ours (decision 2) |
| The recorder's entry | `cSampleRecorder::start`, `fSampleRec.cpp` L283–403: `blockAllNoteOn`, `setSynthMute(kMuted)`, `blockDelayRefresh`, `clearDelay`/`clearReverb`, `disconnectMixerImpact`, `disableLineInImpact`, `setLimiterSidechain(0)` | Same idea as 1.2.0: the engine keeps running but note-ons are blocked and the mix is flattened |
| The engine during a take | `audioEngine::update`, `mtAudioEngine.cpp` L273–303: while `recorder.isRecordingActive()`, the 8 players' `update()` and the synth parameter update are skipped | Same as 1.2.0's early return; needs a hook |
| The recording's memory | `functActionRecord`, `fSampleRec.cpp` L1553–1555: `externalRamSampleMemory`, `SAMPLE_MEMORY_SIZE` | Same as 1.2.0: over the loaded samples, then a reload on leaving (`wasSampleMemoryOverwritten`, L1549, L2321) |
| Instrument in the recorder | `functInstrument` L2940–2962: `stepPopupOnlyMidiInstr`; `start` L384–397 swaps a non-MIDI selection for the recorder's MIDI instrument | MIDI only, as 1.2.0 |
| Bounce or resample anywhere else | `git grep` for resample, bounce, record output: only sample-rate conversion (`tuneResample`, WAV import) | Nothing |

**Upstream (read-only, 2026-11-12):** polyend/tracker-ce has one branch, `main`, and `modular` already
holds its latest commit. Of its 42 pull requests and its issues, none adds output recording. The
recorder ones are fixes: PR 97 (merged 2026-11-12: count-in on an external clock, the lost sample
reload, the instrument left selected, module switch options) and PR 16 (closed unmerged: a stereo
stop deadlock). Issue 28 asks for mono sampling. `gh` isn't installed here, so this was GitHub's
public API through `curl`, plus `git ls-remote`.

## 2. What MT shipped

The behaviour to port. The sources are Modular-Trackular's `LIVE_RESAMPLING_PLAN.md`,
`RESAMPLE_SOURCE_PLAN.md`, `RESAMPLE_INSTRUMENTS_PLAN.md` section 7 and `patch/resample/README.md`.

**Two buttons in the recorder's setup**, in the two columns after Source that are empty for every
source but Radio. A press toggles Off and On.

- **Trk Audio:** the pattern is heard in the recorder. Off is stock.
  - Play in the setup plays the pattern and it's heard, to set levels.
  - Rec then Play: the pattern starts during the take and is heard.
  - Shift+Rec, or Play during the count-in: the pattern starts with the take.
  - A Rec during the count-in never disarms it; a Shift+Rec there arms it.
  - Shift+Rec over a pattern already playing in the setup plays it again from its start.
  - Stopping the take stops the pattern; the delay and reverb stop with it, as stock's recorder does.
  - Off: Shift+Rec is plain Rec and Play stays silent, as stock.
- **Trk Record** (Line In only): the take holds Line In plus the tracker, added sample by sample and
  clipped. With Monitor on, the take is the tracker alone, which already holds the monitored Line In.
  - Trk Record On turns Trk Audio On; Trk Audio Off turns Trk Record Off and stops a pattern that Play
    started in the setup.
- **Kept per project.** Off on a new project and on a project without settings.
- **Recordings go after the loaded samples**, so they stay intact. With less than 10 s free, the take
  goes where stock puts it and the pattern stays silent for it.
- **Any instrument with Trk Audio on** (`rs9`): the instrument list in the recorder has samples, synths
  and MIDI; with Trk Audio off it is MIDI only.
  - The instrument selected before entering stays selected; leaving gives the previous screen its
    instrument back, as stock.
  - Pads and a MIDI controller play it, heard in the setup and during a take, and recorded under Trk
    Record.
  - **Which track plays a live note:** stock's rule with record mode treated as off, from the cursor's
    track: the first track not muted and not sounding (sample instruments: tracks 1–8).
  - **When no track is free**, where stock drops the note: the first sample track, from the cursor's,
    whose sound has ended: a 1-Shot or slice played to its end, the last note's instrument a sample, no
    Roll, Chord or Arp on that step, no delayed note, no live note. Never a loop, wavetable or granular
    sample, a synth or MIDI track. If none has ended, the note is dropped.
  - Nothing is written into the pattern, record mode on or off.
  - Trk Audio Off with a sample or synth selected selects the recorder's MIDI instrument again.
- **Not in MT, and not to be proposed:** a global fix for the record-mode takeover, or a preview-voice
  "17th track". You rejected both.

### 2.1 What changes from MT, by your decisions

| # | MT | MT+ |
|---|---|---|
| 1 | Trk Audio shown for USB | Hidden for the USB source (CE sends the USB input to the jacks there) |
| 2 | Recorded after the metronome; metronome muted from the count-in's end to the take's end, so the last click wasn't heard either | Recorded just before the metronome: every click heard, none recorded, count-in or not |
| 3 | Audible only after Play, Shift+Rec or a live sample/synth pick; a pattern started over MIDI stayed silent | Audible for the whole time the setup or a take is on screen, however the pattern starts |
| 4 | Shift+Play continued the pattern (1.2.0's meaning), audible | Shift+Play plays the song (CE's meaning), audible |
| 5 | Settings saved to `resample.bin` by themselves | A press is an unsaved project change, kept by the project's Save |
| 6 | Every recording after the loaded samples | Only with Trk Audio on; with it off the recorder is CE's, full length included |
| 7 | (1.2.0 reloaded the samples after a take) | CE's reload kept |
| 8 | Recorder buttons only | Same |
| 9 | Trk Record for Line In only | Same: the stereo "Line In" (CE's nine sources, section 13) |

## 3. What is different on CE

| | 1.2.0 | CE | Effect on the port |
|---|---|---|---|
| Hooks | pool words, BLs, ITCM thunk words, chained | one-line `// MT:` calls in CE's source, behind `MT_HAS_RESAMPLE` | No chaining, no layout tests |
| Sample memory | ~60 MB bank, everything in RAM | `SAMPLE_MEMORY_SIZE` = 60 MB − 64 KB − 1 MiB (mods' share); large samples stream from SD and keep only `ramFrames` in RAM | Far more free space for a take (section 4.4). The streaming ring buffers are their own `EXTERNAL_RAM` arrays (`VoiceStreamManager.cpp` L14–47), not in sample memory |
| Instrument addresses | packed from the bank's base | packed by slot order: each active instrument starts after the previous active one's RAM frames (`fileManagerInstrument.cpp` L17–65) | The end of the loaded samples is a walk over 48 instruments, as CE's own `findFreeSampleRamScratchStereo` (`fileTransfer.cpp` L1523) does |
| Per-project settings | `resample.bin`, written a moment after a change | a `mods.bin` record, in the Workspace, saved with the project | New Project and Save As handled by design (decision 5) |
| Synths | 3 synth instruments, gated by `seq+0x7dd` | the Synth SDK, one `SynthVoice` with parts; pattern synth notes gated by `setSynthMute`, live notes go straight to the synth's virtual MIDI port (`sendMidiNoteOn`, `mtMidi.cpp` L665) | Live synth notes need no gate of their own; pattern synth notes need `setSynthMute(kUnmuted)` |
| The engine during a take | early return in `engine_block_update` | `if (!recorder.isRecordingActive())` in `audioEngine::update` (L277) | One condition gains an `||` |
| Where the mix can be tapped | only after the metronome (the export queue) | anywhere in the graph: the limiters' outputs are the whole mix, monitored Line In included (it enters at the tracks-and-sends mixer, `connectInputToMixer` L424ff), before the final mixers add the metronome, SD preview and test tone (`AudioOutput.cpp` L23–27, `connectSpecialInputsToOutput` L693) | Decision 2's tap point |
| Count-in | `seq+0x6fc` callback, recording started from the update | `tick_uStep` (`mtSequencer.cpp` L198–255, interrupt) calls `onPreRollMetronomeEnd`, which sets `startRecordingOnPreRollEnd`; `cSampleRecorder::update` calls `functActionRecord` (main loop) | Same shape. No click handling needed with the new tap |
| USB | no output switch | in the recorder with the USB source, the jacks play the USB input (`refreshOutputSource`, `gSampleRec.cpp` L1074–1092); outside it, USB In+Out with USB attached does the same (`refreshOutputSourceOutsideTheRecorder`, `mtAudioEngine.cpp` L851–865) | Both are CE's and stay; no buttons with the USB source (decision 1) |
| Shift+Play | continue | plays the song (`functPlay`, L2907) | Decision 4 |
| Mix values | the recorder saved and zeroed engine-side levels | `disconnectMixerImpact` writes the defaults **into `mtProject.values`** and keeps the project's own in `mixerValuesStore` (L1571–1660) | The gates must call `connectMixerImpact`/`disconnectMixerImpact` strictly in pairs, or the user's mix is lost into the project (section 4.3) |
| Tracker Mini | — | `MT_MODS` builds only `PLATFORM_IMX` without `TRACKER_MINI` (`modsBuild.h` L14) | Tracker+ layout only (`ConfigScreenColumns`, `sampleRecorder.h` L270–288) |

## 4. Design

### 4.1 Hooks

Every hook sits behind `#if MT_HAS_RESAMPLE` and does nothing unless the recorder visit latched the
mod On (section 4.9), so Off is CE's path.

| # | CE file, place | Call | Runs in | Does |
|---|---|---|---|---|
| 1 | `fSampleRec.cpp` `cSampleRecorder::start`, first line (step 2; `showDefaultScreen` draws the labels before the old place, L399) | `mods::resampleRecorderStarted()` | main | New visit: latch the switch, gates shut, nothing armed. Step 6 adds its own call before the last `functSwitchModule(options)` block: with Trk Audio on, select `globalLastUsedInstrumentIndex` again if L384–397 swapped it for the MIDI one |
| 1b | `fSampleRec.cpp` `cSampleRecorder::start`, after the entry's `engine.setLimiterSidechain(0)` (step 3) | `mods::resample::recorderEntered()` | main | The entry ran, so the gates may open, and the mix is now disconnected. The gates stay shut in a visit whose entry CE skipped (`handleStartupWithPopupAndMenu`): opening there would restore a mix CE never stored |
| 2 | `fSampleRec.cpp` `cSampleRecorder::stop` (L405), first line | `mods::resampleRecorderStopped()` | main | Clear the visit. CE's own `stop()` then undoes the entry and stops the pattern |
| 3 | `fSampleRec.cpp` `cSampleRecorder::update` (L113), first line, before the count-in early return | `resample::recorderUpdate(view)` | main | Open or close the gates by the rule of 4.3; closing stops the pattern, so a take's end does; disarm when nothing counts in or records (step 4). Step 3: the call reads the screen, the take, the buttons and the file manager's status into a `RecorderView` |
| 4 | `fSampleRec.cpp` `functPlay` (L2907), first line (step 4; step 3 needs none, CE's Play is heard with the gates open) | `if (resample::playPressed(shift, countingIn)) return;` | main | During a count-in with the gates open and nothing playing: arm (Shift: the song) instead of playing. Otherwise CE's Play, which is heard because the gates are already open |
| 5 | `fSampleRec.cpp` `functRec` (L2926), first line | `resample::recPressed(shift, onSetup)` | main | Shift held, setup or count-in, gates open, nothing armed yet: arm the pattern. CE's Rec then runs as always |
| 6 | `fSampleRec.cpp` `functActionRecord`, just before `recorder.setConfig(config)` (step 3: after CE has set the buffer and the channels) | `resample::placeBuffer(&address, &size, channels, recordShown, monitorOn)` (step 5 adds the last two: the source shows Trk Record, Monitor is On) | main | With the gates open: the buffer after the loaded samples (4.4). Too little room: CE's buffer, and the gates close here, before the take writes over the samples. Any take left in CE's buffer marks the samples overwritten for the rest of the visit (4.3). Step 5: a moved take with Trk Record on the stereo Line In records the tap, with Monitor's state, which can't change during a take |
| 7 | `fSampleRec.cpp` `functActionRecord`, after `recorder.record()` (L1589) | `resample::takeStarted()` | main | Armed, moved and the gates open: play the pattern (or song) from its start; one already playing is stopped first, as Stop then Play. Always disarms |
| 8 | `fSampleRec.cpp` `functActionRadioFreq`/`functActionRadioLeft`, first line (step 2: on the setup screen `setConfigScreenFunct` gives keys 1 and 2 to these, not to `functActionButton1`/`2`) | `if (resampleSlotPressed(n)) return;` | main | On the setup screen, outside a count-in, a take and CE's windows, for the sources that show the button: toggle Trk Audio / Trk Record with their coupling, store the record, mark the project changed, redraw the two labels |
| 9 | `fSampleRec.cpp` `functInstrument` (L2940) | press: `if (resample::openPicker()) return;` release: `resample::pickerClosed()` after CE's release code | main | Trk Audio on: `showStepPopup(stepPopupInstr, …)` with no clamp; a pick that isn't MIDI leaves `recorderMidiLastUsedInstrumentIndex` alone |
| 10 | `gSampleRec.cpp` `cSampleRecorder::hideRadio` (L526), after the labels' refresh | `drawResampleLabels()` | main | Draw "Trk Audio" and "Trk Record" with Off/On on `label[RADIO_COL1]`/`label[RADIO_COL2]`, for the sources that show them |
| 11 | `Recorder.cpp` `Recorder::record`, around the queue `begin()` loop (L64–67) | `resample::queuesStarting()` before, `resample::queuesStarted()` after | main | Trk Record, Line In, gates open: mask the audio interrupt (`AudioNoInterrupts`, the NVIC's; step 5), begin the tap queues with the input queues, unmask, so all four start in the same interrupt. The first such take of a power-up builds the tap first |
| 12 | `Recorder.cpp` `Recorder::update`, stereo branch (L102–110) | before the copy loop `if (resample::dropBlock()) { free both input blocks; skip the copy and the head step }`; the `memcpy` for each channel becomes `resample::copyBlock(channel, dest, src)` while the tap runs | **interrupt** | Monitor off: drop the take's first Line In block (decision 10), then add the tap's block; Monitor on: take the tap block alone. Saturating 16-bit add |
| 13 | `Recorder.cpp` `Recorder::finishRecording` (L277), after `isActive = false` | `resample::takeFinished()` | **interrupt** (full memory) or main (stop) | End and clear the tap queues; note "take over" for hook 3 |
| 14 | `mtAudioEngine.cpp` `audioEngine::update`, L277 | `if (!recorder.isRecordingActive() \|\| resample::engineRuns())` | **interrupt** | The players' and synths' per-block update runs during a take while the gates are open |
| 15 | `mtSequencer.cpp` `Sequencer::handleNoteOn`, play-mode branch (L3010–3027) | before the loop `const int pick = resample::liveTrack(sel->firstTrack, instrument);`, and the condition at L3023 becomes `pick != kCeRule ? trackIdx == pick : (CE's condition)` | **interrupt** (MIDI in) and main (pads) | The rs9 track rule, in the recorder with Trk Audio on only (4.6) |
| 16 | `mtSequencer.cpp` `handleNoteOn` (L2837) and `handleNoteOff` (L3126), the `else if (isRec())` of each | becomes `else if (isRec() && !resample::playOnly())` | **interrupt** (MIDI in) and main (pads) | In the recorder with Trk Audio on, notes skip the record branch and take the play-mode path, so they never write into the pattern (decision 11). Record mode itself is left as it is |

Plus the registry entry, the record, the importer and the tap objects in `core/mods/` (4.2, 4.8). Every
call goes into `core/mods/resample.cpp`.

### 4.2 The tap

- **Where:** the output of each limiter (`output.getLimiter(kLeft/kRight)`), the signal the final
  mixers take as their input 0 before they add the metronome (input 5), the SD preview (1), the test
  tone (4) and input 6. So the take holds the dry mix, delay and reverb (Master FX's when chosen),
  synths (Synth FX included) and the monitored Line In, and never a click. The Volume knob and the
  jacks' USB source switch are after it, as on 1.2.0.
- **Objects:** two `AudioRecordQueue`s and two `AudioConnection`s of our own, in static storage in
  `core/mods`, built with placement `new` (no heap) at the first Trk Record take of a power-up, with the
  audio interrupt masked, and never taken down. Until then the graph is exactly CE's. After that each
  queue, while not recording, sleeps: since CE's 7482cc8 a stopped queue whose source updates before
  it marks itself inactive, and `transmit` gives an inactive object no block (`record_queue.h`
  `sleepIfIdle`, `AudioStream.cpp`). `begin()` wakes it. While recording, a silent limiter sends no
  block and the queue stores a zeroed one in its place, so the tap still has one block per interrupt. CE's `AudioConnection::disconnect` isn't used: in its middle-of-list case it
  doesn't unlink the connection, and it drops a pending block without releasing it.
- **Update order** (step 0, 11.1): a queue built at run time joins the end of the update list, after
  the limiters and the final mixers, so it gets each limiter block in the interrupt that made it. CE's
  Line In queues sit ahead of their source in the list and get each input block one interrupt late. So
  in a Monitor-off take the first input block is dropped (decision 10): the tap's first block stays
  queued and is paired with the second input block, and from then on each pair was played at the same
  time. The take's Line In starts 2.9 ms later; the tap queues hold one block more during the take.
- **Start:** hook 11, inside `Recorder::record` with the interrupt masked.
- **Copy:** hook 12. Monitor off, the first input block is freed without a copy (above). For each
  input block after it, `readBuffer()` on the matching tap queue: Monitor off, the sum of both, clipped
  to 16 bits; Monitor on, the tap block alone, with nothing dropped. The tap block is
  freed right after. If one is missing (the update order of 11.1 says it can't be), the input block is copied alone and
  a counter goes up, for the tests.
- **Stop:** hook 13, in `finishRecording`, which both the full-memory end (interrupt) and
  `Recorder::stop` (main loop) reach. It ends and clears the tap queues. CE's own `stop()` clears its
  queues from the main loop the same way.
- **Only Line In** (the stereo destination), decision 9.

### 4.3 The gates

"Gates open" means the recorder's entry is undone, so the tracker sounds as in normal playback.

**The rule (hook 3, every main-loop pass of a visit):** open while Trk Audio is on, the source is Line
In or a mic, and either the setup is on screen (counting in included) or a take into the moved buffer
runs. Closed otherwise: Trk Audio off, Radio or USB selected, a take in CE's buffer (closed by hook 6
before it starts), the crop and save screens after a take, the instrument picker over the setup stays
open. So the crop and save screens are CE's, and coming back to the setup opens them again.

Added in step 3, each also closing the gates:
- **CE's entry ran** this visit (hook 1b). In a visit whose entry CE skipped, the gates stay shut, so
  the recorder is CE's.
- **No take of this visit went into CE's buffer.** Such a take (the fallback, or one with Trk Audio
  off) writes over the loaded samples, which CE reloads only on leaving. Until then the tracker would
  play the take as its samples, so it stays silent for the rest of the visit.
- **The file manager isn't saving a take or reloading the samples** (`fmSavingRecordedSound`,
  `fmSavingImportingRecordedSound`, `fmSaveRecordedSoundEnd`, `fmReloadingSamples`,
  `fmReloadSamplesEnd`), so nothing plays samples while the SD card writes or reads them.

**Open** (main loop only; the inverse of the entry, with CE's own calls):

1. `engine.unblockAllNoteOn()` and `sequencer.setSynthMute(kUnmuted)`;
2. `engine.unblockDelayRefresh()`;
3. `engine.connectMixerImpact()`: the project's mix back, **only if this visit's last mixer call was a
   disconnect**;
4. the limiter sidechain from the project (`setLimiterSidechain(mtProject.values.limiterSidechain)`;
   step 3 also ran the test project with the sidechain on track 1: the setup played bit-identical to
   normal playback for 600 blocks, so `resetLimiter` isn't needed);
5. `engine.refreshAllSends()` and `refreshTrackVolume()`, so the per-block update recomputes every
   send and level;
6. "open" set, so hook 14 lets the per-block update run during a take.

The line-in impact and the Monitor level stay as the recorder set them.

**Close:**

1. `sequencer.stop()` if the pattern plays (main loop; it may load the workspace pattern, which is
   fine there). That is how stopping a take stops the pattern;
2. "open" cleared;
3. the entry's calls in its order: `blockAllNoteOn`, `setSynthMute(kMuted)`, `blockDelayRefresh`,
   `clearDelay`, `clearReverb`, `disconnectMixerImpact` (only if the last call was a connect),
   `setLimiterSidechain(0)`. The delay and reverb stop with the take, as in CE's recorder.

**Leaving** needs nothing more: CE's `stop()` undoes the entry and stops the pattern. If the gates are
open then, `mixerValuesStore` still holds the project's values from the entry and the restore gives the
same values back. The pairing flag lives in the mod's state, so no path calls disconnect twice in a row.

**A pattern started over MIDI** (decision 3) sounds from its first note, since the gates are already
open in the setup.

**Step 0 result** (11.2): with the gates opened by these calls before Play, the recorder's setup
played bit-identical to normal playback for 600 blocks, sample tracks, three synth parts, delay and
reverb included. Opened after Play, 150 blocks differed: notes that started while the gates were
shut stay silent until the track plays its next note. So the gates must already be open when a note
starts, which the rule above does.

### 4.4 Sample memory

- **When:** only for a take started with the gates open (decision 6). Otherwise CE's buffer, CE's length.
- **Free start:** the end of the highest active instrument's RAM data: its `sample.address` plus
  `ramFrames` (streamed) or `sample.length` frames, times 2 for stereo, times 2 bytes. That is the rule
  CE's own `findFreeSampleRamScratchStereo` uses; ours is written separately in `core/mods` as a pure
  function, so a host test covers it. Rounded up to 32 bytes.
- **End:** `externalRamSampleMemory + SAMPLE_MEMORY_SIZE`, as CE's recorder uses. The top 15 MB also
  serve as the Sample Editor's effect buffer (`EDIT_BUFF_OFFSET`, `Tracker.cpp` L31); the recorder
  already records over it in CE, and the editor isn't open during a take.
- **Size:** the rest, rounded down to whole 256-byte blocks per channel. `Recorder::setConfig` divides
  it between the channels.
- **Fallback:** under 10 s at the take's channel count (0.88 MB mono, 1.76 MB stereo), CE's buffer; hook
  6 closes the gates first, so the take is CE's and silent, and the tracker stays silent until the
  recorder is left and CE reloads the samples (4.3).
- **What CE gains:** with streaming, a project's large samples hold only their RAM prefix, so most
  projects leave far more than 1.2.0 did. With nothing loaded a stereo take still gets about 350 s
  (61.8 MB ÷ 176.4 KB/s), as in CE today.
- **Readers of the take** go through the recorder: `getAddress`, `getLength`, `getSize` (spectrum,
  crop, undo, preview pads, `RecordingSave`). Only `functActionRecord` names the buffer. Save & Load's
  memory check (`used_memory + getSize() > max_memory`) stays CE's.
- **Reload on leaving:** CE's, kept (decision 7).
- **Step 0 result** (11.3): the rule's end equalled `externalRamSampleMemory + used_memory` exactly,
  with no sample streamed and with 11 of 16 streamed. The only user of free sample memory is
  `findFreeSampleRamScratchStereo`, reached only from `SampleImport::saveStreamedImportSampleToWorkspace`
  (sample import into the Workspace), which doesn't run during a recorder visit. The recorder's Save &
  Load writes the take straight from the recorder's buffer and then reloads all samples, as CE does.

### 4.5 The metronome

No hook. The tap is ahead of the metronome's input, so the count-in and the recording metronome are
heard as CE plays them and are never in the take (decision 2). The test checks a count-in take with the
metronome loud.

### 4.6 Any instrument (rs9)

- **Picker (hook 9):** with Trk Audio on, the press opens `stepPopupInstr` (synths, samples, MIDI)
  instead of `stepPopupOnlyMidiInstr`, with no clamp. In that mode the popup's pads select an
  instrument and preview a note through `handleNoteOn` (`interfacePopups.cpp` L1188–1201), as 1.2.0's
  mode 2 did; it has no load or edit action. The release runs CE's code, except that a non-MIDI pick
  doesn't become `recorderMidiLastUsedInstrumentIndex`.
- **Kept on entry (hook 1), given back on leaving:** CE's `stop()` already restores
  `globalLastUsedInstrumentIndex` (PR 97's fix), so only the entry needs a hook.
- **Heard:** the gates are open in the setup with Trk Audio on (4.3), so no rule of its own.
- **Track rule (hook 15).** CE's play-mode branch takes the first track from the cursor's that isn't
  muted and either has no note open or, in edit mode, has no live note open (the takeover). In the
  recorder with Trk Audio on, `liveTrack` returns the track to use:
  1. the first track from the cursor's that passes CE's edit-mode-off test (not muted, `noteOpen`
     clear, tracks 1–8 for a sample instrument);
  2. if none, the first sample track from the cursor's whose sound has ended: the last note's
     instrument a sample, no Roll, Chord or Arp in its sent step's FX, no delayed note pending, no live
     note (`recOpen`), and the track's voices not playing;
  3. if none, "drop" (no track matches, so CE's loop takes none, as it does today).
  Elsewhere it returns `kCeRule` and CE's condition runs unchanged.
- **The CE fields** (step 0, 11.4): `noteOpen` stays set from a note until a note-off step, the end of
  a set note length, the next note or Stop, as on 1.2.0; the FX types are `stepSent.fx[0..1].type`
  (`FX_TYPE_MIDI_CHORD` among them), with Arp and Roll running as `arp.isOn` and `roll.isOn`; a delayed
  note is `micromove.isActive`; a voice has ended when `AudioPlayMemory::isPlaying()` is false, which
  1-Shot, slice and beat slice set at the sample's end and the loop modes only after a release.
- **Nothing written to the pattern:** in play mode CE writes only with the Pattern screen in front
  (`isPatternScreenActive()`, L3058), which it isn't. **But `isRec()` can be true in the recorder**
  (11.5): record mode armed on an external clock stays on when you open the recorder, and then a pad
  writes into the pattern through CE's record branch (L2837), in stock CE too. With Trk Audio on, hook
  16 sends note-on and note-off past that branch to the play-mode path, so hook 15's rule applies and
  nothing is written (decision 11). With Trk Audio off it stays CE's. Pressing Trk Audio while a pad is
  held could send that note's off down the other path; step 6 tests that no note is left hanging.
- **Trk Audio off with a sample or synth selected (hook 8):** select
  `recorderMidiLastUsedInstrumentIndex`, as CE's entry does.

### 4.7 Buttons and labels

- Tracker+ layout: Source is column 0, `RADIO_COL1`/`RADIO_COL2`/`RADIO_COL3` are 1, 2, 3, Metronome
  Config is column 3 for every source but Radio, then Level, Gain, Monitor, Record. Columns 1 and 2 are
  empty for every source but Radio (`hideRadio`). Trk Audio goes in 1, Trk Record in 2, as on 1.2.0.
- In the config screen, `functActionButton1` does nothing and `functActionButton2` calls
  `functActionRadioLeft`, both only for Radio; the select functions return early for other sources.
  Hook 8 acts before them and leaves Radio alone.
- **Shown for:** Trk Audio for the four Line Ins and the mics, not for USB or USB (M) (decision 1);
  Trk Record for the stereo "Line In" only (decision 9).
  Radio keeps its controls. Switching the source to USB or Radio with Trk Audio on closes the gates and
  stops the pattern; the setting itself stays, and comes back with Line In or a mic.
- **Display bytes:** the two labels exist already with empty text, so the cost is their text.
  Measured in step 2 as the rigs count `API_*` calls, each text at its upper bound (24 + 12 a
  character): the setup screen is 3,932 bytes without the buttons and 4,264 with both (label[1]
  196, label[2] 208), 3,908 under `kFrameControlBytes`. The 40 bytes estimated here before forgot
  that each text is a coprocessor command of its own. `radioSpaceCover`, which `hideRadio` shows over columns 1–3, is
  at y 29–423 and the labels at y 424–479, so it doesn't hide them (11.6).

### 4.8 Settings and data

- **Record:** `mods.bin` record 12 (`Mod::resample` is 11, record = id + 1), version 1, one byte: bit 0
  Trk Audio, bit 1 Trk Record. Absent: both Off. A value with bit 1 and not bit 0 reads as both On.
- **Where it is set:** only the recorder's buttons (decision 8). A press stores the record and marks the
  project changed through `project::markChanged`, like any other project setting (decision 5).
- **New Project, Save As, Open:** by design through `mods.bin` in the Workspace. The settings are read
  into RAM at `load` and latched at each recorder visit; nothing reads a file during a visit.
- **Import of `resample.bin`** (in `modsProject.cpp`'s importer table, like `masterfx`): 12 bytes,
  `RSM1`, then the settings byte (bit 0 audio, bit 1 record), three zero bytes, then a little-endian
  32-bit FNV-1a over bytes 4–7. Valid only with that magic, that checksum and a settings byte of 3 or
  less. A valid file is stored as the record; the importer reports the mod used, so it switches On,
  when the byte isn't 0.
- **Plain CE** opening the project ignores `mods.bin`.

### 4.9 The mod switch

- Registry entry: id `Mod::resample`, name "Resample", `load` only. No `storeIfEdited`, since
  Config doesn't edit the settings and the recorder's buttons store the record themselves; no
  `apply` or `reset` (nothing outside a recorder visit holds state). Placed last in the table.
- **Idle-only in effect, without a new registry flag:** the switch lives in Config → Project Settings →
  Mods, so it can't change while the recorder is on screen. Hook 1 latches it per visit, and every
  hook checks the latched value, so a change takes effect at the next visit. Playback elsewhere is
  never touched: outside a visit, hook 15 returns `kCeRule` and the rest don't run.
- **Off is CE**, checked by the golden test and by `test_resample.py`'s Off case.

### 4.10 USB

- **USB In+Out outside the recorder** (`refreshOutputSourceOutsideTheRecorder`): CE's, unchanged. The
  recorder's `stop()` reapplies it on leaving.
- **USB source in the recorder** (`refreshOutputSource`): the jacks play the USB input in the setup and
  during the take; CE's rule stays, and neither button shows (decision 1).
- **Line In with USB In+Out on:** `refreshOutputSource` picks the regular mix for every source but USB,
  so Trk Audio is heard.
- **USB audio out** gets the final mixers, so a computer hears what the jacks would.

### 4.11 Interrupt safety

| Path | Context | Rule kept |
|---|---|---|
| Hook 14 `engineRuns()` | interrupt, `AUDIO_CORE_RAM` caller | reads one DTCM byte |
| Hook 12 `copyBlock()` | interrupt | no allocation; frees the tap block it read; saturating add on `int32_t`, no float |
| Hook 13 `takeFinished()` | interrupt (full memory) or main (stop) | queue `end()` and `clear()` (CE's `stop()` calls `clear()` from the main loop too), flags |
| Hook 15 `liveTrack()` | interrupt for MIDI notes (`midiUpdate` → `SR->noteOnHandle` → `handleNoteOn`), main loop for pads | reads sequencer and player state already in RAM; no display, no SD, no heap |
| Hook 16 `playOnly()` | as hook 15 | reads the latched visit flags |
| Hook 12 `dropBlock()` | interrupt | frees CE's two input blocks with `freeBuffer()`, sets a flag |
| Building the tap | main loop, first Trk Record take | placement `new` into static storage with the audio interrupt masked (`AudioStream`'s constructor walks the update list unmasked) |
| Hook 11's mask | main loop | `AudioNoInterrupts()`, not `CPSID i`: `begin()` → `clear()` → `AudioStream::release` ends with `CPSIE i` and would unmask a global mask early (step 5) |
| Everything else (gates, Play, `sequencer.stop`, the record store, labels) | main loop | |

CE's own interrupt paths, from `CLAUDE.md`, and how they meet the mod:

- **MIDI Start or Continue** during a visit starts the pattern (or, in Song mode, the song through
  `setSongPos` and `play()`) in the interrupt. The mod hooks neither. With Trk Audio on the gates are
  already open, so it is heard (decision 3); hook 3 sees it like any playing pattern.
- **MIDI Stop** uses `stop(StopContext::audioCallback)`. Hook 3 sees the pattern stopped next pass.
- **Pattern loads:** `loadNextPattern` only records the request; `cFileManager::update` starts it in the
  main loop. The mod doesn't change which pattern comes next, so `isPatternPreloaded` needs nothing.
  Song playback in the recorder (Shift+Play, decision 4) uses this path as anywhere else.
- **MIDI note-off on the crop screen** reaches `display.setControlText` through `hidePreviewValue`: CE's,
  untouched; the mod's hooks aren't on that path.
- **Hook 7** starts the pattern from the main loop, right after the queues start (the count-in case
  through `cSampleRecorder::update`, the immediate case inside the Rec press), as 1.2.0 did.

## 5. Costs

Estimates; step 0 and each step measure.

| | Estimate | Note |
|---|---|---|
| DTCM | about 250 B | the flags and settings byte (about 24 B), two `AudioRecordQueue`s (each a 10-block queue) and two `AudioConnection`s |
| ITCM | 0 to start | the copy loop runs in `Recorder::update`, which is in flash; 1.2.0 measured about 1,000 instructions per block for the mix. Moved to ITCM only if the Profiler asks |
| Flash | 3–5 KB | |
| SDRAM, heap | none | |
| Block pool | +4 at most | each tap queue holds one block between interrupts during a take, and one more in a Monitor-off take (decision 10); after the first Trk Record take each idle queue holds a block only within its own update. On the rig (which reads high): normal playback and gates-open playback in the setup 42, a stock take 39 (11.7) |
| CPU | ≈ normal playback, +≤ 1 % | 1.2.0: +0.7 % over normal playback; on the unit, the Profiler with all 8 synth voices sounding during a Trk Record take. Gates open in the setup cost what normal playback costs |
| Display | under 40 B | 4.7 |

`tools/mem_budget.py` runs on each step's `.axf`.

## 6. Build steps

One patch at a time, each its own commit on your word, on `mod/resample` (this worktree's branch is
`claude/resample-f42a4e`). Each step's emulator test also runs the earlier steps' tests.

0. **Facts on stock CE, no firmware change.** A scratch rig on `boot_rig.py`/`PlayRig`, full build:
   the gate calls of 4.3 against normal playback, sample and synth tracks; that the limiters' output
   holds the monitored Line In and no click; the update order of a queue added at run time, and its
   offset to the Line In queues; the free start of 4.4 against `used_memory` on projects with streamed
   samples; every caller of `findFreeSampleRamScratchStereo`; whether Shift+Rec reaches `functRec`;
   `isRec()` in the recorder; the fields of 4.6; `radioSpaceCover` over columns 1–2; the pool and CPU
   of a take with all 8 voices. **Done when** each fact is written into this file and any design change
   it forces is put to you. **Done 2026-11-12:** section 11, questions 10 and 11.
1. **Skeleton.** `Mod::resample` in the registry and `modsBuild.h` (`MT_HAS_RESAMPLE`), the record,
   the importer, host tests. No recorder hook. **Done when** host tests pass (importer cases below), the
   golden test passes, Config shows Resample, and the `mods.bin` round trip keeps record 12.
2. **Buttons and settings** (hooks 1, 2, 8, 10). First the user page `docs/mods/resample.md` with
   mockups of the recorder's setup (stereo Line In, a mono Line In, a mic, USB; each button Off and On), shown to you before
   any screen code. **Done when** the display calls, caught at CE's
   `API_*` functions as `test_colorsong.py` does (`eve.py` isn't in this repo), show both labels for "Line In",
   Trk Audio only for "Line In (L)", "(R)", "(M)" and the mics, neither for Radio, "USB (M)" or "USB"; presses toggle with the coupling of section
   2; a press marks the project changed; the record round-trips through Save, Save As, Open and New
   Project; the two labels' bytes, counted per control, keep the frame under `kFrameControlBytes`
   (CE's 9d6d10a builds the lists in the MCU; `lastMemoryUsed` is no longer set).
   **Mockups approved 2026-11-19, redrawn with CE's nine sources and approved 2026-11-20:**
   `docs/mods/resample.md` and six `docs/mods/img/resample-*.svg` (`tools/docs/mockups_resample.py`).
   **Done 2026-11-20:** host tests (`test_resample` 96 checks) and `test_resample.py` on a full
   build with plain CE beside it: Off is CE's display calls for all nine sources; On shows the
   buttons per source, presses toggle with the coupling, store record 12 and mark the project
   changed, the switch takes effect at the next visit, and the screen stays under the frame limit
   (4.7). The round trip of the record through Save, Save As, Open and New Project is step 1's.
   `check_mockups.py` passes. Hooks 1 and 8 moved (4.1). Not covered by the emulator: the screen
   itself, and a key pressed during a count-in or a take (refused by the guard, not driven).
3. **Trk Audio** (hooks 3, 4 without the arm, 6, 14). **Done when** in the setup Play, Shift+Play and
   a MIDI Start sound as normal playback (samples, synths, MIDI out, delay and reverb); Rec then Play
   records the input only into the moved buffer, with the loaded samples left byte-identical; stopping
   the take stops the pattern and leaves every value the entry sets as CE's entry leaves it; the user's
   mix values survive any order of open, close, source change and leave; the fallback take is CE's;
   with Trk Audio off the recorder is CE's, buffer included.
   **Done 2026-11-20 in the emulator, on `CE_20261120`:** host tests (`test_resample` 132 checks:
   the gate rule over all 256 combinations of its inputs, and placement as section 7 lists it),
   `test_resample.py` again, and the new `tools/emu/test_resample_sound.py` (in `run_tests.py`'s
   project jobs) on a full build of the hellish downtown project with its synth patches, against
   the reference build. It checks:
   - Play in the setup gives normal playback's 600 blocks bit for bit, with the project's mix set
     away from CE's defaults by CC first, and again with the limiter sidechain on track 1;
   - Shift+Play and a MIDI Start in the setup are heard;
   - Rec then Play: the take goes to the first 32-byte boundary after the loaded samples, holds the
     input only, and the loaded samples are byte-identical after it;
   - stopping the take stops the pattern and leaves everything as CE's entry left it;
   - back on the setup the gates open again;
   - all nine sources open or shut as 4.3 says;
   - after four presses and on leaving, the project's mix is unchanged;
   - the fallback take is CE's, the rest of the visit is silent, and leaving reloads the samples as
     they were;
   - with Trk Audio off, with the mod Off and on plain CE, Play is silent and the take is CE's.

   Costs: DTCM 8 B (8,808 free), ITCM none, flash about 800 B.
   Not run yet: `--preempt` across the gate calls, the pool with all 8 synth voices, the release
   tier, and the unit. The emulator also doesn't show MIDI out on the jacks, or real SD timing for
   the reload after leaving.
4. **Shift+Rec** (hooks 5, 7, 4's count-in arm). **Done when** Shift+Rec with Pre Roll off and on
   behaves as section 2 says; Play and Shift+Rec arm during a count-in (Shift+Play arms the song), a
   plain Rec never disarms; an external-clock count-in given up by Rec (PR 97) leaves nothing armed.
   **Done 2026-11-20 in the emulator, on `CE_20261120`:** host tests (`test_resample` 142 checks:
   what Shift+Rec and a count-in's Play arm), step 2 and 3's emulator tests again, and the new
   `tools/emu/test_resample_shiftrec.py` (in `run_tests.py`'s project jobs) on the hellish
   downtown project. "Starts with the take" is checked as `Sequencer::play` (or the song's start)
   right after `Recorder::record` in the same main-loop pass, with no audio interrupt between. It
   checks:
   - Pre Roll off: Shift+Rec starts the pattern with the take from its first step, and it's heard;
     over a playing pattern it stops it and starts it again with the take; a plain Rec starts
     nothing;
   - Pre Roll on (the count-in took 674 blocks): Shift+Rec, or Play in the count-in, starts the
     pattern with the take, and nothing plays in the count-in; Shift+Play there starts the song; a
     plain Rec in an armed count-in keeps the arm; a count-in with nothing pressed starts nothing;
   - an external clock: the count-in waits twice the internal one's length with no take, Rec gives
     it up, and a later plain Rec starts no pattern;
   - with Trk Audio off, the mod Off and on plain CE: Shift+Rec starts no pattern and the take is
     CE's, and Play in a count-in plays at once, as CE.

   The same test on step 3's builds fails its 8 arm checks and passes CE's.
   Costs: flash 360 B, no DTCM (the arm fits in the visit's padding), no ITCM.
   Not run yet: `--preempt`, the release tier, and the unit. The emulator doesn't show the
   count-in's clicks against the pattern's first step in real time.
5. **Trk Record** (hooks 11, 12, 13 and the tap objects). **Done when** the take equals Line In plus
   the tap (Monitor off, the Line In from its second block, lined up with the tap as played) and the
   tap alone (Monitor on) frame for frame from its first frame, with
   sample and synth tracks sounding (sound test stage 4); a loud sum clips; the jacks are unchanged by
   Trk Record; a count-in take with the metronome loud has no click in it; a take that fills its space
   stops at its end with nothing written after it; before the first Trk Record take the graph is CE's.
   **Done 2026-11-20 in the emulator, on `CE_20261120`:** host tests (`test_resample` 151 checks:
   when a take records the tracker, and the clipped sum), the earlier steps' emulator tests again,
   and the new `tools/emu/test_resample_record.py` (in `run_tests.py`'s project jobs) on the hellish
   downtown project, with a tone written into the I2S input buffer before each audio interrupt. A
   take is checked against the tone fed in each interrupt and the jacks' blocks, which play each
   limiter block one interrupt later and pass the limiters at unity while the metronome is silent.
   It checks:
   - Trk Audio only: the take is CE's, the tone one block late; at boot and through that take the
     update list is the plain CE build's (189 objects);
   - Trk Record, Monitor off: each block is the tone of its own interrupt plus the tap, clipped,
     frame for frame from the first frame (301 blocks); the jacks play what they play with Trk Audio
     only, bit for bit (a fresh run each); no tap block missing; the pool's peak the same (15 blocks
     from the take's start, each);
   - Monitor on: the take is the jacks' sound, the monitored tone in it once, frame for frame;
   - the tap's two queues join the end of the update list at the first Trk Record take, end empty
     and sleep (no block given to them) after it;
   - a loud tone over the mix: 371 samples clipped, every one as the clipped sum;
   - a take with just over 10 s free: it fills its space (3,484 blocks) and stops by itself, the
     sum holds to its last block, nothing is written after its end in either channel, the tap ends
     empty;
   - a count-in take (637 blocks) with the metronome at 100 and at 0, Monitor on: the same take bit
     for bit, while the jacks differ in 71 blocks of the count-in and 35 of the take;
   - Trk Record stored On with Line In (L), Mic LG, and under 10 s free: the take is CE's, no tap.

   The same test on step 4's builds fails its 11 Trk Record checks and passes CE's. The quick tier
   passes, 24 of 24.
   Costs: flash 912 B, DTCM 200 B (8,608 free; the tap's queues 144 B and connections 40 B), no
   ITCM, no SDRAM.
   Not run yet: `--preempt` (hook 11's mask and the stop race of section 10 included), the pool with
   all 8 synth voices, the release tier, the unit, and the CPU of the copy loop (the Profiler).
6. **Any instrument** (hooks 9, 15, 16). **Done when** the picker has the full list only with Trk
   Audio; the instrument is kept and given back; live sample and synth notes sound in the setup and in
   a Trk Record take; the track rule picks as 4.6 says in every case below; nothing is written to the
   pattern with edit mode on or off, nor with record mode armed on an external clock before the visit
   (11.5), while with Trk Audio off that case stays CE's; Trk Audio pressed while a pad is held leaves
   no note hanging; MIDI notes take the same tracks through the interrupt path.
7. **Release tier, ladder, hardware checklist** (sections 7 and 8). Merge into `modular` on your word.

## 7. Tests

Read with [PAST_FAILURES.md](PAST_FAILURES.md): the boot rig (real Open, Play button, main loop,
`--preempt`), samples that really sound (`card_from(workspace=True)`), and a rig that prints what it
ran. Every run under `tools/emu/cpu_limit.py`'s 75 %.

**Host** (`core/mods/tests`):
- importer: valid file with each settings value, record-only normalised, bad magic, bad checksum,
  settings byte over 3, short file, missing file; files made by MT's own `test_resample.py` rig where
  available;
- placement: empty bank (CE's buffer exactly), mono and stereo, unaligned end, a streamed instrument's
  `ramFrames`, gaps between active slots, both sides of the 10 s limit and of the 256-byte rounding,
  a full bank;
- track rule over a small track-state table: free track from the cursor, wrap, sample instrument on
  tracks 1–8 only, muted tracks, no free track with one ended 1-Shot, a loop that never ends, Chord,
  Arp and Roll steps, a delayed note, a live note, all busy (drop), synth and MIDI instruments.

**Emulator** (`tools/emu/test_resample.py`, full build with CE's factory `.psyn` engines on the fake
card as a black box; sound
test stage 4, "recorder tap at level C"):
- **Off = CE:** with the mod Off, and with it On and Trk Audio off, the recorder's blocks, the take,
  its buffer and the loaded samples equal the reference build's;
- the step checks of section 6;
- sound with two sample tracks, a closed synth engine, CHIP and WAVE or PHASE, and a MIDI track;
- pool under 80 blocks with all 8 synth voices, interrupt enable and disable balanced, heap unchanged
  after 20 takes;
- `--preempt` across the gate calls, the tap's construction and the record store;
- our own instructions per block with `test_bundle_cost.py`.

`tools/run_tests.py --tier release` passes before any device test.

## 8. Hardware

After the release tier, first the steps of [DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md) with this
build (L1, L5 and L7 at least). Then one sitting, about 20 minutes. The test project is generated with
the firmware and named then (a few sample instruments, a synth track, a MIDI track, delay and reverb
sends). No Break FX, Performance mode, Export or per-mod Saving checks.

1. Open the test project, Config → Project Settings → Mods: Resample On.
2. Sample Recorder, Line In, both buttons Off: Rec records as in CE, Play is silent, the take plays back.
3. Trk Audio On: Play in the setup is heard at the project's levels with delay and reverb; Rec then
   Play plays the pattern during the take; stopping the take stops the pattern.
4. Shift+Rec with Pre Roll on: the pattern starts with the take.
5. Trk Record On, Monitor off, nothing plugged in: the take holds the pattern. With a source on Line
   In: both. Monitor on: Line In isn't doubled.
6. A count-in take with Trk Record: the clicks are heard and the take has none.
7. Pick a sample instrument in the recorder and play pads with the pattern running, then a synth
   instrument; leave the recorder: the Pattern screen has its instrument back.
8. Save & Load a take, then play the project's other samples: they sound right.
9. Save the project, power off and on, open it: both settings kept. New Project: both Off.
10. During a Trk Record take with all 8 synth voices sounding, read the Profiler; listen for clicks
    or dropouts in steps 3 to 7.

## 9. Decisions

Your answers, 2026-11-12 (1 directly, 2–9 "the recommendations"):

1. **USB source:** Trk Audio, and so Trk Record, are hidden for it. CE's rule that the jacks play the
   USB input there stays.
2. **Metronome:** the tracker is recorded just before the metronome joins the mix, so every click is
   heard and none is recorded, count-in or not. MT muted it only because a binary patch couldn't add a
   tap point there.
3. **A pattern started over MIDI** (MIDI Start from another device): heard, and recorded under Trk
   Record, because with Trk Audio on the tracker is audible for the whole visit. MIDI notes in were
   never affected.
4. **Shift+Play:** CE's meaning (play the song), audible with Trk Audio on.
5. **Saving the buttons:** a press is an unsaved project change, kept by the project's Save.
6. **Where a recording goes:** after the loaded samples only when Trk Audio is on; otherwise CE's
   place and CE's full length.
7. **The sample reload on leaving the recorder:** CE's, kept.
8. **Where the buttons' settings change:** only on the recorder screen.
9. **Trk Record:** Line In only. Since CE's nine sources (section 13), your answer 2026-11-19: the
   stereo "Line In" only. "Line In (L)", "(R)" and "(M)" get Trk Audio only, as the mics do, so the
   tap stays in `Recorder::update`'s stereo branch (hook 12).

From step 0, your answer 2026-11-13 ("Yes, both"):

10. **Line In timing in a Trk Record take with Monitor off:** the take's first Line In block is
    dropped, so the Line In and the tracker line up as they were played, the same as with Monitor on.
    CE's Line In queue gets each input block one interrupt late, while the tap gets the tracker's sound
    in the same interrupt (11.1). The take's Line In starts 2.9 ms later; its length stays the same.
    Hook 12, section 4.2.
11. **Notes in the recorder never write into the pattern with Trk Audio on.** Stock CE writes a pad
    or MIDI note into the pattern from the recorder when record mode was armed on an external clock
    before the visit (11.5). With Trk Audio on, notes take the play-mode path instead (hook 16), and
    record mode itself is left as it is. With Trk Audio off the recorder stays CE's, bug included. A
    write-up for upstream is only on your word; nothing is sent.

## 10. Small choices made, and extras

- **Names:** mod "Resample" in Config; button texts "Trk Audio" and "Trk Record" as MT; source file
  `core/mods/resample.cpp`/`.h`; flag `MT_HAS_RESAMPLE`; test `tools/emu/test_resample.py`.
- **No `idleOnly` flag in the registry:** latched per recorder visit instead (4.9). Tell me if you want
  the registry flag anyway.
- **The tap is built at the first Trk Record take and never taken down,** so the graph is CE's until
  then and CE's faulty `disconnect` is never called (4.2).
- **Hook 11's interrupt mask** around CE's queue starts: MT did the same at its own start.
- **Pure functions for placement, the track rule and the importer**, so host tests cover them.
- **With decision 1:** switching the source to USB or Radio while Trk Audio is on closes the gates and
  stops the pattern; the setting stays for Line In and the mics.
- **With decision 3:** the gates close on the crop and save screens after a take and open again back
  in the setup, so those screens stay CE's (their previews play at CE's flat levels).
- **With decision 4:** Shift+Play during a count-in arms the song, as Play arms the pattern.
- **From step 0 (11.2):** Trk Audio switched On while a pattern already plays (started silent, with
  Trk Audio off) leaves it playing; each track sounds from its next note, since notes that started
  while the gates were shut stay silent. Restarting the pattern would be the other way.
- **One line added to CE_PORT_PLAN section 3.14** pointing here.
- **From step 3:**
  - Hook 1b, so the gates open only after CE's own entry (4.1).
  - Hook 4 moved whole to step 4: step 3's Play needs no hook.
  - Hook 6 placed just before `setConfig`, where CE's buffer and channel count are final.
  - The samples-overwritten and file-manager rules of 4.3, so no garbage is heard after a take in
    CE's buffer and nothing plays while a take is saved or the samples reload.
  - A CE quirk, left as it is: when CE skips the entry (`handleStartupWithPopupAndMenu`), its
    `stop()` still calls `connectMixerImpact`, which can restore the mix stored by an earlier visit.
    Resample doesn't add to it, since hook 1b keeps the gates shut there. A write-up for upstream
    only on your word.
- **From step 4:**
  - A second Play or Shift+Play in a count-in changes what is armed (pattern or song) and never
    disarms, as a plain Rec never does.
  - Shift+Rec keeps a song that Shift+Play armed.
  - Play in a count-in over a pattern that already plays is CE's (it stops it), and an arm stays.
  - A pattern already playing when an armed take starts is stopped and started again with CE's
    own `stop()` and `playPattern()`/`playSong()`, as pressing Stop then Play would.
  - Arming needs the gates open, so with too little room after the samples nothing is armed and
    the take is CE's.

- **From step 5:**
  - Hook 6 gets two more arguments, whether the source shows Trk Record and Monitor's state, so
    hook 11 needs no view of the recorder. Monitor can't change during a take (its keys act on the
    setup screen only), so it is latched at the take's start.
  - Hook 11 masks the audio interrupt in the NVIC (`AudioNoInterrupts`), as CE does elsewhere,
    since CE's `release()` turns a global `CPSID` mask off again.
  - In a Trk Record take, a Line In block the queue doesn't have (CE's own race below) counts as
    silence, where CE's `memcpy` would read address 0.
  - A CE race, left as it is: `Recorder::stop` drains its queues by calling `update()` from the
    main loop while the audio interrupt can run `recorder.update()` too. If the interrupt comes
    between a `readBuffer()` and its `freeBuffer()`, the interrupt's `readBuffer()` returns null
    (CE then copies from address 0 into the take) and its `freeBuffer()` frees the main loop's block.
    It can only touch the take's last block, in RAM, never the card. Resample adds nothing to it. A
    write-up for upstream only on your word.

## 11. Step 0 results

Run on 2026-11-12, with no firmware change for the mod.
- **Build:** a fresh full reference build of `c32fee7` with `CUSTOM_DEFINES=-DMT_NO_MODS`, with
  `libPolySynth.a` and `libPolyDSP.a` in, at `build/emu/TrackerPlus_ref_resample_step0.axf`.
- **Rig:** a scratch rig on `boot_rig.py`, with the real boot, the Workspace and the main loop, under
  `cpu_limit.py`. It isn't in the repo.
- **Project:** a real one with 16 sample instruments (5.5 MB) and three synth parts (CHIP, ACD, VAP).
- **Struct offsets:** read from a small probe compiled with the build's own flags (offsets of CE's
  open structs only).
- **The closed libraries** ran as a black box: nothing hooked or read inside them.

What it couldn't show: CPU (the rig has no clock; the Profiler on the unit does that), a project with
the limiter sidechain on, and anything about the real codec or Line In hardware.

**11.1 The graph.**
- **Final mixer inputs:** 0 limiter, 1 SD preview, 4 test tone, 5 metronome. Input 6 isn't connected
  in this build.
- **Limiter inputs:** each limiter takes its tracks-and-sends mixer on input 0 and the sidechain mux
  on input 1.
- **The Line In** enters that mixer on input 3, straight from the stereo-to-mono object.
- **Update order:** 181 objects, and the final mixers are the last two, so an object built at run
  time updates after them and gets each limiter block in the interrupt that made it.
- **Line In timing:** CE's two Line In record queues (#100, #101) update before their source (#108),
  so they get each input block one interrupt late.
- **`Recorder::update`** runs at the start of `audioEngine::update`, before the graph updates, so it
  copies what the queues got in the interrupt before.

**11.2 The gates.** For 600 blocks from Play, each run against normal playback on the Pattern screen
(peak 32,768, pool 42):

| Run | Peak | Pool | Blocks that differ from normal playback |
|---|---|---|---|
| Recorder as CE has it | 0 | 37 | 465 |
| Gates opened before Play | 32,768 | 42 | none: bit-identical |
| Gates opened after Play | 32,767 | 42 | 150, the last at block 472 |

Opening the gates is the list of 4.3:
1. `unblockAllNoteOn`;
2. `setSynthMute(kUnmuted)`;
3. `unblockDelayRefresh`;
4. `connectMixerImpact`;
5. `setLimiterSidechain` with the project's value, which is 0 here;
6. `refreshAllSends`;
7. `refreshTrackVolume`.

**11.3 Sample memory.**

| Project | Instruments' RAM | Free stereo, reference build | Free stereo, mods build (1 MiB less) |
|---|---|---|---|
| As it is, none streamed | 5,490,478 B | 325.2 s | about 319 s |
| 11 of 16 streamed (only their first 32,768 frames in RAM) | 1,323,470 B | 348.8 s | about 343 s |

- **The rule matches CE:** in both cases the rule of 4.4, the same as CE's
  `findFreeSampleRamScratchStereo`, gave the same end as `used_memory`.
- **Packing:** instruments are packed back to back by slot.
- **Nothing else writes there during a visit:** the scratch-space finder's one caller is sample
  import into the Workspace.

**11.4 Track rule fields.** Read from source: see 4.6.

**11.5 Record mode in the recorder.**
- **The finding:** after `recStart` with the clock from USB (record mode armed, nothing playing),
  then the Sample Recorder, one pad marked the pattern changed twice. The same pad without record mode
  armed changed nothing.
- **Why:** CE's recorder only stops a pattern that is playing when you open it, and `stop()` is the
  only thing that clears record mode outside the Pattern screen.
- **Shift+Rec:** reaches the recorder's `functRec`. With Pre Roll off it starts a take at once, as Rec
  does, so hook 5 has to act before CE's code, as planned.

**11.6 The display.**
- **The cover:** `radioSpaceCover` is shown over columns 1–3 for every source but Radio, at y
  29–423. The labels are at y 424–479, so it doesn't hide them.
- **`eve.py`:** not in this repo. Step 2 catches CE's display calls as `test_colorsong.py` does.

**11.7 Line In, the metronome, the pool.**
- **Line In:** Monitor on (index 0 is "On"; CE's own default is 1, "Off") sets the Line In mix gain
  to 86. A tone fed into the Line In then showed in the limiters' output in the interrupt it arrived,
  in CE's Line In queue one interrupt later, and at the jacks one interrupt after the limiter.
- **The metronome:** one click put signal on the final mixer's metronome input for 17 blocks, peak
  15,901, while the limiters' output stayed at 0 in those blocks. So a tap on the limiters never
  holds a click.
- **The pool:** a stock take peaked at 39 blocks, normal playback and gates-open playback at 42. The
  rig reads high (PAST_FAILURES calibration).

## 12. Rebase onto 5b40785

On 2026-11-17 this worktree moved from `c32fee7` to `modular` `5b40785`, 75 commits later, with CE's
`main` up to `f25fb89`, Master FX 2.1, Synth FX 1.1 and Synths 1.1 (DCO6). Step 1's files went on top
unchanged: one conflict, the host-test `Makefile`'s file and test lists, was resolved by keeping both.

**What changed that the design touches:**
- **Silent blocks are null (CE's 7482cc8, 0238786, `AUDIO_NULL_IS_SILENCE`).** A silent limiter sends
  no block. CE's `AudioRecordQueue` stores a zeroed block in its place while recording, and while
  stopped marks itself inactive, so `transmit` gives it nothing (4.2). The tap needs no change for
  this; the idle tap now costs less. The reverb, the delay and the synth parts skip work on CE's own
  state; the gates only call CE's functions, so the mod keeps no state on those paths.
- **The Line In** enters through an input gate that watches for silence on the delay and reverb sends
  (`connectInputToMixer`). The Line In record queues still update before their source, so decision
  10 still applies; step 0's 11.1 is to be measured again.
- **The limiter** holds Master FX's dynamics slot (Glue, Compressor). The tap is its output, so a
  take holds them, as heard. `setLimiterSidechain` now also switches the sidechain mux on or off;
  the gates still call it, unchanged.
- **The display (CE's 9d6d10a, ed51301):** lists are built in the MCU, three byte limits apply and
  `lastMemoryUsed` is gone (4.7, step 2).
- **Synth engines** are CE's factory `.psyn` files on the card, not linked libraries. Tests put them
  on the fake card (`build/Synths`), black box as before.
- **ITCM:** about 40 KB free since CE's cc53303, so the copy loop could go there if the Profiler asks.
- **Rules added to `CLAUDE.md`:** a user page with mockups for a mod that changes the screen (step
  2), the merge checklist, and a `resample-1.0` tag on the merge (step 7).
- **Unchanged:** `fSampleRec.cpp`, `gSampleRec.cpp`, `Recorder.cpp`, `audioEngine::update` (hook 14
  still at L277) and the record and play-mode branches of `handleNoteOn`/`handleNoteOff` (lines moved,
  4.1). `AudioConnection::disconnect` still has the middle-of-list fault.

**DTCM blocks the port (2026-11-17).** `tools/mem_budget.py` on fresh builds of this tree: modular's
own mods image (`5b40785`) has 4,128 bytes free between heap and stack, 32 over the 4,096 floor. Step
1 takes 56 (its two Config items, 28 each, and the 2-byte settings), leaving 4,072: under the floor.
The later steps need about 250 more (the tap queues and connections, section 5). The mods' own DTCM
is about 6.5 KB, nearly all Master FX audio state that the interrupt uses; the only main-loop-only
part is the Config menu objects, about 25 of 28 bytes each. A way to make room is for you to decide.

Where room could come from (looked at 2026-11-17):
- **SDRAM for the menu objects: no.** `cMenuItem`s are built by C++ constructors in
  `__libc_init_array`, and the SDRAM controller is set up later, in `BOARD_InitBootPeripherals`
  from `main()` (`ExternalSdram.cpp`).
- **The padding before `.bss`: gone.** The map's 1.8 KB is 32 bytes in this build (`.dmabuffers`
  ends at 0x200067E0, `.bss` starts at 0x20006800).
- **CE's `triangleTab`, `gaussTab`, `squareTab` (2,000 each) to flash: no.** The granular player
  reads them per sample in the interrupt.
- **The tap in SDRAM:** possible, since it is built at the first Trk Record take, after boot. It
  saves about 200 bytes, but the interrupt then touches SDRAM each block, against `CLAUDE.md`'s
  DTCM rule, and Resample would still need about 80 bytes (the menu items and flags).
- **The heap 1 KB smaller in mods builds:** `_HeapSize` 0x21E00 to 0x21A00 in
  `platforms/imx/linker/linkerScript.ld`. Tried in a scratch build of step 1: 5,096 bytes free,
  about 4,870 once the port is done. The lowest free heap recorded is 51,980 bytes (8 PERC voices,
  PAST_FAILURES calibration); the VA plan's lowest is 68,740. 1 KB is 2 % of that margin.

**To run again on this base, with the fixed rigs** (PAST_FAILURES: the Unicorn store hook, fresh
build trees, references that stay references):
- step 0: every fact of section 11, plus the gates with a Master FX plugin chosen and with Synth FX on
  a part, and whether `AudioPlayMemory::isPlaying()` still ends as 4.6 says after CE's sample-player
  rewrite and voice sleep;
- step 1: the host tests (passed on `5b40785`, 2026-11-17), `test_resample.py` and the quick tier on a
  build of this tree.

## 13. Catch-up to CE_20261119

On 2026-11-19 this branch merged `modular` at `CE_20261119` (`71b4a8c`, CE's `main` up to `422e0ed`):
25 CE commits. No conflict. Step 1's files are unchanged by it.

**What changed that the design touches:**
- **The recorder's sources (CE's 048809b8, OG USB and stereo samples).** The Tracker+ list is now nine:
  "Line In (L)", "Line In (R)", "Line In (M)", "Line In", "Radio", "Mic LG", "Mic HG", "USB (M)",
  "USB". `sourceTypeLineIn` is gone; `isLineInSource()`, `isUsbSource()` and `isStereoSource()` test
  the groups, and the default source is "Line In" (`sourceTypeLineInS`). Only "Line In" and "USB"
  record stereo. "Line In (L)" and "(R)" record one channel (`useRightChannelForMono`), "Line In (M)"
  and "USB (M)" mix both into one. A mono take goes through `Recorder::update`'s mono branches, not the
  stereo branch that hook 12 changes. **Trk Record: the stereo "Line In" only (decision 9, 2026-11-19).**
  Trk Audio: all four Line Ins and both mics. No buttons: Radio, "USB (M)", "USB". Step 2's mockups
  are redrawn with the nine sources.
- **USB on the OG** (`#ifdef PLATFORM_IMX` removed around USB): no change on the Tracker+.
- **Line In output type** (`mtConfig.general.lineInOutType`) is no longer applied: the input is always
  stereo outside a take. The tap does not use it.
- **Voices** feed the mixers through a stereo mirror (`getStereoMirror()`); the limiter, where the tap
  sits, is unchanged.
- **The sample players (5aaec6f0 and the others of 2026-11-05)** return from their update when playing
  changes. 4.6's ended-sample-track fallback reads `isPlaying()`; step 6 checks it again on this base.
- **Hook sites:** all still there, lines moved: `update` L128, `start` L298, `stop` L432, Button1/2
  L1058/L1086, `destinationBufferSize` L1575, `recorder.record()` L1601, `functPlay` L2916, `functRec`
  L2935, `functInstrument` L2949, `hideRadio` L526, `finishRecording` L278, the `isRec()` branches
  L2832/L3121, `audioEngine::update` L277 unchanged.

**DTCM:** 8,880 bytes free between heap and stack (`tools/mem_budget.py`, mods build of the merge),
up from about 5,096: CE's b3c6473f builds fmt without `std::locale`, which drops about 3.9 KB of
libstdc++ locale data from DTCM, and `instrumentPlayer` grows 256 bytes. The heap is still 137,728.

**Ran on the merge, 2026-11-19:** plain CE and mods builds from a fresh tree; the host tests; the quick
tier, 25 of 25 pass (`test_resample.py` 15 checks). Not run: the release tier (step 1 has no recorder
hook), step 0 again (still to do, section 12).

## 14. Catch-up to CE_20261120

On 2026-11-20, after step 2, this branch merged `modular` at `CE_20261120` (`ff9b46a`, CE's `main` up
to `cf8bd7b`): 10 CE commits, all file manager and file browser fixes. No conflict, and none of step 1's
or step 2's files is touched.

**What changed, and why the design doesn't care:**
- **Sample import (941471d)** closes the gap a failed import left in sample memory. Resample doesn't
  import: the recorder's Save & Load goes through `RecordingSave`, which is unchanged, and then reloads
  the samples as CE does (4.4).
- **The autosave stops (c22d528, bf17231)** before a browse, an export, an SD preview or a firmware
  list, and its stop loop ends when a sample save fails. All main loop; Resample calls none of them.
- **`saveSampleDataToWorkspace` (bf17231)** now ends the operation with `fmSaveError` when the
  instrument file can't be written. The recorder's save doesn't use it; record 12 goes through
  `mods.bin`, which modular's merge checked for every mod.
- **`loadFileToMemory` in `fileWholeOnce` (958ef98)**, the export fixes (bbb2d38, 2cf0d19, 8a00898,
  including `saveMemoryToWav`, which only the export calls), the strict file name ordering (7670291),
  a missing Firmware folder (ce0053e), and the "cannot delete" popup's text (29124ff): not on any path
  Resample hooks or calls.
- **Nothing runs in the audio interrupt** in this merge, and no `// MT:` hook site moved.

**DTCM:** 8,816 bytes free, 64 less than step 2's 8,880: the file browser's new `deleteDeniedText`, as
modular's merge measured (`docs/mt/CE_FIRMWARE_MAP.md` section 15). Heap 137,728, unchanged.

**Ran on the merge, 2026-11-20:** plain CE and mods builds from a fresh tree (386 warnings, all CE's);
the host tests, all suites (`test_resample` 96 checks); the quick tier, 24 of 24 pass, `test_resample.py`
included. Not run: the release tier, and step 0 again (section 12), both still due before Resample
merges into `modular`. The emulator fakes the SD card, so CE's file manager fixes themselves are
modular's merge's to check on the unit, not this branch's.
