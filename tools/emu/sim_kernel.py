# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The simulator's virtual clock: parameters, random streams, audio deadlines, waits, the idle-pass skip
and the event log (docs/mt/SIMULATOR_STAGE1.md, section 1). No Unicorn here, so it can be tested alone.

The kernel decides how far the main loop runs and when interrupts fire. An engine runs the code:
sim_rig.Sim on the firmware, or a test's fake. The engine provides:

    run_slice(n)      run the main loop for n instructions; returns "count", or "skip" when it stopped at a
                      pass start after an idle pass (1.10), or "stop" for any other stop with an unknown count
    masked()          whether the audio interrupts are masked now (PRIMASK, BASEPRI, the NVIC line)
    fire(kind, t)     run one interrupt ("in" or "out") with the clock at t
    next_change(vt)   the next time a pure model's answer changes (the display's coprocessor going free),
                      or None

Every timing value here is a placeholder until stage 4 calibrates it on the unit. Nothing a run prints says
anything about the unit's timing.
"""
from __future__ import annotations

import dataclasses
import hashlib
import heapq
import json
import random
from dataclasses import dataclass, field

SAMPLE_RATE = 44100
BLOCK = 128
NS = 1_000_000_000
PERIOD_NS = (BLOCK * NS) // SAMPLE_RATE          # 2,902,494 (2,902,494.33 exactly)


@dataclass
class Params:
    """One run's parameters. Every field is serialised into the run's record, so a run can be replayed."""
    seed: int = 1
    phase_ns: int = 0                  # where the first audio deadline falls
    jitter_ns: int = 0                 # each deadline moves by a seeded offset within +-jitter_ns
    d_isr: str = "const"               # the output interrupt's time: const, or seeded between lo and hi
    d_isr_frac: float = 0.30           # const: this fraction of a block (placeholder)
    d_isr_lo: float = 0.20             # seeded: between these fractions of a block
    d_isr_hi: float = 0.45
    d_isr_in_frac: float = 0.01        # the input interrupt (placeholder)
    input_interrupt: bool = True       # off: as boot_rig.py, which never runs it
    c_main_ps: int = 2000              # a main-loop instruction, in picoseconds (placeholder: 720 MHz is ~1389)
    c_factor_lo: float = 1.0           # a seeded factor on c_main per pass, for stalls (1.4); off by default
    c_factor_hi: float = 1.0
    max_slice: int = 1000              # instructions: the clock's resolution in the main loop
    mask_step: int = 1                 # slice length while a due interrupt is held by a mask: 1, so it fires on
                                       # the first instruction after the unmask, however short the gap
    t_platform_ns: int = 500           # the device loop's own calls each pass (placeholder)
    t_copro_ns: int = 100_000          # the display's coprocessor, busy after each list (placeholder)
    t_card_call_ns: int = 100_000      # a card call (placeholder)
    t_card_byte_ps: int = 50_000       # a card byte read or written, in picoseconds (placeholder: 20 MB/s)
    cpu_mhz: int = 720                 # CPU_FREQ
    skip: bool = True                  # the idle-pass skip (1.10)
    interrupts_during_init: bool = True
    log_pass_every: int = 0            # log every Nth pass start; 0: none
    force_limit_ns: int = PERIOD_NS    # a chosen point may bring an interrupt this much earlier, at most

    def to_json(self):
        return dataclasses.asdict(self)

    @classmethod
    def from_json(cls, d):
        return cls(**d)

    def check(self):
        assert 0 <= self.jitter_ns <= PERIOD_NS // 4, "jitter is kept within a quarter of a block"
        assert self.c_main_ps > 0 and self.max_slice > 0 and self.mask_step > 0
        assert self.d_isr in ("const", "seeded")


PLACEHOLDERS = ("d_isr", "d_isr_frac", "d_isr_lo", "d_isr_hi", "d_isr_in_frac", "c_main_ps", "t_platform_ns",
                "t_copro_ns", "t_card_call_ns", "t_card_byte_ps")


def stream(seed, name):
    """A random stream of its own per purpose, so a new draw in one doesn't shift the others (1.8)."""
    digest = hashlib.sha256(f"{seed}:{name}".encode()).digest()
    return random.Random(int.from_bytes(digest[:8], "little"))


def deadline(k, phase_ns=0):
    """The k-th audio deadline (k >= 0: phase_ns itself first), rounded once from the exact 128/44100 s, so
    no drift builds up."""
    return phase_ns + (2 * k * BLOCK * NS + SAMPLE_RATE) // (2 * SAMPLE_RATE)


class Log:
    """JSON lines with a running hash. Never holds wall-clock values, so equal runs hash equal."""

    def __init__(self, path=None):
        self.hash = hashlib.sha256()
        self.file = open(path, "w", encoding="utf-8", newline="\n") if path else None
        self.lines = 0

    def write(self, entry):
        line = json.dumps(entry, sort_keys=True, separators=(",", ":"))
        self.hash.update(line.encode() + b"\n")
        self.lines += 1
        if self.file:
            self.file.write(line + "\n")

    def digest(self):
        return self.hash.hexdigest()[:16]

    def close(self):
        if self.file:
            self.file.close()
            self.file = None


@dataclass
class Counters:
    passes: int = 0
    main_insns: int = 0
    slices: int = 0
    irqs_out: int = 0
    irqs_in: int = 0
    late: int = 0
    worst_late_ns: int = 0
    held: int = 0                  # interrupts held back by a mask
    held_ns: int = 0
    worst_held_ns: int = 0
    longest_gap_ns: int = 0        # the longest time the main loop got no slice
    waits: dict = field(default_factory=dict)      # kind -> [count, ns]
    skips: int = 0
    skipped_ns: int = 0
    skips_after_fine_read: int = 0     # skipped stretches after a pass that read micros or clockCycles
    uncertain: int = 0                 # stops with an unknown instruction count
    uncertain_bound_insns: int = 0     # their worst case, summed
    forced: int = 0                    # interrupts fired at a chosen point
    refused: int = 0                   # chosen points where one couldn't fire


class Kernel:
    def __init__(self, params: Params, log: Log | None = None):
        params.check()
        self.p = params
        self.log = log or Log()
        self.vt = 0
        self.c = Counters()
        self.in_slice = False
        self.slice_start = 0
        self.slice_wait = 0
        self.irq_time = None               # the clock an interrupt sees while it runs
        self.context = "main"
        self.irqs_enabled = False
        self.k = 0                         # the next audio deadline's index (0: interrupts off)
        self.next_deadline = None
        self.last_irq_end = 0
        self.last_slice_end = 0
        self.held_since = None
        self.hold_info = None
        self.factor = 1.0
        self.fine_read = False             # this pass read micros or clockCycles
        self.inputs = []                   # (time, seq, payload)
        self._seq = 0
        self.r_jitter = stream(params.seed, "jitter")
        self.r_isr = stream(params.seed, "d_isr")
        self.r_factor = stream(params.seed, "c_factor")

    # -- the clock models read ----------------------------------------------------------------------------
    def now(self):
        """What a timer read sees: an interrupt's own time; in the main loop the slice's start plus the waits
        so far in it (up to one slice behind, the clock's resolution)."""
        if self.irq_time is not None:
            return self.irq_time
        if self.in_slice:
            return self.slice_start + self.slice_wait
        return self.vt

    def wait(self, ns, kind):
        """A modelled wait. In the main loop it adds to the slice's waiting time; interrupts that fall due
        meanwhile fire after the slice, at their deadlines (1.1). In an interrupt it's logged, not charged:
        the interrupt's time is d_isr."""
        ns = max(0, int(ns))
        w = self.c.waits.setdefault(kind, [0, 0])
        w[0] += 1
        w[1] += ns
        if self.irq_time is not None:
            self.log.write({"vt": self.irq_time, "ctx": self.context, "wait": kind, "ns": ns, "in_irq": True})
            return
        if self.in_slice:
            self.slice_wait += ns
        else:
            self.vt += ns
        if ns >= 1_000_000:
            self.log.write({"vt": self.now(), "ctx": "main", "pass": self.c.passes, "wait": kind, "ns": ns})

    # -- interrupts -------------------------------------------------------------------------------------
    def enable_interrupts(self):
        """The audio DMA starts: deadlines from the first one after now."""
        if self.irqs_enabled:
            return
        self.irqs_enabled = True
        now = self.now()
        k = max(0, (now - self.p.phase_ns) * SAMPLE_RATE // (BLOCK * NS))
        while deadline(k, self.p.phase_ns) <= now:
            k += 1
        self.k = k
        self._set_deadline()
        self.log.write({"vt": now, "ctx": "main", "event": "interrupts on", "first": self.next_deadline})

    def _set_deadline(self):
        j = self.r_jitter.randint(-self.p.jitter_ns, self.p.jitter_ns) if self.p.jitter_ns else 0
        self.next_deadline = deadline(self.k, self.p.phase_ns) + j

    def _d_isr(self):
        if self.p.d_isr == "seeded":
            frac = self.r_isr.uniform(self.p.d_isr_lo, self.p.d_isr_hi)
        else:
            frac = self.p.d_isr_frac
        return int(PERIOD_NS * frac)

    def _fire(self, eng):
        due = self.next_deadline
        held = self.held_since is not None
        start = max(self.vt if held else due, self.last_irq_end)
        if held:
            hold = self.vt - self.held_since
            self.c.held += 1
            self.c.held_ns += hold
            self.c.worst_held_ns = max(self.c.worst_held_ns, hold)
            self.held_since = None
        t = start
        if self.p.input_interrupt:
            d_in = int(PERIOD_NS * self.p.d_isr_in_frac)
            self._run_irq(eng, "in", t)
            self.c.irqs_in += 1
            t += d_in
            self.vt += d_in
        d = self._d_isr()
        self._run_irq(eng, "out", t)
        self.c.irqs_out += 1
        end = t + d
        self.vt += d
        self.vt = max(self.vt, end)
        self.last_irq_end = end
        late = start - due
        if late > 0:
            self.c.late += 1
            self.c.worst_late_ns = max(self.c.worst_late_ns, late)
        entry = {"vt": start, "ctx": "irq", "irq": self.k, "deadline": due, "late": late, "d_isr": d, "held": held}
        if held and self.hold_info:
            entry["held_by"] = self.hold_info
        self.log.write(entry)
        self.k += 1
        self._set_deadline()
        self.irq_in_pass = True

    def _force(self, eng):
        """A chosen point (1.4): the next audio interrupt fires here, if the masks allow it, brought earlier
        than its deadline by at most force_limit_ns. Logged as forced, with the shift: a lead to confirm
        with a physical run, not a failure the unit can have."""
        shift = self.next_deadline - self.vt if self.irqs_enabled else None
        entry = {"vt": self.vt, "ctx": "main", "pass": self.c.passes, "chosen": True}
        if shift is None or eng.masked() or shift > self.p.force_limit_ns:
            entry["refused"] = "interrupts off" if shift is None else "masked" if eng.masked() else "too far"
            entry["shift_ns"] = shift
            self.log.write(entry)
            self.c.refused += 1
            return
        entry["forced_shift_ns"] = max(0, shift)
        self.log.write(entry)
        self.c.forced += 1
        if shift > 0:
            self.next_deadline = self.vt
        self._fire(eng)

    def _run_irq(self, eng, kind, t):
        self.irq_time, self.context = t, kind
        try:
            eng.fire(kind, t)
        finally:
            self.irq_time, self.context = None, "main"

    # -- inputs and passes --------------------------------------------------------------------------------
    def queue_input(self, t, payload):
        heapq.heappush(self.inputs, (t, self._seq, payload))
        self._seq += 1

    def pop_input(self):
        """An input due now, delivered at a pass start, or None."""
        if self.inputs and self.inputs[0][0] <= self.now():
            t, _, payload = heapq.heappop(self.inputs)
            self.log.write({"vt": self.now(), "ctx": "main", "pass": self.c.passes, "input": payload,
                            "queued_for": t})
            return payload
        return None

    def pass_start(self):
        """Each main-loop pass: the device loop's own calls take t_platform, and a new stall factor."""
        self.c.passes += 1
        self.fine_read = False
        self.irq_in_pass = False
        if self.p.c_factor_hi != self.p.c_factor_lo or self.p.c_factor_lo != 1.0:
            self.factor = self.r_factor.uniform(self.p.c_factor_lo, self.p.c_factor_hi)
        if self.p.t_platform_ns:
            self.slice_wait += self.p.t_platform_ns if self.in_slice else 0
        if self.p.log_pass_every and self.c.passes % self.p.log_pass_every == 0:
            self.log.write({"vt": self.now(), "ctx": "main", "pass": self.c.passes})

    def _skip_target(self, eng, now):
        """Where the clock jumps after an idle pass: the earliest thing that could make the next pass
        differ (1.10)."""
        targets = [(now // 1_000_000 + 1) * 1_000_000]
        if self.irqs_enabled:
            targets.append(self.next_deadline)
        if self.inputs:
            targets.append(self.inputs[0][0])
        change = eng.next_change(now)
        if change is not None and change > now:
            targets.append(change)
        if self.until is not None:
            targets.append(self.until)
        return min(targets)

    # -- the scheduler ------------------------------------------------------------------------------------
    until = None
    irq_in_pass = False

    def _slice(self, eng, n):
        self.slice_start, self.slice_wait, self.in_slice = self.vt, 0, True
        gap = self.vt - self.last_slice_end
        self.c.longest_gap_ns = max(self.c.longest_gap_ns, gap)
        try:
            how = eng.run_slice(n)
        finally:
            self.in_slice = False
        self.c.slices += 1
        if how == "chosen":
            self.c.uncertain += 1
            self.c.uncertain_bound_insns += n
            self.vt = self.slice_start + self.slice_wait
            self.last_slice_end = self.vt
            self._force(eng)
            return
        if how == "count":
            self.c.main_insns += n
            self.vt = self.slice_start + (n * self.p.c_main_ps * self.factor) // 1000 + self.slice_wait
            self.vt = int(self.vt)
        else:
            # stopped after an unknown part of n: charged 0, and said so (1.1)
            self.c.uncertain += 1
            self.c.uncertain_bound_insns += n
            now = self.slice_start + self.slice_wait
            self.vt = now
            if how == "skip":
                target = self._skip_target(eng, now)
                self.c.skips += 1
                if self.fine_read:
                    self.c.skips_after_fine_read += 1
                if target > now:
                    self.c.skipped_ns += target - now
                    self.vt = target
        self.last_slice_end = self.vt

    def run(self, eng, until_ns, stop=lambda: False):
        """Run until the clock reaches until_ns or stop() is true. Interrupts fire at their deadlines, held
        while masked; the main loop runs in slices up to the next deadline."""
        self.until = until_ns
        try:
            while self.vt < until_ns and not stop():
                if self.irqs_enabled and self.next_deadline <= self.vt:
                    if eng.masked():
                        if self.held_since is None:
                            self.held_since = self.vt
                            info = getattr(eng, "hold_info", None)
                            self.hold_info = info() if info else None
                        self._slice(eng, self.p.mask_step)
                        continue
                    self._fire(eng)
                    continue
                limit = until_ns
                if self.irqs_enabled:
                    limit = min(limit, self.next_deadline)
                n = -(-(limit - self.vt) * 1000 // int(self.p.c_main_ps * self.factor))
                self._slice(eng, max(1, min(self.p.max_slice, n)))
        finally:
            self.until = None

    def summary(self):
        c = self.c
        return {"vt_ns": self.vt, "passes": c.passes, "main_insns": c.main_insns, "slices": c.slices,
                "irqs_out": c.irqs_out, "irqs_in": c.irqs_in, "late": c.late, "worst_late_ns": c.worst_late_ns,
                "held": c.held, "held_ns": c.held_ns, "worst_held_ns": c.worst_held_ns,
                "longest_gap_ns": c.longest_gap_ns, "waits": c.waits, "skips": c.skips,
                "skipped_ns": c.skipped_ns, "skips_after_fine_read": c.skips_after_fine_read,
                "uncertain": c.uncertain, "uncertain_bound_insns": c.uncertain_bound_insns,
                "forced": c.forced, "refused": c.refused, "log_hash": self.log.digest(), "log_lines": self.log.lines}
