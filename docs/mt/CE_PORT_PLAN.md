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
- **Order:** step 0 (groundwork, no mods), then swing, humanize, copypattern, tracklen,
  legato, interpolate, songloop, colorsong, visualizer,
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
- CE has room: about 17 KB DTCM, 16 KB ITCM and 1 MiB of SDRAM set aside, against
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
  sound tests.
- **A CE fix, now upstream.** `handleNoteOnDelaySend` and `handleNoteOnReverbSend` clear both send channels, so a
  note doesn't leak the last note's send (`PAST_FAILURES.md`). Modular carried it for mods builds until upstream's
  `main` took it on 2026-11-07; the merge took main's version, so the reference build has it too.
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
  ~5 KB), tracklen (up to 4,335 B), slicethru Loop bits (6 bytes).
- **Plain CE** opening a project saved with mods ignores `mods.bin`. CE's Save As
  doesn't copy it (it copies only its own files), so the copy made by plain CE loses
  the mod settings. Acceptable: nothing breaks.

**Chords in old projects.** Their values live in the patterns, so no side
file shows them in use. Scanning a project's pattern files on Open is left out for now:
a 1.2.0 project with chords 30–69 opens with Chords Off (those steps play the note
alone, as plain CE) until Chords is turned on in Project Settings → Mods.

**Importing 1.2.0 projects.** On Open, when a project has no `mods.bin`, read each
1.2.0 side file that exists (`swing.bin`, `humanize.bin`, `interpolate.bin`,
`masterfx.bin`, `slicethru.bin`, `resample.bin`, `tracklen.bin`, `colorsong.bin`,
`synths.bin`), check its own checksum, convert, mark the project changed, and turn
that mod On in the project's set (the rest stay Off; the user turns more on in
Project Settings → Mods). Mods whose data is in the patterns (chords)
switch on when the importer finds their values there. The old files stay. Their formats are ours, documented in the patch READMEs.

### 1.7 Memory

- Switch bits and small per-mod state: DTCM (about 21 KB free). Keep each mod's hot state
  under a few hundred bytes.
- Code on hot paths (sequencer microstep, note on/off, audio callbacks): ITCM with
  CE's `AUDIO_CORE_RAM` placement where CE's caller already lives there (about 11 KB free).
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
   practical: chord tables, humanize ranges, interpolate
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
and pairs that share a hook: chords + legato (note order), tracklen + CE's conditions
(pass counts per track length, in `test_tracklen.py`), tracklen + copypattern, swing + humanize (timing),
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

**Dropped, 2026-11-06: CE's own conditions on the Chance FX do this job, and nothing below is
built.** What a short track needs from them is in 3.6. Mod id 6 stays unused. A 1.2.0 project's
Chance 101-137 aren't converted, so they read as CE's `GL`, `!L` and Play/Skip. The old plan, for
the record:

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

Built 2026-11-07, passed its hardware test 2026-11-08. Plan and its decisions:
[COPYPATTERN_PLAN.md](COPYPATTERN_PLAN.md). Id 9,
`MT_HAS_COPYPATTERN`. The 1.2.0 version is final (cp5).

- **CE today:** Duplicate doubles a pattern inside itself. Copy and Paste Pattern copy steps but not the
  pattern's length. The Song screen can make a full copy (Shift + Add Slot, Copy, Paste), but the copy
  goes into the song.
- **What it does:** a Copy to New button on page 3 copies the pattern from RAM, unsaved edits included,
  into the next empty pattern, playing or stopped, and you stay on your pattern (as on 1.2.0, not "then
  opens it" as this section said before). With Variable Track Length On, the lengths go with it through
  `tracklenTracksPasted`, the call Song's paste uses.
- **Hooks:** the button entry and an `update()` call in `fPatternEdit.cpp`, two Config items. No
  change to the sequencer or the file manager. Nothing in the audio interrupt.
- **Memory:** 16,480 B of SDRAM (a snapshot of the pattern and the job), 2,840 B of flash, 56 B of DTCM.
- **Decisions:** five, in the plan's section 11, all as recommended: stay on your pattern, 1.2.0's
  strict "empty", button order Humanize, Copy to New, Trk Len, no lengths copied while Trk Len is Off.
- **Effort:** S.

### 3.6 tracklen (Variable Track Length)

Built 2026-11-06, passed its hardware test 2026-11-07. Id 12, `MT_HAS_TRACKLEN`, switch in Project
Settings > Mods and Config > Mods.

- **What it does:** each track of a pattern can have its own length, 1 to the pattern's. A short
  track plays its own steps over and over, and every track starts again together when the pattern
  ends, as on 1.2.0. It isn't polymeter: in 16 steps a track of 6 plays 1-6, 1-6, 1-4.
- **Positions stay whole-pattern.** Only the step a track reads changes, to position mod length, so
  the pattern's end, song mode, the metronome and the play row are CE's untouched.
- **Hooks:** `play_microStep` (the step read); `switchRowStep` (CE's `passCount`, below);
  `calcStepLength` (Roll's look-ahead); `getDestRecordingStep`, `handleNoteOn`, `handleNoteOff` (live
  recording); `refreshPattern` and the four drawing functions of both trackers (the grid); page 3's
  labels and functions and `functEncoder` (the button); `copySongTracks` (Song's paste).
- **CE's conditions:** with the mod on, a short track's pass ends at its own length, so Play/Skip,
  Skip/Play and the `GL` and `!L` after them count its loops. At length 6 in 16 steps: steps 0-5 are
  count 0, 6-11 are 1, 12-15 are 2, and the next pass goes on from 3. Full-length tracks count as CE.
  Only forward play counts this way: in performance mode, a track playing another direction counts
  as CE does.
- **Where lengths live:** one `mods.bin` record, id 13: 17 bytes for each pattern with a length, its
  number and 16 lengths. No record when there are none, so a project with no lengths has the same
  `mods.bin` as before. Pattern files aren't touched, so Off = CE includes the files. The pattern's
  own per-track field was no use: CE's length edits write only tracks 1-7.
- **A length counts** when it's 1 to the pattern's length - 1. Anything else plays the whole pattern,
  so a pattern that shrinks below a length and grows back has it again.
- **Interrupt:** per-step reads use a DTCM copy of the playing pattern's 16 lengths, refilled when the
  pattern number changes or an edit marks it stale. Recording reads the table: it's one byte, and it
  also comes from the main loop. Any table contents are memory-safe: position mod length never goes
  past the position.
- **Button:** Trk Len, on page 3's first free slot, is held, not pressed, and works in every mode. Its
  second line shows the cursor's track's length or `--`. While it's held the encoder sets the length.
  Whether it's held is read from the button itself when the encoder turns, so a popup that takes the
  release can't leave the encoder stuck on the length.
- **Grid:** past a track's length, rows show the loop in grey. Empty ghost cells are blank. A length
  change redraws at once.
- **Display list:** checked in `test_tracklen.py`, each view CE has. A ghost cell has CE's characters,
  blank ones draw nothing, and grey needs a colour command only where the ghost rows start. CE's worst
  grid is 4,976 B (vertical) and 2,368 B (horizontal). Short tracks stay under that. The one case over it
  is every track of a 2-step pattern at length 1 in the revolver view, alternating grey and not: 5,264 B,
  288 B more. The limit is 8,172 B for all the screen's controls. The button's second line is at most 2
  characters, on a page with fewer labels than page 1.
- **Live recording:** a hit goes on the step the track is playing. A short track's loops share its
  steps, so its first loop's note-offs would soon leave none free, which is what stopped recording after
  one loop on 1.2.0 until its `tl7`. So on a short track a new note may replace a step holding only a
  note-off. A note is never replaced.
- **Memory:** 272 B of ITCM, 104 B of DTCM (`activeLengths`, the grid's marks, the button's text),
  12,800 B of SDRAM (table 4,096, a file buffer 4,347, `projectBuffer` from 6,144 to 10,496), 3,720 B
  of flash. No heap, no OCRAM.
- **Old projects:** `tracklen.bin` imports into the record, and turns the mod on if it holds a length.
  `tools/ce_to_120.py` doesn't write it back.
- **What CE's own actions do with lengths:** they stay with the pattern number. Song's paste carries them, whole
  patterns and track blocks, each length with its track and none included, once the notes have landed. Song's
  Undo restores the notes and the pattern's length but not the track lengths. CE takes the pattern's length only
  for all 16 tracks or an empty destination, so a length that no longer fits plays the whole pattern. Copy and
  Paste Pattern on the Pattern screen don't carry them, as they don't carry the pattern's length. Duplicate, Expand and Shrink keep the length, so the track just
  goes on looping. Undo doesn't restore them, as on 1.2.0. They save with the project, so Discard drops
  them, where 1.2.0 saved them at once. Performance mode's per-track pattern load plays with the playing
  pattern's lengths, not the source's. Editing a step past the length edits the hidden step, as on 1.2.0.
  A larger `mods.bin` reads as damaged on test builds older than this one, whose buffer was 6,144 B.
- **Tests:** host `test_tracklen.cpp`. Emulator `test_tracklen.py`: Off and full length as plain CE note
  for note; a short track as its loop written out, all 16 tracks, with Micro-move, Roll, Break, song mode,
  Play from a position, a looped selection and performance mode; CE's conditions; recording; grid, button,
  Config, files and Song's paste on real pattern files. Release tier: real boot, Open and Play with the interrupt firing inside main-loop code,
  and the cost limit, on the worst-case project with a length on every track of every pattern.
- **Device test, 2026-11-07:** recorder build, from `/mfx_log.txt`. The log has no markers, so the projects are
  matched by order and settings. `L1`, `L5` and `L8`, with lengths 4-8 on every track of every pattern: slowest
  26 %, 49 % and 79 %. `L8` averaged 61 % over 34 s, against 60 % and 78 % without the mod's lengths. No block over
  time, no stall and no lockup record. The button, grid, loops, CE's Play/Skip, live recording, Off and On,
  saving and reopening, and a song of a pattern with lengths then one without all behaved as designed.
  A second firmware with Song's paste passed the same day: whole pattern, track block, Undo, Off, Save and Open. Not on the unit, emulator only: Break FX, performance mode, Export,
  per-mod Saving, and the revolver view's 288 B display case.
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

### 3.9 songloop (Song Loop)

Built 2026-11-08, passed its hardware test 2026-11-09. Designed and written by P-N-E-M for 1.2.0 (the
`songloop` patch, sl4, Modular-Trackular PR #1). This is their design on CE's source: the
behaviour and the test cases are ported, none of the 1.2.0 address hooks. Id 13, `MT_HAS_SONGLOOP`,
switch in Project Settings > Mods and Config > Mods. Nothing is stored in projects, so there is no
`mods.bin` record and no 1.2.0 import.

- **What it does:** in the Song screen's pattern column, with Rec on, Shift + Down selects a block
  of positions from the cursor and Shift + Up takes the bottom back (the top is the anchor). The left
  softkey reads Play Selection. Stopped, the song plays from the block's first position and repeats
  the block. Playing, a block further down waits until the song reaches it (armed), and a block
  behind the play position is jumped to at the end of the pattern. While it repeats, the softkey
  reads Continue Song: the block plays out and the song goes on. During the loop Shift + Up/Down
  resize the block, and Up/Down or the jog wheel move the whole block, both as the author intended.
  Stop ends the loop. With a block, Undo, Add Slot, Delete Slot (and the Delete key) and
  << Pattern >> (and Shift + jog) do nothing and are greyed out. The status bar shows |: :| (looping)
  or |: (armed) in place of the play icon, and so does the Song list next to the play position.
  With the cursor in the grid during the loop, the block has a grey frame. The loop keeps
  Performance on over its pattern ends. With a track-1 Performance play mode other than Fwd, the
  next pattern is asked for on track 16's last two rows, from the main loop (also without a block,
  as sl4 does).
- **CE today:** the Song screen can preview a selection once.
- **Hooks**, each under `MT_HAS_SONGLOOP` and `mods::on`:
  - `getNextSongPattern` (ITCM, in the interrupt): the preload, the block's first pattern at its end.
  - `switchRowStep`'s song-mode pattern end (in the interrupt): to the block's start as the preload
    decided, and Performance kept on while looping. CE's `switchRamPatternsNow` finds the position
    again from the loaded pattern, so without this hook a song that uses a pattern twice would loop
    on a later slot. The emulator test has that case.
  - `Sequencer::stop` (both contexts): the loop ends. It only clears two bytes.
  - `fSongEdit.cpp`: the labels in `update()`, `functPlayPattern` (softkey 0 and Shift + Play),
    Up/Down and the jog in the pattern column, the locks in Undo, Add, Delete and
    `changeSelectedPlayListPattern`, and the grey softkeys in `refreshEditMode`.
  - `songPlayer.cpp`: the block's range for the cursor frame, the grey frame, the list icon.
  - `songTimer.cpp`: the status icon, and a redraw when it changes.
  - `display.cpp`: the glyphs again after both font loads. `Tracker.cpp`: the main-loop preload.
- **Status bar glyphs:** two glyph pairs of the status font (`` ` ^ `` and `{ }`), drawn into RAM_G
  with the play icon's widths, as on 1.2.0. Unlike 1.2.0, they go in only while the mod is On. The
  originals are read back first and written back when it goes Off, so Off shows CE's characters.
  Both CE fonts (Config > General > Display font) take them. With a font they don't fit, the bar
  and the list keep CE's play icon. While the mod is On those four characters show as repeat signs
  in the status font. The UI and its keyboard don't use them.
- **RAM_G:** the list icons at 140,000 and 140,200 (200 B each), between CE's last bitmap (ends at
  128,780) and the controls (150,000). 1.2.0's 0x1F000 is inside CE's synth bitmaps.
- **Display list:** the grey frame is 36 B, only with the cursor in the grid during a loop. The
  frame and the list icon are CE's own commands with other values.
- **Memory** (against `modular` 19a1f1a, `tools/mem_budget.py`): 7,008 B of flash, 32 B of ITCM
  (the hook in `getNextSongPattern` and its veneer), 80 B of DTCM (the 12-byte state the interrupt
  reads, and alignment), 4,224 B of SDRAM (the glyph backups and a work buffer), no OCRAM, no heap.
  The interrupt's part (`preload`, `atPatternEnd`, `noteEntered`, `stopped`) runs from flash, once
  per pattern end.
- **What differs from sl4, all small:** the glyphs only while On (above), new RAM_G addresses, a
  check that the glyphs fit the font, the state reset on a Workspace load and when the switch
  changes, and the jog value goes through CE's `invertValForList` (always false on Tracker+).
- **Tests:** host: `core/mods/tests/test_songloop.cpp`, the patch's own cases plus the frames and
  icons. Emulator: `tools/emu/test_songloop.py`, on CE's sequencer, file manager and Song screen
  handlers. It covers song order over several loops (also with a pattern used twice, and with
  Break), Continue, armed and behind, the block moved during the loop, and Off and On without a block
  note for note against the `MT_NO_MODS` build. It also covers Performance, stop, the locks, the
  labels, the status icon, both fonts and the frames. A deliberate wrong change in the logic, and
  another in the pattern-end hook, were both caught.
- **Effort:** M.

#### Song Loop hardware checklist

Run it after `tools/run_tests.py --tier release` passes, on the lockup recorder build
(`DEVICE_TEST_LADDER.md`). The mod runs a little code in the audio interrupt at each pattern end and
nothing per sample, so only the ladder's lightest step is needed. It leaves out Break FX, performance
mode, Export and per-mod Saving, which are emulator only. Projects: `L1 1 voice none` from the
ladder, and a new project you make on the unit, called `SL test` here.

1. Open `L1 1 voice none`, press Play, wait 10 s, press Play again. `/mfx_log.txt` gets its line
   with no lockup.
2. New Project, save it as `SL test`. Project Settings > Mods: Song Loop On.
3. Make patterns 1 to 6, each with one note on track 1 at a different pitch, so you can hear
   where the song is. Song screen: a song of 1 2 3 4 5 6.
4. Pattern column, Rec on, cursor on position 2. Hold Shift and press Down twice: the frame covers
   2 to 4. Shift + Up: 2 to 3. Shift + Down: 2 to 4 again. The left softkey reads Play Selection,
   the right Play Song. Undo, Add, Delete and << Pattern >> are greyed out and do nothing, and so
   do the Delete key and Shift + jog.
5. Press Play Selection: 2 3 4 2 3 4 and on. The status bar shows |: :| in place of the play icon,
   and the list shows it next to the position playing.
6. The left softkey reads Continue Song. Press Down: the block moves to 3 to 5, and the song follows
   at the next pattern end. Turn the jog wheel: the block moves with it. Shift + Up: one position
   less.
7. Press Continue Song: the block plays out, then the song goes on to 6. The frame is back on the
   position playing, the softkeys are CE's again, and the status bar shows the play icon.
8. While the song plays from 1, select 5 to 6 and press Play Selection: the status bar shows |:
   until the song reaches 5, then |: :|, and 5 6 repeat.
9. While 5 or 6 play, select 1 to 2 and press Play Selection: at the end of that pattern the song
   goes to 1, and 1 2 repeat.
10. During the loop, press Left to put the cursor in the grid: the block has a grey frame in the
    pattern column.
11. Press Play to stop: the loop ends and the status bar is back to normal. The block is still
    selected. Rec off: the selection goes.
12. Shift + Play with a block, stopped: the same as Play Selection.
13. Make the song 1 2 1 2 3. Select positions 3 to 4 and press Play Selection: the list icon stays on
    3 and 4, never on 1 and 2.
14. Config > General > Display font: switch to the other font and back while the loop plays. The
    status bar still shows |: :|, not stray characters.
15. Project Settings > Mods: Song Loop Off. Shift + Down moves the cursor as CE does, the left
    softkey is CE's, and the status bar shows CE's icons. Turn it On again: it works again.
16. Read `/mfx_log.txt`: no lockup or fault record.

Not on the unit, emulator only: Break FX, performance mode and the track-1 play modes.

**Result, 2026-11-09,** recorder build of the uncommitted port on `19a1f1a`: all 16 steps passed.
`/mfx_log.txt`: 5 plays, 57,901 blocks, none over time, no stall and no lockup or fault record.
Audio interrupt average 20 to 38 %, slowest 50 %. Longest main loop gap 105 ms, in the 129 s play
with Master FX on, which is likely step 14's font loads from SD; it wasn't timed on its own. Lowest
free heap 76,120, max stack 7,849.

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

- **Status:** built and passing in the emulator. Its first device test locked up on Play. The stepped
  test in [DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md) found sustained overload from CHIP, which also
  happens with Master FX on Stock, and Master FX adds 2 to 3 points; see section 8 of its plan. CHIP is fixed
  (polyend/tracker-ce#52). The firmware with the fix and upstream's
  optimisations passes the emulator tests and worked on the unit, sound included.
- **CE today:** master reverb (Polyverb) and delay are open source (`MixerAndFxs`,
  `getDelay()`, `getPolyverb()`).
- **Plan:** [MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md), which replaces what this section
  first said: no new audio objects and no routing. Our effects run inside the stock objects'
  `update()`, and switching works during playback (clearing in the main loop), so no `idleOnly`.
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

#### Build plan (2026-11-09, branch `mod/synths`)

Your choices, 2026-11-09: D1 old projects not supported; D2/D3 the effects and chain below,
with a Level knob in place of automatic level make-up; D4 an FX tile with the two LFOs on
one; D5 ids 64 and 65.

**What moves.** The `sy9` DSP from Modular-Trackular's `patch/synths/` (`wt_engine.h`,
`ph_engine.h`, `wt_fx.h`, the two table generators), unchanged where it can be, so a render
can be compared with `sy9`'s sample for sample. None of the 1.2.0 hooks, the model swap,
`synths.bin` or VAP's layout code moves. Each engine is a `CustomSynthVoice` in
`core/synth/engines/` whose voice keeps its own copy of the parameters (filled by
`onParamChange`, per voice, so velocity macros stay per voice) and calls the ported engine.

**Identity.** Own `InstrumentTypeId`, name (`WAVE`, `PHASE`), parameter list, value
displays, Init patch, Macros (as `sy9`'s a to f) and Edit Patch tiles. No VAP names,
ranges or cells: on 1.2.0 many of WAVE's ranges came from VAP's builder, so every range
here is set by us. Mini and medium badges for the model list and Edit Patch, drawn by our
own script.

**Ids (D5).** `InstrumentTypeId` values are stored in patch files. CE's next engine would take
9, so ours sit at **64 (WAVE) and 65 (PHASE)**, clear of upstream; the emulator checks the
library creates, saves, lists and loads patches at those ids, and if it doesn't they fall
back to 9 and 10. The project stores the model as an index into `Synth::kSynthTypes`, so WAVE
is index 7 and PHASE 8. Plain CE opening such a project resets that synth part to its
defaults (`validateSynthPartSettings`); nothing else breaks. If upstream ever appends an
engine at index 7, the merge remaps ours.

**Parameters.** `sy9`'s 66 keep their numbers (0 to 65), so the engines read them as
before; the new effect controls are 66 to 71. `enum_util` scans to 127, so 72 fits.

**Effects (D2, D3).** Two blocks after the voices, per instrument except Pre:

```
WAVE:  osc -> [Distortion, Pre: per voice] -> Drive -> filter -> amp env -> sum of voices
       -> [Distortion, Post] -> [Chorus | Flanger | Phaser] -> out (stereo)
PHASE: DCOs -> DCA -> sum of voices -> [Distortion] -> [Chorus | Flanger | Phaser] -> out
```

Distortion before the modulation effect, as on a pedalboard. Post runs once on the summed
output (`renderPostProcessAudio`), so a chord intermodulates as through a real pedal; Pre
runs in each WAVE voice before Drive and the filter. On PHASE the Distortion always runs in
the Post place, so Pre and Post sound the same (a test checks they match sample for sample).

| Control | Range, default | Notes |
|---|---|---|
| Effect | None, Chorus, Flanger, Phaser; None | `sy9`'s list, extended; Chorus keeps value 1 |
| Rate | 0.05 to 5 Hz; 0.5 Hz | `sy9`'s, shared |
| Depth | 0 to 100 %; 50 % | Chorus as `sy9`. Flanger: the delay sweeps 0.25 ms to 0.25 + 6 ms × Depth. Phaser: the notches sweep up from 200 Hz by 4 octaves × Depth |
| Mix | 0 to 100 %; 50 % | `sy9`'s, loudness kept. At 50 % Flanger and Phaser have their deepest notches |
| Feedback (new) | -95 to +95 %; 50 % | Flanger and Phaser; Chorus ignores it |
| Distortion (new) | Off, Overdrive, Distortion, Bit Crusher; Off | Op-amp clipping. Overdrive after the Tube Screamer: soft clip in the op-amp's feedback, a 720 Hz high-pass into the clipper (the mid hump; bass stays clean), the clean signal kept under the clipped one. Distortion after the DS-1: op-amp gain into a hard diode clip to ground, full bass, scooped tone. Bit Crusher (added 2026-11-09 at the user's request): sample-and-hold to a lower rate and rounding to fewer bits, no anti-aliasing (the aliasing is the sound) |
| Gain / Bits (new) | 0 to 100 %; 50 % | The clipper's gain: Overdrive +6 to +41 dB, Distortion +10 to +50 dB. Bit Crusher: Bits, 16 down to 4, continuous (fewer rounded a normal note to silence) (2^bits levels, so a macro sweeps it smoothly) |
| Tone (new) | 0 to 100 %; 50 % | Overdrive: treble cut, about 700 Hz to 6 kHz. Distortion: the DS-1's low-pass/high-pass blend, dark to bright, with its mid scoop near the middle. Bit Crusher: Rate, 44.1 kHz down to 1 kHz, exponential |
| Placement (new) | Pre, Post; Post | Before or after WAVE's filter. On PHASE it does nothing, and the docs say so |
| Level (new) | -30 to +6 dB; 0 dB | The Distortion's output, as a pedal's Level knob. Each type has a fixed trim under it (Overdrive -10.9 dB, Distortion -8.5 dB, Bit Crusher none), measured so that at 0 dB a held Init note at the default Gain is as loud as with Distortion Off, within 1 dB |

The two knobs are named for the type chosen: Gain and Tone, or Bits and Rate. The
Distortion control has `invalidatesOtherMetadata`, so the library rebuilds the metadata,
and one `// MT:` line in `changeSynthPatchParamValue` rebuilds the patch editor's list
when a control with that flag changes (CE copies the names only on entering a section).
The emulator checks when the library calls our `createMetadata` and that it never does
so in the audio interrupt (it allocates). If either fails: fixed names, Gain and Tone,
with the values showing the type's units ("8 bit", "11.0 kHz").

There is no automatic level make-up: as on the pedals, more Gain gets louder until the clip
holds it, and Level sets the output. The clippers use first-order antiderivative anti-aliasing
(closed forms, no division) and oversampling: Overdrive at 2×, Distortion at 4×, through
polyphase IIR half-band filters (8 coefficients for the outer 2× stage, 4 for Distortion's
inner one). Antiderivatives alone left 1-7 kHz notes aliasing at full Gain; with the
oversampling the test bounds it below -55 dB at 1 kHz and -44 dB at the worst note. The Bit
Crusher is not oversampled: its aliasing is the sound. Flanger and Phaser are stereo the way the Chorus
is: the right side's sweep runs in opposite phase. The Phaser has 4 stages (a Phase 90's
count). Switching any effect on or off fades, as the Chorus does.

**Edit Patch tiles (D4).** CE's grid holds 16 tiles and the 7 Macros take 7, so an engine gets
9. `sy9`'s WAVE used all 9 (Osc, Filter, Amp Env, Env 1, Env 2, LFO 1, LFO 2, Mod, Voice),
so one has to give way for an FX tile:

- WAVE: Osc, Filter, Amp Env, Flt Env, Mod Env, LFO (both LFOs), Matrix, Voice, FX.
- PHASE: DCO, DCW, DCA, DCW Env, DCO Env, LFO, Matrix, Voice, FX.

CE labels tiles from keywords in the section names ("Oscillator" gives Osc) and has no DCO,
DCW or DCA. One `// MT:` hook in `SynthPatchEditor.cpp` gives our two engines their
sections' short names as labels, with the keyword's icon; other engines are untouched.

**Off and builds.** `MT_HAS_SYNTHS` (new, on in mod builds) wraps every hook: the enum values,
the registry lines, `kSynthTypes` and the editors' name and badge tables. `MT_NO_MODS` then
builds plain CE byte for byte. With the mod Off in a project, the model picker in
`SynthEditor.cpp` lists CE's 7 models, plus ours only where that part already uses one, which
keeps playing (`hidesOnly`).

**Old projects (D1, decision 6).** Chosen: not supported. A 1.2.0 project opens its WAVE
or PHASE parts as the replaced stock model with its Init patch, as CE already does. Converting
would mean either reading 1.2.0's patch files, whose format is the closed library's, or
loading them through the library at 1.2.0's ids, and PHASE's (7) is CE's DIRT.

**Memory and CPU.** Measured 2026-11-09. Instructions per 128-sample block, counted in the
emulator over our code only (`tools/emu/test_synths_dsp.py`, which has a ceiling for each);
a block is about 1,700,000 cycles at 600 MHz. The unit is stall-bound, so device time is
the Profiler's, with all 8 voices sounding.

| | Runs | Instructions per block | Memory |
|---|---|---|---|
| WAVE voice | per voice | 15,700 (Init); 48,100 with both oscillators on the super saw; 460 idle | 384 B of state in the voice, which CE allocates on engine select as for every engine |
| PHASE voice | per voice | 13,700 (Init) | 88 B of state in the voice |
| Chorus / Flanger / Phaser | per instrument | 8,700 / 10,000 / 9,400 (12,900 before 2026-11-11) | one state per synth part, 4,488 B each with the Distortions' (Synth FX added a second for a stereo part's right side): 3 × in SDRAM (`__NOINIT(EXTERNAL_RAM)`, cleared by us), 13,464 B of the mod builds' 1 MiB |
| Overdrive (2×) | per instrument (Post) or per voice (Pre, WAVE) | 20,500 (22,900 before 2026-11-11) | in that state, or in the voice for Pre |
| Distortion (4×) | the same | 33,200 (40,200 before 2026-11-11; 44,200 before it ran in stages) | the same |
| Bit Crusher | the same | 3,300 | the same |
| Code and tables | | | flash: 569 KB more than `modular` (about 470 KB of wavetables, 4 KB of cosine table); DTCM 680 B |
| Hot loop | | | ITCM: the Distortion block only (`dist_block`, 7,580 B since 2026-11-11, 5,372 B before; 2,800 B of ITCM stay free with Synth FX's hook). The voices and the effect block run from flash, as CE's own engines do |

For 8 voices: WAVE Init about 125,000; PHASE Init about 110,000; the heaviest case, WAVE
with both oscillators on the super saw, Pre Distortion and the Phaser, about 719,000 (8 ×
48,100 + 8 × 40,200 + 12,900), about 42 % of a block in instructions before stalls. Stock
VAP is 66,000 per voice, 528,000 for 8. That heaviest case is what the Profiler checks
first on the unit.

Cutting the Distortion's cost (2026-11-09): it ran sample by sample with all four half-band
filters live, and its compiled loop had about 640 loads, stores and register moves to 220
arithmetic instructions. Run in stages over 32 samples it gives the same output bit for bit
(50 cases against the old build) and saves 9 %; unrolling the stages saved nothing more, and
staging the Overdrive cost 8 % more, so it stays as it was. What's left is the moves the
all-passes' shared states need and the coefficients' reloads. Anything bigger changes the
sound: 2x for the Distortion about halves it, but it failed the aliasing bound, which is why
it is 4x; a shorter outer half-band saves about 10 % and softens the top above about 19 kHz.
Second-order antiderivatives (ADAA2) at 2x, tried in a float64 model of the chain with the
test's measure (2026-11-09): at full Gain about the same as 4x (worst -47.8 dB against -47.1)
but 1 kHz at -54.8 (the bound is -55); at half Gain 12 to 16 dB worse at 3 and 4 kHz (-54 and
-50 against -70 and -62); and the top darker, -3.5 dB at 20 kHz against 1 kHz where 4x is
+3.3. Not a match, so it stays 4x. CE's own drives (master Saturation, the filter's Overdrive)
are piecewise polynomials at 1x with no anti-aliasing: cheaper, but they alias. Post costs
one Distortion per part, Pre one per voice.

Cutting it again (2026-11-11), after the Synth FX device test put 8 DIRT voices with the
worst insert at 98 %. Copies between registers were 28 % of the Distortion's instructions:
each all-pass's state moved along the chain every sample. The half-band filters now run two
samples a pass, their states in registers and swapping roles between the two, and the
Phaser holds its all-passes in locals for the block instead of reading and writing them in
SDRAM every sample (through `s->ap`, every output store could alias them). Same output bit
for bit: 288 cases against the old build (every Distortion type with Gain, Tone and Level
ramps, fades and type switches; every modulation effect, mono and stereo, with and without
the Distortion). About one copy per all-pass is left: the fused multiply-add overwrites its
addend, and a form without it rounds differently. Then the inner half-band (88.2 to 176.4
kHz) went from 4 coefficients to 3: it only has to remove what folds onto 0-24 kHz at 88.2
kHz, from 64.4 kHz up, since the outer one removes 24-44 kHz after it, and 3 with a 0.2
transition do that at 76.6 dB against 76.1 (`tools/synths/halfband.py`). Against the 4: every
harmonic to 20 kHz within 0.001 dB, every alias within 0.1 dB; the samples differ in phase at
the top only. Distortion 40,251 to 33,209 (-17.5 %), Overdrive 22,923 to 20,548 (-10 %),
Phaser 13,010 to 9,429 (-28 %). `dist_block` grew 2,208 B in ITCM (7,580 B; 2,800 B free).
The 1-7 kHz alias test didn't see an inner coefficient 0.03 off (rejection 30.8 dB), so a
check on 11 and 13 kHz at Gain 0 was added (-68 and -69 dB; the planted fault -59 and -58).

No `double`, heap or SD in the audio paths: the tests count `malloc` and
`createMetadata` calls during every block and fail on any.

**Tests**, in order:
1. **Engine level, our code only** (no host compiler here, so in the emulator: our voice
   classes called directly from the `.axf`, no closed code). `sy9`'s engine C built for Arm as
   the reference: WAVE and PHASE renders equal sample for sample over a set of patches, with
   the Chorus. Then `test_synths.py`'s and `test_phase.py`'s engine checks, ported.
2. **The new effects:** Flanger delay and feedback from clicks; Phaser notch positions against
   Depth and Rate; each clipper's curve, Gain and Tone responses, level make-up, the aliasing
   bound; Pre and Post differ on WAVE and match on PHASE; NaN and garbage values bounded;
   fades with no step.
3. **Through CE's host** (sound rig levels B and C, `boot_rig.py`): engine select, 8 voices,
   voices freed when the amp envelope ends, patch save and load through `FakeSd`, Macros, the
   block pool, heap after 50 engine switches, stress with random notes and parameters.
4. **Off:** Off = CE against the reference build; with the mod Off the picker hides the
   engines and a part that uses one keeps playing.
5. **The tests catch mistakes:** a scratch build with planted faults (a wrong filter type, a
   detune off by a cent, Feedback ignored, Pre and Post swapped) must fail them.
6. `tools/run_tests.py --tier release`, then the ladder in
   [DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md) with WAVE and PHASE projects.

What runs them: level A is `tools/emu/test_synths_dsp.py` on an ELF that
`tools/synths/level_a/build.sh` builds from our sources and `sy9`'s (it needs `sy9`'s
folder, so it isn't in `run_tests.py`); `tools/synths/level_a/plant.py` plants 12 faults
and checks level A fails each. Level B is `tools/emu/test_synths.py` on the full build, a
`run_tests.py` job. CE's synth has 8 voices in all, shared by its 3 parts
(`kSynthMaxVoices`), so the 8-voice checks are one part with all 8 and three parts with
3 + 3 + 2.

Found by level B and fixed (2026-11-09): WAVE's Cutoff had `MacroOperation::kMult`, as the
SDK guide suggests for a cutoff. But WAVE's Cutoff macro is bipolar, and CE starts a bipolar
macro at its centre, which under `kMult` left every WAVE voice at 20 Hz: a 26 dB quieter,
muffled Init note on the unit, while level A, which has no host macros, sounded right. Cutoff
is now `kAdd` (additive in the exponential curve's normalized space, so the centre changes
nothing). Level B now reads what each voice renders with, against `sy9`'s Init, with the
macros set as the model picker sets them, and checks the two engines' loudness against level
A's; both checks failed on the old build.

The device projects (`make_masterfx_test_project.py --synths`, DEVICE_TEST_LADDER.md's Synths
set) were booted through CE's own start-up, project load and Play in `boot_rig.py`, 2026-11-09:
S1 (WAVE), S2 (PHASE) and S5 (8 WAVE voices) open with their parts on WAVE or PHASE, sound, and
keep the heap steady (67,960 B free with 8 WAVE voices; PERC's 3, its most, leave 53,508 B).

Device test 2026-11-11, the Synths set on `TrackerPlus_MT_synthfx_diag.ptf` (with Synth FX's
cost cuts): 0 blocks over time in any of the 30 runs logged. Profiler slowest: S1 about 27 %,
S2 29 %, S3 40 %, S4 45 %, S5 50 %, S6 57 %. The heaviest case set by hand on S5 (on all three
parts both oscillators on Super Saw, Distortion Pre, Phaser), 8-note chords: average 62 to
65 %, slowest 75 to 82 %, 0 of 31,360 blocks over time in 91 s. Free heap 66,160 B at the least;
pool peak 46 of 80; max stack 11,825 B (of 20,480). Each effect by ear on WAVE, the Distortion's
knob names changing with its type (Bits and Rate on the Bit Crusher), and a project and a patch
saved and reopened all worked. With Synths Off a part on WAVE kept playing and kept WAVE in its
picker, PHASE was hidden, and once the part left WAVE it couldn't go back: as designed. The first
play after opening a project reads 3 to 5 points higher than later plays.

**Hardware checklist** (for you, after the tests): chords on 8 voices on each engine (the
1.2.0 screech came only on the unit); the heaviest patch (both oscillators on the super saw,
Pre Distortion, Phaser) at 8 voices, with the Profiler's CPU; each effect by ear; the
Distortion's knob names changing with its type; the picker with the mod On and Off; a
project and a patch saved and reopened. Break FX, performance mode, Export and per-mod
saving stay in the emulator.

### 3.16 synthfx (Synth FX: the Synths effects on every synth part)

Planned 2026-11-09, from the source, with decisions 9-16 chosen the same day; nothing built
yet. Goal: the Synths effects (Chorus, Flanger, Phaser; Overdrive, Distortion, Bit Crusher)
for every synth engine, CE's closed ones included.

- **What can be shared, and what can't.** CHIP's Bitcrush is five lines inside its voice
  loop, not a function, and does what our Bit Crusher does, so there is nothing to reuse.
  DIRT's shapers and PERC's drives are inside `libPolySynth.a`: its headers expose only
  `createShapingSynth()` and `createDrumSynth()`, so they can't be reused or studied. Our
  effects were already functions on a buffer, a state and ten settings (`synthsFx.h`), with
  nothing tied to WAVE or PHASE except where the settings sat (ids 62-71 in the engine's
  parameter array).
- **Where every part's audio passes through open code.** `PolySynth::update`
  (`core/libs/Audio/synth_PolySynth.cpp`, in the audio interrupt) gets each part's block from
  `renderAudioBuffer(..., partIdx)` as floats, before the sends split and the fader. An insert
  there processes the closed engines' output in our code, which the licence allows: nothing
  inside the library is touched or measured. Only Post is possible there; Pre needs the
  engine's own voice.
- **Phase 1: a shared effects library, no change in sound.** `synthsFx.*` and the helpers it
  needs from `synthsDsp.h` move to `core/mods/synthfx/`. The settings are read from a pointer
  to the ten (`p + 62` for WAVE and PHASE), so any open engine can use them, Pre included, and
  their macros still reach them. Proof: WAVE and PHASE render bit for bit as before (the 50
  cases of the staging check), Level A, the planted faults and `MT_NO_MODS` identical.
  - Done 2026-11-09. `core/mods/synthfx/`: `synthFx.h` and `synthFx.cpp` (namespace `mtfx`,
    the settings 0-9 as `FXP_*`) and `fxDsp.h` (`clampf`, `exp2f_fast`, `list_pick`,
    `tan_pade`, `limit`, `fsqrt`). `synths/synthsFx.h` keeps only the ids 62-71, and each call
    passes `p + P_EFFECT`. `fx_claim` takes any owner pointer. Still built with
    `MT_HAS_SYNTHS`; phase 2 adds `MT_HAS_SYNTHFX`. Results: 50 of 50 cases bit for bit
    against the committed Level A build, Level A 57 checks and 0 failures, 12 of 12 planted
    faults caught, Level B 70 checks and 0 failures, the release tier 0 failed of 28 run (4
    more from cache), `MT_NO_MODS` identical, memory as before (`dist_block` 5,372 B in
    ITCM, 5,096 B free; the states 13,008 B in SDRAM).
- **Phase 2: the per-part insert.** In `PolySynth::update`, behind `MT_HAS_SYNTHFX`, right after
  `renderAudioBuffer`: the part's Distortion, then its modulation effect, on
  `tempBufSendsCommonLeft/Right`, so the sends and the fader both get the result.
  - Parts on WAVE or PHASE are skipped: they have the same effects inside, with Pre and macros
    (decision 13).
  - Mono parts (every CE engine but PERC) stay mono until a modulation effect is on; then the
    part is sent as stereo, one more pool block while it plays. Stereo parts (PERC): the
    Distortion runs on each side, the modulation effects take L+R in and give L and R out.
  - PERC renders its sends itself (`rendersAuxSends`), so the insert reaches its main output
    only; its sends stay dry (decision 14).
  - The states: the three `mt_fx` states WAVE and PHASE already claim (SDRAM, 4,336 B each),
    claimed by part instead of by voice. A part is on one engine at a time, so three still
    cover every case; a second `mt_dist` per state for a stereo part's right side is 168 B
    each, 504 B more in SDRAM.
  - With the mod Off, or every effect None and Off, the insert is one test per part and the
    audio is untouched.
  - Done 2026-11-09. `core/mods/synthfx/synthfxMod.h` and `.cpp` (`mods::synthfx`): the switch
    (mod id 15, `MT_HAS_SYNTHFX`, record 16 kept for phase 3), the settings (3 × 10 floats in
    DTCM, every effect Off, the rest at WAVE's Init), and `begin` and `run`, which
    `PolySynth::update` calls for each part before its blocks are taken and right after
    `renderAudioBuffer`. `synthFx.h` gains `dist2` (a stereo part's right side) and
    `fx_block_stereo`: the Distortion on each side, the modulation effect fed (L + R) / 2 with
    the dry signal kept stereo, so PERC's image survives Mix fading in (a change from "L+R in"
    above). A part lets its state go once nothing is chosen and everything has faded out.
  - WAVE and PHASE: the modulation code is now one template for mono and stereo. Its arithmetic is
    unchanged, but the compiler rounds the mono form differently in places: 27 of 36 effect cases
    are bit for bit against phase 1, the other 9 differ in the last bit of some samples (2.4e-7 at
    most) in the blocks where an effect switch fades the Mix. A copy of the old function did the
    same (GCC folded the two start functions into one), so the shared template stayed. The 50
    Distortion cases are bit for bit.
  - Results: `tools/emu/test_synthfx.py` 100 checks and 0 failures; planted faults
    (`tools/emu/plant_synthfx.py`, full builds) 7 of 7 caught; Level A 57 checks and 0 failures;
    Level B (`test_synths.py`) 70 checks and 0 failures; host tests pass (registry 196 checks, 9
    mods built); the release tier 0 failed of 29 run (4 more from cache); `MT_NO_MODS` identical.
  - Cost on the emulator, open code only: the insert adds 194,493 instructions a block on 3 ACD
    parts with Distortion and Phaser (11.4 % of a block) and 279,111 on 3 PERC parts (16.4 %).
    Block pool peak 74 on the 3 ACD parts with the effects (65 without), against 76 for 3 PERC
    parts with none: a mono part sent as stereo takes about 3 more blocks, as a stereo part does.
  - Memory: SDRAM 504 B (995,492 B free); DTCM the settings' 120 B, 56 B less free (14,504 B
    free); ITCM 88 B (the hook grew `PolySynth::update`, which CE keeps in ITCM; 5,008 B free);
    flash about 4 KB.
  - Not done then: `boot_rig.py` (test 5) waited for phase 3, when a project can carry the
    settings, and the device test for the page. Test 5 is done in phase 3.
- **Phase 3: settings and page.**
  - Settings follow the part, as its volume and sends do: loading another patch keeps them,
    since a closed engine's patch can't hold them (decision 15). In RAM: 3 × 10 floats, 120 B
    of DTCM, read by the interrupt, written by the main loop (single 32-bit stores). On card:
    one `mods.bin` record (id 16, version 1, 3 × 10 bytes as 0-127), well inside its 10,496 B.
    A project without the record opens with every effect Off.
  - The page: an "FX" section after the engine's own in the patch editor, drawn by our code
    (decision 16). The section, parameter and value functions in `SynthPatchEditor.cpp` read
    the patch's metadata, so each needs a branch for it. The display list's bytes are counted
    when it is built (8,172 B a frame).
  - Changed 2026-11-10, chosen by the user: **button 3, not a section.** VAP and WTFM already
    fill all 16 tiles of the section grid (7 of them macros), so an FX tile would push a macro
    off. Button 3, blank on the patch editor, reads "Synth FX" on CE's seven engines with the
    mod On; pressing it puts the nine rows in the parameter list, and pressing it again, or
    picking any section or macro, puts the section's rows back. Every tile and macro stays.
    WAVE and PHASE don't get it: their own FX section has the effects.
  - Changed from "0-127": **the record holds each setting's step,** so every value round-trips
    exactly and Feedback's 191 steps fit. Effect and Distortion: the list item (0-3). Rate,
    Depth, Mix, Gain and Tone: 0-100, 1 % a step. Feedback: 0-190, -95 % to +95 %. Level: 0-72,
    -30 dB to +6 dB in 0.5 dB. Placement: always 1 (Post). A missing record, another version
    or another length opens with every effect Off; a byte out of its range is that value's
    Init alone. Settings all at Init in a project without the record write nothing.
  - Done 2026-11-10. `synthfxSettings.cpp`: the settings, their steps, `load` and
    `storeIfEdited` (registry events, so Save, Save As, Open and New Project go through
    `mods.bin` as the other mods' records do) and the rows. `synthfxPage.cpp`: the rows' names
    and texts as WAVE's FX section shows them (Gain and Tone become Bits and Rate for the Bit
    Crusher). `SynthPatchEditor.cpp`, all behind `MT_HAS_SYNTHFX`: button 3, and the section,
    parameter, value, default and macro functions redirected while the rows show. The rows'
    texts and lists are in SDRAM (`__NOINIT(EXTERNAL_RAM)`), filled before each use.
  - Results: host test `test_synthfx` 47 checks and 0 failures (steps, the record, damaged
    records, garbage values, rows); `tools/emu/test_synthfx_page.py` 56 checks and 0 failures
    (mod Off: the same display calls and bytes as the reference build on all 7 engines; the
    rows, edits, Delete, lists, leaving the rows; Save, Save As, Open, New Project);
    `test_synthfx.py` 100 checks and 0 failures; planted faults 18 of 18 caught (11 new: the
    button's rules, the edit's save mark, Delete, leaving the rows, Feedback's zero, the record
    read and written, and three on the host test); host tests pass (registry 196 checks, 9 mods
    built); the release tier 0 failed of 36 run; `MT_NO_MODS` identical.
  - Test 5 done: `make_masterfx_test_project.make_synthfx` writes the worst case (the worst
    pattern at 800 BPM on three PERC parts (stored as 4, 2, 2 then, which CE clamped to 1
    each, PERC's limit; 1, 1, 1 since), Distortion at full Gain into the Phaser on every
    part, Shimmer and BBD), and the release tier boots it with and without `--preempt`
    (`boot_rig.py --expect-synthfx`: the settings loaded from the record, the insert run every
    block, no allocation inside the audio interrupt). 53,136 B heap free, pool peak 64 of 80,
    stack 7,064 B: the same heap and pool as that project with every effect Off, so PERC's,
    not the insert's (the plain worst project on its own engines: 80,004 B and 55).
  - Display list, worst of each engine's patch editor, of 8,172 B: 6,548 B on opening (WTFM;
    CE's 6,452 plus button 3's label), 5,616 B with the rows (VAP and WTFM).
  - Memory: SDRAM 616 B (the rows' texts and lists; 994,876 B free); DTCM and ITCM as phase 2
    (14,504 B and 5,008 B free); no heap.
  - Device test 2026-11-10, the Synth FX set on `TrackerPlus_MT_synthfx_diag.ptf`: all five ran,
    no lockup or fault in `mfx_log.txt`, 0 blocks over time on any. Profiler slowest: F1 about
    33 % (no change with Overdrive at full or the Phaser), F2 37 %, F3 53 %, F4 69 %, F5 78 %
    (Master FX's own `L8` was 78 to 82 %). Button 3, its rows, an edit heard, and Save then reopen
    all worked. Pool peak 48 of 80 at most. Free heap: F4 51,692 B, but F5 21,072 to 21,348 B.
    F4 and F5 were out of spec: PERC allows 1 voice a part and the generator stored 4, 2 and 2,
    which CE clamps on open only when the engine changes. So F4 (after ACD's F3) ran 3 PERC
    voices, as `N6` had, and F5 (after PERC's F2) ran 8, a state the UI can't reach; its 78 %
    and 21 KB overstate the real worst case. See `CE_FIRMWARE_MAP.md`, 2026-11-10. The generator
    now refuses a count over the engine's limit; F4 and F5 store 1, 1, 1 and read about 53 KB in
    any order on the rig, and F6 (8 DIRT voices, the heaviest engine on the unit, with the worst
    insert) is added: 79,688 B. On the unit, same firmware, F2 then F5 then F6: F2 39 % slowest,
    63,808 B; F5 (after F2) average 45 %, slowest 63 %, 51,928 B; F6 average 81 %, slowest 98 %,
    0 of 6,260 blocks over time, main loop gap 34 ms, 78,408 B. So 8 DIRT voices with the worst
    insert on every part, on Init patches, sit at the limit: the insert adds about 15 points to
    `N7`'s 83 %, and Shimmer and BBD on top, or a heavier patch, would go over. The recorder's voices
    figure is the limit, not the count, so it reads 1 on PERC parts; the log lines were matched
    to the projects by their CPU figures.
  - Device test 2026-11-11, the cost cuts (below) on `TrackerPlus_MT_synthfx_diag.ptf`: F4, F5
    and F6, 0 blocks over time on any. F4 (1 PERC voice a part, first time on the unit in spec)
    average 41 %, slowest 56 to 60 %, 51,936 B. F5 average 43 to 48 %, slowest 58 to 61 % (was
    63 %), 51,620 to 51,876 B. F6 average 79 to 81 % (was 81 %), slowest 94 to 96 % (was 98 %),
    0 of 6,677 blocks over time, 78,256 to 78,512 B. F6 with Shimmer and BBD as well: average 82
    to 84 %, slowest 90 to 95 %, 0 of 8,886 blocks over time, main loop gap 37 ms, 78,264 B; it
    holds, where before the cuts it was expected to go over. Max stack 7,825 B throughout.
- **Phase 4, later and only if wanted:** tracker FX commands that move the settings per step.
- **Cost, our code only (emulator instructions per block; about 1,710,000 is a whole block
  before stalls, from the Synths figures):** Chorus 8,700, Flanger 10,000, Phaser 12,900,
  Overdrive 22,900, Distortion 40,300 (80,500 on a stereo part), Bit Crusher 3,300; since
  2026-11-11 Phaser 9,400, Overdrive 20,500, Distortion 33,200 (see the Synths cost note), and
  the worst insert through the host 162,606 on 3 ACD parts (9.6 %, was 194,493) and 227,069 on 3
  PERC parts (13.4 %, was 279,111); release tier 0 failed of 32 run, all 14 Level A plants
  caught, `MT_NO_MODS` identical; on the unit F6 dropped from 98 % to 94-96 % slowest (the
  device test 2026-11-11 above). The worst
  insert: 3 parts with Distortion and Phaser, about 160,000 (9 %) on mono parts and 280,000
  (16 %) on three PERC parts. The engines themselves can't be counted (closed), and CHIP's 8
  voices have locked a unit up before (Master FX on Stock too), so the device test starts at the lightest
  load and reads the Profiler. Memory: 504 B SDRAM, 120 B DTCM, no new ITCM (`dist_block` is
  there already), flash a few KB for the insert and page.
- **Tests,** after `PAST_FAILURES.md`:
  1. Phase 1: bit for bit on WAVE and PHASE; Level A; planted faults.
  2. Each CE engine (ACD, FAT, VAP, WTFM, PERC, DIRT, CHIP) on a part through the real host:
     the insert changes the part's output, each effect type sounds, the output stays bounded.
     Ours with and without the insert is compared, never the engine's internals.
  3. Off: every part's output byte-identical to the reference build's; `MT_NO_MODS`
     identical.
  4. Garbage settings (NaN, out of range): bounded, and a clear makes it sound again (the stock
     reverb's NaN failure).
  5. `boot_rig.py` with three PERC parts (1 voice each, PERC's limit) and the worst insert on all three parts, `--preempt`:
     heap, pool peak (the extra stereo block), no `malloc` in the interrupt.
  6. `mods.bin`: the record round-trips through Save, Save As, Open, New Project; a project
     without it opens Off.
  7. Display bytes on the FX section under the limit, on every engine's patch editor.
  8. Device: the ladder, one part first, then three, with the Profiler.
- **Effort:** M for phases 1 and 2, M for phase 3.

### 3.17 synthfx plugins (`.ptsfx`: every synth effect a file on the card)

Planned 2026-11-11, from the source. Phase 1 (the runtime and the SDK) built 2026-11-11:
"Phase 1, as built" below. Phase 2 (CE's engines on the two slots) built 2026-11-11: "Phase 2,
as built" below. Phase 3 (WAVE and PHASE on the slots) built 2026-11-11: "Phase 3, as
built" below. Phase 4 (the device) run 2026-11-12, then retested with the fixes: "Phase 4, prepared" below. Goal, the user's choices of
2026-11-11: each synth part has two effect slots, FX1 and FX2, that pick from one list of
`.ptsfx` plugins in `/SynthFX/` on the card. A part can't use the same effect in both
slots. Today's six effects (Overdrive, Distortion, Bit Crusher, Chorus, Flanger, Phaser)
stop being built in: they ship as the default pack of plugin files. Choosing a plugin for a
slot puts that plugin's knobs in the slot's rows. Anyone can write a new effect in C, build
it, and copy it to the card. WAVE and PHASE use the same two slots.

- **What CE's `.ptfx` does** (`core/effector/ptfx_loader.cpp`, `ptfx_manager.cpp`,
  `editor-fx-sdk/`): a 472-byte header (name, author, up to 12 parameters of 32 bytes, code
  and `.bss` sizes, three entry offsets), then Thumb code built `-fPIC
  -mpic-data-is-text-relative` with a linker script at 0, so it runs wherever it is loaded.
  The loader reads it into the shared scratch buffer (`externalRamScratchBuff`, SDRAM),
  zeroes `.bss`, cleans the D-cache and invalidates the I-cache, and calls it through a host
  table of math functions. SDRAM is executable (MPU region 8, XN 0), and so is ITCM. One
  plugin at a time, run in the main loop on int16 samples, offline, at any speed.
- **Why a synth effect can't be a `.ptfx`.** A synth effect runs in the audio interrupt,
  every block, while the project plays, up to six at once (two on each of three parts), and
  WAVE's Pre runs one in each voice. So:
  - it needs memory of its own that stays loaded (the shared scratch is the SD stream's,
    Export's and the file lists');
  - one plugin's code must serve many instances, so its state is passed in, not kept in
    globals (the builder refuses writable `.data` and `.bss`);
  - it works on floats in place and must finish inside a block;
  - a slow or crashing plugin stops the audio or hangs the unit, and CE opens the last
    project at boot, so a bad plugin could hang it on every start.
  CE's `.ptfx` effects are written for offline use, so they aren't run as synth effects;
  the format is a sibling, not the same file.
- **The format: `.ptsfx`,** magic `PTSF`, version 1, laid out as CE's header, with the same
  32-byte parameter records, an instance size (RAM per instance) and flags (Stereo, Pre
  allowed). Entries, each taking the instance's RAM first:
  - `init(void* st, const SfxHost* host)`: main loop, once per instance;
  - `set_param(void* st, uint8_t index, float value)`: in the interrupt, before a block,
    only for values that changed;
  - `process(void* st, float* L, float* R, uint32_t frames)`: in the interrupt, in place;
    R is null on a mono part or in a voice;
  - `reset(void* st)`: clears the state (a voice starts from idle, a part changes engine).
  The host table: `sinf`, `cosf`, `tanf`, `tanhf`, `expf`, `logf`, `powf`, `sqrtf`,
  `fabsf`, `fminf`, `fmaxf`, a fast random, `memset`, `memcpy`, the sample rate and the
  block size. No SD, display, heap or `double`, as for our own code in the interrupt.
- **Memory (the user chose 288 KB in all):** SDRAM, `__NOINIT(EXTERNAL_RAM)`, ours,
  cleared by us; no heap. A code pool of 96 KB holds each plugin in use once, however many
  slots use it. An instance pool of 192 KB gives each instance the RAM its header asks for
  (the Distortion about 200 B, the Chorus about 4 KB, a delay up to 64 KB). Both are filled
  in the main loop in order and packed again when a plugin is let go; a load that doesn't
  fit is refused with a popup. Small hot plugins can go in ITCM instead: the Distortion's
  `dist_block` (7,580 B) leaves ITCM once it is a plugin, so about 10 KB there can take the
  heaviest default plugins' code (the Distortion's, at least), as it runs today.
- **Loading,** all in the main loop: the slot is marked off, so the interrupt passes it dry;
  the loader waits one block, reads the file in 256-byte pieces (as CE does), checks the
  header (magic, version, sizes, entry offsets inside the code), syncs the caches, calls
  `init` on each new instance, applies the parameters, and marks the slot on. The scan of
  `/SynthFX/` runs at boot and when the rows open, never in the interrupt.
- **The default pack:** the six effects become six plugins built from today's code
  (`core/mods/synthfx/`), checked bit for bit against today's effects in the emulator. The
  firmware carries them and writes any that are missing to `/SynthFX/` at boot, so a card without them still has every effect.
- **Guards:**
  - Time: the interrupt times each `process` call with the cycle counter (as the Profiler
    does). Over its share of a block (proposed 25 %) in 8 blocks running, the slot is
    bypassed and the main loop says so.
  - Output: NaN and infinity become 0 and the output is held within ±4.
  - Crash on start: before a plugin's `init` and first blocks the main loop writes
    `/SynthFX/.starting` with its name and deletes it after 2 s of clean running. If the
    file is there at boot, that plugin isn't loaded, and a popup names it. A plugin that
    hangs later (an endless loop) still hangs the unit until it is restarted; the marker
    covers the restart.
- **Settings (CE's engines):** record 16 version 2: per part, each slot's plugin file name
  (32 B, empty for none) and its knob values (12 floats); 336 B in all. A version 1 record
  is converted: its Distortion type goes in FX1 as that plugin, its modulation effect in
  FX2, with the same knob values. A missing file opens the slot empty, with a popup.
- **WAVE and PHASE (chosen 2026-11-11):** their own FX section becomes the same two slots,
  kept in the part and saved in the project as on CE's engines, not in the patch: loading a
  patch keeps the part's effects, and macros no longer reach them. A patch saved before
  has its ten FX values converted once, when it loads, into the part's slots (as record 16
  version 1 is). WAVE keeps Placement Pre: on WAVE each slot has a Placement row, and Pre
  runs one instance in each voice before the filter, as today, for plugins whose header
  allows it.
- **Also chosen 2026-11-11:** the firmware writes the default pack to the card when files
  are missing; the one-use rule is per part (two parts can each use the Distortion).
- **The page:** the Synth FX rows become FX1 (the plugin list), its knobs, FX2, its knobs.
  The knobs' names, ranges and units come from the plugin's header, as for Gain and Tone or
  Bits and Rate today. A plugin already in the other slot isn't offered.
- **The SDK (`synthfx-sdk/`):** `tracker_synthfx.h`, `ptsfx_builder.py` (from CE's
  `ptfx_builder.py`, CE's MIT licence kept), the default pack's sources as the examples
  plus a short Delay, and a guide `docs/synthfx_plugins.md` with the rules and the cost
  budget. `tools/synthfx/check_ptsfx.py` runs a built plugin in the emulator: header checks,
  instructions per block against the 25 % share, output bounded, `set_param` and `reset`
  at any time.
- **CPU:** the same effects cost about what they do now if their code runs from ITCM;
  from SDRAM, through the I-cache, it could be a few points more on the unit, and F6 is at
  94-96 %. The device test reads that first (F6 with the default pack against today's
  figures).
- **Phases:**
  1. The runtime: format, loader, pools, guards, the SDK and builder, the default pack as
     plugins, bit for bit against today's effects; nothing on the page yet.
  2. CE's engines: two slots, record 16 version 2 with the conversion, the rows from the
     header, the one-use rule.
  3. WAVE and PHASE on the two slots.
  4. Device ladder: F1-F6 with the default pack against today's figures, a third-party
     style Delay, a deliberately slow plugin to see the bypass, the start marker.
- **Tests,** after `PAST_FAILURES.md`: the default pack bit for bit; each through the real
  host on the full build; Off and empty slots byte-identical to the reference build,
  `MT_NO_MODS` identical; damaged files rejected (bad magic, version, sizes, offsets outside
  the code, truncated, writable `.data`); the pools (full, packing, a refused load); the
  time and output guards; the start marker; the record (version 1 converted, version 2
  round-trips through Save, Save As, Open and New Project); the one-use rule; display bytes
  with 2 × 12 knobs; `boot_rig.py` with six instances; planted faults for each guard.
- **Effort:** XL (each phase M to L).
- **Phase 1, as built (2026-11-11).** Nothing on the page and nothing in the audio path yet: the
  built-in effects still play. At start, with the mod on (in `mods.cfg` at boot, or later in a
  project, from `mods::update()` in the main loop), the firmware makes `/SynthFX/`, writes the
  pack files that are missing, and lists the folder.
  - **Files:**
    - `core/mods/synthfx/ptsfx.h`, the SDK's header (a host test checks they are the same);
    - `synthfxPool.h` (first fit);
    - `synthfxPlugins.h/.cpp`: the runtime, platform-free, with memory, clock, cache sync
      and host table passed in;
    - `synthfxPluginsCe.cpp`: the firmware's side, with CE's `PtfxLoader::syncInstructionCache`,
      `Timer::clockCycles` and libm;
    - `synthfxPack.cpp` with the generated `synthfxPack.inc` (`tools/synthfx/build_pack.py
      --embed`);
    - `FileIo` gained `exists` and `list` (on CE's `listDirContents`);
    - `TrackerImpl::update` calls `mods::update()`.
  - **Changed from the plan above:**
    - **The format** is `ptsfx.h`, not `tracker_synthfx.h`. The header is 476 bytes.
      - `set_param` takes a `uint32_t` index.
      - `process` takes `frames` and `in_channels`.
      - An optional `format()` gives a knob's text.
      - The host table has `print` (for `format()`) and no `memset`/`memcpy`; the builder
        links `ptsfx_support.c` into every plugin instead.
      - The flags are WIDENS (mono in, stereo out) and PRE.
    - **Knobs:** a knob's value is always `fmaf(i, step, min)` for a whole step i. The
      default is snapped to that grid too.
    - **The pools** are first fit, and nothing moves: the interrupt may be running a block,
      so a freed block leaves a hole, which later fits reuse.
    - **Loading:**
      - The file is read whole into its code block, 4 bytes in, so the code lands 32-byte
        aligned. Its header is checked again there, and its size against the list.
      - An instance's `active` byte is set last and cleared first. On the single core the
        main loop never runs while the interrupt does, so there is no block to wait for.
    - **The start marker** holds no name, and settles after 5 s, not 2. Found at start, it
      puts the runtime in safe mode: the plugins a project asks for aren't loaded until the
      user picks one and it settles. That is simpler, and it covers a plugin that hangs a
      few seconds in.
    - **The output guard's** common case has no branch per sample: sums of `v - v`, and
      VMAXNM of `|v|`. Only a block that needs it goes sample by sample. A NaN or infinity
      also resets the plugin before its next block.
    - **The default pack** shares its arithmetic with the firmware: the effects' code moved
      into `synthFxDsp.h` (inline, in `mtfx::dsp`), and `synthFx.cpp` keeps the entry
      points. The firmware builds the per-block helpers as plain functions (`MTFX_SETUP`),
      as before: inlined, they grew `dist_block`'s ITCM copy by 1,450 B and pushed some
      of the closed libraries' cymbal code out of ITCM. Now it is within 4 B of before,
      and CE's ITCM set is unchanged.
  - **Memory** (`tools/mem_budget.py`, full build, against `TrackerPlus_MT_diag_8b7597c`):
    - SDRAM free 681,484 B (was 994,732): the pools' 288 KB, plus about 18 KB of tables
      (catalog 5.6 KB, instances 4.2 KB, the list's names 7 KB);
    - flash about 49 KB for the pack's copy;
    - ITCM free 2,792 B (was 2,800). `process` and the guard are `AUDIO_CORE_RAM`, but
      nothing calls them yet, so they aren't linked. Phase 2 links them (about 0.5 KB), or
      moves them to flash if ITCM is short;
    - DTCM: 43 B of the runtime's flags and pointers.
  - **The SDK:**
    - `synthfx-sdk/` holds `ptsfx.h`, `ptsfx_builder.py` (CE's licence noted),
      `ptsfx_support.c`, `pack/` (the six) and `examples/` (Tremolo, a short Delay);
    - `docs/synthfx_plugins.md` is the guide;
    - `tools/synthfx/check_ptsfx.py` checks a built plugin on the emulator with the
      firmware's runtime: the header, texts, output bounded, no stray writes, `reset` as a
      fresh instance, and instructions per block against the quarter-block share. It found
      a `reset` bug in the first Tremolo.
  - **Tests (2026-11-11):**
    - `tools/synthfx/test_pack.py`: the pack bit for bit against the built-in effects,
      32 of 32;
    - `tools/synthfx/test_runtime.py`: the runtime as the firmware builds it, on the
      emulator (`tools/synthfx/rig/`, with the test plugin `probe.c`), 78 of 78. It covers:
      - the start and the catalog;
      - each pack plugin through the runtime as called directly;
      - knobs: set after init, then only when they change, snapped, stepped, their texts;
      - loading: alignment, the cache sync, state zeroed, the limits (8 plugins, 56
        instances, the code and state pools full), holes reused;
      - the guards, and the marker and safe mode;
      - the cost: +1,010 instructions per block around a mono plugin, +1,816 around a
        widening one;
    - host test `test_synthfx_plugins`, 99 checks: the pool, `checkHeader` against 31
      spoiled headers, the start, the catalog, the marker, the header copy;
    - `tools/synthfx/rig/plant.py`: 20 planted faults (16 in the rig, 4 in the host test),
      all caught;
    - the `MT_NO_MODS` reference build is byte-identical; the full and diagnostic builds
      are clean.
    - `run_tests.py` runs `synthfx plugins` and `synthfx pack`.
    - Level A's and `plant_synthfx.py`'s planted faults that named `synthFx.cpp` now name
      `synthFxDsp.h`, where that code moved. Rerun after the move: Level A's 14 of 14
      caught, and `plant_synthfx.py`'s two in `synthFxDsp.h` caught. Level A rebuilt after
      the move gives the same output as the 8b7597c build, sample for sample, in all 288
      effect cases.
    - `run_tests.py --tier release` with the diagnostic build: 34 of 34 pass.
    - Not run: `plant_synthfx.py`'s other faults (their files didn't change); no device
      test (nothing a user can reach has changed but the boot's pack writing and scan).
- **Phase 2, as built (2026-11-11).** CE's engines (ACD to CHIP) play their Synth FX through the
  plugins in two slots per part; the built-in effects no longer run on them. WAVE and PHASE
  still use `synthFx.cpp` in their own FX section (phase 3).
  - **Files:**
    - `synthfxMod.cpp`: the slots in the audio interrupt, `begin` and `run`;
    - `synthfxSlots.cpp` (new): making each slot's plugin in the main loop and letting it go;
    - `synthfxSettings.cpp`: the slots' settings and record 16;
    - `synthfxPage.cpp`: the rows;
    - `SynthPatchEditor.cpp`: the rows on button 3, now from the plugin's header;
    - `fIinstrEdit.cpp`: `cInstrumentEditor::update` calls `mtSynthFxPoll`, which redraws the
      rows when a slot's plugin is made, let go or bypassed;
    - `mods::update` calls `synthfx::updateSlots` after the runtime's own update.
  - **The slots.** Each slot holds a file name and 12 knob values; NaN means the plugin's
    default.
    - The settings live in SDRAM. The main loop alone reads them, and `synthfx::init` (the
      registry's init) empties them at boot.
    - The interrupt has its own copy per slot in DTCM: the instance, a phase, and whether
      the plugin widens.
    - A pick never waits on the interrupt:
      1. the running plugin is retired, and the interrupt fades it out over its next block;
      2. a later main-loop pass frees it once it has faded, or after 50 ms if the part isn't
         rendering;
      3. the new plugin is made, gets its knobs (snapped onto its steps, and written back to
         the settings) and is published;
      4. the interrupt fades it in over its first block.
    - Opening a project remakes all six slots in the main loop. The page shows Loading until
      the plugin is ready.
    - Signal path:
      - FX1 runs, then FX2.
      - A mono part goes out as stereo while a widening plugin runs in either slot.
      - A non-widening plugin on a mono part gets `R` null.
    - With the mod Off, or on WAVE or PHASE, the interrupt passes the part dry.
  - **Changed from the plan above:**
    - Record 16 version 2 is 576 B: per slot, a 48-byte file name (the runtime's limit, not
      32) and 12 little-endian floats, with NaN as 0x7FC00000. Damaged entries load as empty
      slots, and non-finite knobs load as the default. FX2 naming FX1's file loads empty.
      A record is rewritten only after an edit, so a damaged or version 1 record stays on
      the card until the user changes something.
    - A missing or refused file shows no popup. The slot keeps its name and reads, for
      example, `Missing: Delay`; it is tried again each time the rows open. The other
      reasons are Bad file, Too big, No room, Safe mode and Read error. A slot the time
      guard stopped reads `Slow: <name>`.
    - Version 1's conversion:
      - Gain and Tone become the plugin's percent knobs and Level its dB, in 0.5 dB steps.
      - The modulation effect's Rate, Depth and Mix carry over, and Feedback becomes
        step - 95.
      - Out-of-range bytes are Init, as before.
  - **The page:**
    - The rows are FX1, its knobs, FX2, its knobs, at most 26, with 14 lines visible.
    - A slot's list is None plus every catalog plugin with status ok, without the other
      slot's. The slot's own file is added if it isn't listed (missing).
    - A `PTSFX_PARAM_LIST` knob of up to 16 steps is a list; any other knob is a bar, centred
      when min = -max.
    - The encoder moves a knob one step a tick, or (steps - 1) / 200 steps a tick on a long
      range.
    - Delete restores a knob's default, or sets a slot to None.
    - The rows' texts and lists are in SDRAM (3,568 B).
    - Display list: worst 6,548 B, on WTFM's patch screen as it opens (CE: 6,452 B); 5,940 B
      with 10 rows of knobs.
  - **Memory** (`tools/mem_budget.py`, full build, against phase 1's):
    - ITCM free 2,264 B (was 2,792): `process` and the guard are now linked;
    - SDRAM free 675,204 B (was 681,484): the slots' settings (576 B), the rows (3,568 B),
      and the slots' main-loop state;
    - DTCM: the interrupt's copy, `runs` (18 B) and `live` (72 B); the free figure reads
      14,456 B, as before;
    - the cost, three ACD parts with Distortion and Phaser: 171,135 instructions a block
      (10.1 % of a block), the plugins' own included; on PERC 238,453 (14.0 %). Block pool
      peak 74 (ACD) and 76 (PERC, the same as PERC without the slots).
  - **Tests (2026-11-11):**
    - `tools/emu/test_synthfx.py`, rewritten for the slots, on all seven engines:
      - Off sounds as the reference build;
      - each pack plugin in FX1, and Distortion into Phaser, fades in over one block,
        measured from the first sample that changes (the output path delays the part 5
        samples);
      - FX1 runs before FX2, and swaps and clears fade out and let the plugin go;
      - WAVE and PHASE are skipped;
      - garbage knobs are snapped;
      - no malloc in the interrupt;
      - the cost, as above.
    - `as_before` plays a version 1 project through the plugins against phase 1's build
      (`--before`, `TrackerPlus_MT_p1.axf`, from 42a415e):
      - of its 49 cases, the 42 without a Phaser at Feedback 90 % agree sample for sample by
        block 80 after the pick;
      - the old built-in Distortion ramped in over 8 blocks and the plugin doesn't, so they
        differ at first (CHIP's Bit Crusher takes the longest, 72 blocks);
      - behind the Phaser at Feedback 90 % the difference rings on: within the last bit by
        105 to 162 blocks, with a last-bit difference while the note sounds.
    - `tools/emu/test_synthfx_page.py`:
      - the rows, lists, knobs, Delete, the swap, the redraw;
      - the display bytes;
      - projects: an old one, a version 1 one converted, Save, Save As, New, Off, every file
        closed.
    - `boot_rig.py --expect-synthfx`: a version 1 project's six slots made at the start.
    - `tools/emu/plant_synthfx.py`: 26 planted faults (audio path, slots, record, host test,
      page), all caught. The first run missed the two fade faults: the checks then looked
      at the block's first 4 samples, which are still the dry sound before it.
    - `run_tests.py --tier release` with the diagnostic build: 36 of 37 passed. `synthfx`
      failed `as_before`'s limits, which were then fixed and rerun alone, section by section,
      with every engine; the tier wasn't run again as a whole.
    - The `MT_NO_MODS` reference build is byte-identical; the full and diagnostic builds are
      clean.
    - Not run: no device test (phase 4). Real time, SDRAM cache misses and the plugins' cost
      with 8 voices sounding are device readings.
- **Phase 3, as built (2026-11-11).** WAVE and PHASE play their effects through the same two
  slots; their own effects (the Distortion in the voice, and `synthFx.cpp`'s block after it)
  are gone from them. With Synth FX Off they play dry, as CE's engines do (the user's choice,
  2026-11-11).
  - **Files:**
    - `wtEngine.h`: the voice's Distortion is a hook, `pre(pre_ctx, out)`, where
      `dist_block` was: before Drive and the filter. `fresh` is still set on a note from idle.
    - `waveSynth.cpp`: the hook calls `synthfx::runPre(voice, buf, fresh)`; the FX section
      and its names are gone. `phaseSynth.cpp`: the same, without a hook.
    - `synthsSdk.h`, `synthsSdk.cpp`: the post process is `toStereo` alone; `distKnobName`,
      `kFxSection` and the Distortion's `.invalidates` are gone. Parameters 62 to 71 stay,
      so old patches load, and do nothing; a macro aimed at one no longer changes the sound.
    - `synthfxMod.cpp`: `begin` and `run` take WAVE and PHASE; `runPre`; `partIsWave`;
      `convertOldPatches`. `synthfxSlots.cpp`: a Pre slot is made 8 times.
      `synthfxSettings.cpp`: record version 3, `setSlotPre`, `fromOldPatch`.
      `synthfxPage.cpp`: the Placement row. `mods.cpp`: `mods::update` calls
      `convertOldPatches`.
    - The pack: the Overdrive, Distortion and Bit Crusher have `PTSFX_FLAG_PRE`.
  - **The audio path:**
    - A WAVE or PHASE part goes into the slots in mono (`Insert.inStereo` false): its two
      sides are alike, so FX1 runs on one channel, and a widening plugin makes it stereo.
    - Placement Pre (a slot with Pre and a plugin flagged PRE) is made with 8 instances. On a
      WAVE part each voice runs its own on its block before the filter (`runPre`), and `run`
      skips the slot on the part, moving its fade on. On another engine the first instance
      runs on the part, as Post.
    - CE's library passes voice index 0 to every voice, so `runPre` gives voices seats by
      address: a voice that rendered in the last block keeps its seat; any other takes a
      seat nobody rendered in last block, and that seat's instances reset, as they do when
      the voice starts a note from idle.
  - **Old patches.** `convertOldPatches` runs in the main loop, with the mod On or Off, and
    only while the file manager is idle (a project opening may not have its record in yet).
    A WAVE or PHASE patch with an Effect or a Distortion has them moved into its part's
    slots, as record version 1 is (the patch's floats × 100, Level as it is; FX1 Pre if WAVE
    and Placement Pre). It then sets the patch's Effect to None and Distortion to Off
    (`kPatchAssign`, with the audio interrupt off), marks the patch and the project edited,
    and has the slots made again.
  - **Changed from the plan above:**
    - Record 16 version 3 is 582 B: version 2's 576, then a Placement byte per slot (1:
      Pre; anything else, or an empty slot, is Post). Version 2 loads with every slot Post.
    - The `Slot` is 100 B (the file at 0, the knobs at 48, Pre at 96, padded).
    - Old patches are converted by the main loop after they load, not inside the load: one
      check covers a patch opened, a project opened and a patch picked, and a project's
      record is in by then.
  - **The page.** Button 3 is on every engine when the mod is On; WAVE's and PHASE's FX tile
    is gone (8 sections). On a WAVE part, a slot with a PRE plugin has a Placement row (Pre,
    Post) under it; Delete sets Post. Changing it makes the slot again. A knob's edit is
    copied to all 8 instances (`knobEdited`). Display list: WAVE's patch screen as it opens
    is now the worst, 7,092 B (limit 8,172); PHASE 6,576 B; 5,952 B with WAVE's 10 rows.
  - **Memory** (`tools/mem_budget.py`, full build, against phase 2's):
    - ITCM free 9,808 B (was 2,264): `dist_block` and its copy no longer linked;
    - SDRAM free 688,492 B (was 675,204);
    - DTCM free 14,272 B (was 14,456): `runs` grows to 8 ids a slot (72 B), `live` to
      168 B, and the seats (192 B);
    - instances: 56, enough for every slot Pre on every part (48).
    - Cost (emulator instructions, the plugins' own included): one WAVE part with all 8
      voices, the Distortion and the Bit Crusher both Pre, 307,397 a block (18.1 %); three
      WAVE parts 3 + 3 + 2, each the Distortion Pre and the Phaser, 307,051 (18.1 %).
  - **Tests (2026-11-11):**
    - `tools/run_tests.py --tier release`: 37 run, 0 failed (synthfx plugins cached);
    - host tests (`core/mods/tests`): 65 checks, 0 failed;
    - `test_synthfx.py`'s new sections: `wave_off_and_mono` 7/0, `old_patches` 22/0, `pre` 9/0;
      `test_synths.py` 32/0; `test_synths_dsp.py` passes; the page test passes;
    - planted faults: `plant_synthfx.py` 40 of 40 caught, Level A 3 of 3;
    - the diagnostic build has only CE's own warnings; the `MT_NO_MODS` reference is
      byte-identical.
    - Tight: PHASE at Feedback 90 % agrees with the phase 1 build from block 166, against a
      limit of 170.
    - Not run: anything on the unit (phase 4, `DEVICE_TEST_LADDER.md` S5).
- **Phase 4, prepared (2026-11-11).** No firmware change: the device test's files and the emulator
  runs of them first. Firmware `build/TrackerPlus_MT_synthfx_plugins_diag.ptf`, the lockup recorder
  build of 761cfc1 that the release tier ran.
  - **Files:** `tools/synthfx/device/` (GuardSlow, GuardHang, `build.sh`, which also builds the SDK's
    Delay and Tremolo into `build/device/synthfx_card/SynthFX`); `make_masterfx_test_project.py
    --synthfx-plugins` (record 16 version 3: P1 the Delay, P2 8 WAVE voices with the Distortion Pre and
    the Phaser on every part, G1 GuardSlow, G2 GuardHang); `DEVICE_TEST_LADDER.md`'s plugin set.
  - **Emulator, before the unit:** `test_synthfx.py`'s `guards`, with a clock that counts only the
    plugins' own instructions, as cycles: GuardSlow Light 300,020 instructions a block (6.0 a turn), kept
    running, of the budget's 435,000; Heavy over it for exactly 8 blocks, then bypassed, the sound sample
    for sample as without it; the Delay echoes, 22,262 instructions a block (1.3 %), under the rules;
    with the marker on the card a project's GuardHang and Delay aren't made, a user's pick is, and safe
    mode ends 5 s after it; the marker is on the card before GuardHang's first block. `boot_rig.py
    --expect-hang` (release tier `boot synthfx hang`): CE's own Open of G2 hangs inside the plugin, with
    the marker on the card, no file open and the Workspace's `mods.bin` written; a second start from that
    card opens the Workspace with GuardHang skipped and plays. Planted faults: 3 of 3 (the budget a whole
    block, safe mode skipping nothing, the marker never written). Release tier: 39 jobs, 0 failed.
  - **What the marker covers:** it is written when a plugin is made and removed 5 s later, by the
    clock, not by blocks run. A part with voices renders every block, so its plugins run from the first
    block and a hang then is covered. A plugin on a part with no voices doesn't run until the part gets
    one; if that is more than 5 s later and it hangs, the restart isn't in safe mode. Once safe mode has
    ended, opening the page tries the skipped slots again (`retryFailed`), so a hanging plugin left in a
    slot hangs again; the marker covers that restart too. The device steps set such a slot to None first.
  - **Not known until the unit:** the plugins' cost from SDRAM through the I-cache against F1 to F6's
    figures with the built-in effects (F6 was 94 to 96 % slowest); a GuardSlow turn's cycles (2 to 6 are
    possible; Heavy is over the budget unless a turn is under 1.8 cycles); the recorder's restart from a
    hang inside a plugin.
  - **Device test 2026-11-12,** `TrackerPlus_MT_synthfx_plugins_diag.ptf`, from `/mfx_log.txt` and the
    Profiler:
    - F1 to F6 through the plugins, as the built-in effects read before: F1 33 %, F2 37 %, F3 53 %, F4
      60 %, F5 61 %, F6 95 % slowest (log: average 82 %, slowest 92 %, 0 blocks over time). Code from
      SDRAM costs CE's engines nothing that shows.
    - S5 (8 WAVE voices, dry): average 35 %, slowest 44 %.
    - P2 (8 WAVE voices, the Distortion Pre and the Phaser on every part): average 68 %, slowest 77 %
      (89 % on the first play). With Super Saw on both oscillators: average 80 to 81 %, slowest 89 %,
      and 1 of 902 blocks over time on the first play (slowest 100 %). The built-in effects read
      average 62 to 65 %, slowest 75 to 82 % there: WAVE's Pre through 8 plugin instances costs about
      16 points more than the old Distortion in ITCM.
    - GuardSlow Heavy: 8 of 30,077 blocks over time, all 8 back to back, slowest 107 %, then bypassed:
      the time guard works. That puts a turn at about 5.6 cycles (Light about 16 % of a block). Light
      itself wasn't read: the project opened in safe mode.
    - Three lockups, all GuardHang (`pc` in the code pool, the same one instruction each sample, no
      fault): G2 as planned, and twice before it, when FX1's list was scrolled past the Phaser. The list
      makes each plugin as it is scrolled over, and GuardHang was on the card from the start, which the
      steps shouldn't have done. The recorder restarted the unit each time, and the next start was in
      safe mode ("Safe mode: Delay", no knobs) until a pick ran 5 s.
    - **Bug:** the rows weren't redrawn once a plugin was made, so a pick stayed "Loading" until the page
      was opened again, and the names drawn for the Loading layout sat over the values of the made one
      (on WAVE, the encoder on "Placement" then changed FX1's plugin). CE calls a screen's `update` only
      while its `moduleRefresh` is set, and the editor's `update` clears it, so `mtSynthFxPoll` ran once.
      The page rig called `update` every pass, past that gate. Fixed: `mtSynthFxPoll` returns whether the
      rows are shown, the editor's `update` keeps `moduleRefresh` set while they are, and button 3 sets
      it. The rig now calls `update` only while `moduleRefresh` is set (found from the editor's first
      write in `update`), and the page test has the device's case: on WAVE, FX1 Distortion Pre swapped
      for the Bit Crusher with the rows open, Loading and then the rows by themselves, each name over its
      value; the encoder on Placement sets Pre and leaves FX1 alone. With the gate, the old build failed 6
      of the page checks; planted faults 3 of 3 (the poll gone, run only once, not started by button 3;
      the last was missed until the rig ran the editor's first `update` after its start, as the unit does).
    - **The wait (the user's choice, 2026-11-12).** A pick from a slot's list is made once the encoder
      has rested `kPickRestMs` (400 ms), so turning through the list makes none of the plugins it
      passes. `setRowStep` calls `remakeWhenStill`, which marks the slot; `updateSlots` (main loop)
      remakes it once the time is up. Meanwhile the old plugin plays on, the slot reads Loading,
      and `slotInstance` is -1, so no knob or Placement rows show. Delete (None) and Placement
      still remake at once. Since the edit stores the record before the plugin is made, the slot
      stores it again once its pick is made and the old one let go (`storeWhenMade`), so the
      record has the plugin's knobs on its steps; before, a swap's knobs stayed NaN in the record
      until the next edit. RAM: `live` (DTCM) is 48 bytes bigger, 8 a slot, 192 in all. The page test has the case:
      three ticks 100 ms apart read and make none (a hook counts `plugins::create`), Loading with no
      knob rows, then only the last made once the encoder rests, with its knobs in the record.
      Planted faults: the pick made at once, the wait never ends, a waiting slot's knobs shown,
      Delete waits, the record not stored when a pick is made, the record stored before a swap is
      made. All 20 of the page's planted faults caught (Delete waiting was missed until the Delete
      check counted instances; "an edit not marked for saving" is now caught by Placement's record
      byte, since a pick's record is stored once it is made). Firmware for the retest:
      `build/TrackerPlus_MT_synthfx_wait_diag.ptf`. Release tier: 33 run, 6 cached, 0 failed.
  - **Retest 2026-11-12,** `TrackerPlus_MT_synthfx_wait_diag.ptf`, GuardHang off the card:
    - The unit started normally (no safe mode).
    - P1: FX1's list turned quickly end to end and back: Loading, no stall; Tremolo and Delay each
      made about half a second after the encoder stopped. Playing, a slow turn past two or three
      plugins left the sound alone until the encoder stopped. Log: average 25 %, slowest 45 %.
    - P2: FX1 Distortion to Bit Crusher, stopped and playing: the rows redrew by themselves, each
      name over its value; Placement changed Pre and Post and left FX1 alone; Delete read None at
      once. The Profiler's slowest didn't rise during the swap. Log: average 62 to 68 %, slowest 76 to
      86 %, 0 blocks over time.
    - G1 Light: average 38 %, slowest 45 % (the Profiler read 66 % on the first play, 44 % on the
      second; the log has no block over 45 % in that play). Light costs about 18 points against the
      bypassed slot's 20 % average, as the 5.6 cycles a turn predicted (16 %).
    - G1 Heavy mid-play: 8 blocks over time, all back to back, slowest 109 %, then bypassed (20 to
      21 % average, 26 to 30 % slowest). The bypass lasts until the plugin is made again (a pick or
      the project opened), so Light then Heavy again, or stop and play, stays at 29 %: as designed.

## 4. Order and why

| # | Mod | Proves | Effort |
|---|---|---|---|
| 0 | groundwork | switches, Config, `mods.cfg`, `mods.bin`, emulator, golden | L |
| 0b | per-project mod sets | project's set in `mods.bin`, defaults in `mods.cfg` | S |
| 1 | chords | pattern value range, encoder limit, grid text | S |
| 2 | condtrig | dropped: CE's own conditions do it | |
| 3 | swing | per-project setting, import | S |
| 4 | humanize | first Pattern button, dynamic placement | S–M |
| 5 | copypattern | a Pattern button that writes files; plan in COPYPATTERN_PLAN.md | S |
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
5. **tracklen lengths:** pattern field or `mods.bin` record. Chosen 2026-11-06: `mods.bin`.
6. **Old WAVE/PHASE instruments:** convert to the new engines, or not supported. Chosen
   2026-11-09: not supported (section 3.15).
7. **Pull requests to Polyend:** they'd get relicensing rights over the change.
   Recommended: not for mods; maybe for plain bug fixes.
8. **Tag names:** `v*` tags auto-publish a GitHub Release in CE's workflow. Suggested
   `v1.0.0-mt1` style, published only when you say so.
9. **Synth FX: its own mod or part of Synths.** Chosen 2026-11-09: its own (id 15), so CE's
   engines get the effects without WAVE and PHASE in the picker; the effects code is shared.
10. **Synth FX Off:** the insert bypassed and its page hidden, the settings kept. Chosen
    2026-11-09.
11. **Synth FX on CHIP:** through the insert (no change to CE's file) or inside CHIP (Pre and
    macros, but CHIP's patches then hold settings stock CE doesn't know). Chosen 2026-11-09:
    the insert. CHIP's own Bitcrush stays either way.
12. **Synth FX on stereo parts (PERC):** the Distortion on each side (twice the cost), or on
    L+R (half, but the part goes mono). Chosen 2026-11-09: each side.
13. **Synth FX on WAVE and PHASE parts:** skipped, since they have the effects inside, or
    stacked on top. Chosen 2026-11-09: skipped.
14. **Synth FX and PERC's sends:** the insert reaches PERC's main output only and its sends
    stay dry; or the insert is off for PERC. Chosen 2026-11-09: allowed, with the page saying
    so.
15. **Synth FX settings follow the part or the patch.** A closed engine's patch can't hold
    them, so chosen 2026-11-09: the part, like its volume and sends.
16. **Synth FX page:** a section at the end of the patch editor, or a popup from the part
    screen. Chosen 2026-11-09: the section. Changed 2026-11-10 by the user to button 3 on the
    patch editor, since VAP and WTFM have no free tile (3.16 Phase 3).
