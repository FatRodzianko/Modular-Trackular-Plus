# Device tests go up in steps

Every firmware that touches audio or the interrupt goes to the unit this way, after
`tools/run_tests.py --tier release` passes. The tests can't show anything that takes real time, so this
is where that gets found out, one step at a time, with a firmware that explains itself if it stops.

## The diagnostic build

`CUSTOM_DEFINES=-DMT_LOCKUP_RECORDER=1`. Make doesn't track `-D` flags, so touch the files that test it
when switching (`core/source/Tracker.cpp`, `core/mods/mods.cpp`, `core/mods/lockupRecorder.cpp`, and in
`platforms/imx/source`, `TimeControl.cpp`, `PlatformTrackerIMX.cpp`). Other builds
compile none of it. `tools/emu/test_lockup_recorder.py` tests it.

| Event | What it does |
|---|---|
| Hard, bus, MPU or usage fault | Saves the location and fault registers, then restarts. CE's handlers spin for ever |
| Main loop stopped for 3 s while playing, or 20 s while stopped | SysTick runs above the audio interrupt, so it still fires. It saves where the code was, whether the audio interrupt was active, the share of the last second spent in it, and return addresses from the stack, then restarts |
| Each Stop | Adds a line to `/mfx_log.txt`: average and slowest interrupt, blocks over time, blocks back to back, longest main loop gap, pool, heap and stack |
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
| `N6 8 voices PERC` | 8 | PERC | Stock + Stock | The most memory: 53,508 B of heap free on the rig, against about 80,000 B |
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
is set by hand on `S5`: on each of the three parts, Wave 1 and Wave 2 on Super Saw, Distortion on
Distortion with Placement Pre, Effect on Phaser. Instructions per block for that, from
[CE_PORT_PLAN.md](CE_PORT_PLAN.md) 3.15: about 719,000, against 528,000 for 8 voices of VAP.

## What a firmware comes with

Each one handed over says what ran, what didn't, and the risk to the unit.
