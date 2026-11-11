# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Level B for the Synths mod: WAVE (64) and PHASE (65) through CE's real synth host, on the full
build with the closed libraries in (tools/emu/sound_rig.py: CE's start-up, Synth, audioEngine and
its updateAudioBuffers, the interrupt's own entry). The libraries run as they are and nothing
hooks inside them (sound_rig's Guard); what comes out is what is checked.

0. The Init values: what each voice renders with is sy9's Init, with the macros as the model
   picker leaves them; the engines' loudness keeps level A's relation.
1. Each engine: a note sounds and stops after its release, the same twice, no asserts, the audio
   interrupt turned back on as often as off.
2. 8 voices, CE's total across its 3 parts: an 8-note chord on one part, every effect and the
   Distortion in each place, and 3 parts sharing the 8 (3 + 3 + 2): sound, no asserts, no
   clipping run, the heap and the block pool held.
3. The SDK's rules: no malloc and no createMetadata in the audio interrupt (every block, notes
   on and off, chords, effects switched); notes reach the engines from Synth's own calls.
4. Dynamic names: Distortion set to Bit Crusher (through the patch, as the editor sets it) names
   Gain "Bits" and Tone "Rate", back to Overdrive "Gain" and "Tone"; how long the rebuild keeps
   the audio interrupt off (instructions, against a block's worth).
5. Engine switches: 30 rounds of WAVE, PHASE, CHIP and back on a part, notes held: no heap growth
   after the first round.
6. The switch Off: a part already on WAVE or PHASE keeps playing, sample for sample as On.
7. Patch files: a WAVE patch saved and loaded back restores a changed value.
8. Off = CE: the stock engines are checked mods build against reference build, sample for sample,
   by tools/emu/test_sound_rig.py.

Usage: python tools/emu/test_synths.py build/emu/TrackerPlus_MT_full.axf
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_S0

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sound_rig import SoundRig, peak  # noqa: E402

WAVE, PHASE, CHIP = 64, 65, 8
P_EFFECT, P_DIST, P_GAIN, P_TONE, P_PLACEMENT = 62, 67, 68, 69, 70
SYNTHS_MOD = 7                      # Mod::synths
KNOB = 0                            # Patch::ParamChangeSource::kKnob
BLOCK_BUDGET = 1_700_000            # a block at 44.1 kHz on a 600 MHz core, in cycles: an instruction count
                                    # over it would certainly drop audio while interrupts are off
CHORD = (48, 52, 55, 59, 62, 65, 69, 72)
RENDER = {WAVE: "_ZN7mtsynthL15wt_voice_renderEPNS_8wt_voiceEPKfPff",
          PHASE: "_ZN7mtsynthL15ph_voice_renderEPNS_8ph_voiceEPKfPff"}
VELOCITY = 100 / 127                # Synth::triggerTestNote's, through the Velocity macro to Volume (39)
# Level A (test_synths_dsp.py, sy9's engines): note 60 on the Init patches with Mix -1 (oscillator 1
# alone: Init's two are detuned 8 cents, so a short window's level depends on their starting phases,
# which come from each voice's address), RMS over blocks 8-40: WAVE 0.1525, PHASE 0.1253, WAVE +1.7 dB.
# Through the host the two should keep that within 2 dB.
P_MIX = 4                           # both engines
WAVE_OVER_PHASE_DB = 1.71

failures: list[str] = []
checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def f32(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", v))[0]


class Rig(SoundRig):
    def __init__(self, axf):
        super().__init__(axf)
        m = self.m
        self.in_block = False
        self.malloc_in_block = 0
        self.meta_calls = {"block": 0, "other": 0}
        for name in ("malloc", "_malloc_r", "calloc", "_calloc_r", "realloc", "_realloc_r"):
            if name in self.syms and self.syms[name].addr:
                self.watch(self.syms[name].addr, self._on_malloc)
        self.meta = [self._own("WaveSynthVoice14createMetadataE"), self._own("PhaseSynthVoice14createMetadataE")]
        for a in self.meta:
            self.watch(a, self._on_meta)
        self._get_patch = self.first("_ZN5Synth8getPatchEi")
        self._full_name = self.first("_ZN5Synth16getParamFullName")
        self._save = self.first("_ZN5Synth9savePatchEiPKc")
        self._load = self.first("_ZN5Synth9loadPatchEiPKc")
        self.switches = m.addr("_ZN4mods8switchesE")

    def set_instrument(self, part, type_id):
        """As the model picker does (SynthEditor.cpp): the type, then the part's settings with its
        macros at their defaults (bipolar at the centre) sent to the engine."""
        super().set_instrument(part, type_id)
        settings = self.m.alloc(512, 4)
        self.m.call(self.first("_ZN5Synth21initSynthPartSettingsEiR17SynthPartSettings"), self.synth, part,
                    settings, max_insns=10_000_000)
        self.m.call(self.first("_ZN11audioEngine28refreshSynthInstrumentParamsER17SynthPartSettingsi"),
                    self.engine, settings, part, max_insns=10_000_000)

    def voice_values(self, render_fn, note=60, blocks=3):
        """The parameter values a voice renders with: p at our render function's entry (r1)."""
        seen = []
        self.watch(self.m.addr(render_fn),
                   lambda mm: seen.append(struct.unpack("<72f", mm.read(mm.uc.reg_read(UC_ARM_REG_R1), 4 * 72))))
        self.m.uc.ctl_flush_tb()
        self.blocks(2)
        self.note(0, note, True)
        self.blocks(blocks)
        return seen[-1]

    def _own(self, part):
        hits = sorted((n for n in self.syms if part in n and "lambda" not in n and "UlR" not in n), key=len)
        if not hits:
            raise KeyError(part)
        return self.syms[hits[0]].addr

    def _on_malloc(self, mm):
        if self.in_block:
            self.malloc_in_block += 1

    def _on_meta(self, mm):
        self.meta_calls["block" if self.in_block else "other"] += 1

    def block(self):
        self.in_block = True
        try:
            return super().block()
        finally:
            self.in_block = False

    # -- the patch, through its own interface (Patch.h's virtual order) ------------------------
    def patch(self, part):
        return self.m.call(self._get_patch, self.synth, part, max_insns=1_000_000)

    def open_code(self):
        """The code ranges outside the closed libraries (ITCM and flash), merged: hooks for counting
        go only here, so nothing is counted or traced inside the libraries."""
        if not hasattr(self, "_open"):
            closed = sorted((a, b) for a, b, _ in self.guard.ranges)
            merged = []
            for a, b in closed:
                if merged and a <= merged[-1][1] + 256:        # padding between a library's own functions
                    merged[-1][1] = max(merged[-1][1], b)
                else:
                    merged.append([a, b])
            self._open = []
            for lo, hi in ((0x0, 0x20000), (0x60000000, 0x61000000)):
                at = lo
                for a, b in merged:
                    if b <= lo or a >= hi:
                        continue
                    if a > at:
                        self._open.append((at, a - 1))
                    at = max(at, (b + 1) & ~1)       # code is 2-aligned; a range may end odd
                if at < hi:
                    self._open.append((at, hi - 1))
        return self._open

    def count_open(self, fn) -> int:
        n = [0]
        hooks = [self.hook_range(a, b, lambda uc, x, s, u: n.__setitem__(0, n[0] + 1)) for a, b in self.open_code()]
        self.m.uc.ctl_flush_tb()            # hooks reach only code translated after them
        fn()
        for h in hooks:
            self.m.uc.hook_del(h)
        self.m.uc.ctl_flush_tb()
        return n[0]

    def set_param(self, part, pid, value) -> int:
        """Patch::setParamValue (slot 4), as the editor calls it (the value, a tiny::optional<float>,
        goes by reference). Returns the open code's instructions it took (the libraries' own
        aren't counted)."""
        p = self.patch(part)
        fn = self.m.r32(self.m.r32(p) + 4 * 4)
        v = self.m.alloc(8, 8)
        self.m.write(v, struct.pack("<f", value))
        return self.count_open(lambda: self.m.call(fn, p, pid, v, KNOB, max_insns=100_000_000))

    def get_param(self, part, pid) -> float:
        """Patch::getParamValue (slot 0): the optional comes back through a pointer."""
        p = self.patch(part)
        fn = self.m.r32(self.m.r32(p))
        out = self.m.alloc(8, 8)
        self.m.call(fn, out, p, pid, max_insns=1_000_000)
        return struct.unpack("<f", self.m.read(out, 4))[0]

    def full_name(self, part, pid) -> str:
        out = self.m.alloc(32, 8)
        self.m.call(self._full_name, out, self.synth, part, pid, max_insns=5_000_000)
        ptr, n = self.m.r32(out), self.m.r32(out + 4)
        return self.m.read(ptr, n).decode("latin-1")

    def set_switch(self, on: bool):
        self.m.write(self.switches + SYNTHS_MOD, bytes([1 if on else 0]))


def sound(axf, engine, notes=(60,), blocks=40, voices=None, params=()):
    r = Rig(axf)
    r.set_instrument(0, engine)
    if voices:
        r.set_voices(0, voices)
    for pid, v in params:
        r.set_param(0, pid, v)
    for n in notes:
        r.note(0, n, True)
    return r, r.blocks(blocks)


def clip_run(x, limit=32000, run=8):
    """The longest run of samples at the rails."""
    best = cur = 0
    for v in x:
        cur = cur + 1 if abs(v) >= limit else 0
        best = max(best, cur)
    return best >= run, best


def init_values(axf):
    """What each voice renders with on its Init patch, macros as the model picker leaves them, is
    sy9's Init (the values level A renders with): a macro's default must change nothing. Volume
    is Init times the velocity. Then the two engines' loudness keeps level A's relation."""
    from test_synths_dsp import FX_INIT, PHASE_INIT, WAVE_INIT
    peaks = {}
    for e, name, init in ((WAVE, "WAVE", WAVE_INIT), (PHASE, "PHASE", PHASE_INIT)):
        r = Rig(axf)
        r.set_instrument(0, e)
        got = r.voice_values(RENDER[e])
        want = {**init, **FX_INIT, 39: VELOCITY}
        bad = [(i, round(got[i], 4), w) for i, w in sorted(want.items())
               if abs(got[i] - w) > 1e-3 * max(1.0, abs(w))]
        check(not bad, f"{name}: the voice renders with the Init values, macros at their defaults "
                       f"(differ: {bad[:4]})")
        _, (left, _) = sound(axf, e, blocks=40, params=((P_MIX, -1.0),))
        tail = left[8 * 128:]
        peaks[name] = math.sqrt(sum(v * v for v in tail) / len(tail))
    rel = 20 * math.log10(max(peaks["WAVE"], 1e-3) / max(peaks["PHASE"], 1e-3))
    check(abs(rel - WAVE_OVER_PHASE_DB) <= 2.0,
          f"Init note 60, one oscillator, through the host: RMS WAVE {peaks['WAVE']:.0f}, PHASE "
          f"{peaks['PHASE']:.0f}, WAVE {rel:+.1f} dB (level A {WAVE_OVER_PHASE_DB:+.1f} dB)")


def engines(axf):
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        r, (left, right) = sound(axf, e)
        check(not r.failures, f"{name}: set up and played, no asserts {r.failures[:2]}")
        check(peak(left) > 100, f"{name}: a note sounds (peak {peak(left)})")
        _, again = sound(axf, e)
        check((left, right) == again, f"{name}: the same twice, sample for sample")
        r.note(0, 60, False)
        r.blocks(600)
        tail = r.blocks(10)
        check(peak(tail[0]) <= 2 and peak(tail[1]) <= 2, f"{name}: note off, silence after the release "
                                                          f"(peak {max(peak(tail[0]), peak(tail[1]))})")
        check(r.interrupts["disable"] == r.interrupts["enable"], f"{name}: audio interrupts turned back on")
        check(r.malloc_in_block == 0 and r.meta_calls["block"] == 0,
              f"{name}: no malloc ({r.malloc_in_block}) and no createMetadata ({r.meta_calls['block']}) "
              f"in the audio interrupt")


def chords(axf):
    """CE's synth has 8 voices in all, shared by its 3 parts (kSynthMaxVoices, asserted in
    Synth::setVoiceCount). So the heaviest cases are one part with all 8 voices (Pre Distortion
    runs once per voice) and 3 parts sharing them 3 + 3 + 2 (each part has its own effect state)."""
    combos = [(e, n, fx, dist, place, (8, 0, 0)) for e, n in ((WAVE, "WAVE"), (PHASE, "PHASE"))
              for fx in (0, 1, 2, 3) for dist, place in ((0, 1), (2, 0), (1, 1), (3, 0))]
    combos += [(e, n, fx, dist, place, (3, 3, 2)) for e, n in ((WAVE, "WAVE"), (PHASE, "PHASE"))
               for fx, (dist, place) in zip((0, 1, 2, 3), ((2, 0), (1, 1), (3, 0), (2, 1)))]
    for e, name, fx, dist, place, layout in combos:
        r = Rig(axf)
        used = [part for part in range(3) if layout[part]]
        for part in range(3):                       # free the voices first, then hand them out
            if not layout[part]:
                r.set_voices(part, 0)
        for part in used:
            r.set_instrument(part, e)
            r.set_voices(part, layout[part])
            for pid, v in ((P_EFFECT, fx), (P_DIST, dist), (P_PLACEMENT, place), (P_GAIN, 1.0)):
                r.set_param(part, pid, v)
        heap0 = r.heap_brk
        held = {part: CHORD[:layout[part]] if len(used) > 1 else CHORD for part in used}
        for part in used:
            for n in held[part]:
                r.note(part, n + 12 * part - 12, True)
        left, right = r.blocks(60)
        for part in used:
            for n in held[part][:2]:
                r.note(part, n + 12 * part - 12, False)
        more = r.blocks(30)
        clipped, run = clip_run(left + more[0])
        what = (f"{name}, {'+'.join(str(layout[p]) for p in used)} voices, Effect {fx}, "
                f"Distortion {dist} {'Pre' if place == 0 else 'Post'}")
        ok = (not r.failures and peak(left) > 100 and peak(right) > 100 and not clipped
              and r.heap_brk == heap0 and r.malloc_in_block == 0 and r.meta_calls["block"] == 0
              and r.interrupts["disable"] == r.interrupts["enable"])
        check(ok, f"{what}: chords sound (peak {peak(left)}), longest clip run {run}, "
                  f"heap +{r.heap_brk - heap0}, malloc in the interrupt {r.malloc_in_block}, "
                  f"block pool peak {r.pool_peak()} {r.failures[:1]}")


def names(axf):
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        r = Rig(axf)
        r.set_instrument(0, e)
        r.note(0, 60, True)
        r.blocks(4)
        before = r.meta_calls["other"]
        cost = r.set_param(0, P_DIST, 3)
        rebuilt = r.meta_calls["other"] - before
        crush = (r.full_name(0, P_GAIN), r.full_name(0, P_TONE))
        r.blocks(4)
        cost2 = r.set_param(0, P_DIST, 1)
        od = (r.full_name(0, P_GAIN), r.full_name(0, P_TONE))
        plain = r.set_param(0, P_GAIN, 0.3)
        r.blocks(4)
        check("Bits" in crush[0] and "Rate" in crush[1] and "Gain" in od[0] and "Tone" in od[1],
              f"{name}: Bit Crusher names {crush}, Overdrive {od}")
        check(rebuilt >= 1, f"{name}: the library calls createMetadata when the Distortion changes ({rebuilt})")
        check(max(cost, cost2) < BLOCK_BUDGET // 2,
              f"{name}: a Distortion change keeps the interrupt off for {max(cost, cost2):,} open-code instructions "
              f"(a plain one {plain:,}; a block is {BLOCK_BUDGET:,} cycles)")
        check(r.meta_calls["block"] == 0 and r.malloc_in_block == 0,
              f"{name}: none of it in the audio interrupt (createMetadata {r.meta_calls['block']}, "
              f"malloc {r.malloc_in_block})")
        check(abs(r.get_param(0, P_GAIN) - 0.3) < 1e-3, f"{name}: the patch holds the value set "
                                                         f"({r.get_param(0, P_GAIN):.3f})")


def switches(axf):
    r = Rig(axf)
    r.set_instrument(0, CHIP)
    r.blocks(2)
    heap = []
    for round_ in range(30):
        for e in (WAVE, PHASE, CHIP):
            r.set_instrument(0, e)
            r.note(0, 60, True)
            r.note(0, 64, True)
            r.blocks(3)
        heap.append(r.heap_brk)
    check(not r.failures and heap[-1] == heap[1], f"30 rounds of WAVE, PHASE, CHIP with notes held: heap "
                                                  f"{heap[1] - r.heap_base:,} after round 2, "
                                                  f"{heap[-1] - r.heap_base:,} after 30 {r.failures[:1]}")
    check(r.malloc_in_block == 0, f"engine switches: no malloc in the audio interrupt ({r.malloc_in_block})")


def switch_off(axf):
    for e, name in ((WAVE, "WAVE"), (PHASE, "PHASE")):
        outs = []
        for on in (True, False):
            r = Rig(axf)
            r.set_instrument(0, e)
            r.set_switch(on)
            r.note(0, 60, True)
            outs.append(r.blocks(30))
        check(outs[0] == outs[1] and peak(outs[1][0]) > 100,
              f"{name}: with the switch Off a part already on it plays on, as with it On")


def patch_files(axf):
    r = Rig(axf)
    # the rig never runs cFileManager::init, so its fileSystem is null: make the Patches folder
    # here. The save and load themselves go through CE's patch store and FakeSd.
    r.stub(r.m.addr("_ZN12cFileManager19createPatchesFolderEv"), lambda mm: r.sd.dirs.add("Patches"))
    r.set_instrument(0, WAVE)
    r.set_param(0, 12, 900.0)
    name = r.m.put_str("MTTEST")
    saved = r.m.call(r._save, r.synth, 0, name, max_insns=200_000_000) & 0xFF
    r.set_param(0, 12, 5000.0)
    loaded = r.m.call(r._load, r.synth, 0, name, max_insns=200_000_000) & 0xFF
    got = r.get_param(0, 12)
    check(saved == 1 and loaded == 1 and abs(got - 900.0) < 1.0,
          f"a WAVE patch saved and loaded back (save {saved}, load {loaded}): Cutoff {got:.0f} Hz "
          f"{r.failures[:1]} {sorted(r.sd.files)[:4]}")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    axf = Path(sys.argv[1])
    init_values(axf)
    engines(axf)
    names(axf)
    switch_off(axf)
    switches(axf)
    patch_files(axf)
    chords(axf)
    print(f"{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
