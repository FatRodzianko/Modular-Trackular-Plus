# Copy to New on MT+: build plan

Status (2026-11-08): built, and merged into `modular` after its hardware test. The host and
emulator tests pass (section 9), and every step of the hardware checklist passed (section 10). The
five decisions are made (section 11).
Mod id 9 (`Mod::copypattern`, already reserved in `core/mods/mods.h`), flag `MT_HAS_COPYPATTERN`,
branch `mod/copypattern` off `modular`. Section 3.5 of [CE_PORT_PLAN.md](CE_PORT_PLAN.md) points here.

The behaviour is the 1.2.0 mod's, version cp5 (Modular-Trackular, `patch/copypattern/README.md` and
`docs/COPY_PATTERN_PLAN.md`): one button on the Pattern screen copies the pattern you're on, unsaved
edits and every track's length included, into the next empty pattern, playing or stopped, and you
stay where you are. With Variable Track Length, the copy also gets the Trk Len lengths. Every CE
reference below was checked on `modular` at `1191209`, after the merge of upstream's `main`. Line
numbers only show where to look.

## 1. What CE already does, and what the mod adds

CE's own ways to copy a pattern:

- **Pattern screen, page 2: Duplicate, Expand, Shrink, Copy, Paste** (`fPatternEdit.cpp`
  `functDuplicatePattern` and on). Duplicate doubles the pattern inside itself. Copy and Paste copy
  steps (a selection, or the whole pattern) through `sequencer.copyToBuffer` and `pasteFromBuffer`.
  To copy into a new pattern by hand: Copy, find an empty pattern yourself with the Pattern encoder,
  Paste. Paste doesn't bring the pattern's length or the Trk Len lengths.
- **Song screen: Shift + Add Slot, then Copy and Paste** (`fSongEdit.cpp` `functAddSlot`,
  `functCopy`, `functPaste`). Shift + Add Slot inserts a song slot holding the first pattern whose
  Song-screen block is empty, counted from pattern 1. Pasting a whole block onto it goes through
  `copySongTracks` (`fileManagerSong.cpp`), which copies all 16 tracks from the file, takes the
  pattern's length, and on MT+ carries the Trk Len lengths (`tracklenTracksPasted`). Unsaved edits
  come along, since leaving the Pattern screen saves the pattern (`functSwitchModule` →
  `saveWorkspacePatternNow`).

So CE can make a full copy into a new pattern, through the Song screen, in about five presses, and
it puts the copy in the song, where you may not want it. Its "empty" is only the Song-screen block
(section 4), counted from pattern 1.

**What the mod adds:** one press on the Pattern screen, while playing or stopped. The song isn't
changed. It uses a safer meaning of "empty" (section 4). The copy has the pattern as it is in RAM,
and you stay on the pattern you're on. That's less than on 1.2.0, where there was no other way at
all. Decision 1 is whether it's still worth porting. I think it is: it's small (S), and making
variations of a pattern is a common job.

## 2. The button

- **Where:** page 3 of the Pattern screen's bottom bar (More, More), through the mods' button list
  in `fPatternEdit.cpp` (`modPatternButtons`, read with `mods::patternEditorButton`). Page 3 has
  five free buttons, 2 to 6 (`kPatternButtonFirst`, `kPatternButtonSlots` in
  `modsPatternButtons.h`); CE binds 0 Pattern Name, 1 Track Name and 7 More. The buttons of the
  mods that are on take the free buttons in list order.
- **List order (decision 4):** Humanize, Copy to New, Trk Len, as on 1.2.0. Copy to New is then the
  third button alone, the fourth with Humanize, and Trk Len moves one place right when both are on.
  Trk Len finds its own button by its function (`trackLengthSlot` compares `hold` with
  `functTrackLength`), so the move needs no other change.
- **Label:** "Copy to" with "New" on the second line, through the entry's `text2`, like Trk Len's
  second line. A press button, so it works in edit mode (the record button) and is greyed out
  otherwise, like Humanize. The code for that is already in `setActionButtonPage3Funct` and
  `showActionButtonPage3Labels`.
- **When the buttons run out:** the loops stop at five, so a sixth button mod's button just
  wouldn't show. Three are planned today (Humanize, Copy to New, Trk Len). No other 1.2.0 mod has a
  Pattern-screen button, so this only matters for a future mod. That mod's plan should then pick
  what gives way, such as a fourth page.

## 3. The copy

**The press** (`functCopyToNew`, main loop) only records the job: the pattern you're on, and that a
search has started. It touches no files. A second press while a job is running is ignored.

**The work** is done in `mods::copypattern::update()`, called from `cPatternEditor::update()`
(main loop, next to the Trk Len and Master FX calls there). It has two parts.

1. **The search**, starting at the pattern after yours, up to 255, then from 1 up to yours (as on
   1.2.0). Patterns ruled out by what's in RAM cost nothing (section 4). It checks at most one
   pattern on the card per update, so the screen stays live.
2. **The copy**, in the same update that found the empty pattern, so nothing can change that
   pattern between the check and the write:
   1. Check that you're still on the job's pattern, that the switch is still On, and that the file
      manager is idle (`newFileManager.getStatus() == fmIdle`, as Color Song checks).
   2. Take a snapshot: note `mtProject.values.actualPattern` and `sequencer.getPatternToUI()`,
      then `sequencer.copyCurrentPatternToBuff(snapshot)` (CE's own call: a copy of the pattern the
      editor edits, so unsaved edits are in it, with CE's header and CRC), then read both again.
      If either changed, the interrupt switched patterns during the copy (song mode moving on), so
      stop with "Pattern changed - try again". Interrupts stay on throughout.
   3. Write the snapshot as `Workspace/patterns/pattern_NN.mtp` (`cWorkspacePatternFileFormat`)
      with the same calls as CE's `writePatternFile`: remove, open with `kWriteCreateNew`, one
      16,432-byte write, close. They go through `newFileManager.getFileSystem()`, because
      `writePatternFile` is private. The file is byte for byte what CE's own save would write for
      that pattern. If the write fails, remove the file again, so the pattern stays empty, and show
      "Copy failed - SD card".
   4. Do what CE's `saveWorkspacePatternNow` does after a write:
      `newFileManager.updatePatternBitmask(target, snapshot)` (the Song-screen block; it also tells
      Color Song about the pattern), `mtProject.values.projectNotSavedFlag |= 1` and
      `newFileManager.setProjectStructChanged()`. The target's own changed flag
      (`changesFlags.pattern[n]`) stays clear: it means "the pattern in RAM must be saved as n",
      which would be wrong here.
   5. Track lengths (section 5).
   6. "Copied to pattern N".

**What the copy holds:** everything in the pattern file: every track's steps, all 128 of them
including past the pattern's end, each track's length byte, the pattern's length, and the `m` and
`V` columns. Nothing else in CE belongs to a pattern number except its name (below) and its
Song-screen block (step 4).

**Pattern name** (`PatternsMetadata`, one file for all 255 names): left alone, as on 1.2.0. The
target keeps its name, normally none. Copying the name would mean rewriting the whole names file on
every copy, and two patterns with one name would be confusing.

**Opening the new pattern (decision 2):** you stay on your pattern, as on 1.2.0. Section 3.5 of
the port plan said "then opens it", which 1.2.0 never did. If you want to jump to the copy, CE's
`setActualPattern(target)` does it in one call when stopped. While playing in song mode, though,
jumping would take the screen off the song's pattern.

**All patterns in use:** "No empty pattern left", and nothing is written. Patterns run 1 to 255
(`PATTERN_INDEX_MIN`, `PATTERN_INDEX_MAX`).

**Messages:** CE's popup, `mtPopups.show(0, text)`, as CE's own "Pattern is too long to make this
operation." on this screen. It keeps the text pointer, so the text lives in a static buffer.

| Message | When |
|---|---|
| Copied to pattern N | done |
| No empty pattern left | every other pattern is in use; nothing written |
| Pattern changed - try again | you switched pattern, or the song moved on, before the copy; nothing written |
| Busy - try again | the file manager stayed busy (a project save or open, a pattern load) for 5 s; nothing written |
| Copy failed - SD card | the write failed; the half-written file was removed |
| Not in performance mode | performance mode has changed the pattern's tracks (`isPerformancePatternChanged`); nothing written |

**Dropped without a message:** the project changes (`mods::onWorkspaceLoaded`, the hook boot, Open
and New Project already go through), or the switch is turned Off, while a job is running.

**Undo:** none, as on 1.2.0. Your pattern doesn't change, and the target was empty, so clearing it
undoes the copy. One edge from CE: Song's Undo keeps old pattern files. If the target got an empty
file through a Song action that Song's Undo still holds, undoing that action brings the old pattern
back over the copy.

## 4. Which pattern counts as empty (decision 3)

Recommended: the 1.2.0 rule, with CE's new columns added. Pattern n is a target when all of these
hold:

1. It's not your pattern.
2. **Its Song-screen block is empty:** `allPatternsBitmask[n-1]` and `allPatternsBitmaskMidi[n-1]`
   are both 0. This is the only test CE's Shift + Add Slot uses. No card access.
3. **It isn't in the song or about to play:** not anywhere in `mtProject.song.playlist`, not the
   queued pattern (`sequencer.sequencialSwitch_GetNext()`), and not in `perfTracksPatterns` while
   performance mode is on. No card access. This matters beyond keeping silent bars: CE reads the
   next pattern into RAM before it plays (in song mode, and once from the interrupt on MIDI Start),
   and that copy could later be saved back over ours.
4. **Its file holds nothing:** no file (`newFileManager.isPatternEmpty(n)`, which in CE only means
   "no file"), or a file read with CE's `loadWorkspacePatternToTempBuffer` (it converts older file
   versions) in which every step of every track, all 128, has note −1 (`STEP_NOTE_EMPTY`),
   instrument 0, both FX types 0, volume below 0 and micro-move below 0. That's CE's `clearStep`
   state. Note-offs, a lone instrument and FX values without a type count as used, exactly as
   each of those is stored. A file CE can't read counts as used.

Test 2 alone would let a copy overwrite notes past a pattern's end (CE's blocks only look up to the
pattern's length) and change what the song plays. Test 4 is one `exists` per pattern with an empty
block, plus one 16 KB read when a file exists. Patterns no one has edited have no file, so most
searches need no reads.

The alternative is CE's rule alone, test 2, searched from pattern 1. It's simpler, needs no card
reads, and matches Shift + Add Slot, but it can lose hidden notes and change the song.

## 5. Track lengths and the rest of `mods.bin`

- **Variable Track Length On:** after a successful write,
  `mods::tracklenTracksPasted(source, target, 0, 0, 16)`, the call Song's paste already uses. It
  copies all 16 lengths, none included, so a target with old lengths of its own gets the source's.
  It stores the record and marks the project changed only if a length changed. It's main-loop code
  and writes RAM only; `mods.bin` goes to the card with the next Workspace save. A failed copy
  copies no lengths.
- **Variable Track Length Off (decision 5):** recommended, the same as Song's paste: lengths aren't
  copied while the switch is Off (`tracklenTracksPasted` checks it). `mods.bin` stays as it was. A
  target that kept old lengths from when the mod was On has those again if it's turned back on.
  The other way, copying them even while Off, is one condition less, but it changes a mod's data
  while that mod is Off.
- **Not in the build** (`MT_HAS_TRACKLEN=0`): the call isn't compiled.
- **Color Song:** nothing to copy. Its colours follow a pattern's content, so the copy gets the
  source's colour when it's put in the song. `updatePatternBitmask` already flags the target for
  it (`colorsong::patternChanged`, flags only), and its Song-screen update reads the target from
  the card. No more than that.
- **Conditional trigs:** CE's own conditions do it on MT+, and the conditions live in the steps, so
  they come with the file. The pass counts are player state, not per pattern.
- **Humanize, Global Swing, Master FX:** project settings, not per pattern. Nothing to do.
- **Copy to New's own data:** none. It needs no `mods.bin` record, only its switch in record 0.

## 6. Threads

- **Nothing runs in the audio interrupt.** The press is a button handler and the work is in the
  Pattern screen's `update()`, both main loop. The cancel is the mod's `reset` in the registry,
  which `mods::onWorkspaceLoaded` walks from `WorkspaceProjectLoad`'s finish. No interrupt path
  reaches that (`CLAUDE.md` lists the ones that exist). No CE function that the interrupt can reach gets a hook.
- **Reads of what the interrupt writes:** `actualPattern` and the RAM pattern pointers, which
  `switchRamPatternsNow` changes together. They're read before and after the snapshot (section 3,
  step 2). The song's `playlist` and the queued pattern number are only read.
- **Playing, pattern mode:** the pattern you're on doesn't change, so nothing to watch for. The
  copy is the pattern at the moment of the snapshot.
- **Recording:** a note recorded from MIDI is written by the interrupt. If one lands during the
  snapshot's few hundred microseconds, the copy may hold part of that step (its note without its
  instrument, say). The pattern you're on is unaffected. Recording from the pads is main loop and
  can't overlap.
- **Song mode:** the song moving on between the press and the copy gives "Pattern changed - try
  again". Patterns in the song are never targets, so the song plays on as before.
- **CE's own risk, not ours:** MIDI Start in Song mode makes the interrupt save and load pattern
  files (`playSongFromPosition`). If that lands during our write, two card operations overlap.
  That already happens with CE's own pattern saves from the main loop, and our write is one more
  of them. The emulator test fires it there to check the target file and the snapshot survive; the
  card itself can only be checked on the unit.
- **Main-loop time:** at worst, in one update, one 16 KB read (the search) plus one 16 KB write.
  That's about what CE does itself on every pattern change (`saveWorkspacePatternNow` then
  `loadWorkspacePatternNow`).
- **No `double`, no heap.** `snprintf` for the message text, as CE does in the main loop.

## 7. Off = CE

- **Switch Off:** the button's list entry is skipped (`nthPatternButton`), so the other mods'
  buttons close up, and `update()` returns at once with no job. No files are written.
- **Files:** a copy is an ordinary CE pattern file, written with CE's header and CRC, so plain CE
  and Copy to New Off open it as any pattern. The only `mods.bin` change is Trk Len's, and only
  with that mod On.
- **Old projects:** 1.2.0's Copy to New kept no side file, so a 1.2.0 or plain-CE project opens with
  it Off (section 1.6 of the port plan) until it's switched on in Project Settings → Mods. New
  projects take Config → Mods.
- **`MT_NO_MODS`:** every hook in a CE file sits in `#if MT_HAS_COPYPATTERN`, so the reference
  build stays byte-identical to plain CE. Checked 2026-11-07: `main` at the merged commit (`bc3a758`)
  and the branch's `MT_NO_MODS` build, both `EDITION_NAME=CE`, give the same `.ptf` byte for byte.
- **Code changes:**
  - Ours: `core/mods/copypattern.h/.cpp` (the rules as plain functions for host tests, the job and
    `update()`), `modsBuild.h` (`MT_HAS_COPYPATTERN`), `mods.h` (`kBuiltModCount`, the calls),
    `mods.cpp` (the two calls), `modsRegistry.cpp` (its entry: `init` at boot, and `cancel` as
    its `reset` when a Workspace loads).
  - CE files, each marked `// MT:`: `fPatternEdit.cpp` (the list entry, the press function, the
    `update()` call) and `configMenu.cpp` (the switch in Project Settings → Mods and in Config →
    Mods, with descriptions).
  - No change to the sequencer, the file manager or the drawing code. `fPatternEdit.cpp` has mixed
    line endings; keep each line's own.

## 8. Display list and memory

- **Display list:** page 3 already draws all 8 labels; the button only puts text in one of them:
  "Copy to" and "New", about 36 bytes more than an empty label (one text command each, 12 bytes
  plus the string padded to 4). With all three mods on, page 3 has about 55 characters of labels,
  against page 2's 90 (Render Selection, Duplicate Pattern, and so on), so page 3 stays below a
  page CE already draws. The popup is CE's own, with a shorter text than CE's on this screen. The
  test measures both against the 8,172-byte limit on the worst grid view, and page 3 against page 2.
- **Memory**, measured: `tools/mem_budget.py` on the full build against the same build with
  `-DMT_HAS_COPYPATTERN=0`:

| Where | What | Bytes |
|---|---|---|
| SDRAM, `__NOINIT(EXTERNAL_RAM)` | the snapshot (one `strPattern`), zeroed by `copypattern::init` at boot | 16,432 |
| SDRAM | the job (source, search position, start time) and the message text | 48 |
| Flash | code, texts, the Config items' setups and descriptions, the button entry (1.2.0's code was 1,597 B) | 2,840 |
| DTCM | two Config menu items (28 each) | 56 |
| ITCM, OCRAM, heap | none | 0 |

Mods' SDRAM goes from 18,868 to 35,348 bytes of the 1 MiB (1,013,228 left). DTCM has 15,320 bytes
between heap end and stack base, above the 4,096 floor. The snapshot could share CE's own
temporary pattern buffer (`fileManagerPatternBuffer`, which the search already uses through
`loadWorkspacePatternToTempBuffer`) and save the 16 KB, but then the mod would depend on CE only
ever using that buffer from the main loop. At 1.6 % of the 1 MiB, its own buffer is the safer
choice. (Mine to make; say if you'd rather save it.)

## 9. Tests

Read for this plan: `PAST_FAILURES.md`. The case that applies is the one crash during heavy editing
that was never reproduced. Main-loop paths get the interrupt fired inside them (`--preempt`), and
this mod is all main-loop editing code, so the emulator test does that.

**Host** (`core/mods/tests/test_copypattern.cpp`, in CI): the empty-step rule (each field alone),
the empty-pattern rule on plain arrays (block, playlist, queue, performance tracks), the search
order and wrap-around, none left, and the message texts (pattern 1, 99, 100, 255).

**Emulator** (`tools/emu/test_copypattern.py`, full build with `libPolySynth.a` and `libPolyDSP.a`
in, `fakefs`, against the mods `.axf` and the `MT_NO_MODS` reference; `machine.py` holds it to
75 % of the CPU). The synth libraries are only linked in. Nothing reads or measures inside them.

1. **Button:** absent with the switch Off. With it On, in every combination of Humanize and Trk Len:
   the right slot, greyed outside edit mode, the other mods' buttons where the order puts them, and
   Trk Len's hold and second line still on its own button.
2. **Copy, stopped:** the file is what CE's `saveWorkspacePatternNow` writes for the RAM pattern,
   unsaved edits included and not the older file. CE's reader reads it back identical, with each
   track's length kept (16, 32, 64, 128 and mixed). The block is set and the project shows unsaved
   changes. Your pattern, both RAM patterns, the song position and the target's changed flag are
   untouched. Nothing is written on the press, and at most one pattern file is read per update.
3. **Copy, playing:** pattern mode with the sequencer ticking between updates; song mode, where the
   song moves on before the copy ("Pattern changed") and where it doesn't (copied). Also with
   `--preempt`, the interrupt fired inside the snapshot and the write: a pattern switch there must
   give "Pattern changed", and a MIDI Start there must leave the target file and your pattern
   intact.
4. **Search:** each reason to skip, alone: block, playlist, queued, performance tracks, a note past
   the pattern's end, only an `m`, only a `V`, a lone note-off, an unreadable file. Also an old-
   version empty file (converted, so it counts as empty), an empty file, a missing file,
   wrap-around past 255, and none left.
5. **Failures:** pattern changed between the press and the copy, project opened meanwhile, switch
   turned Off meanwhile, the file manager busy past 5 s, a write error (the file is removed, the
   block and lengths unchanged), performance mode.
6. **Track lengths:** Trk Len On: the target gets the source's lengths in place of its own, stopped
   and playing, and `Workspace/mods.bin` has them after the Workspace save. Trk Len Off:
   `mods.bin` byte-identical. A failed copy copies none.
7. **Color Song:** the target is flagged, and the Song screen reads it once it's in the song.
8. **Save and Open:** after a copy, Save, open another project, open this one again: the pattern
   and its lengths are there. Discard: neither is.
9. **Display list:** page 3 with each combination of buttons, against page 2 and the 8,172-byte
   limit on the worst grid view.
10. **Off = CE:** with the switch Off, the same session gives the same files as the reference
    build. The golden test still passes, and the `MT_NO_MODS` `.ptf` matches plain CE.

No sound test stage: the mod doesn't change what plays. `tools/run_tests.py` gets a `copypattern`
job; `--tier release` runs before the device test, with the boot rig's real Open and Play.

**As built.** The list above was the plan. What the tests do now:

- **Host:** 60 checks. Besides the rules above, the job on a fake CE: the copy in the first update,
  RAM rule-outs that skip the card, one card check per update, the 5 s busy limit, the playing gap
  (below), pattern changed, a second press ignored, Off and cancel, and performance mode at the press
  and during the search. Queued patterns and performance tracks are checked here only.
- **Emulator:** 88 checks, through CE's own file manager, writer, reader and sequencer on a fake
  card. The interrupt is simulated by a code hook: it swaps the RAM patterns inside
  `copyCurrentPatternToBuff` ("Pattern changed", nothing written), and changes both RAM patterns
  inside the card write (the file is still the snapshot). A real MIDI Start isn't run there.
- **Playing gap:** while playing, at most one card check per 100 ms, so a long search can't take the
  card from CE's own streaming for long. Not in the plan; added while building.
- **Button:** it checks that the press is bound only through edit mode's flag. The grey drawing is
  CE's, not checked.
- **Display list:** the test compares the label text page 3 draws with all three mods on (Humanize,
  Copy to / New, Trk Len) against page 2's, which CE already draws. Page 3 has less. It doesn't
  measure the 8,172-byte list itself.
- **Left out:** the old-version empty file (CE converts it before the mod sees it), the busy file
  manager (host only), Save and Open of a copy (CE's own project copy; on the device, checklist
  step 13), and Off giving the same files as the reference build (the test checks that Off writes
  nothing, and the reference build has none of the mod's symbols).

**Not covered by the tests:** the card's real timing (how long a search and a write take on a real
SD card), the popup as you see it, and two card operations at once from MIDI Start.

## 10. Hardware checklist

After `tools/run_tests.py --tier release` passes, with the lockup recorder build
(`DEVICE_TEST_LADDER.md`). The mod doesn't touch audio, so the ladder's projects are needed only for
step 1, at its lightest load. Leaves out Break FX, performance mode, Export and per-mod Saving.
Projects: `L1 1 voice none` from the ladder, and a new project you make on the unit, called
`CP test` here.

1. Open `L1 1 voice none`, press Play, wait 10 s, press Play again. `/mfx_log.txt` gets its line
   with no lockup. That's the firmware itself, before the mod is used.
2. New Project, save it as `CP test`. Config → Project Settings → Mods: Copy to New On, Humanize
   and Track Length Off.
3. Pattern 1: notes on tracks 1, 2 and 9 (a MIDI track), length 64. Don't save. Record mode on,
   More, More: the third button reads "Copy to / New". Record mode off: it's greyed out and does
   nothing.
4. Record mode on, press Copy to New: "Copied to pattern 2", and you stay on pattern 1. Go to
   pattern 2: the same notes and length 64. The project shows unsaved changes.
5. Pattern 3: one note on step 60, then length 32 (the note is now past the end). Go back to
   pattern 1, press Copy to New: "Copied to pattern 4". Patterns 2 and 3 are skipped, and pattern 3
   still has its note when you set its length back to 64.
6. Turn Humanize on: Copy to New moves to the fourth button. Turn Track Length on: Trk Len is on
   the fifth. Each still works.
7. On pattern 1, hold Trk Len and set track 1 to 5 and track 2 to 7. Copy to New: "Copied to
   pattern 5". Pattern 5 has the same lengths, shown in grey past them, and plays like pattern 1.
8. Turn Track Length Off, copy pattern 1 again (pattern 6), turn Track Length back on: pattern 6 has
   no lengths (decision 5).
9. Press Play on pattern 1 and press Copy to New while it plays: no hiccup in the sound, and the
   copy is right.
10. Song screen: make a song of patterns 1, 2, 1. Play the song, go to the Pattern screen and press
    Copy to New a few times as the song goes on. Each gives "Copied to pattern N" or "Pattern
    changed - try again", and the song plays on unchanged. None of the copies is pattern 1 or 2.
11. Live recording: record mode on, Play, play pads on pattern 1, press Copy to New. The copy has
    what was recorded up to then.
12. Stop, turn Color Song on, and on pattern 1 press Copy to New. Put that copy in the song next to
    pattern 1: its blocks have the same colours as pattern 1's.
13. Save, open `L1 1 voice none`, open `CP test` again: all the copies are there, with their lengths.
    Make one more copy, open another project and choose not to save: on reopening, that copy
    isn't there.
14. Project Settings → Mods: Copy to New Off. The button is gone and the others close up.
15. Read `/mfx_log.txt`: no lockup or fault record.

Not on the unit, emulator only: all 255 patterns full, a write error, a busy file manager, MIDI
Start during a copy, performance mode.

**Result, 2026-11-08,** recorder build of `1ff23e7` (after the mod registry merge): all 15 steps
passed. In step 10 every press gave "Copied to pattern N"; "Pattern changed - try again" never came
up, as the copy usually finishes in the update after the press, before the song can move on. The
emulator test covers that message. `/mfx_log.txt`: 50 plays, 71,397 blocks, none over time, no
stall and no lockup or fault record. Audio interrupt average 20 to 31 %, slowest 49 %. Longest main
loop gap 22 ms, lowest free heap 74,760, max stack 7,841.

## 11. Decisions

All five chosen on 2026-11-07, each as recommended.

1. **Port it at all**, now that CE's Song screen can do a full copy (Shift + Add Slot, Copy,
   Paste). Chosen: yes. It's one press on the Pattern screen, it leaves the song alone, and it has
   a safer "empty".
2. **After the copy:** stay on your pattern (1.2.0) or jump to the copy. Chosen: stay. It's what
   1.2.0 did and was tested, and jumping while the song plays would take the screen off the song's
   pattern.
3. **Which pattern counts as empty:** the 1.2.0 rule, with CE's `m` and `V` (section 4), or CE's
   Song-screen rule alone. Chosen: the 1.2.0 rule. CE's alone can lose notes past a pattern's end
   and change what the song plays.
4. **Button order:** Humanize, Copy to New, Trk Len (1.2.0's) or Humanize, Trk Len, Copy to New
   (Trk Len stays where its MT+ hardware test had it). Chosen: 1.2.0's, as 1.2.0 users know it and
   MT+ hasn't been released.
5. **Trk Len lengths when that mod is Off:** not copied, as Song's paste does, or copied anyway.
   Chosen: not copied, so a mod that's Off keeps its data unchanged.

Mine, unless you say otherwise: the search starts after your pattern and wraps (1.2.0). Pattern
names stay with their pattern number. The label is "Copy to / New", and the messages are 1.2.0's. A
5 s limit replaces 1.2.0's 2,000 updates. There's no undo, the copy is refused in performance mode,
and the snapshot has its own 16 KB buffer.

**Beyond the brief:** the performance-mode refusal, and the MIDI Start case in section 6. Neither
needs a decision unless you want the copy to work in performance mode.

## 12. Build steps, once the plan is OK'd

1. `copypattern.h` rules and host tests.
2. The job, `update()`, the button, the Config items and the `onWorkspaceLoaded` cancel.
3. `test_copypattern.py`, the `run_tests.py` job, `mem_budget.py` figures into section 8.
4. `--tier release`, the `MT_NO_MODS` comparison, a CI build, then the hardware checklist.
5. Merge into `modular` after your hardware test; you push.
