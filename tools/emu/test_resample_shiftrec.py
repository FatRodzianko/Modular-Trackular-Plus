#!/usr/bin/env python3
"""Resample's Shift+Rec and the count-in's Play, on the real boot with a real project.

Usage: python tools/emu/test_resample_shiftrec.py <mods.axf> <plain CE.axf> <project folder>

Step 4 of docs/mt/RESAMPLE_PORT_PLAN.md. In the Sample Recorder on Line In with Trk Audio on:
1. Pre Roll off: Shift+Rec starts the take and the pattern from its start together, also over a
   pattern already playing; a plain Rec starts nothing.
2. Pre Roll on: Shift+Rec, or Play in the count-in, starts the pattern with the take and nothing
   plays during the count-in; Shift+Play there starts the song; a plain Rec in the count-in never
   disarms; a count-in with nothing pressed starts nothing.
3. An external clock's count-in given up by Rec (PR 97) leaves nothing armed.
4. Trk Audio off, the mod Off and plain CE: Shift+Rec is a plain Rec, and Play in the count-in
   plays at once, as CE does.

"Together" means Sequencer::play runs after Recorder::record in the same main-loop pass, with no
audio interrupt between them. What doesn't run here: real time, the screen.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_resample_sound as snd  # noqa: E402
from test_resample_sound import KEY, PLAY, REC, SHIFT, LOOP_EVERY, Run, check, to_line_in  # noqa: E402

# strMtConfig: header (10 bytes), audioCodecConfig (16, at 12 for its float), firmware (8),
# midi at 36 (size, then clkIn), values (150), general (18), interface (12), metronome at 230
# (size, state, preRoll). 248 bytes in all, as the symbol's size; preRoll is checked by a read.
CLK_IN, PRE_ROLL = 38, 233
CLOCK_INTERNAL, CLOCK_USB = 0, 1
CONFIG_SIZE = 248


class Shift(Run):
    def __init__(self, axf, project):
        super().__init__(axf, project)
        m = self.m
        self.irq = 0
        interrupt = self.r.interrupt

        def counted(*a, **k):
            self.irq += 1
            return interrupt(*a, **k)
        self.r.interrupt = counted
        self.events = []
        for name, sym in (("record", "_ZN8Recorder6recordEv"), ("play", "_ZN9Sequencer4playEh"),
                          ("song", "_ZN9Sequencer20playSongFromPositionEh"), ("stop", "_ZN9Sequencer4stopEv"),
                          ("count-in", "_ZN9Sequencer16showPreRollPopupEv")):
            self.r.watch(m.addr(sym), self._event(name))
        self.config = m.addr("mtConfig")

    def _event(self, name):
        def on(mm):
            self.events.append((name, self.irq, mm.arg(1) if name == "play" else None))
        return on

    def since(self, mark):
        return self.events[mark:]

    def set_config(self, at, value):
        self.m.write(self.config + at, bytes([value]))

    def pre_roll_on(self):
        m = self.m
        return m.call(m.addr("_ZN9Sequencer17isPreRollConfigOnEv"), m.addr("sequencer")) & 0xFF

    def rec(self, shift=False):
        self.press(REC, held=SHIFT if shift else None)

    def wait_take(self, limit=6000, during=None):
        """Main loops and interrupts until the take records. during() each pass before it does."""
        for _ in range(limit):
            if self.recording():
                return True
            if during is not None:
                during()
            self.loop_block()
        return False


def started_together(run, mark, what):
    """The take's record() and then play() (or the song's start) with no interrupt between."""
    ev = run.since(mark)
    rec = [e for e in ev if e[0] == "record"]
    starts = [e for e in ev if e[0] == what]
    if len(rec) != 1 or not starts:
        return False, f"events {[e[:2] for e in ev][:12]}"
    after = [e for e in starts if ev.index(e) > ev.index(rec[0])]
    if not after:
        return False, f"{what} only before the take: {[e[:2] for e in ev][:12]}"
    return after[0][1] == rec[0][1], f"record at interrupt {rec[0][1]}, {what} at {after[0][1]}"


def no_start(run, mark):
    ev = run.since(mark)
    rec = [i for i, e in enumerate(ev) if e[0] == "record"]
    return rec and not any(e[0] in ("play", "song") for e in ev[rec[0]:]), [e[:2] for e in ev][:12]


def finish(run):
    run.stop_take()
    run.back_to_setup()


def recorder_on_line_in(axf, project, trk_audio, mod_off=False):
    run = Shift(axf, project)
    if mod_off:                         # the switch latches at the visit's start
        run.m.write(run.m.addr("_ZN4mods8switchesE") + snd.RESAMPLE_ID, bytes([0]))
        run.m.write(run.m.addr("_ZN4mods8resample8settingsE"), bytes([1, 0]))
    check(run.m.sym("mtConfig").size == CONFIG_SIZE, "strMtConfig's size as the layout above",
          str(run.m.sym("mtConfig").size))
    run.set_config(PRE_ROLL, 1)
    check(run.pre_roll_on() == 1, "Pre Roll's byte found")
    run.set_config(PRE_ROLL, 0)
    check(run.pre_roll_on() == 0, "Pre Roll off")
    check(run.enter_recorder() and to_line_in(run), "the recorder on Line In")
    if trk_audio and run.mods:
        run.press(KEY[1])
        run.blocks(LOOP_EVERY)
        check(run.engine_open() == 1, "Trk Audio On: the gates open")
    return run


def test_mods(axf, project):
    run = recorder_on_line_in(axf, project, trk_audio=True)

    # 1. Pre Roll off
    mark = len(run.events)
    run.rec(shift=True)
    check(run.wait_take(), "Shift+Rec: the take starts")
    ok, detail = started_together(run, mark, "play")
    check(ok, "Shift+Rec: the pattern starts with the take", detail)
    play = [e for e in run.since(mark) if e[0] == "play"]
    check(play and play[-1][2] == 0, "from its first step", str(play[-1:]))
    heard = run.blocks(300)
    check(run.playing() and run.peak(heard) > 1000, f"the pattern is heard in the take: peak {run.peak(heard)}")
    finish(run)
    check(not run.playing() and run.engine_open() == 1, "the take's end stopped it; back on the setup, open")

    run.press(PLAY)
    run.blocks(200)
    mark = len(run.events)
    run.rec(shift=True)
    check(run.wait_take(), "Shift+Rec over a playing pattern: the take starts")
    ev = run.since(mark)
    ok, detail = started_together(run, mark, "play")
    stopped = any(e[0] == "stop" for e in ev)
    check(ok and stopped, "the playing pattern starts again from its start, with the take", detail)
    finish(run)

    mark = len(run.events)
    run.rec()
    check(run.wait_take(), "a plain Rec: the take starts")
    ok, detail = no_start(run, mark)
    check(ok and not run.playing(), "a plain Rec starts nothing", str(detail))
    finish(run)

    # 2. Pre Roll on
    run.set_config(PRE_ROLL, 1)

    def silent_count_in(label, presses=(), shift_rec=False):
        """Rec (Shift+Rec) with Pre Roll on; presses [(button, held)] a little into the count-in.
        Returns the mark, and whether anything played before the take."""
        mark = len(run.events)
        run.rec(shift=shift_rec)
        played = {"any": False}
        counted = [e for e in run.since(mark) if e[0] == "count-in"]
        check(counted and not run.recording(), f"{label}: the count-in starts")
        run.blocks(60)
        for button, held in presses:
            run.press(button, held=held)
            run.blocks(20)

        def during():
            if run.playing():
                played["any"] = True
        check(run.wait_take(during=during), f"{label}: the take starts after the count-in")
        return mark, played["any"]

    mark, played = silent_count_in("Shift+Rec, Pre Roll on", shift_rec=True)
    ev = run.since(mark)
    count_in = next(e[1] for e in ev if e[0] == "record") - next(e[1] for e in ev if e[0] == "count-in")
    print(f"     the count-in took {count_in} blocks")
    ok, detail = started_together(run, mark, "play")
    check(ok and not played, "Shift+Rec, Pre Roll on: silent count-in, the pattern starts with the take", detail)
    finish(run)

    mark, played = silent_count_in("Play in the count-in", presses=[(PLAY, None)])
    ok, detail = started_together(run, mark, "play")
    check(ok and not played, "Play in the count-in arms: the pattern starts with the take, not before", detail)
    finish(run)

    mark, played = silent_count_in("Shift+Play in the count-in", presses=[(PLAY, SHIFT)])
    ok, detail = started_together(run, mark, "song")
    check(ok and not played, "Shift+Play in the count-in: the song starts with the take", detail)
    finish(run)

    mark, played = silent_count_in("a plain Rec in an armed count-in", presses=[(REC, None)], shift_rec=True)
    ok, detail = started_together(run, mark, "play")
    check(ok and not played, "a plain Rec in the count-in never disarms", detail)
    finish(run)

    mark, played = silent_count_in("a count-in with nothing pressed")
    ok, detail = no_start(run, mark)
    check(ok and not played and not run.playing(), "a count-in with nothing pressed starts nothing", str(detail))
    finish(run)

    # 3. an external clock's count-in, given up by Rec
    run.set_config(CLK_IN, CLOCK_USB)
    mark = len(run.events)
    run.rec(shift=True)
    run.blocks(2 * count_in)
    check(not run.recording() and any(e[0] == "count-in" for e in run.since(mark)),
          "external clock: the count-in waits for a clock that doesn't come (twice the internal one's length)")
    run.rec()
    run.blocks(2 * count_in)
    check(not run.recording() and not run.playing(), "Rec gives the count-in up (PR 97): no take, still none after as long again")
    run.set_config(CLK_IN, CLOCK_INTERNAL)
    run.set_config(PRE_ROLL, 0)
    mark = len(run.events)
    run.rec()
    check(run.wait_take(), "then a plain Rec with Pre Roll off: the take starts")
    ok, detail = no_start(run, mark)
    check(ok and not run.playing(), "nothing was left armed by the given-up count-in", str(detail))
    finish(run)

    # 4. Trk Audio off
    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    check(run.trk_audio() == 0 and run.engine_open() == 0, "Trk Audio Off: shut")
    plain_ce(run, "Trk Audio off")
    check(not run.r.failures, f"no failures in the run: {run.r.failures[:4]}")
    del run

    run = recorder_on_line_in(axf, project, trk_audio=False, mod_off=True)
    run.blocks(LOOP_EVERY)
    check(run.trk_audio() == 1 and run.engine_open() == 0, "the mod Off with Trk Audio stored On: shut")
    plain_ce(run, "the mod Off")
    check(not run.r.failures, f"no failures in the mod Off run: {run.r.failures[:4]}")


def plain_ce(run, label):
    """CE's recorder: Shift+Rec is a plain Rec; Play in the count-in plays at once."""
    run.set_config(PRE_ROLL, 0)
    mark = len(run.events)
    run.rec(shift=True)
    check(run.wait_take(), f"{label}: Shift+Rec starts a take")
    ok, detail = no_start(run, mark)
    check(ok and not run.playing(), f"{label}: Shift+Rec starts no pattern", str(detail))
    check(run.take()[0] == run.bank, f"{label}: the take is CE's")
    run.stop_take()
    run.back_to_setup()

    run.set_config(PRE_ROLL, 1)
    mark = len(run.events)
    run.rec()
    run.blocks(60)
    check(not run.recording(), f"{label}: Rec with Pre Roll on counts in")
    played_at = len(run.events)
    run.press(PLAY)
    plays = [e for e in run.events[played_at:] if e[0] == "play"]
    check(plays and run.playing() and not run.recording(), f"{label}: Play in the count-in plays at once, as CE")
    check(run.wait_take(), f"{label}: the take starts after the count-in")
    if run.playing():
        run.press(PLAY)                 # CE's take doesn't stop the pattern: Play does
    run.stop_take()
    run.back_to_setup()
    run.set_config(PRE_ROLL, 0)


def test_ref(axf, project):
    run = recorder_on_line_in(axf, project, trk_audio=False)
    plain_ce(run, "plain CE")
    check(not run.r.failures, f"plain CE: no failures: {run.r.failures[:4]}")


def main():
    mods_axf, ref_axf, project = sys.argv[1:4]
    test_mods(Path(mods_axf), Path(project))
    test_ref(Path(ref_axf), Path(project))
    print("failures:", snd.failures)
    return 1 if snd.failures else 0


if __name__ == "__main__":
    sys.exit(main())
