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

## What a firmware comes with

Each one handed over says what ran, what didn't, and the risk to the unit.
