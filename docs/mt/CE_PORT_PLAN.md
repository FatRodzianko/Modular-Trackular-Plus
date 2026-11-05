# Porting the mods to Tracker CE: build plan

Status (2026-11-02): step 0, step 0b (per-project mod sets, section 1.12) and chords
(section 3.1) hardware-tested and committed. Base: Polyend Tracker CE v1.0.0 (`9b9a13b`) in the fork `FatRodzianko/Modular-Trackular-Plus`, branch `modular`.
Background, licence and the per-mod survey: [TRACKER_CE_REVIEW.md](TRACKER_CE_REVIEW.md).
Line numbers below are for CE v1.0.0 and only show where to look; they move with
every CE release.

## Summary

- **One build with every mod in it, each switched Off/On per project.** No more 2^N
  mixes, web patcher, layout tests, RAM regions or hook chains. Compile flags stay as
  a back door to leave a mod out of a build, not as a user choice.
- **Off means exactly CE.** Every mod is a small call from a CE file into our own
  file, behind `if (mods::on(...))`. Off falls through to CE's own code. A golden test
  compares an all-off build with plain CE.
- **Per-project settings use CE's own project files.** One `mods.bin` per project, in
  the Workspace like the synth patches, copied in on Open and out on Save and Save
  As. Save As, New Project and Save As over another project then work by design:
  the bug class fixed mod by mod on 1.2.0 doesn't exist here.
- **Each project has its own mod set** (which mods are On), saved in its `mods.bin`
  with the settings. A device-wide default set in `Mods/mods.cfg` (our own file, not
  CE's config struct) is what New Project starts with. Project templates, later,
  carry both (section 1.12).
- **Order:** step 0 (groundwork, no mods), then chords, condtrig, swing, humanize,
  copypattern, tracklen, legato, interpolate, songloop, colorsong, visualizer,
  slicethru Loop, masterfx, resample, synths. One mod at a time: port, test,
  hardware check, merge, then the next.
- **Eight decisions are yours**, listed in [section 5](#5-decisions-for-you). None
  block step 0.

## 1. The modular approach on CE

### 1.1 Why runtime switches

The review weighed three models ([section 3](TRACKER_CE_REVIEW.md#keeping-pick-and-choose-three-models)).
Runtime switches win on CE because:

- The user downloads one `.ptf`. No per-selection builds, no build server.
- Testing is one build with switch states as inputs. Today's release tier spends most
  of its 43 minutes proving that 2^15 binaries were put together right; on CE that
  question is gone.
- CE has room: about 21 KB DTCM, 22 KB ITCM and 1 MiB of SDRAM set aside, against
  832 bytes of startup RAM on 1.2.0. All 15 mods added 28 KB of RAM on 1.2.0. Paying
  for every mod's code and state at all times is affordable. Figures:
  [CE_FIRMWARE_MAP.md](CE_FIRMWARE_MAP.md) section 15.
- A mod costs one switch, not a doubling.

What it costs: a mod's code is always in the image, and its Off path must truly be
CE's path. Section 1.8 is how that's checked.

### 1.2 Code layout in the fork

```
core/mods/                 our code; MIT header on files that are wholly ours
  mods.h, mods.cpp         ModId list, on/off bits, names, defaults
  modsConfig.cpp           Mods/mods.cfg read/write
  modsProject.cpp          per-project mods.bin: records, Workspace copy, dirty flag
  modsImport.cpp           reads 1.2.0 side files (swing.bin …) once
  chords.cpp/.h            one pair per mod
  condtrig.cpp/.h
  …
core/mods/tests/           host tests (built and run in CI with the host gcc)
```

- **Hooks in CE files are one or two lines each**, marked `// MT:` so a grep lists
  every place we touched. The logic lives in `core/mods/`. Example shape: in
  `getChordBitmask`, `if (mods::on(Mod::chords)) return chords::bitmask(chord);`
  before CE's own code.
- **Compile flags.** Each hook also sits in `#if MT_HAS_CHORDS` (default 1, set in
  `core/mods/modsBuild.h`, overridable through the makefile's `CUSTOM_DEFINES`).
  A build with a flag at 0 has no trace of that mod. Used for a debug build, for
  bisecting, or if a mod ever costs too much to ship always-in.
  `CUSTOM_DEFINES=-DMT_NO_MODS` leaves every mod out: built with `EDITION_NAME=CE
  BUILD_TAG=9b9a13b` it gives Polyend's CE 1.0.0 `.ptf` byte for byte (checked with
  chords in), so every hook really is compiled out. It is the plain-CE reference the
  emulator tests compare against, with `MT_NO_SYNTH=1` so far and a full build for the
  sound tests. Until polyend/tracker-ce#52 is merged, `modular` also has that PR's CHIP
  fix, so this build matches Polyend's `main` plus `ChiptuneSynthVoice.cpp` from the PR,
  not `main` alone. Merging `main` after the PR lands ends the difference.
- **Licence.** Files under `core/mods/` are ours and can carry MIT. The one-line
  hooks are changes to CE files and fall under CE's licence, as does the fork as a
  whole (review section 1). Nothing from the fork goes back into Modular-Trackular,
  which stays MIT and Polyend-free.

### 1.3 The switch registry

- `enum class Mod : uint8_t` with fixed numbers that are never reused, in today's
  web-patcher bit order where it exists, so old docs stay readable.
- A table entry per mod: id, Config name (fits the menu width), default, flags:
  - `live`: can change while playing (most);
  - `idleOnly`: switches when playback stops (masterfx, resample: they change audio
    routing);
  - `hidesOnly`: Off only hides it from pickers, data stays usable (synths: an
    instrument already set to WAVE keeps playing; the model picker no longer offers
    it).
- `mods::on(Mod)` is a bit test on one `uint32_t` in DTCM holding the open
  project's set. Cheap enough for the per-microstep paths (`AUDIO_CORE_RAM`
  functions in the sequencer).

### 1.4 Config screens

- **Config → Mods**, a new top-level group (`baseMenuGroupCount` 7 → 8 for
  `PLATFORM_IMX` in `configMenu.cpp` L21–32; in step 0). It holds the **defaults for
  new projects**: one Off/On item per mod, A–Z, built with the
  `menuTypeItemListTextWithAction` pattern CE uses for its own switches
  (`melSnapToZeroMode`). The action stores the bit and writes `mods.cfg`. A last item
  shows how many mods the build has.
- **Config → Project Settings → Mods**: the same list for the open project, then a
  "Defaults: Config > Mods" line saying where the defaults are (step 0b). Changing one
  marks the project changed, like any other project setting. It is CE's first group
  inside a group: CE's menu code handles it (Enter goes in, Back comes out) but CE
  itself never used it. With chords, each Config → Mods item's description says it is
  the default for new projects.
- **Per-project settings stay in Config → Project Settings**, where the 1.2.0 users
  know them (Global Swing, Humanize, Interpolate, Master FX, Slice Thru, Resample):
  new groups under `menuProject` (`projectMenuGroupCount` grows by one per group).
- **A switched-off mod's settings group.** CE's menu items are static objects and the
  menu headers show no hide/disable flag. Step 0 checks the menu code; if hiding
  isn't there, the group stays and shows "(mod off)" as its first line.

### 1.5 Defaults file

- `Mods/mods.cfg` on the SD card, in the same record format as `mods.bin` (magic
  `MTC1`): a 12-byte header (magic, format, payload length, CRC-32 of the payload),
  then records `{id, version, length, data}`. Record 0 holds one Off/On byte per mod:
  the set New Project starts with. Other records can hold device-wide settings later.
- Missing or damaged: every mod Off (decision 1), and the file is rewritten on the next
  change. Unknown records, and switches of mods a build doesn't know, are kept.
- Written when a switch changes, straight from the Config action: CE reads and writes
  small files synchronously from the main loop (its pattern-name file does the same),
  so 1.2.0's "SD only from a screen update" constraint is gone.
- Not in `strMtConfig`: CE says to add fields only at the end of each sub-struct,
  and every upstream change to it would collide with ours.

### 1.6 Per-project data: `mods.bin`

One file per project instead of eight side files.

- **Format:** as `mods.cfg` with magic `MTP1`. One CRC covers the whole file: a
  damaged file means defaults for every mod (SD damage is rare and per-record CRCs
  weren't worth the code). A mod's record is absent until its settings leave the
  defaults. Unknown records are kept and written back, so an older build saving a
  newer build's project loses nothing.
- **Record 0 is the project's mod set**, one Off/On byte per mod, same layout as
  `mods.cfg`'s record 0. A mod the record doesn't cover (added by a later build, or
  a project from before step 0b) is Off: a project opens as it was saved.
- **A project saved by this firmware always has a `mods.bin`**, even an empty one.
  Without that, a project whose settings went back to defaults would re-import its
  old 1.2.0 side files on every open.
- **Lives in the Workspace while the project is open**, exactly as CE handles synth
  patches. Hooks:

| When | CE place | What we add |
|---|---|---|
| Open | `ProjectOpen.cpp` L358, after `copyWorkspacePatches(src, cWorkspacePath)` | copy `<project>/mods.bin` → `Workspace/mods.bin`, then load it into RAM; if it doesn't exist, run the 1.2.0 importer (below) |
| New Project | `createNewProjectInWorkspace` (`fileManagerWorkspace.cpp`), which already calls `clearWorkspace` | mod set from `mods.cfg`, settings at defaults |
| Save (any) | `saveProjectToWorkspaceFinish` (`WorkspaceProjectSave.cpp` L119–122), beside `saveDirtyWorkspacePatches` | write RAM → `Workspace/mods.bin` if dirty |
| Save, Save As, Save As over another | `saveProjectFinish` (`ProjectSave.cpp` L509–545), beside `copyWorkspacePatches(cWorkspacePath, dest)` | copy `Workspace/mods.bin` → `<dest>/mods.bin`, replacing any old one |

- **Changing a setting marks the project changed**, through CE's own flag, so
  "unsaved changes" warns and Discard really discards. This differs from 1.2.0,
  where settings saved themselves a moment after a change (decision 2).
- **Size:** settings are a few bytes each. The largest records: colorsong (up to
  ~5 KB), tracklen if kept here (decision 5), slicethru Loop bits (6 bytes).
- **Plain CE** opening a project saved with mods ignores `mods.bin`. CE's Save As
  doesn't copy it (it copies only its own files), so the copy made by plain CE loses
  the mod settings. Acceptable: nothing breaks.

**Chords and condtrig in old projects.** Their values live in the patterns, so no side
file shows them in use. Scanning a project's pattern files on Open is left out for now:
a 1.2.0 project with chords 30–69 opens with Chords Off (those steps play the note
alone, as plain CE) until Chords is turned on in Project Settings → Mods.

**Importing 1.2.0 projects.** On Open, when a project has no `mods.bin`, read each
1.2.0 side file that exists (`swing.bin`, `humanize.bin`, `interpolate.bin`,
`masterfx.bin`, `slicethru.bin`, `resample.bin`, `tracklen.bin`, `colorsong.bin`,
`synths.bin`), check its own checksum, convert, mark the project changed, and turn
that mod On in the project's set (the rest stay Off; the user turns more on in
Project Settings → Mods). Mods whose data is in the patterns (chords, condtrig)
switch on when the importer finds their values there. The old files stay. Their formats are ours, documented in the patch READMEs. Pattern contents
are converted at load time (condtrig, below).

### 1.7 Memory

- Switch bits and small per-mod state: DTCM (about 21 KB free). Keep each mod's hot state
  under a few hundred bytes.
- Code on hot paths (sequencer microstep, note on/off, audio callbacks): ITCM with
  CE's `AUDIO_CORE_RAM` placement where CE's caller already lives there (~22 KB free).
- Main-loop tables: SDRAM, with `__NOINIT(EXTERNAL_RAM)`. Startup doesn't zero it, so
  the owner does. Mod builds lower `SAMPLE_MEMORY_SIZE` by 1 MiB for this (decision 3).
- 1.2.0's mods had no big buffers of their own: masterfx uses the stock delay and reverb
  buffers, resample the free part of sample memory, and synths keep wavetables in flash.
- `tools/mem_budget.py` reports real free space and fails under a floor. Each mod's section
  states its cost. Rules: `CLAUDE.md`, Memory.
- Memory is reserved whether a mod is on or off. Simple and deterministic; Off costs
  RAM, not CPU.

### 1.8 Off = CE, and how it's proven

- Every hook's Off branch is CE's original code, untouched (the `if` falls through).
- **Golden test:** the same scripted sessions run on plain CE (`main`) and on
  `modular` with every switch Off: same MIDI out, same sequencer events per tick,
  same files written, same audio blocks. Run on every merge into `modular`.
- **Project round trip:** a project made with mods Off has no `mods.bin` and
  byte-identical patterns.
- **What plain CE does with mod data** (checked in CE's code):
  - chord values 30–69: the root note only (`getChordBitmask` maps past `MAX_CHORD`
    to `MIN_CHORD`);
  - Chance values 175–211: always play (`evaluateChanceFx` ends in `return true`);
  - `mods.bin`: ignored.

### 1.9 Name and version

- `EDITION_NAME` in `version.mk` (decision 4, e.g. `MT`), so Config → Info shows
  CE's version plus ours.
- `.ptf` name starts with `TrackerPlus` (the updater lists only those), at most 31
  characters: e.g. `TrackerPlus_MT_1.0.0-1.ptf`.

### 1.10 Build, branches, CI

- `main` stays the untouched mirror of polyend/tracker-ce. `modular` is the
  integration branch. Each mod gets `mod/<name>`, merged into `modular` after its hardware
  test passes. No PR unless one is wanted for a review or a CI run.
- `build.yml` today builds on pushes to `main`, `v*` tags, PRs to `main` and manual
  runs. Add `modular` to the push and pull_request branches (a two-line change), and
  upload the `.axf` and `.map` beside the `.ptf` (the emulator needs the symbols),
  plus `LICENSE` and `NOTICE.md`.
- CI is the reference build; its output was shown byte-identical to Polyend's
  release. Local builds: CE pins GNU Arm 10.3-2021.10; the toolchain next to this
  repo is 15.3, which may build but won't match CI byte for byte. Use CI for
  anything the user flashes.
- `v*` tags auto-publish a GitHub Release (decision 8 covers our tag names).
- **Upstream releases:** update `main` from polyend/tracker-ce, merge `main` into a
  branch off `modular`, fix the `// MT:` hooks that moved, run all tests, hardware
  check, merge.

### 1.11 Testing

Three layers replace today's tiers.

1. **Host tests** (CI, Linux gcc, seconds): our logic with no CE headers where
   practical: chord tables, condtrig decisions per pass, humanize ranges, interpolate
   ramps and wrap, swing values, colorsong colour allocation, `mods.bin`/`mods.cfg`
   read and write (damaged files included), every 1.2.0 importer against real
   sample files.
2. **Emulator** on the CE `.axf`: `tools/emu`'s Unicorn machine ported to CE. Same
   chip, so the memory map carries over, checked against CE's
   `TrackerPlus_memory.ld`. Functions found by symbol name, never by address. Drives
   the sequencer (`Sequencer::tick`), file hooks with `fakefs`, and screen drawing
   with `eve.py`. **Synths are included:** since CE `9cc7aae`, section 1(b) of the closed
   libraries' licence allows running them here, so tests run on full builds (your call,
   2026-11-04). Section 2(c) still bans reverse engineering, so closed engines are a
   black box. `MT_NO_SYNTH=1` builds stay available but can't test engines. Sound
   checks: [SOUND_TEST_PLAN.md](SOUND_TEST_PLAN.md), built in stages with the mods
   that need them.
3. **Hardware checklist** per mod, by you, in the usual scope (no Break FX or
   Performance mode). CE has a built-in `Profiler` (`core/source/Profiler.cpp`) for
   CPU checks on the device, which replaces the timing-headroom estimates.

**Switch matrix**, not mix cover sets: all Off (golden), all On, each mod alone,
and pairs that share a hook: chords + legato (note order), condtrig + tracklen
(pass counts per track length), tracklen + copypattern, swing + humanize (timing),
interpolate + tracklen (ramp wrap at a track's own length), humanize + colorsong
(colours ignore `m`/`V`).

### 1.12 Per-project mod sets, defaults and templates

Asked for on 2026-11-02, after the step-0 hardware check.

- **Which mods are On is part of the project**, not the device. Two projects can use
  different mods, and a project sounds the same wherever it's opened.
- **Defaults for new projects** in Config → Mods (`mods.cfg` record 0). New Project
  copies them into the project's set. Changing the defaults never touches existing
  projects.
- **Old projects** (plain CE, 1.2.0 or step 0) open with every mod Off, except mods
  the importer finds in use (section 1.6).
- **Turning a mod Off in a project keeps its data**: its record stays in `mods.bin`
  and pattern values stay, so On again brings everything back. While Off, CE's
  behaviour applies (section 1.8).
- **Compiled-out mods** (`MT_HAS_*` at 0) keep their bytes in both files, like
  unknown records, so a full build later sees them again.
- **Project templates (a later feature, after the ports or when wanted).** A CE
  project folder already holds everything: patterns, instruments, synth patches,
  `mods.bin`, and the samples themselves (CE copies each into
  `<project>/samples/instrNN.wav`). So a template is a project folder kept under
  `Templates/<name>/`, and it carries its samples as copies, not as links to files
  elsewhere. "Save as template" saves the project there; "New from template" loads it
  into the Workspace like Open but leaves the project unnamed, like New Project, so
  the first save asks for a name and the template stays as it was. The template's
  `mods.bin` brings both the mod set and the settings. To check when it's built:
  where CE keeps track names (expected in the project file) and the screen to put the
  two actions on.

**Step 0b** (before chords, small): `mods.cfg` record 0 becomes the defaults; the
project store gets record 0 (`modsSwitches.cpp` reads and writes it for both files);
`mods::on` reads the project's set; New Project copies the defaults; importers say
whether the project used their mod. Config → Project Settings → Mods holds only the
"Defaults" line until chords. Host tests cover each case above. On the device the only
change is that group; the switches themselves get their hardware check with chords.

## 2. Step 0: groundwork, before any mod

Deliverable: a `modular` build with an empty Config → Mods group, `mods.cfg` and
`mods.bin` working, and the golden test passing. No mod yet.

1. Local clone of the fork (see "What I need from you" in the chat) and a `modular`
   CI build on each push (`build.yml` change).
2. `core/mods/` skeleton: registry, Config group, `mods.cfg`, `mods.bin` with the four
   Workspace hooks, importer framework (no importers yet).
3. Emulator port to the CE `.axf`; golden test harness (plain CE vs all-off).
4. Host test target in CI.
5. Checks in CE's code that this plan leaves open:
   - menu items: hide or disable support;
   - file writes: screen-update constraint or file-manager queue;
   - `FEATURE_SYNTH`-less test build;
   - SDRAM share from 1.2.0's sizes (done late: see decision 3);
   - where CE marks a project changed (for settings edits).
6. **First hardware flash of anything CE.** You held off on trying stock CE; this is
   the point to do it: back up the SD card, flash plain CE `1.0.0` from Polyend's
   release, check it, then the step-0 build, then roll back to 1.2.0 if you like.

Effort: the largest single step, about as much as two mid-sized mods.

## 3. Mod by mod

Each entry: what changes in CE, our code, data, old projects, tests, hardware check,
effort (S, M, L as in the review). The behaviour itself is as in each
`patch/<name>/README.md` unless noted.

### 3.1 chords

**Retired 2026-11-04.** CE merged these chords itself (polyend/tracker-ce#22, values
30-67), so the mod was removed and its hooks went back to CE's code. CE's numbers differ
from the mod's from 34 on, so 1.2.0 projects using those values play CE's chords. The mod
id stays unused. The notes below are what was built.

- **CE today:** 30 chords, `MAX_CHORD = 29` (`mtChordDefs.h` L5).
- **Hooks:** `getChordBitmask` and `getChordBitmaskForArp` (`mtSequencer.cpp`
  L2326–2341): values 30–69 from our table; `getFxMax` (`mtSequencer_patternEdit.cpp`
  L1745) for the Chord FX: 69 when on; the Chord FX value text in the grid and the
  FX popup: our names.
- **Our code:** the chord table and inversion logic from `patch/chords/chords.c`
  (ours), unchanged in meaning: values 30–69 keep their 1.2.0 numbers.
- **Data:** in the pattern. No settings.
- **Old projects:** values carry over as they are.
- **Off:** encoder stops at 29; stored values past it play the root (CE behaviour).
- **Tests:** host: table and bitmask per value; emulator: notes sent per chord value,
  arp order; grid text.
- **Hardware:** a few chords, inversions, Arp, on sample and MIDI tracks.
- **Effort:** S. First because it proves the switch, a pattern value range, the
  encoder limit and grid text with almost no risk.
- **Built (2026-11-02):** `core/mods/chords.h/.cpp` (the 1.2.0 table's values 30–69). CE
  already plays, ends and arpeggiates a chord from its mask, so the hooks are small:
  `getChordBitmask` and `getChordBitmaskForArp` take the mod's mask for 30–69;
  `changeSelectionFxValue` lets the encoder go to 69; `makeFxValLabelWithAlgin`
  writes the intervals. Pads, Fill, random fill and the Random FX value keep CE's
  0–29, as on 1.2.0. Re-picking the Chord type in the FX picker clamps a value past
  29 to `57B`, as CE does (not hooked). Host tests: the list against CE's chords'
  inversions and the 1.2.0 numbers (27 checks). Emulator, against the plain-CE reference: the notes
  sent and ended for every value in both FX columns, Off and On; the text; the
  Config lines and their files (18 checks). On the device: the encoder and the Arp
  (the emulator can't reach them by symbol).
- **Hardware-tested (2026-11-02):** both Config lines, the encoder limit Off and On,
  chords and their OFF, the Arp in either column, save and reopen, Off and back On,
  New Project defaults. With Chords Off, an Arp on a value past 29 plays every
  semitone: CE reads an unknown chord as chord 0, which the Arp plays chromatically.
- **Inversions only (2026-11-03, user's call):** the list stops at 69: the 40
  inversions (the triads and 7th chords, then the other inversions of CE's chords).
  The 1.2.0 patch's 70–236 (every other chord of one to three notes within an
  octave) are left out, as 200-odd encoder steps past `57B` is too many for one FX.
  70–255 play as plain CE (the note alone), so a 1.2.0 project that used them plays
  those steps as the note alone. 30–69 keep their numbers, so 1.2.0 projects and a
  possible upstream PR agree. Hardware-tested 2026-11-03: the encoder stops at `168`,
  the chords and the Arp on them play, both Config lines.

### 3.2 condtrig

- **CE today:** its own conditions on Chance 101–174 (`GL`, `!L`, Play N/Skip M,
  Skip N/Play M), not the same as ours (`1st`, `!1s`, `A:B` = pass A of every B).
- **Values:** ours move to 175–211 (`1st` 175, `!1s` 176, `1:2` … `8:8` 177–211,
  same order as today). The encoder goes on past CE's 174 into ours when on.
- **Hooks:** `evaluateChanceFx` (`mtSequencer.cpp` L2174): 175–211 decided by
  `playerRow.passCount`, setting `lastCondFired` like CE's own so `GL`/`!L` can follow
  ours; `getFxMax`: 211 when on; Chance value text (`gPatternEdit.cpp` L1737–1768).
- **Pass counts:** CE already counts passes per track (`switchRowStep`). With
  tracklen, a track's pass ends at its own length, as on 1.2.0.
- **Old projects:** 1.2.0 patterns are format 5. Stock 1.2.0 never stores Chance
  above 100, so 101–137 in a format-5 pattern can only be condtrig's. After CE's
  `translatePatternFromFileVersion5ToFileVersion6` (`fileManagerPattern.cpp`
  L327–330, which doesn't touch FX values), remap Chance values 101–137 → 175–211 (+74) in both FX slots.
  This runs whatever the switch says: it's a data fix, and with condtrig Off the
  values then play always instead of being misread as CE's `GL`.
- **Off:** encoder stops at 174; 175–211 play always.
- **Tests:** host: decisions per pass; emulator: a pattern over 8 passes, mixed with
  CE's conditions; v5 remap on a real 1.2.0 project.
- **Hardware:** `1st`, `!1s`, a few `A:B`, mixed with `GL`.
- **Effort:** M. Second because it proves the pattern-upgrade hook.

### 3.3 swing (Global Swing)

- **CE today:** swing resets to 50 on Stop (`Sequencer::stop` →
  `player.swing.resetSwing()`, L1558) but not at pattern starts; a Swing FX on a
  muted track is skipped (`play_microStep` returns early for muted tracks, L390).
  So half of the 1.2.0 problem is gone.
- **Hooks:** at play start and at each pattern start (where `switchRowStep` handles
  the last row, L1705, and song mode's next pattern, L369/L781): set the project's
  amount with `swing.setSwing` and the long step first. Pattern-start hooks run in the
  audio interrupt, so the amount must already be in RAM: no `mods.bin` read and no
  `double` there.
- **Data:** `mods.bin` record: on/off, amount. Project Settings → Global Swing.
- **Old projects:** `swing.bin` import.
- **Tests:** emulator: tick timing per take and per pattern start, Swing FX inside a
  pattern, song mode.
- **Hardware:** as the 1.2.0 checklist; plus Save As, New Project → Save As (now
  by design).
- **Effort:** S. Third because it's the first per-project setting: proves
  `mods.bin`, Project Settings items and the import path.

### 3.4 humanize

- **CE today:** dedicated per-step `m` (micro-move) and `V` (volume) columns, and
  Fill → Random over a selection.
- **Hooks:** a Humanize action button on the Pattern screen's action pages
  (`fPatternEdit.cpp` L5108–5154); the action writes random `m` and `V` into the
  selection, through CE's pattern-edit helpers, with `storePatternUndoRevision` first
  so undo works.
- **Change from 1.2.0, for the better:** FX slots stay free (no more micro-move and
  volume FX).
- **Button placement:** the first mods button, placed at run time on the first free
  button of the Pattern action pages (your rule). A small `mods::patternButtons()`
  list that every button mod registers into; enabled mods take the free buttons in
  order. Check CE's page 3 has free buttons (button 0 Name Pattern, 1 Name Track, 7
  next page are taken).
- **Data:** `mods.bin` record: timing range, volume on/off, volume range, MIDI
  velocity and sample volume defaults. To decide during the port: whether the two
  defaults still mean anything now that every step has a `V` value.
- **Old projects:** `humanize.bin` import; old micro-move/volume FX stay where they
  are (CE plays them).
- **Tests:** host: ranges; emulator: selection result, undo, button placement with
  other button mods on/off.
- **Effort:** S–M.

### 3.5 copypattern (Copy to New)

- **Port after the 1.2.0 version is final** (in progress now).
- **CE today:** Duplicate doubles a pattern's length; Copy/Paste Pattern exists.
- **Hooks:** a Copy to New button (dynamic placement); the action copies the open
  pattern, unsaved edits included, to the first free pattern number through CE's file
  manager (Workspace pattern file plus pattern metadata), then opens it.
- **With tracklen:** lengths come along (per decision 5).
- **Tests:** emulator: copy with unsaved edits, all slots full, button placement.
- **Effort:** S.

### 3.6 tracklen (Variable Track Length)

- **Port after the 1.2.0 version is final.**
- **CE today:** every track is forced to track 0's length: `updateTrackLengths`
  (`mtSequencer.cpp` L3092) and in several file paths (`fileManagerPattern.cpp`
  L485, L549, L583, the last for Performance mode). Performance mode alone has
  per-track lengths at play time.
- **Hooks:** `switchRowStep` (L1695–1860): each track wraps at its own length (CE's
  Performance-mode code shows the shape); `updateTrackLengths`: skip the forcing when
  on; Pattern grid: grey ghost loop past a track's end (`gPatternEdit.cpp`); live
  recording: `getDestRecordingStep` (L2523) per track length; Trk Len button
  (dynamic placement).
- **Where lengths live (decision 5):**
  - in the pattern's own per-track `length` field: natural, copies with the
    pattern, plain CE plays it flat; but CE writes that field in several places, and
    an edit saved while the mod is Off flattens it;
  - or in a `mods.bin` record per pattern, as `tracklen.bin` on 1.2.0: patterns stay
    pure CE; copypattern and CE's Copy/Paste must carry it.
  Recommendation: the `mods.bin` record, to keep Off = CE including the files.
- **Old projects:** `tracklen.bin` import.
- **Tests:** emulator: polymeter over many passes, with condtrig and interpolate;
  recording; song mode.
- **Effort:** M.

### 3.7 legato

- **What it does:** G = legato order only on MIDI and synth tracks: the new note's
  note-on before the old note's note-off (no portamento CCs, your call).
- **Hooks:** the note path in `play_microStep`, around `sendNoteOff` (L2472) and
  `sendNoteOn` (L2421): swap the order for a step with G when on.
- **Bonus on CE:** the Synth SDK decides legato from note order (`isLegatoStep`
  from `hadHeldNote`, `SynthVoiceSDK.h` L340), so the order change also gives CE's
  own engines and ours their legato behaviour without more code.
- **Tests:** emulator: MIDI byte order; synth note events. Sound test stage 1 comes with
  this mod (envelopes and Off = CE on all engines, [plan](SOUND_TEST_PLAN.md)).
- **Hardware:** MIDI to an external synth; a synth track with Glide Mode.
- **Effort:** S–M.

### 3.8 interpolate

- **What it does:** CC FX values ramp per microstep between steps with a CC FX;
  switches for MIDI CC and Synths; in Pattern mode the ramp wraps last CC → first
  (kept, your call).
- **Hooks:** per microstep in `play_microStep`, sending through `sendCC` (L629) for
  MIDI and the synth parameter path (`setSynthParam`, `mtAudioEngine.cpp` ~L1140).
  This is a hot path: keep the per-microstep work to a table lookup, in ITCM.
- **Data:** `mods.bin`: MIDI CC on/off, Synths on/off.
- **Old projects:** `interpolate.bin` import.
- **Tests:** host: ramp math and wrap; emulator: CC stream per microstep, with
  tracklen; a Macro ramp moves the sound (sound test stage 2,
  [plan](SOUND_TEST_PLAN.md)); CPU via the Profiler on hardware.
- **Effort:** M.

### 3.9 songloop

- **What it does:** Shift+Down marks a song selection; Play Selection loops it;
  Continue Song. Nothing saved. Keep the PR author's design.
- **CE today:** the Song screen can preview a selection once.
- **Hooks:** selection keys and buttons in `fSongEdit.cpp`; the song-mode pattern
  change where the sequencer asks `getNextSongPattern()` (`mtSequencer.cpp` L369,
  L781): wrap to the selection's start while looping.
- **Tests:** emulator: pattern order over several loops, Continue, stop/start.
- **Effort:** M.

### 3.10 colorsong

- **What it does:** song blocks coloured by content; colours stay put; saved per
  project.
- **Hooks:** block colour in `songPlayer.cpp` `fillBlocks` (`colors[1]`, muted
  `colors[7]`); `updatePatternBitmask` tells the mod a pattern changed. Saves can run in
  the audio interrupt (MIDI Start in Song mode → `playSongFromPosition` →
  `saveWorkspacePatternNow`), so that only sets a flag. The Song screen's `update()` does
  the rest: one pattern file per call, through CE's reader, none while the file manager is
  busy and one per 100 ms while playing; the actual pattern comes from RAM if it's unsaved.
  Hashes skip the `m` and `V` columns, as 1.2.0 skips humanize's FX.
- **Display list:** no bytes added. CE already sends one `API_COLOR` per block; the mod
  chooses its value. A full grid is 2,316 bytes On and Off.
- **Themes:** the mod overrides the fill of filled blocks only. A block's lightness keeps
  a distance of 7 from the theme's empty-block colour, below it on a light theme, which
  gives 1.2.0's floor of 14 on CE's dark themes, so 1.2.0's colours are unchanged there.
- **Data:** `mods.bin` record, the image of 1.2.0's `colorsong.bin` (up to 5,124 bytes);
  the file imports as it is. Keys are the same on both firmwares, in spite of the pattern
  format change, since the hash reads the fields and not the file. A colour following an
  edit goes out with the next Workspace save and doesn't mark the project changed.
- **Memory:** 12,144 bytes of tables in SDRAM (9,180 for patterns, 2,964 for colours),
  zeroed by `colorsong::init` at boot, and the project's `mods.bin` buffer grew by 4 KB.
- **Tests:** host: `core/mods/tests/test_colorsong.cpp`, allocation rules and the file
  format; emulator: `tools/emu/test_colorsong.py`, the Song screen's display commands, CE's
  file reader and a real 1.2.0 project.
- **Effort:** M.

### 3.11 visualizer

- **What it does:** hold Note+Instr+FX1+FX2 in Pattern play to light the pads with
  what plays; animation only while showing (vz6).
- **Hooks:** `mtPadsBacklight.cpp` (pad colours), key combination in the Pattern
  screen's button handling, pad colour restore on exit (`fPatternEdit.cpp`
  L4176–4252 use `padsBacklight`).
- **Tests:** emulator: pad colour calls per tick, restore.
- **Effort:** S–M.

### 3.12 slicethru (Loop)

- **CE today:** Play Thru per Slice/Beat Slice instrument (`slicePlayThru`,
  `mtStructs.h` L654). That's our Thru; we drop ours.
- **Port:** Loop only: a slice plays on and loops the whole sample until note-off.
  Reverse plays once (as on 1.2.0).
- **Hooks:** `endSlice` in `play_memory_beat_slice.cpp` (L46, L57) and
  `play_memory_slice.cpp`; for samples streamed from SD (256 KB and up), the loop
  wrap in `VoiceStreamManager`, which loop modes already use.
- **Data:** a `mods.bin` record with one Loop bit per instrument (CE's `.pti` format
  stays untouched). Loop needs Play Thru on.
- **Old projects:** `slicethru.bin` import: Thru → CE's `slicePlayThru` on the
  instrument; Loop → Thru plus our bit.
- **Tests:** emulator: sample positions at the end of a slice, RAM and streamed.
- **Effort:** S–M (the streaming case is the work).

### 3.13 masterfx

- **CE today:** master reverb (Polyverb) and delay are open source (`MixerAndFxs`,
  `getDelay()`, `getPolyverb()`).
- **Hooks:** our reverbs and delays as audio objects in the master chain; a selector
  routes the send through stock or ours. Changes only when stopped (`idleOnly`).
  The stock object must not run when ours is chosen (the device is stall-bound;
  measure with the Profiler).
- **Our code:** the DSP from `patch/masterfx/` (ours), on the stock delay and reverb buffers
  as on 1.2.0.
- **Data:** `mods.bin`: reverb choice, delay choice; `masterfx.bin` import.
- **Tests:** host: DSP against the 1.2.0 test references; emulator: routing and effects,
  synth sends included (sound test stage 3, [plan](SOUND_TEST_PLAN.md)); CPU on
  hardware with all 8 synth voices sounding (your budget rule).
- **Effort:** M.

### 3.14 resample

- **What it does:** Trk Audio / Trk Record: record the master or any instrument's
  output into the Sample Recorder (rs8, rs9).
- **Hooks:** a source option in the Sample Recorder (`fSampleRec.cpp`); a tap that
  feeds the master (or one track's) audio into the recording queues
  (`AudioInput.h`); routing in `mtAudioEngine.cpp`.
- **Redo:** 1.2.0's "free part of sample memory" logic. CE streams large samples
  from SD, so far more of sample memory is free for the recording than on 1.2.0.
- **Data:** `mods.bin`: the two settings; `resample.bin` import.
- **Note:** the USB In+Out rule that silences the jacks (`mtAudioEngine.cpp`
  L851–865) is CE's and stays.
- **Tests:** emulator: recorded blocks equal the master blocks, synth tracks included
  (sound test stage 4, [plan](SOUND_TEST_PLAN.md)); hardware: record a
  playing pattern, compare by ear and by file.
- **Effort:** M–L.

### 3.15 synths (WAVE, PHASE)

- **CE today:** the Synth SDK: engines are source files registered in
  `core/synth/registry.cpp` (`CHIP` is the open example).
- **Port:** WAVE and PHASE as SDK engines (`CustomSynthVoice`, `onNoteOn(ctx, note,
  velocity, isLegatoStep)`, up to 8 voices), each with **its own** `InstrumentTypeId`,
  name, parameter list and pages. That meets your requirement (own name, own
  controls) more directly than the 1.2.0 swap; the swap itself existed only because
  1.2.0 had five fixed model slots. Chorus goes in `renderPostProcessAudio`.
- **Off (`hidesOnly`):** the model picker stops offering them; instruments already
  using them keep playing.
- **Data:** patches through CE's own synth patch files (the Workspace patch copy).
  No `synths.bin`.
- **Old projects:** instruments that used WAVE/PHASE through a replaced model:
  convert to the new type with the patch values mapped (decision 6), or leave them as
  the stock model.
- **Tests:** host renders of our voices (our code plus the SDK headers, no closed
  libraries), compared with the 1.2.0 references; emulator: our engines through CE's real
  host and graph, beside the closed ones (sound test stage 5,
  [plan](SOUND_TEST_PLAN.md)); hardware: chords on 8 voices (the 1.2.0 screech came only
  on hardware).
- **Effort:** L.

## 4. Order and why

| # | Mod | Proves | Effort |
|---|---|---|---|
| 0 | groundwork | switches, Config, `mods.cfg`, `mods.bin`, emulator, golden | L |
| 0b | per-project mod sets | project's set in `mods.bin`, defaults in `mods.cfg` | S |
| 1 | chords | pattern value range, encoder limit, grid text | S |
| 2 | condtrig | pattern upgrade hook | M |
| 3 | swing | per-project setting, import | S |
| 4 | humanize | first Pattern button, dynamic placement | S–M |
| 5 | copypattern | second button; after 1.2.0 final | S |
| 6 | tracklen | sequencer per-track wrap, grid; after 1.2.0 final | M |
| 7 | legato | note order | S–M |
| 8 | interpolate | hot microstep path | M |
| 9 | songloop | Song screen | M |
| 10 | colorsong | Song screen drawing, big record | M |
| 11 | visualizer | pads | S–M |
| 12 | slicethru Loop | audio objects, SD streaming | S–M |
| 13 | masterfx | audio graph | M |
| 14 | resample | recorder routing | M–L |
| 15 | synths | SDK engines | L |

One mod at a time: branch, port, host and emulator tests, golden test still
passing, your hardware check, merge into `modular`. Each merge is a release
candidate.

## 5. Decisions for you

1. **Default set for new projects** on first boot: all Off (CE until you turn things
   on) or all On. Chosen: all Off.
2. **When per-project settings save:** with the project (CE's way: Discard drops
   them) or instantly as on 1.2.0. Recommended: with the project.
3. **SDRAM for the mods:** chosen 2026-11-04: 1 MiB of sample memory, in mod builds only.
   That's about 37 times what all the 1.2.0 mods added. The longest stereo recording goes
   from 5:56 to 5:50, and large Wavetable and Granular samples, which can't stream, have
   1 MiB less room.
4. **Edition name** for `version.mk` and the `.ptf` name. Suggested: `MT`.
5. **tracklen lengths:** pattern field or `mods.bin` record. Recommended: `mods.bin`.
6. **Old WAVE/PHASE instruments:** convert to the new engines, or not supported.
7. **Pull requests to Polyend:** they'd get relicensing rights over the change.
   Recommended: not for mods; maybe for plain bug fixes.
8. **Tag names:** `v*` tags auto-publish a GitHub Release in CE's workflow. Suggested
   `v1.0.0-mt1` style, published only when you say so.
