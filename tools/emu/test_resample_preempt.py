#!/usr/bin/env python3
"""Resample under preemption: the recorder's keys and pads with the audio interrupt firing inside them.

Usage: python tools/emu/test_resample_preempt.py <mods.axf> <project folder> [--seeds 1,2] [--takes 20]

Step 7 of docs/mt/RESAMPLE_PORT_PLAN.md (section 7: `--preempt` across the gate calls, the tap's
construction and the record store; the heap after 20 takes; the pool with all 8 synth voices). On a
device the buttons and pads are read in the main loop and the audio interrupt can come at any
instruction the firmware hasn't masked. Here every main loop, button press and pad press runs with the
interrupt fired every few thousand instructions, on the stack the device would use, and held back
while the NVIC mask (AudioNoInterrupts) or PRIMASK is on, as a device holds it. MIDI notes come inside
those interrupts, as the MIDI handlers run on the unit.

Per seed, in the Sample Recorder on Line In: Trk Audio and Trk Record On, Play and Stop, then N takes
(Rec, or Shift+Rec every other one), each with a pad held and MIDI notes coming in during it, the
crop screen left each time; Trk Audio pressed while pads are held; the visit left. It checks: no
assert or rig failure; the NVIC mask and PRIMASK off at the end and the mask calls balanced; the tap
never missed a block; no note left open; the pattern untouched; the pool back where it started; the
heap the same after the takes as after the first; the gates shut after leaving.

Then the project the release tier holds 8 synth voices in (make_masterfx_test_project "held"): the
pool's peak during a Trk Record take with the pattern playing, under 80 blocks.

What this can't show: real time, the real codec and Line In, the card.
"""
import argparse
import random
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_resample_sound as snd  # noqa: E402
import test_resample_instruments as ti  # noqa: E402
from test_resample_sound import KEY, PLAY, REC, SHIFT, LOOP_EVERY, check, to_line_in  # noqa: E402
from machine import STACK_TOP, STUB_RET  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.arm_const import (UC_ARM_REG_BASEPRI, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK,  # noqa: E402
                               UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP)

GAPS = (2000, 40000)                    # instructions between interrupts
POOL_USED, POOL_MAX = "_ZN11AudioStream11memory_usedE", "_ZN11AudioStream15memory_used_maxE"
TAP_MISSING = "_ZN4mods8resample10tapMissingE"
ENGINE_OPEN = "_ZN4mods8resample10engineOpenE"
TAP_STORE, TAP_STORE_SIZE = "_ZN4mods8resample12_GLOBAL__N_113tapQueueStoreE", 0x90
STACK_FILLED = 20480 - 0x400            # boot_rig.fill_stack paints all but the top 1 KB
TAP_LIMIT = 3800                        # 1 % of the CPU (test_bundle_cost.py's figure)


class Pre(ti.Inst):
    def __init__(self, axf, project, seed):
        super().__init__(axf, project)
        r = self.r
        r.rng = random.Random(seed)
        self.fired = 0
        self.midi_queue = []            # (on, pitch) to send inside the next interrupts
        self.midi_sent = 0
        plain_interrupt = r.interrupt

        def interrupt(stack=None):
            if self.midi_queue:
                on, pitch = self.midi_queue.pop(0)
                name = "_Z15handleUsbNoteOnhhh" if on else "_Z16handleUsbNoteOffhhh"
                r.in_interrupt = True
                try:
                    self.m.call(self.m.addr(name), 1, pitch, 100 if on else 0, max_insns=50_000_000, stack=stack)
                finally:
                    r.in_interrupt = False
                self.midi_sent += 1
            return plain_interrupt(stack=stack)
        r.interrupt = interrupt
        r.loop = lambda max_insns=None: self._add_fired(r.loop_preempted(*GAPS))
        r.press = lambda button: self._held(button, True)
        r.release = lambda button: self._held(button, False)
        m = self.m
        m.write(m.addr("mtConfig") + ti.NOTES_IN, bytes([ti.NOTES_IN_USB]))
        m.write(m.addr("mtConfig") + ti.NOTES_CHANNEL, bytes([0]))
        m.write(m.addr("mtConfig") + ti.MIDDLE_C, bytes([2]))

    def _add_fired(self, n):
        self.fired += n

    def _held(self, button, down):
        (self.r.held.add if down else self.r.held.discard)(button)
        self.call_preempted("_Z12onButtonPushh" if down else "_Z15onButtonReleaseh", button)

    def call_preempted(self, name, *args):
        """name(args) as main-loop code, the audio interrupt firing inside it as in loop_preempted."""
        r, m = self.r, self.m
        uc = m.uc
        for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            uc.reg_write(reg, v)
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP - 0x100)
        uc.reg_write(UC_ARM_REG_LR, STUB_RET | 1)
        pc = m.addr(name) | 1
        while True:
            try:
                uc.emu_start(pc, STUB_RET, count=r.rng.randint(*GAPS))
            except UcError as e:
                raise RuntimeError(f"emulation stopped at pc={uc.reg_read(UC_ARM_REG_PC):#010x}: {e}") from e
            now = uc.reg_read(UC_ARM_REG_PC)
            if now == STUB_RET:
                return uc.reg_read(UC_ARM_REG_R0)
            pc = now | 1
            if r.audio_masked or uc.reg_read(UC_ARM_REG_PRIMASK) or uc.reg_read(UC_ARM_REG_BASEPRI):
                r.masked += 1
                continue
            state = uc.context_save()
            r.interrupt(stack=(uc.reg_read(UC_ARM_REG_SP) - 0x68) & ~7)
            uc.context_restore(state)
            self.fired += 1

    def pad_down(self, pad):
        self.call_preempted("_Z9onPadPushh", pad)
        self.held.add(pad)
        self.r.loop()

    def pad_up(self, pad):
        self.call_preempted("_Z12onPadReleaseh", pad)
        self.held.discard(pad)
        self.loop_block()

    def u16(self, name):
        return struct.unpack("<H", self.m.read(self.m.addr(name), 2))[0]


def recorder_run(axf, project, seed, takes):
    run = Pre(axf, project, seed)
    m, r = run.m, run.r
    hash0 = run.pattern_hash()
    r.fill_stack()
    masks0 = dict(r.interrupts)          # boot enables once before any disable
    check(run.enter_recorder() and to_line_in(run), f"seed {seed}: the recorder on Line In")
    run.press(KEY[1])
    run.press(KEY[2])
    run.blocks(LOOP_EVERY)
    check(m.read(m.addr("_ZN4mods8resample8settingsE"), 2) == b"\x01\x01" and m.r8(m.addr(ENGINE_OPEN)) == 1,
          f"seed {seed}: Trk Audio and Trk Record On, the gates open")
    run.press(PLAY)
    run.blocks(100)
    run.press(PLAY)
    run.blocks(60)
    pool0 = run.u16(POOL_USED)
    heaps = []
    for take in range(takes):
        shift = take % 2 == 1
        if shift:                       # Shift+Rec: the pattern starts with the take
            run.press(REC, held=SHIFT)
            for _ in range(2000):
                if run.recording():
                    break
                run.loop_block()
        else:
            run.start_take()
        if not run.recording():
            check(False, f"seed {seed}: take {take + 1} starts")
            break
        pad = take % 16
        run.midi_queue.append((True, 60 + take % 12))
        run.blocks(10)
        run.pad_down(pad)
        run.blocks(20)
        run.midi_queue.append((False, 60 + take % 12))
        run.pad_up(pad)
        run.blocks(20)
        if run.playing():
            run.press(PLAY)
        run.stop_take()
        run.back_to_setup()
        run.blocks(LOOP_EVERY * 2)
        heaps.append(r.free_heap())
    check(len(heaps) == takes and len(set(heaps[1:])) <= 1 and (not heaps or heaps[-1] == heaps[0]),
          f"seed {seed}: the heap the same after each of {len(heaps)} takes ({sorted(set(heaps))})")
    check(run.u16(TAP_MISSING) == 0, f"seed {seed}: the tap never missed a block ({run.u16(TAP_MISSING)})")

    # Trk Audio pressed while pads are held, both ways
    run.pad_down(3)
    run.pad_down(4)
    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    run.all_up()
    run.pad_down(5)
    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    run.all_up()
    run.blocks(60)
    check(not run.open_notes(), f"seed {seed}: no note left open", str(run.open_notes()))
    check(run.pattern_hash() == hash0, f"seed {seed}: nothing written to the pattern")
    check(run.u16(POOL_USED) <= pool0, f"seed {seed}: the pool no higher than where it started "
          f"({run.u16(POOL_USED)} / {pool0}; the effects' tails hold a few)")

    check(run.leave_recorder(after_take=True), f"seed {seed}: leave the recorder")
    check(m.r8(m.addr(ENGINE_OPEN)) == 0 and run.live_on() == 0, f"seed {seed}: left: the gates shut, no live rule")
    uc = m.uc
    check(not r.audio_masked and not uc.reg_read(UC_ARM_REG_PRIMASK),
          f"seed {seed}: the NVIC mask and PRIMASK off at the end")
    masks = {k: r.interrupts[k] - masks0[k] for k in masks0}
    check(masks["disable"] == masks["enable"] and masks["disable"] > 0,
          f"seed {seed}: the mask calls balanced during the run ({masks})")
    print(f"     seed {seed}: {run.fired} interrupts inside main-loop code, {r.masked} held back by a mask, "
          f"{run.midi_sent} MIDI events in interrupts, max stack {r.max_stack()} B")
    check(r.max_stack() < STACK_FILLED, f"seed {seed}: the stack stayed inside the painted part ({r.max_stack()} B)")
    check(run.fired > 100 * takes and run.midi_sent >= 2 * takes, f"seed {seed}: the interrupt fired inside the presses")
    check(not r.failures, f"seed {seed}: no failures: {r.failures[:4]}")


class TapCost:
    """Instructions in the tap's own work, per audio interrupt: the mod's two record queues' update
    and hook 12's copyBlock (which takes the place of CE's memcpy, so this is an upper bound on
    what the tap adds). Nothing in the synth host or the .psyn pool runs inside these calls."""

    def __init__(self, run):
        m, r = run.m, run.r
        self.m, self.r = m, r
        store = m.addr(TAP_STORE)
        queue_update = m.addr("_ZN16AudioRecordQueue6updateEv") & ~1
        copy_block = m.addr("_ZN4mods8resample9copyBlockEiPsPKs") & ~1
        self.blocks, self.calls = [], 0
        state = {"ret": None, "n": 0}

        def code(uc, address, size, user):
            if state["ret"] is not None:
                if address == state["ret"]:
                    state["ret"] = None
                else:
                    state["n"] += 1
                return
            if address == copy_block or (address == queue_update and
                                         store <= uc.reg_read(UC_ARM_REG_R0) < store + TAP_STORE_SIZE):
                state["ret"] = uc.reg_read(UC_ARM_REG_LR) & ~1
                state["n"] += 1
                self.calls += 1
        self.hook = m.uc.hook_add(UC_HOOK_CODE, code)
        m.uc.ctl_flush_tb()
        plain = r.interrupt

        def interrupt(stack=None):
            state["n"] = 0
            out = plain(stack=stack)
            self.blocks.append(state["n"])
            return out
        self.plain, r.interrupt = plain, interrupt

    def stop(self):
        self.m.uc.hook_del(self.hook)
        self.m.uc.ctl_flush_tb()
        self.r.interrupt = self.plain


def pool_run(axf, project):
    """8 synth voices held: the pool during a Trk Record take with the pattern playing."""
    import make_masterfx_test_project as maker
    with tempfile.TemporaryDirectory() as temp:
        held = maker.make(project, Path(temp), "held", keep_masterfx=True)
        run = Pre(axf, held, 1)
        m = run.m
        m.write(m.addr(POOL_MAX), m.read(m.addr(POOL_USED), 2))
        run.press(PLAY)
        run.blocks(300)
        normal = run.u16(POOL_MAX)
        run.press(PLAY)
        run.blocks(100)
        check(run.enter_recorder() and to_line_in(run), "8 voices: the recorder on Line In")
        run.press(KEY[1])
        run.press(KEY[2])
        run.blocks(LOOP_EVERY)
        run.press(PLAY)
        run.blocks(100)
        m.write(m.addr(POOL_MAX), m.read(m.addr(POOL_USED), 2))
        check(run.start_take(), "8 voices: the Trk Record take starts with the pattern playing")
        tap = TapCost(run)
        heard = run.blocks(300)
        tap.stop()
        take = run.u16(POOL_MAX)
        run.stop_take()
        print(f"     8 voices: pool peak {normal} in normal playback, {take} in the Trk Record take")
        print(f"     the tap's own instructions per block: worst {max(tap.blocks)}, average "
              f"{sum(tap.blocks) // len(tap.blocks)} over {len(tap.blocks)} blocks, {tap.calls} calls")
        check(min(tap.blocks) > 0 and max(tap.blocks) <= TAP_LIMIT,
              f"the tap runs every block and costs at most {TAP_LIMIT} instructions a block ({max(tap.blocks)})")
        check(run.peak(heard) > 1000, f"8 voices: heard during the take (peak {run.peak(heard)})")
        check(take < 80 and take - normal <= 4, f"8 voices: the take's pool peak under 80, at most 4 over playback "
              f"({take} / {normal})")
        check(not run.r.failures, f"8 voices: no failures: {run.r.failures[:4]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mods", type=Path)
    ap.add_argument("project", type=Path)
    ap.add_argument("--seeds", default="1,2")
    ap.add_argument("--takes", type=int, default=20)
    a = ap.parse_args()
    for seed in (int(s) for s in a.seeds.split(",")):
        recorder_run(a.mods, a.project, seed, a.takes)
    pool_run(a.mods, a.project)
    print("failures:", snd.failures)
    return 1 if snd.failures else 0


if __name__ == "__main__":
    sys.exit(main())
