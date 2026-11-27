# Device tests go up in steps

Every firmware that touches audio or the interrupt goes to the unit this way, after
`tools/run_tests.py --tier release` passes. The tests can't show anything that takes real time, so this
is where that gets found out, one step at a time, with a firmware that explains itself if it stops.

## The diagnostic build

`CUSTOM_DEFINES=-DMT_LOCKUP_RECORDER=1`. Make doesn't track `-D` flags, so touch the files that test it
when switching (`core/source/Tracker.cpp`, `core/mods/mods.cpp`, `core/mods/lockupRecorder.cpp`,
`core/libs/Audio/synth_PolySynth.cpp`, and in
`platforms/imx/source`, `TimeControl.cpp`, `PlatformTrackerIMX.cpp`). Other builds
compile none of it. `tools/emu/test_lockup_recorder.py` tests it.

| Event | What it does |
|---|---|
| Hard, bus, MPU or usage fault | Saves the location and fault registers, then restarts. CE's handlers spin for ever |
| Main loop stopped for 3 s while playing, or 20 s while stopped | SysTick runs above the audio interrupt, so it still fires. It saves where the code was, whether the audio interrupt was active, the share of the last second spent in it, and return addresses from the stack, then restarts |
| Each Stop | Adds a line to `/mfx_log.txt`: average and slowest interrupt, blocks over time, blocks back to back, how often CE's synth went over its 2.1 ms limit (each one silences the synth for 32 blocks), longest main loop gap, pool, heap and stack |
| Next start, at the Pattern screen | Writes what was saved to `/mfx_log.txt` |

The saved record is in SDRAM, with the essentials in SNVS as well.

How to read a lockup line:

| The line says | It means |
|---|---|
| A fault, with an address | Look up `pc` in the `.axf`. An address inside the closed libraries only tells us it was there; we don't look further |
| Main loop stalled, audio interrupt active, about 1024 of the last 1024 ms | The interrupt takes all the CPU, so the main loop never runs: overload |
| Main loop stalled, audio interrupt not active, `pc` the same each time | The main loop is stuck waiting on something |
| Nothing logged, and it needed unplugging again | Interrupts were off, or the CPU itself locked up. The recorder can't see that |

Cost, in the diagnostic build only: 3,344 B of flash, 1,512 B of DTCM, 160 B of SDRAM, no ITCM.

## The steps

`python tools/emu/make_masterfx_test_project.py "<project>" build/device/ladder --ladder` writes the
projects. Copy them into the card's `Projects` folder. Lightest first, and stop at the first problem.
For each: open the project, press Play on the Pattern screen, wait about 10 seconds, press Play again
to stop. Nothing needs judging by ear.

| Project | Voices | Reverb + delay | What it isolates |
|---|---|---|---|
| `L1 1 voice none` | 1 | Stock + Stock | The diagnostic firmware itself |
| `L2 1 voice Tape` | 1 | Stock + Tape | The lightest effect (18,900 instructions a block) |
| `L3 1 voice Spring` | 1 | Spring + Stock | The lightest reverb (34,000) |
| `L4 1 voice Shimmer BBD` | 1 | Shimmer + BBD | The heaviest pair, at the lightest load |
| `L5 4 voices none` | 4 | Stock + Stock | Voices alone |
| `L6 4 voices Shimmer BBD` | 4 | Shimmer + BBD | |
| `L7 8 voices none` | 8 | Stock + Stock | Voices alone, at the limit. 1.2.0 went over 100 % here |
| `L8 8 voices Shimmer BBD` | 8 | Shimmer + BBD | The project that locked up |

Each step sounds every voice all the time, which no normal song does, so it's a load test and not a
promise about songs. If L7 stops the unit, Master FX isn't the cause of that stop.

The first run, 2026-11-05: fine to `L4`, choppy screen at `L5` and `L6`, stalled at `L7` and `L8`.
[MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md) section 8 has the readings. These projects use the
source project's patches, so `L1` to `L6` measure CHIP.

## The second set: which engines reach 8 voices

`python tools/emu/make_masterfx_test_project.py "<project>" build/device/no_chip --engines` writes them. All
three parts use one engine with its Init patch, so nothing depends on the source project's sound. A stall
on one engine says nothing about the next, so this set doesn't stop at the first problem.

| Project | Voices | Engine | Reverb + delay | What it isolates |
|---|---|---|---|---|
| `N1 4 voices CHIP Init` | 4 | CHIP | Stock + Stock | Against `L5`: how much of CHIP's cost is that patch |
| `N2 8 voices ACD` | 8 | ACD | Stock + Stock | |
| `N3 8 voices FAT` | 8 | FAT | Stock + Stock | |
| `N4 8 voices VAP` | 8 | VAP | Stock + Stock | |
| `N5 8 voices WTFM` | 8 | WTFM | Stock + Stock | |
| `N6 3 voices PERC` | 3 | PERC | Stock + Stock | The most memory: 53,508 B of heap free on the rig, against about 80,000 B. PERC allows 1 voice a part |
| `N7 8 voices DIRT` | 8 | DIRT | Stock + Stock | |
| `N8 8 ACD Shimmer BBD` | 8 | ACD | Shimmer + BBD | Master FX at 8 voices, if `N2` runs |

These are Init patches, so a real patch on the same engine can cost more or less.

The first run, 2026-11-05: all eight ran with no stall.
[MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md) section 8 has the readings.

## The fourth set: Master FX after the CHIP fix

`python tools/emu/make_masterfx_test_project.py "<project>" build/device/masterfx_fixed --sound` writes the two new
projects. The rest are the first set's. The firmware is the recorder build with the CHIP fix and upstream's
optimisations.

| Project | Voices | What it isolates |
|---|---|---|
| `L8 8 voices Shimmer BBD` | 8 | The load that locked up |
| `R2 Real project sends` | The project's own | A real project with its own Shimmer + BBD, and every instrument and part sending 40 to both. The source's samples send nothing |
| `A1 Master FX sound` | 1 | Short notes with gaps, to hear each effect and check the screens |

The first run, 2026-11-05: all three worked, `L8` at about 80 % and `R2` at about 60 %.
[MASTERFX_PORT_PLAN.md](MASTERFX_PORT_PLAN.md) section 8 has the readings.

## The Synths set: WAVE and PHASE

`python tools/emu/make_masterfx_test_project.py "<project>" build/device/synths --synths` writes them,
the same way as the second set: all three parts on one engine with its Init patch. The firmware is a
mod build (plain CE resets a part on an engine it doesn't know). Lightest first; stop at the first
problem.

| Project | Voices | Engine | Reverb + delay | What it isolates |
|---|---|---|---|---|
| `S1 1 voice WAVE Init` | 1 | WAVE | Stock + Stock | The engine itself |
| `S2 1 voice PHASE Init` | 1 | PHASE | Stock + Stock | |
| `S3 4 voices WAVE Init` | 4 | WAVE | Stock + Stock | |
| `S4 8 voices PHASE Init` | 8 | PHASE | Stock + Stock | The lighter engine at the limit |
| `S5 8 voices WAVE Init` | 8 | WAVE | Stock + Stock | |
| `S6 8 WAVE Shimmer BBD` | 8 | WAVE | Shimmer + BBD | Master FX on top |

The heaviest case can't be written into a project (patches are the closed library's format), so it
is set by hand on `S5`: on each of the three parts, Wave 1 and Wave 2 on Super Saw; with Synth FX
On, button 3: FX1 the Distortion, FX2 the Phaser (Placement Pre, in each voice, is gone since WAVE
became a `.psyn`). Instructions per block for that with the old built-in effects and Pre, from [CE_PORT_PLAN.md](CE_PORT_PLAN.md) 3.15: about 719,000, against
528,000 for 8 voices of VAP; the slots' share through the plugins is in 3.17's phase 3. With Synth
FX Off, WAVE and PHASE have no effects (since 3.17 phase 3).

## The Synth FX set: the insert on CE's engines

`python tools/emu/make_masterfx_test_project.py "<project>" build/device/synthfx --synthfx` writes them:
all three parts on one engine with its Init patch, and a `mods.bin` with Synth FX and Master FX On and
each part's effect settings in record 16 (the rest of the source's `mods.bin` kept). A mod build with
Synth FX. Lightest first; stop at the first problem. "Worst FX" is Distortion at full Gain into the
Phaser, Mix 100 %, Feedback +90 %, on every part.

| Project | Voices | Engine | Synth FX | Reverb + delay | What it isolates |
|---|---|---|---|---|---|
| `F1 1 voice ACD Overdrive` | 1 | ACD | Overdrive | Stock + Stock | The insert at its lightest |
| `F2 1 voice PERC worst FX` | 1 | PERC | Worst FX | Stock + Stock | A stereo part |
| `F3 4 voices ACD worst FX` | 4 | ACD | Worst FX | Stock + Stock | Mono parts sent as stereo |
| `F4 3 voices PERC worst FX` | 3 | PERC | Worst FX | Stock + Stock | Three stereo parts, PERC's most |
| `F5 3 PERC worst Shimmer BBD` | 3 | PERC | Worst FX | Shimmer + BBD | Master FX on top |
| `F6 8 voices DIRT worst FX` | 8 | DIRT | Worst FX | Stock + Stock | The heaviest engine on the unit (`N7`, 83 %) with the insert |

Before 2026-11-10, `N6`, `F4` and `F5` stored 4, 2 and 2 PERC voices, which the UI can't save. CE clamped
them to 1 a part only when the project before had another engine, so a run's figures depended on the order.
`make_masterfx_test_project.py` now refuses a count over the engine's limit.

On each, the patch editor's button 3 reads "Synth FX" and shows the part's rows, so the settings can
be checked and changed by hand there.

## The Synth FX plugin set: the slots, WAVE at the limit, a user's plugin and the guards

Since 3.17 phase 2, Synth FX plays through `.ptsfx` plugins in `/SynthFX/`. The F set above still
works: its projects hold version 1 settings, which the firmware converts into the two slots, so
its figures compare with the built-in effects'. This set adds what only plugins have.

`python tools/emu/make_masterfx_test_project.py "<project>" build/device/synthfx_plugins --synthfx-plugins`
writes the projects, with record 16 version 3 (each slot's plugin and knobs; the Placement bytes 0), and
`tools/synthfx/device/build.sh` writes the card's extra plugins to `build/device/synthfx_card/SynthFX`:
the SDK's Delay and Tremolo, built as a user would, and two guard tests that are not for music.
GuardSlow passes the sound through after a busy loop: Light is well under the time guard's quarter
block, Heavy well over. GuardHang's first block never returns.

| Project | Voices | Engine | Synth FX | What it isolates |
|---|---|---|---|---|
| `P1 1 voice ACD Delay` | 1 | ACD | FX2 the Delay | A user's plugin from the card, opened from a project |
| `P2 8 WAVE worst FX` | 8 | WAVE | Distortion, Phaser at Feedback +90 % and Mix 100 %, every part | WAVE as a `.psyn` with both slots, at the limit (before 2026-11-13: `P2 8 WAVE Pre worst FX`, the Distortion in each voice) |
| `G1 1 voice ACD GuardSlow` | 1 | ACD | FX1 GuardSlow | The time guard. Since CE's 2.1 ms synth limit (2026-11-14), Heavy trips it in its first block on the unit (about 2.3 ms there), and the slot is bypassed in that block: one silence of 32 blocks (93 ms), then the part plays; the recorder's "Synth over its limit" reads 1 |
| `G2 1 voice ACD GuardHang` | 1 | ACD | FX1 GuardHang | The start marker: the unit hangs, the recorder restarts it, safe mode |

GuardHang goes on the card only for `G2`, after everything else: on 2026-11-12 it was there from
the start, and turning FX1's list past the Phaser hung the unit twice. A list then made each plugin
it was turned over; since then a pick is made only once the encoder rests 0.4 s, but resting on
GuardHang still hangs the unit. `G2` hangs the unit on purpose, so it is last and only on the recorder build. The hang comes at the
Open, with the sequencer stopped, so the recorder restarts the unit after its 20 s. CE then opens the
Workspace, which is `G2`, and the marker must keep its plugin from loading. Take
`/SynthFX/GuardHang.ptsfx` off the card after it. If the unit hangs again at every start, the marker
failed: take the card out and delete that file on a computer.

The emulator ran each before the unit: `test_synthfx.py`'s `guards` (GuardSlow, the Delay, safe
mode, the marker before a plugin's first block) and the release tier's `boot synthfx hang` (CE's own
Open of `G2` hangs inside the plugin with the marker on the card and no file open, and a second start
from that card is in safe mode and plays). Neither has a clock, so the time guard's reading there is
instructions taken as cycles.

## The Master FX plugin set: the slots from the card, Glue, Compressor, and the time guard

Since `masterfx-2.0`, Master FX plays through `.ptmfx` plugins in `/MasterFX/`; the firmware writes
its pack there when a file is missing, and since `masterfx-2.1` replaces a pack file that is still
as an earlier release shipped it. In 2.1, 2.0's Glue is Compressor, and Glue is a new bus compressor
with its own Attack, Release and Threshold and a Tracks list. `python tools/emu/make_masterfx_test_project.py "<project>"
build/device/masterfx_glue --masterfx-plugins` writes the projects (`--masterfx-duck` writes `M4`,
`--masterfx-glue` `M5` and `--masterfx-compressor` `M6`): all three parts on ACD with its Init patch, and a `mods.bin` with Master
FX On and its record 4 (version 3 since 2.1: five own knobs a slot, then the Ducks or Tracks set). `tools/masterfx/device/build.sh`
writes the card's extra plugins to `build/device/masterfx_card/MasterFX`: GuardSlow (and the other
guard tests, which are for the emulator), and the SDK's Room and Ducker as a user would build them.

| Project | Voices | Record 4 | What it isolates |
|---|---|---|---|
| `M1 8 ACD Shimmer BBD` | 8 | version 1: Shimmer + BBD by number, which today's release reads too | The heaviest pair on both firmwares, the same project: the plugins' cost against the built-in effects |
| `M2 8 ACD Shimmer BBD Glue` | 8 | version 3: Shimmer, BBD and Glue by file, Glue on the master | The three slots at once, at the limit |
| `M3 1 voice ACD GuardSlow` | 1 | version 3: GuardSlow in the dynamics slot, Load Heavy | The time guard: about 600,000 instructions a block, over its eighth of a block on any count, so the slot is bypassed 8 blocks after it starts and the Master page's Limiter column reads Bypassed. The master passes through, so the voice plays on |
| `M4 1 voice ACD Room Ducker` | 1, plus a kick on track 1 and a closed hat on track 3 | version 3: Room and Ducker, the Ducks list on Master | Written by `--masterfx-duck`. The Ducks list and the own knobs: the kick as the Sidechain key ducking Track 3, then Synth 1 as well; Ducker's Depth and Curve, Room's Width, then Compressor's Ratio and Makeup. Needs Room and Ducker on the card. The play rig (`PlayRig`) doesn't load samples; the bus test boots the firmware (`BootRig`), which does |
| `M5 1 voice ACD Glue drums` | 1, plus M4's kick and hat | version 3: Glue on Track 1 and Track 3 (the Tracks list), Attack 1.0 ms, Threshold -30 dB, Ratio 10:1, Makeup +6 dB | Written by `--masterfx-glue`. Glue on a bus of the two drum tracks: the drums squashed together, the held synth voice untouched. The rig's `bus` test checks the same in the emulator: sample for sample with CE's mixers on Hard Clip under full scale, and against CE's Saturate curve with Saturate |
| `M6 1 voice ACD Compressor duck` | 1, plus M4's kick and hat | version 3: Compressor, the Ducks list on Synth 1; in project.mt CE's Sidechain on Track 1 and Threshold -20 dB | Written by `--masterfx-compressor`. Compressor's page since 2.1: the Sidechain list in column 3, the Ducks list in column 4, the Gain Reduce meter above Back. The kick ducks the held synth voice on every hit. In the emulator, booted and played: the synth drops to about 1 % on the kick and is under 70 % for about 96 ms, then back to full |

Both firmwares are lockup recorder builds: today's release (`CE_20261113`, `d9a6795`) runs `M1` only,
the plugin firmware all three. Lightest first is `M3`, but it needs GuardSlow on the card, so it
comes last; it doesn't hang.

The emulator ran each before the unit: `tools/masterfx/test_guards.py` (the release and quick tiers'
`masterfx guards`: GuardSlow Heavy bypassed after exactly 8 blocks, the output and gain guards, the
marker and safe mode with GuardHang), `test_masterfx_rig.py play` (8 voices with Shimmer + BBD + Glue
on a real project) and `tools/masterfx/compare_cost.py` (the release tier's `masterfx cost`: each slot
against today's release). None of
them has a clock, so the time guard's reading there is instructions taken as cycles. GuardHang isn't
run on the unit: the emulator covers the marker, and Synth FX's `G2` showed the recorder's restart.

## The DCO6 set: the Synths mod's third engine, and Unison

`python tools/emu/make_dco6_test_projects.py <mods.axf> "<project>" build/device/dco6/Projects` writes
them: all three parts on DCO6, every voice held. E1 to E3 have no patch files, so the firmware starts
the parts on DCO6's Init. E4's and E5's patches are written by the firmware in the sound rig
(`Synth::savePatchToWorkspace`) and loaded back there before they go in. The firmware is a mod build
with `DCO6.psyn` in the card's `System/Synths` folder (`Synths` before CE's 94cdc96). Lightest first; stop at the first problem.

| Project | Voices | Patch | What it isolates |
|---|---|---|---|
| `E1 1 voice DCO6 Init` | 1 | Init | The engine itself |
| `E2 4 voices DCO6 Init` | 4 | Init | |
| `E3 8 voices DCO6 Init` | 4 + 2 + 2 | Init | 8 voices, the limit (18,900 instructions a voice in the emulator; WAVE 14,900) |
| `E4 8 DCO6 Unison 4` | 8 on Synth 1 | Unison 4, Detune +25 ct, Width 100 % | The unison hook in CE's host, in the interrupt: 2 stacks of 4, stereo |
| `E5 8 voices DCO6 heavy` | 4 + 2 + 2 | all four sources, HPF 80 Hz, Bass Boost, Resonance 70 %, Vintage 100 %, the Random Smooth LFO on cutoff | The heaviest patch (22,700 a voice) |

E4's four held notes make two stacks (each note takes a stack of four; the third and fourth take the
first two's), so all 8 voices sound, and the sound is wide. Its log line is the hook's first run on
the unit: note-ons there run in the audio interrupt.

## The Resample set: the recorder with the tracker in it

`python tools/emu/make_resample_test_projects.py "<project>" build/device/resample_card` writes the two
Resample projects, and the ladder's `--ladder` into the same folder gives `L1`, `L5` and `L7`. First
`L1`, `L5` and `L7` as in the steps above, then the plan's ten steps
([RESAMPLE_PORT_PLAN.md](RESAMPLE_PORT_PLAN.md) section 8) on these:

| Project | What's in it | What it isolates |
|---|---|---|
| `RS1 Resample test` | The source's patterns, samples and synth patches, every sample and synth part sending 40 to the delay and the reverb; Resample Off and never set | The buttons, Trk Audio, Shift+Rec, Trk Record, instruments in the recorder, Save and reopen |
| `RS2 1.2.0 buttons` | The source with a 1.2.0 `resample.bin`, both buttons On, and Resample never set in `mods.bin` | The importer on the unit's card: opening it turns Resample and both buttons On |
| `L7 8 voices none` | The ladder's 8 held voices | A Trk Record take at the voice limit: the tap's cost and the pool |

The emulator opens both RS projects through CE's Open and reads what the steps expect: RS1 Resample
Off, both buttons Off; RS2 Resample On, both On. The tap's own work is about 2,780 instructions a
block in the emulator (about 0.7 % of the CPU by 1.2.0's figure), its code in flash.

## What a firmware comes with

Each one handed over says what ran, what didn't, and the risk to the unit.
