# Master FX as card plugins (`.ptmfx`): feasibility and plan

Status: feasibility done and the design approved, 2026-11-14 (section 9). Building. Branch `mod/masterfx-plugins`, off `modular` at
`d9a6795`. Steps 1 to 3 committed (`982c6ea`), step 4 in `12e2329`, step 5 in `0dd2f08`, the sidechain
destination (section 5.1) in `00211ec`, step 6 (the SDK and docs) in `b1f71d8`; step 7 (tests and the
device) written, not committed.

Where the build stands (section 8's steps):
- Steps 1 to 3 are written: the shared parts, the runtime, the pack, record version 2, the built-in DSP
  removed, the Master page and Config lists made from the card.
- What ran:
  - The host tests: all pass, with the new `test_masterfx_plugins` (121 checks) and `test_masterfx`
    rewritten for version 2 (70 checks).
  - Synth FX on the shared parts: its rig (78 checks) and its planted faults (20 of 20 caught); the
    quick tier on the step 1 firmware, 20 of 20.
  - `tools/masterfx/test_pack.py`: each of the eight pack plugins gives the built-in effect's output bit for
    bit (the `d9a6795` firmware's), knobs moved and a clear in the run, at two load addresses.
  - The plugin firmware's own path on the sound rig, by a one-off probe (not yet a test in the tiers): the
    pack written to the fake card at the start, a project's files chosen and `apply` run, then a note
    through CE's sends. Six reverb and delay pairs covering all eight plugins play sample for sample as the
    `d9a6795` firmware's built-in effects.
  - The firmware builds; `mem_budget` is ok: ITCM free 69,984 → 49,568, DTCM 12,516 → 9,136, SDRAM
    688,348 → 603,408.
- The emulator tests now load the plugins through the real path on the plugin firmware
  (`tools/emu/masterfx_runtime.py`: the runtime started on a fake card, the pack's file installed for
  masterfx-1.x's index), and still choose by index on older firmware.
- Step 4, the dynamics slot:
  - `AudioEffectLimiter::update`: with a plugin in the slot, the left object takes its own blocks and
    the right object's, runs the plugin once on the stereo master with both keys, sends both on and
    sets both objects' meter values; the right object has nothing left to do. CE builds the left object
    first and both after the mixer, so the right block is queued: checked in the emulator, one
    `process` call a block.
  - The limiter is never blocked, since the dry master goes through it. While the main loop puts a
    plugin in or clears its memory, the hook passes the master through untouched (`kDynamicsDry`).
  - CE's limiter state is kept as at power-up while a plugin plays, so Stock after a plugin plays as if
    the plugin never had.
  - `resetLimiter` (Sample Editor, Sample Recorder): the slot's 64 KB cleared, the plugin reset.
  - The knobs are CE's as its setters were given them (so the file browser's preview, which sets the
    limiter's objects and not the project, works as on CE): Attack and Release in ms, Threshold through
    CE's curve as 0 to 1, the key On or Off.
  - The glue compressor, `Glue.ptmfx`, the pack's ninth file: 1,216 B of code, 52 B of state, float
    blocks, own knobs Ratio and Makeup. With a key, the key keeps its level, as CE's limiter does.
    Nothing limits the output after the makeup: past full scale it clips (to tell users in the SDK
    docs).
  - What ran: host tests (all pass, `test_masterfx_plugins` 126 checks); the new `dynamics` section of
    `test_masterfx_rig.py` on the sound rig (20 checks: Stock is the reference build sample for sample,
    Glue with the threshold at the top is CE's master 5 samples sooner, with it down both sides turned
    down and the meter on both objects, a silent key leaves the master alone, the reset, Stock again,
    Off, a missing file); the quick tier on the step 4 firmware, 19 of 19 run (3 cached); the
    `MT_NO_MODS` build's flash image the same as step 3's, byte for byte; `mem_budget` ok (ITCM free
    49,152, DTCM 9,112).
  - Not run: the ported emulator tests on a masterfx-1.x firmware (they keep its index path); the
    release tier; anything on the unit.
- Step 5, the UI (`0dd2f08`):
  - The Master page has a third list, Limiter, in column 3 (the Delay page's PingPong list, moved there
    and back as the Sync and Rate lists are borrowed for Reverb and Delay). The stops are Volume,
    Reverb, Delay, Limiter, then CE's. A plugin stopped by the time guard reads "Bypassed" under its
    column.
  - Each slot's page title says what plays: "Reverb: Shimmer", "Limiter: Glue (bypassed)",
    "Limiter: Missing: Gone.ptmfx"; Stock and the mod Off keep CE's title. The time guard's stop is
    polled in the page's `update()`.
  - A plugin's own knobs (up to 2) sit in columns 5 and 6 of the Reverb and Limiter pages: its label,
    its text (the plugin's `format`, so Glue shows "4.0:1" and "+0.5 dB"), a bar, the buttons under
    the columns, the encoder, Up/Down and Delete (its default). The delay page has no free column, so a
    delay plugin's own knobs stay at what the project holds or the plugin's default.
  - An own knob turned on the page is stored in the project's record (`mods::masterfxKnobEdited`). A
    knob the project never set stays unset in settings, and the plugin plays its default: `apply` no
    longer writes the plugin's value back.
  - Config > Project > Master FX has a third list, Limiter.
  - Memory: DTCM free 9,112 → 9,096; the page's text buffers are in SDRAM.
  - Display bytes, upper bounds with the heaviest choices: Master page 5,752, Reverb 3,476, Delay
    5,004, Limiter with Glue 5,596, against 8,172.
  - What ran: host tests (all pass; `test_masterfx` 79 checks, 9 new for the title and own knobs with
    nothing installed); `test_masterfx_rig.py` ui on the step 5 firmware (75 checks: mod Off makes the
    reference build's display calls, the stops, the Limiter list, the Limiter page with Glue, Stock and
    a missing file, Bypassed, the borrowed lists given back) and dynamics (19 checks); the quick tier on the step 5 firmware, 19 of 19 run (3 cached);
    the `MT_NO_MODS` build the same as step 4's, all 6,696 loaded sections byte for byte.
  - Not run: a reverb plugin with own knobs (none in the pack has any, so that page's own-knob columns
    ran only through the Limiter page's shared code); the release tier; anything on the unit.
- The sidechain destination (section 5.1), committed in `00211ec`:
  - The Limiter page's column 4 holds CE's Sidechain list or, with Shift + its button, the Ducks list:
    Master, Track 1 to 8, Synth 1 to 3. Its button marks or clears the item under the cursor (the
    encoder or Up/Down move it), Delete goes back to Master, and the label's second line says
    "Master", the one track, or "3 tracks". The page opens on the Sidechain list. Only while a plugin
    that can duck plays, on the Tracker+.
  - The record: `Settings.ducks`, a mask of 11 destinations, after the three slots (172 bytes). The
    unreleased 168-byte version 2 still loads, with no ducks.
  - The ABI: `PTMFX_FLAG_DUCK`, for float dynamics plugins only, and `PtmfxBlock.gain`. While the
    project ducks tracks the host gives the plugin the key as its block and as its key, throws its
    output away, and takes its gain (clamped to 0 to 4, NaN as 1 and a reset). Glue has the flag.
  - Where it runs: the panning amplifiers (`AudioAmplifier::update`, an MT: hook) of the 8 sample
    voices and of the 3 synth parts, after the track's volume and its sends. The master passes
    through untouched.
  - Timing: the gain is worked out once a block, by the first ducked track that finds the key
    already made, or else by the limiter. A track made before its key (all of them with a synth part
    as the key, since PolySynth runs after the panning amplifiers) uses the gain of the block before:
    2.9 ms late. Two gain buffers, 1 KB of DTCM.
  - The meter shows the mean of the gain's turning down.
  - The SDK's builder (`ptmfx_builder.py`) checks the flag as the firmware does; the pack rebuilt and
    embedded (Glue: 1,288 B of code, flags 0x3).
  - Memory: DTCM free 9,096 → 7,840 (the gain buffers and the state); ITCM free 48,256.
  - Display bytes: the Limiter page with the Ducks list up, 5,924 of 8,172.
  - What ran: the host tests (all pass; `test_masterfx` 92 checks, with the record's ducks, the
    168-byte record and the Ducks list's names, marks and text; `test_masterfx_plugins` 129, with the
    flag's rules); the new `ducks` section of `test_masterfx_rig.py`, now in the quick tier (36
    checks: Synth 1 ducked by a Line in key and by Synth 2, the master untouched before the key and
    with a silent destination, who works out the gain in each case, each of the 11 destinations its own
    two amplifiers, the meter, Stock, Off, no key and a plugin without the flag duck nothing, and the
    page: Shift + column 4, marks, the cursor, Delete, the record stored, the Sidechain list back);
    `ui` (75) and `dynamics` (19) again; the quick tier, 19 of 19 run (3 cached); the `MT_NO_MODS`
    build the same as step 5's, all 6,713 non-debug sections byte for byte.
  - Not run: a sample track as the ducked destination with sound (the rig plays synth parts; the
    sample voices' amplifiers are checked as wired, and run the same code); the cost of a ducked
    track (to measure on the unit with the Profiler); the release tier; anything on the unit.
  - Glue's attack: with CE's default knobs the first changed block was 2 blocks after a Line in key
    started, 3 after a Synth 2 key (the extra block is the update order above).
- Step 6, the SDK and docs (`b1f71d8`):
  - `docs/masterfx_plugins.md`, the guide, in Synth FX's 13 sections: the slots and their blocks, the
    lifecycle, ducking chosen tracks, a tutorial, the Master page, the header, the host, the binary
    format, the memory, loading and guards, the cost table, the checker, and the comparison with CE's
    and Synth FX's formats. `masterfx-sdk/README.md`. A `.ptmfx` column in
    `docs/synthfx_plugins.md`'s comparison table.
  - Two examples: `room.c` (a float reverb in both of the reverb slot's areas, CE's four knobs, a
    Width knob; 832 B of code) and `ducker.c` (a float dynamics plugin with the duck flag, Threshold
    renamed, a list knob shown by `format`, the meter; 784 B). `build_pack.py` builds them with the
    pack, into `build/masterfx/examples`.
  - `tools/masterfx/check_ptmfx.py`: a plugin on a fake card, listed, read and installed by the
    firmware's runtime on the emulated Cortex-M7, then run through the runtime's `process` on noise,
    a sine and silence at several of CE's and its own knob settings (dynamics: no key, a key,
    ducking). It checks the header as the firmware reads it, the own knobs' texts, float output and
    the gain before the guard, writes outside the state, memory, output blocks and own stack, the
    output after CE's clear against a fresh instance, and the instructions per block against the time
    guard's eighth of a block. `masterfx_runtime.py` takes the files to put on the card.
  - What ran: the checker on the two examples and the 9 pack files, all pass (the most, Shimmer, at
    48,623 instructions a block, 22.3 % of the guard's share; Glue gets a note that its Makeup at
    +12 dB clips); on 6 planted faults, each caught (a write past the state, output that isn't a
    number, the gain left unfilled, a `reset` that keeps the level, a slow loop, a state size of 0);
    the host's own cost around a float plugin, about 6,900 instructions a block (10,600 with a key),
    a few dozen around a 16-bit one. The new quick-tier job `masterfx sdk` (the checker on the
    examples and the pack), passes in 61 s. The pack rebuilt the same, byte for byte, and `--check`
    matches the firmware's copy.
  - Found while writing: a bypassed plugin is installed again, and so un-bypassed, only by picking
    another choice and then it, or turning the mod off and on. Picking the same line again, or
    opening a project that uses the same plugin, keeps it bypassed. The guide says so.
  - Not checked: the firmware's guards acting, anywhere: the bypass after 8 slow blocks, the reset
    after a NaN, the output and gain clamps. The checker catches those faults before the guard; the
    rig's `ui` section only sets the bypassed byte to check the page. Step 7's planted faults (below). The
    guide's build commands were run as the pack's build is, in WSL, not from Windows.
- Step 7, tests and the device, written, not committed:
  - `tools/masterfx/test_guards.py`, a new job in the quick and release tiers (`masterfx guards`, 50
    checks, 19 s): the firmware's own guards acting, with planted plugins from
    `tools/masterfx/device/` (`build.sh`, in WSL) on the fake card and an instruction count as the
    clock. GuardSlow on Heavy is bypassed by the 8th block in a row over the budget, and the master
    passes through untouched; on Light, and on Heavy with no budget, it never is; installing it again
    clears the bypass. GuardOutput's NaN and Inf become 0 for that sample, with a reset before the next
    block; its 100.0 is held at full scale, with no reset. GuardGain's NaN gain becomes 1 with a reset,
    9 and -1 are held within 0 to 4, and the meter's NaN and 2 within 0 to 1. GuardHang leaves
    `/MasterFX/.starting` on the card before its first block; the next start is in safe mode and
    skips the project's plugin but takes the user's pick; the marker goes, and safe mode ends, 5 s
    after the last install.
  - `tools/masterfx/compare_cost.py`, a new release-tier job (`masterfx cost`, about 10 minutes):
    CE's reverb, delay and limiter objects' and the panning amplifiers' `update`, whole, in
    instructions a block, on the sound rig with 4 voices into both sends, against today's release
    (`d9a6795`). Stock: reverb 34,700 and delay 12,045 on both, the limiter +0.1 %. The pack against
    the built-in effects: Freeverb +0.3 %, Plate -0.4 %, Spring +0.3 %, Shimmer +0.2 % (49,875),
    Gated and Reverse +0.0 %, Tape +0.4 %, BBD +2.1 % (746 more, 36,195). Glue against CE's limiter at
    a threshold that works: +13 % without a key (16,479), -12 % with one (18,388). The amplifiers:
    3,586 against 3,360 (+226, the ducking hook's test, nothing ducked). The pack's limit is 2 % or
    1,000 instructions, whichever allows more: BBD's DSP is masterfx-1.x's source built as a plugin,
    and 746 instructions are 0.04 % of a block.
  - `test_masterfx_rig.py`: `play` now also plays 8 voices with Shimmer + BBD + Glue on a real
    project, CE's `.psyn` engines on the fake card (Glue installed, 300 blocks, no asserts, nothing
    written over the code, the output changed, the level within 3 dB of Stock); 49 checks pass.
    `dynamics` now also checks the bypassed slots: Glue bypassed passes the master untouched, Shimmer
    and BBD bypassed give the dry sound sample for sample; 25 checks pass.
  - The device check's projects: `make_masterfx_test_project.py --masterfx-plugins` writes M1 (8
    voices, Shimmer + BBD as record version 1, which today's release reads too), M2 (version 2 with
    Glue) and M3 (1 voice, GuardSlow on Heavy). Opened in the emulator before the unit: the plugin
    firmware installs M1's and M2's files, and today's release plays M1's pair. The ladder's new
    section has them.
  - The two lockup recorder builds, from fresh archives of `b1f71d8` and `d9a6795`.
  - The release tier on the plugin firmware (`b1f71d8`'s build), the recorder build as `--diag`: 46
    jobs, 0 failed of the 21 that ran, the other 25 passed before on the same firmware and tests
    (the cache). It ran with `MT_TEST_CPUS=3` and 3 jobs, the user's 50 % limit, in 1 h 40 min.
  - The first device check, 2026-11-15, with both lockup recorder builds and M1 to M3 (the
    unit's `/mfx_log.txt`):
    - M1, 8 voices with Shimmer + BBD: today's release averages 62 % of a block, slowest 79 % and
      73 % over two runs; the plugin firmware averages 62 %, slowest 81 % and 74 %.
    - M2, with Glue as well: 62 % to 63 % average, slowest 74 % to 76 % over four runs.
    - M3: GuardSlow is bypassed (the Limiter column read Bypassed) and the voice plays on; the
      log's slowest is 28 % to 34 %, so its 8 slow blocks came at the project's open, before Play.
    - Every run: 0 blocks over time, none back to back, CE's synth never over its limit, the
      audio pool at 40 of 80, free heap 79 to 81 KB, max stack 7,905 B (8,569 B on today's).
  - What the first device check found, all three on the plugin firmware's Master page, now fixed:
    - A step in any of the three lists moved the other two lists' bars to Stock and slid them back.
      Each step gave all three lists their data again with `start` 0, and CE's list draws its bar
      from `start` and slides it to the value over a few frames. Now each list's `start` is its
      choice, and a step that leaves the lists' contents as they were only sets the values.
    - A file copied to `/MasterFX/` after the start stayed Missing until the unit restarted: the
      folder was listed only at the start. Now `masterfx::load` (a project's open) and the Master
      page's opening (`masterfx::pageOpened`) list it again, and the page's opening applies.
    - The Missing text, with the file's name, spilled out of its column under the list in CE's
      17 pt font. Now the list's line and the column say `Missing`, the slot's page title still
      names the file, and the text under each column is cut to its 94 px (CE cuts only a
      label's first line), so a long plugin name can't spill either.
    - Checked: the new `lists_and_card` part of `test_masterfx_rig.py ui` (16 checks: the bars on
      their choice from the first frame, no bar moving after Limiter steps, Up at Stock, Missing
      in the column and the file in the title, a copied file listed and playing once the page
      opens and once a project opens, a 31-character name cut to 10 characters at a planted 9 px
      a character, as the rig doesn't unpack the fonts). The build from before the fixes fails
      12 of them; the fixed build passes `ui`, 91 checks. Host tests all pass; `mem_budget` ok, SDRAM
      96 B more. The quick tier on the fixed build, 0 failed of 21 run; the release tier, with the
      fixed code's lockup recorder build as `--diag`, all 46 jobs passed (9 in the last run, 37
      cached from runs on the same firmware and tests).
  - The recheck on the unit, 2026-11-15, with the fixed code's lockup recorder build
    (`TrackerPlus_MFX2_fix_diag.ptf`) and M3, the user's report: Limiter steps, Up at Stock
    included, leave the Reverb and Delay bars where they are, and so do Reverb steps; the column
    says Missing inside its box and the title names the file; GuardSlow.ptmfx copied over USB is
    listed once the card comes back and the Master page opens, and the column reads Limiter /
    Bypassed; leaving the page and coming back keeps all of it. The user's two readings in M3 were
    71 % and 28 % (59 % and 28 % on the first check). The log, three runs: average 20 % of a block,
    slowest 31 %, 28 % and 27 %, 0 blocks over time, none back to back, the synth never over its
    limit, free heap 80,796 B, max stack 8,025 B. One run's longest main loop gap was 209 ms, the
    run that took in the USB card mode and the folder's new listing; the others 7 ms and 0 ms.
  - Not needed again: M1 and M2 on the fixed firmware. They passed on the first check, the fixes
    change only the main loop's page and the listing, and M3 on the fixed firmware gave the same
    audio cost as before. The time guard against real cycles: done on the unit, M3 bypassed
    GuardSlow on both checks.
  - The device check M4, 2026-11-15, on the fixed code's lockup recorder build, the user's
    report: Room and Ducker listed and playing from the card's files; the Limiter page's title,
    Ducker's Trigger in place of LimiterThr, Depth 60 % and Curve Smooth; the kick (a sample
    track) as the Sidechain key dipping the hats and the synth but not itself; the Ducks list
    (Shift + the Sidechain button) with Master marked, Track 3 alone ducking only the hats (a
    sample track as a destination), Synth 1 added reading 2 tracks and ducking both, Delete back
    to Master, the Sidechain list back with the kick kept; Depth 100 % ducking to silence (its
    design: a full duck at 100 %), Delete back to 60 %, Curve Linear and Smooth; Glue's Ratio and
    Makeup with their defaults, Makeup 6.0 dB louder, Delete back for both; Room's Width 80 %,
    Delete back. By ear, Ratio 20.0 :1 and Width 0 % were hard to tell apart ("sounds right"),
    so those two are seen on the screen but not clearly heard. The log, four runs (the steps
    played without stopping, so the Ducks settings aren't apart in it): average 22 % of a block,
    slowest 42 %, 39 %, 39 % and 40 %, 0 blocks over time, none back to back, the synth never
    over its limit, the pool at 40 of 80 at most, free heap 79,316 B, max stack 8,757 B (of
    20,480), the longest main loop gap 37 ms. Ducking's own cost isn't apart from Room's and
    Ducker's: all of it, with one synth voice and two sample tracks, stayed at 22 % on average.
    M4 in the emulator on the fixed build: it opens with Master FX On, Room and Ducker active from
    the card's files, 13 notes through the sequencer, no asserts, the Ducks list's Track 3 +
    Synth 1 reach the firmware's mask (0x104) and ducking is live every block; with Synth 1 as
    the key the gain goes down to 0.4. With the kick as the key the gain stays 1: the emulator
    doesn't load sample tracks' audio, so the kick is silent there.
- Not yet:
  - the user page is written, not yet checked on the unit: `docs/mods/masterfx.md`, linked from the
    README's Master FX row, with six mockups drawn by `tools/docs/mockups_masterfx.py` from the
    fixed firmware's code (the Master page's lists, Shimmer's words on the Reverb page, Glue's own
    knobs, the Ducks list, Bypassed, Missing). `tools/docs/check_mockups.py` (which now also reads
    `masterfx-sdk/pack/` for the pack's names) passes, 41 images. The user pages from `modular`
    (`e44d0617`, `b091ce22`: `docs/mods/`, `tools/docs/`, the README and CLAUDE.md) are in this
    branch's working tree as `modular` has them, plus this branch's changes (the README's Master FX
    row, the checker's sources), so step 8's merge sees only those as new;
  - step 8, whose checklist then checks the page against the merged firmware's labels.

Today's mod is in [MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md): eight effects built into the firmware,
run inside CE's own reverb and delay objects. This plan turns them into plugins loaded from the card,
adds a third slot for the limiter, and gives users an SDK to write their own.

## Summary

- **It is feasible.** All three slots (reverb, delay, limiter) can run card-loaded plugins. The audio
  interrupt calls them in the same places today's effects run: inside CE's `AudioEffectPolyverb::update`,
  `AudioEffectShortDelay::update` and, new, `AudioEffectLimiter::update`.
- **The cost is about zero, measured in the emulator.** Each of today's eight effects, built as a
  position-independent plugin and called through a pointer, runs within 1 % of the built-in effect's
  instructions (section 3). The host's side of a call is 9 instructions with CE's 16-bit blocks. A float
  block costs the host 1,553 more.
- **Placement matters more than the call.** A plugin's code can be loaded into ITCM. The MPU lets ITCM run
  code, and 69,984 B of it are free since CE took the closed libraries out. Each plugin's small state can go
  in DTCM. Then a plugin runs from the same memories as today's effects. On the unit, Synth FX's plugins
  from SDRAM already cost nothing visible on single instances (F1 to F6, 2026-11-12). Many instances' state
  in SDRAM did cost more: P2, 16 points.
- **Follow Synth FX's `.ptsfx` rules, not CE's `.psyn` or `.ptfx`.** The plugins are position independent
  with no `.data` or `.bss`, have a versioned host table, and are guarded in the interrupt. Master FX gets
  its own format, `.ptmfx` in `/MasterFX/`, and a smaller runtime: one instance a slot, each slot with the
  memory it owns. It shares Synth FX's generic parts: the pool, the knob model, the catalog, the start
  marker and the cache sync.
- **Old projects keep working.** CE's knob values stay where CE stores them. The record's effect index
  (0 to 6, 0 to 2) maps to a file name in the default pack, and the pack is built from today's DSP. A test
  can check it plays the same, sample for sample.
- **A glue compressor costs less than CE's limiter.** The prototype runs 6,824 instructions a block,
  against CE's limiter at 14,420 (20,834 with its sidechain). Every candidate is costed in section 5.
- **The sidechain needs nothing new for one track.** CE's limiter already keys from one source the user
  picks: tracks 1 to 8 after their faders, Line in L or R, or Synth 1 to 3. CE pays for those taps in
  every block (the two muxes: 410 instructions). A plugin gets the same key. Several tracks at once needs
  a summing mux, about 150 instructions per chosen source per side (estimated, not measured), and a new
  list control.

## 1. CE's plugin loaders, and ours

| | `.psyn` (CE) | `.ptfx` (CE) | `.ptsfx` (Synth FX, ours) |
| :--- | :--- | :--- | :--- |
| What it is | a synth engine | a sample-editor effect, offline | a real-time effect on a synth part |
| Folder | `/Synths/` | `/Effects/` | `/SynthFX/` |
| Code | relocated ELF (ABS32, MOVW/MOVT), `.data`, `.bss` and constructors allowed | position independent | position independent, no `.data`/`.bss` |
| Memory | 1,920 KB arena in CE's 6 MB pool, LRU eviction | the sample editor's 128 KB code and 384 KB scratch, one plugin | 96 KB code and 192 KB state pools, no eviction |
| Loads | 1 MB boot preload, then on selection; `ensurePluginLoaded` can read the card with audio masked (`setVoiceCount`) | when applied | in the main loop, once the encoder rests |
| Runs in | audio interrupt | main loop | audio interrupt |
| Host table | `PsynHostApi`, versioned, file reads | `PtfxHostApi`, file I/O | `PtsfxHost`, versioned, maths only |
| Guards | CE's 2.1 ms synth limit | none needed | time, output, start marker |

Master FX's job matches Synth FX's: someone else's code in the audio interrupt, on audio CE already
routes. So it takes Synth FX's rules. CE's two loaders don't fit:
- `.psyn` relocates a big image into an arena that can evict plugins, and it reads the card from the audio
  path.
- `.ptfx` runs one effect offline, with file access.

**Share the loader?** In part. Synth FX's runtime (`synthfxPlugins.cpp`) is one global catalog with 56
instances, fades and per-voice seats. Master FX needs one instance a slot and memory tied to each slot, so
a second copy of that runtime would be mostly unused. These parts are generic, and both mods would share
them:
- `synthfxPool.h`;
- the knob step maths and text helpers;
- the header checks' pattern;
- the catalog listing;
- the start marker and safe mode;
- the cache sync.

Moving them into `core/mods/plugins/` is a refactor of a released mod. Synth FX's tests cover it, and
`MT_NO_MODS` must stay byte-identical.

## 2. Can each type run as a plugin?

- **Reverb.** Yes. The plugin replaces `reverb.Process` inside `AudioEffectPolyverb::update`, with CE's
  two buffers as its memory: 32 KB in DTCM and 32 KB in SDRAM. That is where today's six reverbs already
  run.
- **Delay.** Yes, in `AudioEffectShortDelay::update`, with CE's 1.5 MB of SDRAM lines. The host keeps
  `silentSamplesCount` from the plugin's output, since Export uses `isSilent()` for the tail.
- **Limiter / dynamics.** Yes, with care. CE runs two mono `AudioEffectLimiter` objects, left then right,
  and a compressor needs both sides together. `audioEngine` builds `mixerAndFxs` before `output`, and
  `limiter[0]` before `limiter[1]`. So when `limiter[0]` updates, the right block is already queued on
  `limiter[1]`. The left object's hook takes both blocks and runs the plugin once. The right object's hook
  sends its processed block on. This order is read from the source and checked in the emulator (step 4).
  - The key comes from CE's sidechain mux.
  - The Gain Reduce meter reads a gain the plugin returns.
  - `resetLimiter` (Sample Editor, Sample Recorder) resets the plugin.
  - Stock stays CE's limiter, untouched, including its sidechain, which ducks the mix but keeps the key
    track at its level (`g × main + (1 − g) × key`).
- **Gated mode.** "Gated" and Reverse share `gated_dsp.h` with a flag today. They become two pack files
  from the one source, so old projects' indices 5 and 6 still map. 1.2.0's old Gated delay, delay 3, still
  imports as the stock delay, as today.
- **Performance mode and Break FX.** Performance mode only changes the sends, and Break FX is the
  sequencer's. Neither touches a slot, so neither interacts with plugins beyond the sound they send in.
  Checked in the emulator only, never in the user's hardware list.

## 3. Performance, measured

All in the emulator, instructions per 128-frame block, on the real firmware built from `d9a6795`. 1 % of
the CPU is about 3,800 instructions (1.2.0's readings). The built-in effects were measured on the real
graph: 4 voices playing into both sends, the effect's arguments captured, then each call counted alone.
The plugins are today's DSP headers built with Synth FX's builder flags (`-fPIC
-mpic-data-is-text-relative`, no GOT), loaded at an SDRAM address, and checked to give the same bytes when
linked at two addresses. They ran on the same buffers and knobs.

| Effect | Built in | Plugin, 16-bit blocks | Plugin, float blocks (the DSP converts back to 16-bit) |
| :--- | ---: | ---: | ---: |
| Freeverb | 44,959 | 44,810 | 52,884 |
| Plate | 38,737 | 38,980 | 46,948 |
| Spring | 34,014 | 34,138 | 42,226 |
| Shimmer | 48,466 | 48,970 | 56,988 |
| "Gated" | 41,704 | 41,145 | 49,118 |
| Reverse | 41,516 | 41,034 | 49,086 |
| Tape | 18,866 | 18,100 + the silence scan (about 800) | 33,231 |
| BBD | 23,261 | 22,459 + the silence scan | 41,046 |

- **The call and its indirection:** the host's side with a do-nothing plugin is 9 instructions with
  16-bit blocks. A float block, converted in, NaN-guarded and converted back, costs the host 1,553. So
  16-bit blocks (Q15, CE's own) are the default ABI and the pack uses them. Float blocks are a flag for
  plugins that want them: about 0.4 % of the CPU.
- **Code and data placement, as lines touched per block:**
  - code: 49 to 94 32-byte lines, 1.6 to 3.0 KB each;
  - state in DTCM: as built in;
  - state in SDRAM (the measurement build kept it beside the code): up to 83 more SDRAM lines a block
    (BBD's).
  - **So state, constants and code don't go in SDRAM.** Code and constants load into an ITCM area for
    each slot; state goes in DTCM.
- **On the unit:** not measured. The emulator doesn't model the cache or the stalls, which is why the
  placement above copies today's. The unit's own evidence is in the summary.
- **Memory, from `tools/mem_budget.py` on `d9a6795`:** ITCM 69,984 free, DTCM 12,516, SDRAM 688,348,
  Flash 15,025,596.

  | Region | Change | Why |
  | :--- | ---: | :--- |
  | ITCM | +24 KB, less the 5,584 B of the built-in loops | an 8 KB code area for each slot |
  | DTCM | +2 KB, less today's 1,144 B of state | fast state: reverb 1 KB, delay and dynamics 512 B each |
  | SDRAM | about +80 KB of the mods' 1 MiB | the dynamics slot's 64 KB, a load buffer, the catalog |
  | Flash | about +13 KB | the built-in DSP (19 KB) goes; the embedded pack and the runtime come in |
  | Heap | 0 | no malloc |
- **Load from the card, and the audio meanwhile.** A pack plugin is 2 to 3.5 KB. It is read in the main
  loop into an SDRAM buffer while the old effect keeps playing. Then the switch runs as today: the object
  is blocked, its buffers cleared in the main loop, the code copied into the slot's ITCM area and synced,
  `init` called, and the slot published. The effect is silent for the clear. The dry sound isn't touched,
  and audio is never masked. Not measured: the card's read time (the emulator fakes the card) and the
  1.5 MB clear on the unit.

## 4. Old projects, and how a project names its plugins

- **Record 4 (`mods.bin`), version 2:** each slot stores a file name (as Synth FX does, up to 47
  characters) and the plugin's own knob values. Version 1 is still read: its two indices map to the pack's
  file names (`Freeverb.ptmfx` and so on; 0 is Stock), and the record is rewritten at the next save.
- **CE's knobs** (Size, Damping, Predelay, Diffusion; Time, Feedback, Filter, PingPong, Sync and Rate;
  Attack, Release, Threshold and Sidechain) stay in CE's project fields. A plugin reads them every block,
  as today's effects do, and its header may rename their labels.
- **1.2.0's `masterfx.bin`:** imported as today, into version 2's names.
- **A missing or refused file:** the slot keeps the name and the page reads `Missing: Shimmer`. Section 9
  asks what plays meanwhile. The firmware writes any missing pack file at start, as Synth FX does, so only
  a third-party plugin can be missing for long.
- **Mod Off:** CE's stock effect in each slot, and the record is kept.
- **The same sound:** the pack is built from the same DSP headers. A `test_pack`-style emulator test
  compares each one with today's firmware, sample for sample, over many blocks and knob settings. Today's
  effects then come out of the firmware.

## 5. A new dynamics type in the limiter slot

Prototypes written for the measurement (`dyn.c`): stereo, float blocks, built as plugins. Their cost is
against CE's limiter on the same graph, both sides together.

| Type | Instructions a block | Against CE's limiter | Memory | Notes |
| :--- | ---: | ---: | :--- | :--- |
| CE's limiter | 14,420 | 1.0 | 0 | 20,834 with its sidechain on; a float divide and a 5-sample shift per sample per side |
| Glue compressor | 6,824 (6,884 keyed) | 0.47 | about 50 B | stereo-linked peak detector with a key high-pass, soft knee, gain every 8 frames, ramped, makeup, mix |
| Opto-style compressor | 6,329 | 0.44 | about 40 B | detector on the output, two release envelopes |
| Tape saturation, 2× | 14,667 | 1.0 | about 60 B | pre- and de-emphasis, asymmetric soft clip, top loss |
| Clipper | 4,102 | 0.28 | 8 B | soft knee |
| Clipper, 4× polyphase | 27,679 | 1.9 | about 130 B | two 2× half-band stages |
| Pitch shifter | 13,714 | 0.95 | 32 KB of lines | two heads and a crossfade: plain quality; a better one costs more |

- Add 1,553 to each for float blocks, or nothing if the plugin uses 16-bit blocks.
- The pitch shifter fits the slot's 64 KB, but it isn't dynamics: it is a creative effect, and Shimmer has
  one inside it already. A clipper and tape saturation fit beside a compressor, as plugins of the same
  slot.
- **Proof of concept: the glue compressor only.** Its knobs:
  - CE's Attack, Release and Threshold;
  - its own Ratio and Makeup, in the page's free columns 5 and 6;
  - CE's Gain Reduce meter;
  - CE's Sidechain list as its key.
- **Several key tracks (the user's idea):**
  - **Cost:** the two muxes already receive every input each block. Summing a chosen set instead of
    passing one is about 150 instructions per source per side (estimated) and 2 more audio blocks from the
    pool (peak 55 of 80).
  - **Where:** CE's graph taps each track's amplifier after its fader (`connectVoiceToOutput`), Line in
    (8, 9) and each synth part after its fader (10 to 12). No other tap is needed.
  - **What it needs:** an MT: hook in `AudioMux::update`, which runs from flash, and a multi-select
    control on the Limiter page, whose display bytes must be counted.

### 5.1 A sidechain destination (the user's design, 2026-11-14)

What CE does today: the Sidechain list picks the key (a track after its fader, Line in L or R, or a synth
part). The limiter's gain comes from the key and is applied to the whole master, and then CE adds the key
back: `out = g × master + (1 − g) × key` (`effect_limiter.cpp`, `processSidechain`). So everything but
the key is turned down, and the key keeps its level.

What the user wants: a second choice, the **destination**, so the kick can duck only the bass.
- **Default:** Master, as today.
- **Other choices:** one sample track (1 to 8) or one synth part (1 to 3).

How it fits CE's graph (Tracker+, read from the source):
- A sample track is `memoryPlayer → bitDepth → filter → amplifier (volume) → panningAmplifier →` the
  tracks mixer.
- The reverb and delay sends are taken from the filter, before the volume.
- The key is taken from the amplifier, after the volume.
- A destination track's compressor runs on that track's stereo block, in place, between its `amplifier`
  and its `panningAmplifier`. That's an MT: hook in the panning amplifier's update for that one track.
  Its sends are taken earlier, so they aren't ducked (decided below).
- A synth part as the destination runs at its panning amplifiers, after its Synth FX slots and its
  volume (built: section 1's status says how).
- The plugin doesn't change: `process` gets a stereo block and a key, whether the block is the master or a
  track. The host decides where it runs. When the destination isn't the master, the master's limiter is
  CE's (Stock, with its sidechain off).

Several destinations (the user's question, 2026-11-14: "kick ducks the bass and chord tracks"):
- Possible, and cheaper than one compressor per track. With a key, the gain comes from the key alone:
  CE's limiter works it out from the key's level (`effect_limiter.cpp`, `processSidechain`), and the
  block being ducked doesn't enter into it. So one compressor works out the gain once per block, and
  each destination track multiplies its block by it.
- The ABI gets one more field: `PtmfxBlock.gain`, a float[128] the plugin fills with the gain it
  applied, sample by sample. A plugin that can drive other destinations says so with a flag, because
  a saturator or clipper has no single gain to give.
- The master stays a choice of its own: either the master, or a set of tracks (sample tracks 1 to 8,
  synth parts 1 to 3), saved as a mask in the dynamics slot's record. Record version 2 hasn't been
  released, so the field goes into it before the release.
- Each destination ducks by the same amount. A threshold per track would need an instance per track.
- Cost: the compressor once, plus about 400 to 600 instructions per destination track for the
  multiply (an estimate, to measure). Built with two gain buffers, 1 KB of DTCM (the timing below).
- The page: CE's lists choose one item, so the destinations need a list with a mark for each item that
  a press toggles. Its display bytes are counted with the rest of the page.

Decided 2026-11-14: a destination track's sends aren't ducked (taken before its volume, as CE does).

Two limits to check in the emulator before it's built:
- **Update order.** The objects run in the order they were made. If the key's track comes after the
  destination's in that order, the key reaching the destination is one block (2.9 ms) old. The attack
  then starts up to 2.9 ms late; the compressor itself is unchanged. Probed on the sound rig: the 8
  sample voices first, then the Line in, the tracks mixers, delay, reverb, the synth parts' panning
  amplifiers, PolySynth, and last the key muxes, the limiters and the output. So a sample track or the
  Line in as the key reaches the synth parts and the later sample tracks in the same block; a synth
  part as the key is always a block late. Built that way (section 1's status), and checked by the rig.
- **Cost.** One compressor instance on a track costs about the same as on the master: 6,824
  instructions measured on the master (a track's block is the same size), plus the hook's call.

## 6. Guards and the interrupt's time

CE's 2.1 ms limit counts only `PolySynth::update`, so Master FX runs outside it. Each slot gets Synth
FX's guards:

- **Time:** `process` timed with the cycle counter. Over its budget for 8 blocks running, the slot is
  bypassed, and the page says so. Proposed budget: an eighth of a block, about 218,000 cycles. Today's
  heaviest effect is about 49,000 instructions.
- **Output:** NaN and infinity become 0, then the plugin is reset.
- **Start:** a marker, `/MasterFX/.starting`, from a new plugin's first block until 5 s later. If it is
  found at start, the project's plugins aren't loaded until the user picks one.
- **Boot gate:** no plugin runs before the first Pattern screen update, the rule today's effects keep.

A crash or a hang inside a plugin can't be stopped. The marker is for that.

## 7. The ABI and the SDK

`masterfx-sdk/ptmfx.h`, version 1, little-endian, the same shape as `ptsfx.h`:

- **Header:** magic `PTMF`, version, flags, slot (reverb, delay or dynamics), name[32], author[16], and
  the sizes of state and code.
- **Labels:** each CE knob's label as the plugin wants it shown.
- **Own knobs:** up to 2, as `PtsfxParam`, 32 bytes each, with the same step rules.
- **Entry offsets.**

The entries:

| Entry | Runs in | What |
| :--- | :--- | :--- |
| `init(st, const PtmfxMemory* mem, const PtmfxHost* host)` | main loop | zeroed state in DTCM; `mem` is the slot's memory: reverb 32 KB DTCM + 32 KB SDRAM, delay 3 × 512 KB SDRAM, dynamics 64 KB SDRAM, all cleared |
| `set_param(st, index, value)` | interrupt | own knobs, before a block |
| `process(st, const PtmfxBlock* b)` | interrupt | `b`: L and R in place (the reverb's send in, return out), the delay's mono send, the key's L and R (dynamics), CE's knob values for the slot; returns the gain for the meter (dynamics) |
| `reset(st)` | interrupt | after CE clears the buffers (project load, Export, Sample Editor or Recorder) |
| `format(host, index, value, out, size)` | main loop | optional: a knob's text |

- **Flags:** `PTMFX_FLAG_FLOAT` for float blocks (±1.0). Without it, CE's Q15 16-bit blocks.
- **Host table:** Synth FX's maths, `random` and `print`, plus the tempo (for synced delays) and the
  sample rate.
- **Builder:** `ptmfx_builder.py`, from `ptsfx_builder.py`, with the same refusals: no `.data`, `.bss`
  or GOT, the same bytes at two link addresses, no double-precision helpers. Code is at most 8 KB, the
  size of a slot's ITCM area.
- **Checker:** `tools/masterfx/check_ptmfx.py`: the plugin in the emulator with the firmware's runtime.
- **Docs:** `docs/masterfx_plugins.md`, with the same 13 sections at the same depth as
  `docs/synthfx_plugins.md`, CE's `custom_synth_sdk.md` and `modular_effects_system.md`. The comparison
  table in Synth FX's doc gets a `.ptmfx` column.
- **The pack:** the eight effects and the glue compressor are in the firmware, written to `/MasterFX/`
  when missing. Their sources are in `masterfx-sdk/pack/`.

## 8. Build plan, after the go-ahead

Each step is a commit, with its tests passing and `MT_NO_MODS` byte-identical.

1. **The shared parts.** Move them into `core/mods/plugins/` and put Synth FX on them. Synth FX's host,
   emulator and page tests must pass unchanged.
2. **The runtime.** Master FX's catalog, loader, ITCM code areas, DTCM state, guards, marker and pack
   writer, with host tests.
3. **The pack.**
   - Port the eight effects, with the state reached through a pointer.
   - Test them sample for sample against today's built-in effects.
   - Record version 2, with version 1 and 1.2.0's file converted.
   - Remove the built-in DSP.
4. **The dynamics slot.** The limiter hooks, the stereo pair, the key, the meter, the reset; Stock as CE.
   The glue compressor plugin.
5. **The UI.**
   - A third list on the Master page (the Limiter column).
   - The plugins' labels.
   - Own knobs in the free columns.
   - The Missing and Bypassed text.
   - The display bytes counted against 8,172.
6. **The SDK and docs**, with the builder, the checker and two examples.
7. **Tests and the device.**
   - The release tier.
   - The sound rig with all 8 voices and CE's `.psyn` engines on the fake card.
   - CPU against today's Master FX, in instructions.
   - Planted faults.
   - A short device check: one project, 8 voices with Shimmer + BBD + Glue, on today's release and on the
     plugin firmware, read from the diag log.
8. **The merge.** `CLAUDE.md`'s checklist against every mod, then the `masterfx-2.0` tag on the user's
   OK.

## 9. Decisions for the user

Answered 2026-11-14, with the go-ahead to build:
- 1: (a), CE's pages with up to 2 own knobs.
- 2: a missing plugin plays CE's Stock.
- 3: yes. CE's reverb, delay and limiter stay built in. If this ever goes to CE, its maintainers can decide
  whether to convert them.
- 4: replaced by the sidechain destination (section 5.1). It comes after the master compressor works.

- Sends: a destination track's sends aren't ducked.
- Several destination tracks: the user would like them if possible, and one destination is acceptable
  (section 5.1 has the shared-gain design).

The questions as they were asked:

1. **Knobs on the pages:**
   - (a, recommended) CE's pages as they are, with the plugin's labels, plus up to 2 own knobs in the free
     columns. The reverb and Limiter pages have columns 5 and 6 free; the delay page has none.
   - (b) A generic knob list like Synth FX's, with up to 12 knobs.
2. **A missing plugin:** play CE's Stock in that slot (recommended), or nothing.
3. **"Stock" means CE's own reverb, delay and limiter,** built in as now. Only MT+'s effects become
   plugins. Is that the meaning of "the stock ones shipped as plugins"?
4. **Several key tracks for the sidechain:** now, or after the compressor proof of concept
   (recommended: after).

## 10. Not checked

- Anything on the unit: cycles, stalls, the cache, the card's read time, the clear's length, ITCM code
  copied at runtime.
- The pack in the firmware's own path: one probe run only, outside the test tiers, with one knob setting.
- The display bytes on the unit: the emulator's count is an upper bound, from the display list's commands
  as the rig catches them.
- The multi-key mux's cost: estimated, not measured.
- Ducking: what a ducked track costs (the multiply, and the gain worked out in a track's update); a
  sample track ducked, with sound (the rig plays synth parts only).
- The pitch shifter's and clipper's sound: these prototypes were only costed.

The measurement sources (`mfx_plug.cpp`, `host_glue.cpp`, `dyn.c`, `measure*.py` and the build scripts)
are in the session's shared notes folder, not in the repository.

## 11. Master FX 2.1: Glue on chosen tracks, and Compressor (2026-11-15)

Branch `mod/masterfx-glue`, off `modular` at `250cd04` (Master FX 2.0, `masterfx-2.0`).

### What the user asked for

- **Compressor:** 2.0's Glue renamed, the same behaviour and knobs: CE's Attack, Release and
  Threshold and CE's Sidechain list; Ratio 1 to 20 by 0.5, Makeup 0 to 12; the Ducks list with
  Shift + Sidechain (in its own column since 2026-11-16, below). `Compressor.ptmfx`, "Compressor" in the list.
- **Glue:** a new bus compressor on the Limiter page: Attack 0.1 to 100 ms (10 ms), Release 50 ms
  to 2 s (200 ms), Threshold -40 to 0 dB (-12 dB), CE's Gain Reduce, a Tracks list (Master,
  Track 1 to 8, Synth 1 to 3; Master), Ratio 1:1 to 10:1 (4:1), Makeup 0 to +12 dB (0 dB), Back.
  Only the chosen tracks are compressed, together. Ranges, not a few steps; Attack and Release
  log-spaced, a step a jog-wheel click.
- **The pack on the card:** a `/MasterFX` file byte for byte one a release shipped is replaced,
  safely; any other file is left alone; a stray `.new` is removed at the start.
- Not done, by the user's choice: plugin IDs, and converting 2.0's projects (the firmware still
  reads their records).

### Design

- **Format version 2** (`ptmfx.h`): the header is 336 bytes, with 5 own knobs (`params` at 176);
  version 1 (240 bytes, 2 knobs) is still read. New flags: `PTMFX_FLAG_OWN_DYNAMICS` (0x4: knobs
  0 to 2 in the Limiter page's columns 0 to 2, 3 and 4 in columns 5 and 6; needs 3 knobs or more,
  and more than 2 need it) and `PTMFX_FLAG_BUS` (0x8: dynamics, with `FLOAT`, not with `DUCK`).
  The builder and `checkHeader` refuse the rest.
- **Record 4 version 3** (`masterfx.h`): 208 bytes, 3 × (48 B file + 5 knobs) + the Ducks or
  Tracks set + 2 reserved. Version 2 (172 bytes, and the 168-byte one from before the ducks) is
  read, its two knobs as knobs 0 and 1. A dynamics choice sets the set back to the master.
- **The bus** (`core/libs/Audio/mixer.cpp`, `masterfxDsp.cpp`): it runs in CE's tracks mixer
  (`AudioMixer<11>`, inputs 0 to 7 the sample tracks, 8 to 10 the synth parts, the same numbers as
  the ducks' destinations). With a bus plugin, a set and `busLive`, the left object's `update`
  (an MT: hook, `if constexpr` on the size, so only this mixer has it) takes both objects' blocks,
  sums the chosen inputs into the bus and the rest into the mix (CE's `applyGainThenAdd`, the
  mixer's own gains), runs the plugin on the bus (`busRun`), adds the bus back and saturates with
  CE's `saturate` and the project's type, as CE's sum would be. The right object then finds no
  blocks and sends nothing. The limiter passes the master through, sets `busLive` (the bus starts a
  pass later, with the limiter's knobs in `busKnobs`) and shows the plugin's gain. No memory of
  its own: the bus is summed as floats straight into the plugins' float scratch (exact: sums of
  16-bit samples, scaled by a power of two), the mix on the stack a side at a time (512 B, as
  CE's own mixer update has; the unit's max stack reading is about 7.8 KB of 20 KB), and the
  blocks are held in a 2 × 11 pointer array until the mix is made. A bus's float output is held
  within ±11 (the mixer's 11 inputs at full scale), not ±4.
  - A second version kept the bus and the mix in 2 KB of DTCM `.noinit`. CE's merge of
    `ed51301` takes DTCM free to 4,680 on its own, so that would have put the normal build under
    the 4,096 floor.
  - A first version took the bus at the panning amplifiers and put the master back at the limiter
    as master − bus + the plugin's bus. That is only right where CE's mixers are linear: CE's
    tracks mixer and dry mix mixer both saturate with the project's type, and Saturate is ×1.5 at
    low level in each, so a real project's drums reached the master about 2.2 times what the bus
    held, and Glue on them did too little. Found by the rig's project check (a booted project);
    the graph rig's synth checks had passed. CE's mixer update is in flash (its `AUDIO_CORE_RAM` doesn't reach the template's
    instances), and the hook is with it.
- **The page** (`gMasterParams.cpp`, `fMasterParams.cpp`): an `OWN_DYNAMICS` plugin's knobs take
  CE's Attack, Release and Threshold columns (`showMasterfxLimiterColumn`, the four limiter
  branches through `ownKnobAt`); the Tracks list is the Ducks list's control, always up, labelled
  "Tracks". No new controls, so the display list grows by the texts only: Glue's page 5,840 B of
  8,172 (upper bound), Compressor's 5,668 B.
- **The pack's replacement** (`writePackFile`): a missing file is written. A file that is already
  this pack's is left. A file that is one of 2.0's (name, size and FNV-1a hash in `kShipped`,
  `masterfxPack.inc`) is written beside it as `.new`, read back, then the old one removed and the
  new one renamed (written in place if the card can't rename); a bad read-back leaves the old
  file. A `.new` at the start is removed first, so a power cut at any step leaves the old file,
  the new file, or no file, which the next start writes.
- **Glue's DSP** (`masterfx-sdk/pack/glue.cpp`, written for this): each side through a ~35 Hz
  one-pole high-pass for the detector, the louder side's peak over 8 frames, a log2 gain computer
  with a 6 dB soft knee, attack and release smoothing of the turning down, the gain glided over
  the 8 frames with the makeup. Attack step i is 0.1 · 10^(i/20) ms (0 to 60), Release step i is
  50 · 2^(i/6) ms, held at 2 s (0 to 32).

### Cost

- **Memory** (`mem_budget.py`, against 2.0's build): DTCM 72 B more (7,840 → 7,768 free, floor
  4,096), ITCM 224 B (48,256 → 48,032). Stack: up to about 600 B more in the tracks mixer while a
  bus runs.
- **CPU** (`compare_cost.py`, instructions a block on the sound rig with 4 voices, against
  `d9a6795`; instructions aren't cycles):

  | Choice | Update | Today | 2.1 | Change |
  | :--- | :--- | ---: | ---: | ---: |
  | Compressor, no key | limiter | 14,570 | 16,511 | +13.3 % |
  | Compressor, a key | limiter | 20,984 | 18,420 | -12.2 % |
  | Glue on the master | limiter | 14,570 | 15,256 | +4.7 % |
  | Glue on the master | tracks mixer | 15,897 | 15,903 | +0.0 % |
  | Glue on Synth 1 | limiter + tracks mixer | 30,467 | 31,653 | +3.9 % |
  | Stock, or no bus | amplifiers | 3,360 | 3,586 | +6.7 % (as 2.0) |

  Glue's DSP alone: 6,155 instructions at most (`check_ptmfx.py`), 1,632 B of code, 48 B of state.
  The bus on top of Glue on the master: 494 instructions with Synth 1 in it, about 0.03 % of a
  block. Each further track that plays adds about 1,000 (a bus track is copied, then added as
  floats, where a mix track is added once), estimated from the loop, not measured: all 11 would be
  about 10,500, 0.6 %. The hook is in flash, as CE's mixer is; its cache misses on the unit aren't
  in these counts.

### Tests

All on the build from this branch before CE's `ed51301` came in (`250cd04` base); rerun after that
merge as below.

- **Host tests:** `test_masterfx` 104 and `test_masterfx_plugins` 174 pass; the other host tests 0
  failures.
- **`check_ptmfx.py`:** 10 passed: the pack's and the SDK's examples load, version 2 headers and the
  flags' rules, each plugin's worst block (Glue 6,155 instructions).
- **The rig (`test_masterfx_rig.py`):** ui 115, graph 33, dynamics 25, ducks 36 and bus 53 pass. The bus
  section: on Hard Clip, Glue that changes nothing leaves the master exactly as it was, and +6 dB
  on the drums or on Synth 1 is the master plus that part again, sample for sample; on Saturate
  (a real project's), the same against CE's Saturate curve, exactly; the project's own knobs squash
  the drums; M5 as the device project makes it plays with the bus live. The bus section wasn't in
  `run_tests.py`'s tiers; it is now.
- **`compare_cost.py`:** 0 failed (the table above).
- **`mem_budget.py`:** above its floors: DTCM 7,768 free (the diagnostic build 6,240), ITCM 48,032.
- **The release tier** (`run_tests.py --tier release`, with the diagnostic build and a real project):
  38 of 39 passed. `masterfx play` failed on its M2 load check, which held the output within 3 dB of
  Stock. That was written for 2.0's Glue on CE's knobs (now Compressor). M2's Glue is the new one at
  its defaults (-12 dB, 4:1, no makeup), and it turned that 8-voice mix, peaking at -1.5 dBFS, down
  by 3.2 dB: a compressor at those settings does that, the firmware is right. The check is now that
  Glue only turns the mix down, by less than the 9 dB 4:1 above -12 dB takes off a full-scale peak.
  Rerun with the new check: 49 pass (-3.2 dB both sides).
- **The user page:** its labels, ranges and defaults checked against `glue.cpp`, `compressor.cpp`,
  `gMasterParams.cpp` and `masterfx.cpp`. Glue's two mockups had the figure's name in the screen's
  title bar; it is "Limiter: Glue" now, as the firmware and the ui rig have it.

### The Ducks list's own column (2026-11-16)

The user's request: on Compressor's page the column above Back was empty; put the Gain Reduce meter
there, still labelled "Gain Reduce" and with "Back" at the bottom, so the Ducks list can have its
own column.

- With a ducking plugin (`canDuck`, not a bus plugin), `showLimiterScreen`'s MT: hooks put the
  Ducks list in column 3, in the Delay page's PingPong list (moved there as the Master page moves it
  for the Limiter choices; `showDelayScreen` moves it back), always up. CE's Sidechain list stays in
  column 4, and Shift + its button is CE's again. The Gain Reduce meter is column 7's bar, CE's
  meter style; label 7 says "Gain Reduce" over "Back" (its two lines), and button 7 is still Back.
  `resizeBarControl` gives column 7's bar CE's style back on the next page. The cover goes off,
  as Ratio and Makeup fill columns 5 and 6.
- Column 3 is a place: Left and Right stop on it, the encoder and Up/Down move the cursor, Delete
  clears, and the button under it selects it, then marks. A new `setButtonObj` for button 3 on the
  Limiter page; it does nothing without the Ducks list there.
- Glue (a bus plugin) is as before: Gain Reduce in column 3, the Tracks list in column 4.
- The plugins aren't changed: the pack is byte for byte the tested one (`Compressor.ptmfx`
  included), so the pack's replacement and its hashes don't change.
- The tested state before this is commit `be7827f`, also the local branch
  `backup/masterfx-2.1-glue5`.
- What ran on this build (before CE's ed51301): the host tests, every suite 0 failures; the rig's
  ui section 120 pass, ducks 38 pass (rewritten for column 3); the release tier with the diag
  build, 0 failed of 39 run; `check_mockups` 0 failures; `mem_budget` DTCM 7,760 bytes free (8 less
  than before), diag DTCM 6,232, ITCM 48,032. What didn't: the unit. On the unit, still to check:
  that the two-line Gain Reduce over Back label reads well, the page's real display bytes, and the
  meter moving above Back.

### Sidechain before Ducks, and no "Limiter" on a plugin's page (2026-11-16)

The user's corrections to the layout above: the source comes before what it turns down, so CE's
Sidechain list is column 3 and the Ducks list column 4; and a plugin that isn't the stock limiter
doesn't say "Limiter" in its controls.

- With a ducking plugin, `showLimiterScreen` moves CE's Sidechain list to column 3 (its position
  is set each time the page opens: column 4 otherwise), and the Ducks list (the moved PingPong
  list) is column 4. Column 3's button is CE's Sidechain button in its place
  (`functSelectLimiterSidechain3`), and the encoder, Up/Down and Delete do CE's Sidechain changes
  there; column 4's button, encoder, Up/Down and Delete work the Ducks list, as they work Glue's
  Tracks list. `masterfxSidechainColumn()` says where CE's list is: 3, 4, or 0 when a bus plugin's
  Tracks list has its control.
- A Limiter plugin that doesn't name CE's knobs gets "Attack", "Release" and "Threshold"
  (`showMasterfxLimiterWords`), and a Limiter plugin's page title is its name alone ("Compressor",
  "Glue (bypassed)"). Stock keeps CE's words and "Master Limiter"; a missing file's title still
  reads "Limiter: Missing: …", as CE's limiter is what plays then. The Master page's column
  heading "Limiter" is CE's slot heading for every choice, and stays.
- The plugins and the pack aren't changed.
- What ran (before CE's ed51301): the host tests, 0 failures; the rig's ui section 121 pass (mod
  Off still makes exactly the reference build's display calls: the Sidechain list moves only when
  its column changes) and ducks 42 pass (CE's Sidechain button, encoder and Delete in column 3, read
  from `setLimiterSidechain` as the rig's project has no track names; the page 6,996 bytes of
  8,172); `check_mockups` 0 failures. Not run, by the user's choice: the release tier, which waits
  for the CE merge.

### modular with CE f25fb89 merged in (2026-11-16)

`modular` (`6301640`: CE's `ed51301`, `cc53303` and `f25fb89` merged, 23 CE commits) merged into
this branch. Each CE change against what 2.1 adds:

- **The bus hook (fixed).** CE's 7482cc8 rewrote the mixer: with `AUDIO_NULL_IS_SILENCE` (on, in
  `AudioStream.h`) `AudioMixer::update` calls `AudioMixerBase::mix`, and the old update is under
  `#else`. git merged 2.1's MT: hook into the `#else` branch only, with no conflict, so Glue on
  chosen tracks would have done nothing. The hook is now in the built update too, and
  `modsBusUpdate` sets an input's `previousMultiplier` when it has no block, as CE's `mix()` does
  (its gain ramp is finished; `isInputSilenced` reads it). The rest of `modsBusUpdate` already
  takes a missing block as silence and sends nothing on a side with none, as `mix()` does; the
  plugin still runs on a silent bus. `nm`: `AudioMixer<11>::modsBusUpdate<11>()` is in the image.
- **The dynamics slot over silence:** `modular`'s `AudioEffectLimiter::modsUpdate` runs the plugin
  on a zeroed block when the master sends none. Both of 2.1's dynamics plugins go through it: the
  rig's silence check now runs Compressor and Glue (below), and `dynamicsProcess` still sets
  `busLive` on every block.
- **Settings in SDRAM** (`modular`'s DTCM fix): 2.1's 208-byte record is there too, emptied by
  `masterfx::init`. Read and written in the main loop only, as before.
- **The display lists** (ed51301): the Limiter page uses CE's controls only; the rig's upper bound
  is as before (6,996 of 8,172 bytes on Compressor's page).
- No other CE change touches 2.1's code.

Tests changed, each for what the firmware does:
- `test_masterfx_rig.py` dynamics: `modular`'s silence check was written for 2.0's Glue (2.1's
  Compressor). It runs on Compressor as before, and on the new Glue with 350 silent blocks: Glue's
  Release is 200 ms, and after 19 blocks it is still turning down (the next note 4.7 dB down; 2.6 dB
  after 59 blocks with its fastest Release; 0.06 dB after 349), which a frozen level wouldn't do.
- `compare_cost.py`: `modular`'s plain-CE Stock row and 2.1's Compressor and Glue rows together;
  "each update runs" is now over 500 instructions, as CE's tracks mixer costs about 750 here.
- `make_masterfx_test_project.py --masterfx-compressor` writes `M6 1 voice ACD Compressor duck`:
  Compressor ducking Synth 1, CE's Sidechain on Track 1 and Threshold -20 dB in project.mt (0x4FB
  and 0x128, from `cFileManager::writeProjectFileToFileStruct`). Booted and played in the emulator:
  ducking live, the synth down to about 1 % on the kick and under 70 % for about 96 ms.

What ran, on `TrackerPlus_MT_ce1116` (mods), `TrackerPlus_ref_ce1116` (MT_NO_MODS, the reference)
and `TrackerPlus_MT_ce1116_diag` (the lockup recorder), all from this tree:
- The build with -Werror; host tests, 14 suites, 0 failures; `check_mockups` 43 images, 0 failures.
- `mem_budget.py`: DTCM 4,136 free (floor 4,096; `modular` alone 4,176), ITCM 39,808, SDRAM 610,824,
  Flash 14,964,404.
- The release tier: 53 jobs, 51 passed; the two failures were the tests above, which then passed.
  The jobs that import the project maker ran before M6 was added to it (an added function only).
- `verify_card.py` on M2, M3, M5 and M6 with the recorder image: Glue (Attack 10.0 ms: the new one)
  in M2, Glue on the drums in M5, Compressor in M6, GuardSlow in M3; the titles without "Limiter".
- Not run: the MT_NO_MODS image against plain CE f25fb89 byte for byte (`modular`'s merge did
  that; 2.1 adds only `MT_HAS_MASTERFX` code).

### Not checked

- Anything on the unit: the sound of Glue and Compressor, the cost in cycles, the pack replaced on a
  real card, the Gain Reduce meter above Back, Back itself. The device list has each.
- The cost with more than one bus track playing: estimated from the loop, measured with one.
- The display bytes on the unit: the emulator's figure is an upper bound.
- Glue's sound against a reference compressor: none was used; the rig checks what it does
  (squashes, lifts, leaves the rest), not how it sounds.
