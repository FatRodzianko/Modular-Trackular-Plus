# What the unit has already shown

Read this before planning a mod's tests, and turn anything that applies into a named test. Add to it
whenever the unit shows something new. Sources are in Modular-Trackular unless marked MT+.

A pass on any test here means that failure didn't come back in what the test ran. It doesn't mean a
firmware is safe on the unit. [DEVICE_TEST_LADDER.md](DEVICE_TEST_LADDER.md) is the step after the tests.

| What happened on the unit | Why the emulator passed | Test now | What that test can't show |
|---|---|---|---|
| **Play locked the unit up** with 8 voices, Shimmer and BBD; it had to be unplugged. Found on 2026-11-05: sustained overload of the audio interrupt, and it happens with Master FX on Stock too (MT+, `MASTERFX_PORT_PLAN.md` section 8) | The rig skipped the real boot, main loop, Play button and interrupt timing | `tools/emu/boot_rig.py`: real boot, Open, Play button, main loop. `--preempt` fires the interrupt inside main-loop code | Anything that takes real time. It has passed on this project on both builds, so the rig doesn't reproduce the lockup |
| **The first Freeverb hung at boot**, and the unit needed the pinhole (`FINDINGS.md` section 19). Stress found no stray writes or runaway loops, which pointed at timing | No clock, and no interrupt in the middle of other code | The ladder and the lockup recorder | The rig still has no clock |
| **130 % on the Config screen, glitches, no lockup** (`TIMING_HEADROOM_PLAN.md` section 14). That figure is the worst block in each second, not an average | No clock | `tools/emu/test_bundle_cost.py` for our own code. The recorder logs each Play's average and slowest block on the unit | The closed libraries aren't counted, by licence |
| **8 allocated voices read about 80 % idle**, as an allocated voice renders whether it sounds or not (same section) | No clock | The ladder adds voices step by step and logs each | Only the unit can say |
| **A colour table at `0x20280000` froze the Song screen**: the MPU allowed RAM that doesn't exist, and the fault handler spins for ever (`FINDINGS.md` section 11) | The emulator once mapped too much memory | `machine.py` maps only what the unit has, so an access outside it stops the test. `tools/emu/stress_masterfx.py` also watches all other memory | Addresses a run doesn't reach |
| **NaN settings silence the stock reverb until a restart** (`FINDINGS.md` section 19) | Not tested at first | `stress_masterfx.py`: garbage settings, then each effect must sound again after a clear | Stock keeps this problem; the UI can't send NaN |
| **One crash during heavy editing**, never reproduced (`FINDINGS.md` section 19) | Unknown | `--preempt` on main-loop paths. The recorder would now catch it on a test firmware | Editing paths aren't driven by the rig yet |
| **A wrong measurement from a test bug**: memory hooks added after code had run undercounted reads 10 to 100 times (Modular-Trackular's `CLAUDE.md`) | A test tool's own limit | Hooks go in before any code runs, then `ctl_flush_tb` | Other tool limits we haven't met yet |

## Test bugs that gave wrong answers on MT+

Each of these hid or invented a result, so check a new rig for the same kind of mistake.

| Bug | Effect | Now |
|---|---|---|
| The rig used its own `Synth`, not TrackerImpl's | Sequencer notes went nowhere, and the pool figure was too low | The rig uses TrackerImpl's |
| Effects counted from the wrong path | 65 of 80 for the pool, then 80 of 80. The real Open, Play and main loop give 55 of 80 | Quote pool figures from `boot_rig.py` only |
| The interrupt fired while the firmware had it masked | A crash in the closed libraries during Open, on both builds, which looked like a CE bug. It went away once the rig honoured `AudioNoInterrupts` and treated enable and disable as a state, since they don't nest | A crash under `--preempt` first needs checking against the masks |
| The rig didn't check it ran what it claimed | It looked like Master FX testing, with both effects still Stock | `boot_rig.py` prints the active effects, notes and pool |

## Calibration

The rig should agree with the unit where both can read. If it stops agreeing, the rig is wrong.

| Reading | Unit | `boot_rig.py` |
|---|---|---|
| Free heap after boot | 80,472 to 80,708 B | 80,620 B |
| Max stack | 7,777 B; 7,333 to 7,729 B on the ladder | 7,280 B with the interrupt fired inside main-loop code |
| Free heap on the ladder, Stock | 84,340 B at 1 voice, 83,460 B at 4 | 85,864 B and 84,996 B: about 1,500 B high |
| Block pool peak on the ladder | 34 of 80 | 49 at 1 and 4 voices, 55 at 8: the rig reads high, so it's a safe bound and not a reading |

The recorder's slowest block agreed with the Config screen's maximum on the unit, both 125 %.
