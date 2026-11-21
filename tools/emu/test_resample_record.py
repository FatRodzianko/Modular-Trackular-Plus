#!/usr/bin/env python3
"""Resample's Trk Record on the real boot, with a real project's samples and synths sounding.

Usage: python tools/emu/test_resample_record.py <mods.axf> <plain CE.axf> <project folder>

Step 5 of docs/mt/RESAMPLE_PORT_PLAN.md. A tone goes into the Line In: the I2S input buffer that
audioEngine::updateInputAudioBuffers hands the engine, rewritten before each audio interrupt. In the
Sample Recorder on Line In, Shift+Rec starts each take with the pattern.

What the take is checked against, per audio interrupt n (n = A is the first one after
Recorder::record):
- I(n), the tone fed in n. CE's Line In queues get each input block one interrupt late, so a CE take
  is I(A - 1), I(A), ...
- J(n), the jacks' block. The jacks play each limiter block one interrupt later (11.7), and with the
  metronome silent the final mixer passes the limiters at unity, so J(n + 1) is the tap's block n.

Checks:
1. Trk Audio only: the take is CE's (the tone, one block late); the graph is CE's (the update list as
   long as at boot, and as the plain CE build's).
2. Trk Record, Monitor off: block j is I(A + j) + J(A + j + 1), clipped, frame for frame from the
   first frame; the jacks play what they play with Trk Audio only (a fresh run each); the graph has
   the tap's two queues at its end, asleep after the take.
3. Monitor on: block j is J(A + j + 1), the monitored tone in it once.
4. A loud tone over the mix: the sum clips.
5. A count-in take with the metronome at 100 and at 0 (fresh runs): the same take, bit for bit,
   while the jacks differ, so every click is heard and none is recorded.
6. A take that fills its space (instrument 1 made to fill sample memory to just over 10 s): it stops
   by itself, the sum holds to its last block, nothing is written after it, the tap ends empty.
7. Trk Record stored On with a mono Line In and a mic, and in the fallback (under 10 s free): the
   take is CE's and the tap doesn't run.
8. Plain CE: the tone take is I(A - 1), I(A), ...
What doesn't run here: real time, the real codec and Line In, the screen.
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_resample_sound as snd  # noqa: E402
from test_resample_shiftrec import PRE_ROLL, Shift  # noqa: E402
from test_resample_sound import KEY, LOOP_EVERY, REC, check, to_line_in  # noqa: E402

BLOCK = 128
INPUT = "_ZN11audioEngine23updateInputAudioBuffersEPvS0_i"
FIRST_UPDATE = "_ZN11AudioStream12first_updateE"
NEXT_UPDATE = 16            # AudioStream: vptr, four bytes of flags, destination_list, inputQueue, next_update
ACTIVE = 4
POOL_USED, POOL_MAX = "_ZN11AudioStream11memory_usedE", "_ZN11AudioStream15memory_used_maxE"
TAP_STORE = "_ZN4mods8resample12_GLOBAL__N_113tapQueueStoreE"
TAP_RUNNING, TAP_MISSING = "_ZN4mods8resample10tapRunningE", "_ZN4mods8resample10tapMissingE"
AVAILABLE = "_ZN16AudioRecordQueue9availableEv"
METRO_STATE, METRO_VOLUME = 232, 236  # strMtConfig.metronome: size at 230, state, preRoll (233), num, den, volume
MONITOR = 6                 # the key under Monitor: the first press selects it, the next ones switch it
LINE_IN_L, MIC_LG = 0, 5
SOURCES = snd.SOURCES
LEVEL = 8000


def tone(n, level):
    """The Line In's block n: different on each side, and different from block to block."""
    left = [int(level * math.sin(2 * math.pi * (n * BLOCK + i) / 100.0)) for i in range(BLOCK)]
    right = [int(0.7 * level * math.sin(2 * math.pi * (n * BLOCK + i) / 77.0 + 1.0)) for i in range(BLOCK)]
    return left, right


def clip(v):
    return 32767 if v > 32767 else -32768 if v < -32768 else v


class Rec(Shift):
    def __init__(self, axf, project):
        super().__init__(axf, project)
        m = self.m
        self.level = 0
        self.fed = {}
        self.jacks = {}
        self.buf = m.alloc(4 * BLOCK, 4)
        usb = m.alloc(4096, 4)
        m.write(self.buf, bytes(4 * BLOCK))
        m.write(usb, bytes(4096))
        m.call(m.addr(INPUT), self.r.engine, self.buf, usb, BLOCK, max_insns=1_000_000)
        counted = self.r.interrupt

        def fed(*a, **k):
            n = self.irq + 1                    # the number counted() gives this interrupt
            left, right = tone(n, self.level)
            self.fed[n] = (left, right)
            m.write(self.buf, struct.pack(f"<{2 * BLOCK}h", *[v for pair in zip(left, right) for v in pair]))
            out = counted(*a, **k)
            self.jacks[n] = out
            return out
        self.r.interrupt = fed
        self.restore = None

        def placed(mm):
            if self.restore is not None:
                at, value = self.restore
                self.restore = None
                m.write(at, struct.pack("<I", value))
        self.r.watch(m.addr("_ZN8Recorder9setConfigENS_6ConfigE"), placed)

    def frames_for_placement(self, at, value):
        """A word of the project's set for the take's placement only: Recorder::setConfig, right after
        hook 6, puts the old one back."""
        self.restore = (at, self.m.r32(at))
        self.m.write(at, struct.pack("<I", value))

    # -- the graph ----------------------------------------------------------------------------
    def update_list(self):
        m = self.m
        objects, p = [], m.r32(m.addr(FIRST_UPDATE))
        while p and len(objects) < 1000:
            objects.append(p)
            p = m.r32(p + NEXT_UPDATE)
        return objects

    def tap_queues(self):
        if TAP_STORE not in self.m.elf.symbols:
            return []
        s = self.m.sym(TAP_STORE)
        return [s.addr, s.addr + s.size // 2]

    def settings(self):
        """mods::resample::settings: Trk Audio, Trk Record (two bools)"""
        return tuple(self.m.read(self.m.addr("_ZN4mods8resample8settingsE"), 2))

    def tap_running(self):
        return self.m.r8(self.m.addr(TAP_RUNNING)) if TAP_RUNNING in self.m.elf.symbols else 0

    def tap_missing(self):
        if TAP_MISSING not in self.m.elf.symbols:
            return 0
        return struct.unpack("<H", self.m.read(self.m.addr(TAP_MISSING), 2))[0]

    def available(self, queue):
        return self.m.call(self.m.addr(AVAILABLE), queue) & 0xFFFFFFFF

    def pool_reset(self):
        m = self.m
        m.write(m.addr(POOL_MAX), m.read(m.addr(POOL_USED), 2))

    def pool_max(self):
        return struct.unpack("<H", self.m.read(self.m.addr(POOL_MAX), 2))[0]

    # -- the recorder -------------------------------------------------------------------------
    def to_source(self, target):
        for _ in range(SOURCES + 1):
            if self.source() == target:
                return True
            self.next_source()
        return self.source() == target

    def monitor_heard(self):
        """The pattern stopped and a tone in: Monitor On puts it on the jacks."""
        start = self.irq
        self.blocks(30)
        return max(max(map(abs, self.jacks[n][0])) for n in range(start + 10, start + 30)) > LEVEL // 4

    def set_monitor(self, on):
        level, self.level = self.level, LEVEL
        for _ in range(4):
            if self.monitor_heard() == on:
                break
            self.press(KEY[MONITOR])
        heard = self.monitor_heard()
        self.level = level
        return heard == on

    def take_start(self, during=None):
        """Shift+Rec; the take's first interrupt A."""
        mark = len(self.events)
        self.rec(shift=True)
        ok = self.wait_take(during=during)
        rec = [e for e in self.since(mark) if e[0] == "record"]
        return ok and len(rec) == 1, (rec[0][1] + 1) if rec else None

    def take_blocks(self):
        """The take, block by block: (left, right) each 128 frames; a mono take has right None."""
        left, right, length = self.take()
        m = self.m
        mono = left == right
        out = []
        for j in range(length // BLOCK):
            a = list(struct.unpack(f"<{BLOCK}h", m.read(left + 2 * BLOCK * j, 2 * BLOCK)))
            b = None if mono else list(struct.unpack(f"<{BLOCK}h", m.read(right + 2 * BLOCK * j, 2 * BLOCK)))
            out.append((a, b))
        return out

    def jacks_from(self, a, n):
        return [self.jacks[k] for k in range(a, a + n)]


def compare(take, expect, what):
    """take and expect: lists of (left, right) blocks. The first block that differs, or None."""
    for j, (got, want) in enumerate(zip(take, expect)):
        for side in (0, 1):
            if got[side] is not None and list(got[side]) != list(want[side]):
                k = next(i for i in range(BLOCK) if got[side][i] != want[side][i])
                return f"{what}: block {j} of {len(take)}, side {side}, frame {k}: {got[side][k]} != {want[side][k]}"
    return None


def as_ce(run, a, take):
    return [run.fed[a - 1 + j] for j in range(len(take))]


def summed(run, a, take):
    out = []
    for j in range(len(take)):
        i, t = run.fed[a + j], run.jacks.get(a + j + 1)
        if t is None:
            break
        out.append(([clip(x + y) for x, y in zip(i[0], t[0])], [clip(x + y) for x, y in zip(i[1], t[1])]))
    return out


def alone(run, a, take):
    return [run.jacks[a + j + 1] for j in range(len(take)) if a + j + 1 in run.jacks]


def recorder(axf, project, trk_record, metronome=None):
    """The recorder on Line In, Pre Roll off unless metronome is given, Trk Audio (and Trk Record)
    On, Monitor off."""
    run = Rec(axf, project)
    run.set_config(PRE_ROLL, 0)
    if metronome is not None:
        run.set_config(PRE_ROLL, 1)
        run.set_config(METRO_STATE, 1)
        run.set_config(METRO_VOLUME, metronome)
    check(run.enter_recorder() and to_line_in(run), "the recorder on Line In")
    if run.mods:
        run.press(KEY[2] if trk_record else KEY[1])
        run.blocks(LOOP_EVERY)
        want = (1, 1) if trk_record else (1, 0)
        check(run.settings() == want and run.engine_open() == 1,
              f"{'Trk Record' if trk_record else 'Trk Audio'} On: settings {run.settings()}, the gates open")
    check(run.set_monitor(False), "Monitor Off: the tone isn't on the jacks")
    return run


def finish(run):
    run.stop_take()
    run.back_to_setup()


def test_mods(axf, ref_axf, project):
    ref_boot = len(Rec(ref_axf, project).update_list())

    # 1 and 2: the first take of a fresh run, Trk Audio only and Trk Record
    run = recorder(axf, project, trk_record=False)
    boot = len(run.update_list())
    check(boot == ref_boot, f"at boot the update list is the plain CE build's: {boot}, {ref_boot}")
    run.level = LEVEL
    run.pool_reset()
    ok, a = run.take_start()
    check(ok, "Trk Audio only: Shift+Rec starts the take")
    check(run.tap_running() == 0, "Trk Audio only: no tap")
    run.blocks(300)
    finish(run)
    pool_audio = run.pool_max()
    take = run.take_blocks()
    check(len(take) > 250, f"Trk Audio only: the take is {len(take)} blocks")
    bad = compare(take, as_ce(run, a, take), "Trk Audio only")
    check(bad is None, "Trk Audio only: the take is CE's, the tone one block late", bad)
    jacks_audio = run.jacks_from(a, 300)
    check(max(max(map(abs, b[0])) for b in jacks_audio) > 1000, "the pattern plays in the take")
    check(len(run.update_list()) == boot, "Trk Audio only: the graph is CE's after the take")
    run.press(KEY[2])
    run.blocks(LOOP_EVERY)
    check(run.settings() == (1, 1) and len(run.update_list()) == boot, "Trk Record On: the graph CE's until a take")

    # 3. Monitor on, the same run
    check(run.set_monitor(True), "Monitor On: the tone on the jacks")
    run.level = LEVEL
    ok, a = run.take_start()
    check(ok and run.tap_running() == 1, "Monitor on: the take starts, the tap runs")
    graph = run.update_list()
    tap = run.tap_queues()
    check(len(graph) == boot + 2 and graph[-2:] == tap, f"the tap's two queues at the end of the update list: {len(graph)}")
    run.blocks(300)
    finish(run)
    take = run.take_blocks()
    bad = compare(take, alone(run, a, take), "Monitor on")
    check(bad is None and len(take) > 250, f"Monitor on: the take is the tap alone, frame for frame ({len(take)} blocks)", bad)
    check(run.tap_missing() == 0, f"Monitor on: no tap block missing ({run.tap_missing()})")
    check(run.tap_running() == 0 and all(run.available(q) == 0 for q in tap), "after it: the tap ended and empty")
    run.blocks(4)
    check(all(run.m.r8(q + ACTIVE) == 0 for q in tap), "the idle tap asleep (no block given to it)")
    check(run.set_monitor(False), "Monitor Off again")

    # 4. loud
    run.level = 30000
    ok, a = run.take_start()
    run.blocks(200)
    finish(run)
    take = run.take_blocks()
    want = summed(run, a, take)
    bad = compare(take, want, "loud")
    clipped = sum(1 for j, (i, t) in enumerate(zip([run.fed[a + j] for j in range(len(want))],
                                                   [run.jacks[a + j + 1] for j in range(len(want))]))
                  for x, y in zip(i[0] + i[1], t[0] + t[1]) if abs(x + y) > 32767)
    check(ok and bad is None and clipped > 0, f"a loud sum clips: {clipped} samples clipped", bad)

    # 6. a take that fills its space
    used = run.m.r32(run.used_memory)
    memory0 = run.used_memory - 4 - 48 * 36
    stereo = run.m.r8(memory0 + 9) or run.m.r32(memory0 + 4) != run.m.r32(memory0)
    channels = 2 if stereo else 1
    ram_frames = memory0 + 12
    end = run.bank + run.bank_size
    room = 2 * (10 * 44100 * 2 + 40 * 256)              # a little over 10 s of stereo
    fill_from = end - room
    run.m.write(fill_from, bytes([0xA5]) * room)
    run.frames_for_placement(ram_frames, (fill_from - run.bank) // (2 * channels))
    run.level = LEVEL
    ok, a = run.take_start()
    left, right, _ = run.take()
    check(ok and run.tap_running() == 1 and fill_from <= left < fill_from + 64,
          f"a take with just over 10 s free: moved ({left:#x}), the tap runs")
    for _ in range(5000):
        if not run.recording():
            break
        run.blocks(LOOP_EVERY)
    check(not run.recording(), "the take stopped by itself at the end of its space")
    left, right, length = run.take()
    per_channel = right - left
    check(right + per_channel <= end and per_channel % 256 == 0, f"each channel {per_channel} bytes, inside sample memory")
    take = run.take_blocks()
    bad = compare(take, summed(run, a, take), "full")
    check(bad is None, f"the full take holds the sum to its last block ({len(take)} blocks)", bad)
    after = [bytes(run.m.read(base + 2 * length, per_channel - 2 * length)) for base in (left, right)]
    check(all(x.count(0xA5) == len(x) for x in after) and per_channel == (end - left) // 2 // 256 * 256,
          f"nothing written after the take's end in either channel ({len(after[0])} bytes each)")
    check(run.tap_running() == 0 and all(run.available(q) == 0 for q in tap), "the full take's end: the tap ended and empty")
    run.blocks(LOOP_EVERY * 4)
    run.press(KEY[7])                                       # CE's full-memory window
    run.back_to_setup()
    check(run.engine_open() == 1, "back on the setup: the gates open")

    # 7. Trk Record stored On, but not recorded
    for source, name in ((LINE_IN_L, "Line In (L)"), (MIC_LG, "Mic LG")):
        check(run.to_source(source), f"{name} selected")
        run.blocks(LOOP_EVERY)
        check(run.settings() == (1, 1), f"{name}: Trk Record still stored On")
        ok, a = run.take_start()
        check(ok and run.tap_running() == 0, f"{name}: the take starts without the tap")
        run.blocks(100)
        finish(run)
        take = run.take_blocks()
        want = [(f[0], None) for f in as_ce(run, a, take)]
        bad = compare(take, want, name)
        check(bad is None and take and take[0][1] is None, f"{name}: the take is CE's, the left input mono", bad)
    check(run.to_source(snd.LINE_IN), "Line In again")
    run.blocks(LOOP_EVERY)
    run.frames_for_placement(ram_frames, run.bank_size // (2 * channels) - 1000)
    ok, a = run.take_start()
    check(ok and run.take()[0] == run.bank and run.tap_running() == 0,
          "under 10 s free with Trk Record: CE's buffer, no tap")
    run.blocks(100)
    finish(run)
    take = run.take_blocks()
    bad = compare(take, as_ce(run, a, take), "fallback")
    check(bad is None, "the fallback take is CE's", bad)
    check(not run.r.failures, f"no failures in the run: {run.r.failures[:4]}")
    del run

    # 2. Trk Record, Monitor off, the first take of a fresh run
    run = recorder(axf, project, trk_record=True)
    run.level = LEVEL
    run.pool_reset()
    ok, a = run.take_start()
    check(ok and run.tap_running() == 1, "Trk Record, Monitor off: the take starts, the tap runs")
    run.blocks(300)
    finish(run)
    pool_record = run.pool_max()
    take = run.take_blocks()
    bad = compare(take, summed(run, a, take), "Monitor off")
    check(bad is None and len(take) > 250,
          f"Monitor off: the take is the Line In plus the tap, frame for frame ({len(take)} blocks)", bad)
    check(run.tap_missing() == 0, f"Monitor off: no tap block missing ({run.tap_missing()})")
    jacks = run.jacks_from(a, 300)
    differ = [i for i in range(300) if jacks[i] != jacks_audio[i]]
    check(not differ, "the jacks with Trk Record: what they play with Trk Audio only, bit for bit",
          f"{len(differ)} differ, first {differ[:5]}")
    check(pool_record - pool_audio <= 4, f"the pool: {pool_record} blocks with Trk Record, {pool_audio} without")
    check(not run.r.failures, f"no failures in the run: {run.r.failures[:4]}")
    del run

    # 5. the metronome
    takes, jacks, counts = [], [], []
    for volume in (100, 0):
        run = recorder(axf, project, trk_record=True, metronome=volume)
        check(run.set_monitor(True), f"metronome {volume}: Monitor On")
        start = run.irq
        ok, a = run.take_start()
        check(ok and run.tap_running() == 1, f"metronome {volume}: a count-in, then the take with the tap")
        run.blocks(300)
        finish(run)
        takes.append(run.take_blocks())
        jacks.append(run.jacks_from(start + 1, a - start - 1 + 300))
        counts.append(a - start - 1)
        if volume == 0:
            bad = compare(takes[-1], alone(run, a, takes[-1]), "metronome 0")
            check(bad is None, "metronome 0: the take is the jacks' sound", bad)
        check(not run.r.failures, f"metronome {volume}: no failures: {run.r.failures[:4]}")
        del run
    check(counts[0] == counts[1] and counts[0] > 100, f"both count-ins {counts} blocks")
    count_in = [i for i in range(counts[0]) if jacks[0][i] != jacks[1][i]]
    in_take = [i for i in range(counts[0], len(jacks[0])) if jacks[0][i] != jacks[1][i]]
    check(count_in, f"the count-in's clicks heard: {len(count_in)} blocks differ on the jacks")
    check(in_take, f"the take's clicks heard: {len(in_take)} blocks differ on the jacks")
    bad = compare(takes[0], takes[1], "metronome")
    check(bad is None and len(takes[0]) == len(takes[1]), "the take with the metronome at 100: no click in it", bad)


def test_ce(ref_axf, project):
    run = Rec(ref_axf, project)
    check(run.enter_recorder() and to_line_in(run), "plain CE: the recorder on Line In")
    check(run.set_monitor(False), "plain CE: Monitor Off")
    run.level = LEVEL
    mark = len(run.events)
    run.press(REC)
    check(run.wait_take(), "plain CE: Rec")
    rec = [e for e in run.since(mark) if e[0] == "record"]
    a = rec[0][1] + 1
    run.blocks(200)
    finish(run)
    take = run.take_blocks()
    bad = compare(take, as_ce(run, a, take), "plain CE")
    check(bad is None and len(take) > 150, f"plain CE: the take is the tone one block late ({len(take)} blocks)", bad)
    check(not run.r.failures, f"plain CE: no failures: {run.r.failures[:4]}")


def main():
    mods_axf, ref_axf, project = sys.argv[1:4]
    test_ce(Path(ref_axf), Path(project))
    test_mods(Path(mods_axf), Path(ref_axf), Path(project))
    print("failures:", snd.failures)
    return 1 if snd.failures else 0


if __name__ == "__main__":
    sys.exit(main())
