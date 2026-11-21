#!/usr/bin/env python3
"""Resample's any instrument (rs9) on the real boot with a real project.

Usage: python tools/emu/test_resample_instruments.py <mods.axf> <plain CE.axf> <project folder>

Step 6 of docs/mt/RESAMPLE_PORT_PLAN.md. In the Sample Recorder on Line In:
1. The Instr list: CE's MIDI-only list with Trk Audio off, every instrument with it on; a sample or a
   synth picked there plays from the pads and doesn't become the recorder's MIDI instrument.
2. The instrument chosen before the visit is kept on entry with Trk Audio on and given back on leaving;
   Trk Audio off, or a source without it, selects the recorder's MIDI instrument.
3. Live sample and synth notes sound in the setup, and a live note is in a Trk Record take.
4. The track rule of the plan's 4.6, checked on every press against the rule worked out here from the
   sequencer's and the voices' own fields: free tracks from the cursor's, sample tracks only for a
   sample, muted ones skipped, sounding ones skipped, and with none free an ended 1-Shot track during
   playback, else the note dropped. 16 synth notes take 16 tracks and the 17th is dropped. MIDI notes
   in the audio interrupt take tracks the same way.
5. Nothing is written to the pattern: edit mode off and on, and with record mode armed on an external
   clock before the visit; there, with Trk Audio off, CE's record branch writes the pad, as in plain CE.
6. Trk Audio pressed while a pad is held, either way, leaves no note open.
7. The mod Off and plain CE: CE's list and CE's instrument.

Field offsets are the step 5 build's DWARF (readelf); each one is checked by a behaviour below before
the run relies on it. What doesn't run here: real time, the screen, a MIDI cable.
"""
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_resample_sound as snd  # noqa: E402
from test_resample_sound import KEY, PATTERN, PLAY, REC, LOOP_EVERY, Run, check, to_line_in  # noqa: E402

INSTR = 19                              # interfaceButtonInstr
LINE_IN, USB = snd.LINE_IN, snd.USB
VALUES = snd.VALUES
LAST_INSTRUMENT = VALUES + 1            # strMtValues.lastUsedInstrument
TRACK_MUTE, MIDI_TRACK_MUTE = VALUES + 28, VALUES + 1586
INSTRUMENT_SIZE, PLAY_MODE = 372, 60    # strInstrument, its playMode
SAMPLES, MIDI_START, SYNTH_START, SYNTHS = 48, 48, 64, 3
# Sequencer: selection.firstTrack at 1, ptrPlayer at 12, currentPattern at 20 (one of seq[2]: CE swaps
# it with bufferPattern), player at 28; strPlayer.isREC at 21 and track[16] at 84, 112 bytes each.
FIRST_TRACK, PTR_PLAYER, CURRENT_PATTERN, PLAYER, IS_REC, TRACKS, TRACK_SIZE = 1, 12, 20, 28, 21, 84, 112
# strPlayerTrack: stepSent (instrument 1, fx types 2 and 4), noteOpen, recOpen, noteSource (int16),
# roll.isOn, arp.isOn, micromove.isActive.
SENT_INSTRUMENT, SENT_FX, NOTE_OPEN, REC_OPEN, NOTE_SOURCE = 1, (2, 4), 27, 28, 30
ROLL_ON, ARP_ON, MICROMOVE_ON = 58, 68, 96
FX_ROLL, FX_MIDI_CHORD, FX_ARP = 3, 14, 21
# playerEngine (instrumentPlayer[8]): audioVoice at 296, its two AudioPlayMemory of 324 bytes, playing at 32.
ENGINE_VOICE, PLAY_MEMORY, PLAYING = 296, 324, 32
REC_MIDI_INSTRUMENT = 641               # cSampleRecorder.recorderMidiLastUsedInstrumentIndex
POPUP_STATE = 116                       # InterfacePopups.stepPopupState
POPUP_NONE, POPUP_INSTR, POPUP_ONLY_MIDI = 0, 2, 4
PATTERN_SIZE = 16432
# strMtConfig.midi at 36: clkIn 38, notesInMode 42, notesInChannel 43, middleC 47.
CLK_IN, NOTES_IN, NOTES_CHANNEL, MIDDLE_C = 38, 42, 43, 47
CLOCK_INTERNAL, CLOCK_USB, NOTES_IN_USB = 0, 1, 1
NO_TRACK = -1


class Inst(Run):
    def __init__(self, axf, project):
        super().__init__(axf, project)
        m = self.m
        self.seq = m.addr("sequencer")
        self.player = self.seq + PLAYER
        self.engines = m.addr("instrumentPlayer")
        self.engine_size = m.sym("instrumentPlayer").size // 8
        self.popups = m.addr("mtPopups")
        self.recorder = m.addr("sampleRecorder")
        self.pattern = m.addr("seq")
        self.config = m.addr("mtConfig")
        self.sample_notes = []          # (track, instrument) per playerEngine::noteOn
        self.synth_notes = []           # (note, channel, toSynth) per sendMidiNoteOn
        self.r.watch(m.addr("_ZN12playerEngine6noteOnEhaahhhh"), self._on_sample)
        self.r.watch(m.addr("_Z14sendMidiNoteOnhhhb"), self._on_midi)
        self.held = set()

    def _on_sample(self, mm):
        self.sample_notes.append(((mm.arg(0) - self.engines) // self.engine_size, mm.arg(1) & 0xFF))

    def _on_midi(self, mm):
        self.synth_notes.append((mm.arg(0) & 0xFF, mm.arg(2) & 0xFF, mm.arg(3) & 0xFF))

    # -- state --------------------------------------------------------------------------------
    def instrument(self):
        return self.m.r8(self.project + LAST_INSTRUMENT)

    def rec_midi(self):
        return self.m.r8(self.recorder + REC_MIDI_INSTRUMENT)

    def popup(self):
        return self.m.r8(self.popups + POPUP_STATE)

    def live_on(self):
        name = "_ZN4mods8resample6liveOnE"     # None on a build before step 6
        return self.m.r8(self.m.addr(name)) if name in self.m.elf.symbols else (None if self.mods else 0)

    def is_rec(self):
        return self.m.r8(self.player + IS_REC)

    def first_track(self):
        return self.m.r8(self.seq + FIRST_TRACK)

    def current_pattern(self):
        return self.m.r32(self.seq + CURRENT_PATTERN)

    def pattern_hash(self):
        return hashlib.sha256(self.m.read(self.current_pattern(), PATTERN_SIZE)).hexdigest()

    def track(self, t):
        m = self.m
        b = self.player + TRACKS + t * TRACK_SIZE
        return {
            "noteOpen": m.r8(b + NOTE_OPEN), "recOpen": m.r8(b + REC_OPEN),
            "source": int.from_bytes(m.read(b + NOTE_SOURCE, 2), "little", signed=True),
            "instrument": m.r8(b + SENT_INSTRUMENT), "fx": [m.r8(b + o) for o in SENT_FX],
            "roll": m.r8(b + ROLL_ON), "arp": m.r8(b + ARP_ON), "micromove": m.r8(b + MICROMOVE_ON),
        }

    def muted(self, t):
        return self.m.r8(self.project + (TRACK_MUTE + t if t < 8 else MIDI_TRACK_MUTE + t % 8)) != 0

    def voice_playing(self, t):
        base = self.engines + t * self.engine_size + ENGINE_VOICE
        return any(self.m.r8(base + k * PLAY_MEMORY + PLAYING) for k in (0, 1))

    def ended(self, t):
        """4.6: a sample track whose last sound has played to its end, nothing more to come."""
        f = self.track(t)
        last = f["instrument"]
        if last >= SAMPLES:
            return False
        mode = self.m.r8(self.project + last * INSTRUMENT_SIZE + PLAY_MODE)
        repeats = any(x in (FX_ROLL, FX_MIDI_CHORD, FX_ARP) for x in f["fx"]) or f["roll"] or f["arp"]
        return (mode in (0, 4, 5) and not repeats and not f["micromove"] and not f["recOpen"]
                and not self.voice_playing(t))

    def expected(self, sample):
        """The plan's 4.6 rule from the fields as they are now: (track, 'free' | 'ended' | 'drop')."""
        first = self.first_track()
        order = [(first + i) % 16 for i in range(16)]
        for t in order:
            if sample and t >= 8:
                continue
            if not self.muted(t) and not self.track(t)["noteOpen"]:
                return t, "free"
        for t in order:
            if t < 8 and not self.muted(t) and self.track(t)["noteOpen"] and self.ended(t):
                return t, "ended"
        return NO_TRACK, "drop"

    def live_track(self, source):
        hits = [t for t in range(16) if (f := self.track(t))["noteOpen"] and f["recOpen"] and f["source"] == source]
        return hits[0] if len(hits) == 1 else (NO_TRACK if not hits else hits)

    def open_notes(self):
        return [t for t in range(16) if self.track(t)["recOpen"]]

    # -- input --------------------------------------------------------------------------------
    def pad_down(self, pad):
        """The pad's note-on, with no audio interrupt between it and the caller's last read."""
        self.m.call(self.m.addr("_Z9onPadPushh"), pad, max_insns=300_000_000)
        self.held.add(pad)
        self.r.loop()

    def pad_up(self, pad):
        self.m.call(self.m.addr("_Z12onPadReleaseh"), pad, max_insns=300_000_000)
        self.held.discard(pad)
        self.loop_block()

    def all_up(self):
        for pad in sorted(self.held):
            self.pad_up(pad)
        self.blocks(LOOP_EVERY)

    def midi(self, on, pitch):
        """A USB note in the audio interrupt, as the MIDI handlers run on the unit."""
        self.r.in_interrupt = True
        try:
            name = "_Z15handleUsbNoteOnhhh" if on else "_Z16handleUsbNoteOffhhh"
            self.m.call(self.m.addr(name), 1, pitch, 100 if on else 0, max_insns=50_000_000)
        finally:
            self.r.in_interrupt = False

    def set_config(self, at, value):
        self.m.write(self.config + at, bytes([value]))

    def pick(self, list_pos):
        """Instr, a pad on the list, Instr let go: the instrument at that list position."""
        self.r.press(INSTR)
        self.loop_block()
        shown = self.popup()
        self.pad_down(list_pos)
        self.pad_up(list_pos)
        self.r.release(INSTR)
        self.loop_block()
        return shown


def press_and_check(run, pad, sample, label, kinds):
    """A pad held: the track it takes is the rule's. kinds collects which case each press was."""
    want, kind = run.expected(sample)
    samples, midis = len(run.sample_notes), len(run.synth_notes)
    run.pad_down(pad)
    got = run.live_track(pad)
    sounded = run.sample_notes[samples:] if sample else [n for n in run.synth_notes[midis:] if n[2]]
    ok = got == want
    if sample and want != NO_TRACK:
        ok = ok and [n[0] for n in sounded] == [want]
    if want == NO_TRACK:
        ok = ok and not sounded
    kinds.append(kind)
    if not ok:
        check(False, f"{label}: pad {pad} takes the rule's track",
              f"got {got}, rule {want} ({kind}), sounded {sounded}")
    return ok


def test_mods(axf, project):
    run = Inst(axf, project)
    m = run.m
    check(m.r32(run.seq + PTR_PLAYER) == run.player, "Sequencer's layout: ptrPlayer points at player")
    check(run.current_pattern() in (run.pattern, run.pattern + PATTERN_SIZE),
          "Sequencer's layout: currentPattern points at one of seq[2]")
    check(run.engine_size * 8 == m.sym("instrumentPlayer").size, f"8 players of {run.engine_size} bytes")
    sample = one_shot(run)
    check(sample is not None and sample + SYNTHS < 48, f"a 1-Shot sample instrument in the project: {sample}")
    sample_pad, synth_pad = sample + SYNTHS, 0          # list positions: the synths, then the samples
    before_visit = run.instrument()
    hash0 = run.pattern_hash()

    check(run.enter_recorder() and to_line_in(run), "the recorder on Line In")
    check(run.engine_open() == 0 and run.live_on() == 0, "Trk Audio off: shut, no live rule")
    midi0 = run.instrument()
    check(MIDI_START <= midi0 < SYNTH_START and run.rec_midi() == midi0,
          f"CE's entry: the recorder's MIDI instrument selected ({midi0})")

    # 1. the list
    run.r.press(INSTR)
    run.loop_block()
    check(run.popup() == POPUP_ONLY_MIDI, f"Trk Audio off: Instr shows CE's MIDI-only list ({run.popup()})")
    run.r.release(INSTR)
    run.loop_block()
    check(run.popup() == POPUP_NONE and run.instrument() == midi0, "let go: closed, still the MIDI instrument")

    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    check(run.engine_open() == 1 and run.live_on() == 1, "Trk Audio On: open, the live rule on")
    notes = len(run.sample_notes)
    shown = run.pick(sample_pad)
    check(shown == POPUP_INSTR, f"Trk Audio on: Instr shows every instrument ({shown})")
    check(run.instrument() == sample and run.popup() == POPUP_NONE,
          f"its pad picks sample instrument {sample} (strMtValues.lastUsedInstrument at {LAST_INSTRUMENT})",
          str(run.instrument()))
    check(any(n[1] == sample for n in run.sample_notes[notes:]), "the list's pad previews the sample",
          str(run.sample_notes[notes:]))
    check(run.rec_midi() == midi0, "a sample picked isn't the recorder's MIDI instrument")
    run.all_up()

    # 3. a live sample note sounds; 4. the rule, stopped
    kinds = []
    ok = press_and_check(run, 10, True, "a live sample note", kinds)
    heard = run.blocks(60)
    check(ok and kinds == ["free"] and run.peak(heard) > 1000,
          f"a live sample note in the setup: the cursor's track {run.first_track()}, heard: peak {run.peak(heard)}")
    run.all_up()
    check(not run.open_notes(), "let go: no note open", str(run.open_notes()))

    kinds = []
    ok = all([press_and_check(run, pad, True, "sample notes held", kinds) for pad in range(9)])
    check(ok and kinds == ["free"] * 8 + ["drop"],
          "9 sample notes held: the 8 sample tracks from the cursor's, the 9th dropped", str(kinds))
    run.all_up()
    check(not run.open_notes(), "all let go: no note open", str(run.open_notes()))

    cursor = run.first_track()
    muted = (cursor + 1) % 8
    m.write(run.project + TRACK_MUTE + muted, bytes([1]))
    kinds = []
    ok = press_and_check(run, 0, True, "muted", kinds) and press_and_check(run, 1, True, "muted", kinds)
    check(ok and run.live_track(1) == (cursor + 2) % 8, f"track {muted} muted: skipped", str(run.live_track(1)))
    run.all_up()
    m.write(run.project + TRACK_MUTE + muted, bytes([0]))

    synths = len(run.synth_notes)
    shown = run.pick(synth_pad)
    check(run.instrument() == SYNTH_START and run.rec_midi() == midi0, f"pad 0 of the list picks synth 1 ({run.instrument()})")
    run.all_up()
    kinds = []
    ok = press_and_check(run, 10, False, "a live synth note", kinds)
    heard = run.blocks(120)
    print(f"     a live synth note: peak {run.peak(heard)}")
    check(ok and any(n[2] for n in run.synth_notes[synths:]) and run.peak(heard) > 100,
          f"a live synth note in the setup: sent to the synth, heard: peak {run.peak(heard)}")
    run.all_up()
    kinds = []
    ok = all([press_and_check(run, pad, False, "synth notes held", kinds) for pad in range(17)])
    check(ok and kinds == ["free"] * 16 + ["drop"], "17 synth notes held: 16 tracks from the cursor's, the 17th dropped",
          str(kinds))
    run.all_up()
    check(not run.open_notes(), "all let go: no note open", str(run.open_notes()))

    # 4. the rule while the pattern plays: ended 1-Shot tracks once none is free
    run.pick(sample_pad)
    run.all_up()
    run.press(PLAY)
    run.blocks(300)
    kinds = []
    ok = True
    for pad in range(12):
        ok = press_and_check(run, pad, True, "during playback", kinds) and ok
        if kinds[-1] == "drop":
            break
        run.blocks(3)
    print(f"     during playback: {kinds}")
    check(ok and "ended" in kinds, "during playback: free tracks, then ended 1-Shot tracks, as the rule", str(kinds))
    run.all_up()
    check(not run.open_notes(), "all let go: no note open", str(run.open_notes()))
    run.press(PLAY)
    run.blocks(200)

    # 1-Shot's isPlaying ends on this base, with the pad still held
    run.pad_down(0)
    t = run.live_track(0)
    run.blocks(1)
    playing = isinstance(t, int) and 0 <= t < 8 and run.voice_playing(t)
    played = 1
    while playing and played < 4000 and run.voice_playing(t):
        run.blocks(1)
        played += 1
    check(playing and not run.voice_playing(t),
          f"a held 1-Shot note: its voice plays, then stops at the sample's end (track {t}, {played} blocks)")
    check(not run.ended(t), "held: not ended (a live note holds the track)")
    run.all_up()

    # MIDI notes in the interrupt
    run.set_config(NOTES_IN, NOTES_IN_USB)
    run.set_config(NOTES_CHANNEL, 0)
    run.set_config(MIDDLE_C, 2)
    kinds = []
    for pitch in (60, 62, 64):
        want, kind = run.expected(True)
        run.midi(True, pitch)
        got = run.live_track(pitch + 100)
        kinds.append(kind)
        if got != want:
            check(False, f"MIDI note {pitch}: the rule's track", f"got {got}, rule {want}")
            break
    else:
        check(kinds == ["free"] * 3, "3 MIDI notes in the interrupt: the rule's tracks", str(kinds))
    for pitch in (60, 62, 64):
        run.midi(False, pitch)
    run.blocks(LOOP_EVERY)
    check(not run.open_notes(), "MIDI notes let go: no note open", str(run.open_notes()))

    # 3. a live note in a Trk Record take
    run.press(KEY[2])
    run.blocks(LOOP_EVERY)
    check(m.read(m.addr("_ZN4mods8resample8settingsE"), 2) == b"\x01\x01", "Trk Record On")
    check(run.start_take(), "Rec: the take starts")
    run.blocks(20)
    run.pad_down(5)
    run.blocks(150)
    run.all_up()
    run.blocks(50)
    run.stop_take()
    left, right, length = run.take()
    take = m.read(left, 2 * length) + m.read(right, 2 * length)
    check(length > 0 and take.count(0) < len(take) * 9 // 10,
          f"the live note is in the Trk Record take ({len(take) - take.count(0)} of {len(take)} bytes not zero)")
    run.back_to_setup()
    run.press(KEY[2])
    run.blocks(LOOP_EVERY)
    check(run.pattern_hash() == hash0, "edit mode off: nothing written to the pattern")

    # 2. hand back on Trk Audio Off, and on a source without it
    run.pick(sample_pad)
    run.all_up()
    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    check(run.live_on() == 0 and run.instrument() == midi0, f"Trk Audio Off: the MIDI instrument again ({run.instrument()})")
    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    check(run.live_on() == 1 and run.instrument() == midi0, "Trk Audio On again: the MIDI instrument stays")
    run.pick(sample_pad)
    run.all_up()
    for _ in range(snd.SOURCES):
        run.next_source()
        if run.source() == USB:
            break
    check(run.source() == USB and run.live_on() == 0 and run.instrument() == midi0,
          f"USB: no Trk Audio, the MIDI instrument ({run.instrument()})")
    run.r.press(INSTR)
    run.loop_block()
    check(run.popup() == POPUP_ONLY_MIDI, "USB: CE's MIDI-only list")
    run.r.release(INSTR)
    run.loop_block()
    for _ in range(snd.SOURCES):
        run.next_source()
        if run.source() == LINE_IN:
            break
    check(run.source() == LINE_IN and run.live_on() == 1, "back on Line In: the live rule on")
    run.pick(sample_pad)
    run.all_up()
    check(run.leave_recorder(after_take=True), "leave the recorder")
    check(run.instrument() == before_visit, f"left: the instrument from before the visit ({before_visit})",
          str(run.instrument()))

    # 5. edit mode on
    run.press(REC)
    check(m.call(m.addr("_Z10isEditModev")) & 0xFF == 1, "Rec on the Pattern screen: edit mode on")
    chosen = run.instrument()           # the Pattern screen's: its cursor's step sets it
    check(run.enter_recorder(), "the recorder with edit mode on")
    run.blocks(LOOP_EVERY)
    check(run.source() == LINE_IN and run.live_on() == 1 and run.instrument() == chosen,
          f"Trk Audio on: the instrument chosen before the recorder kept ({chosen})", str(run.instrument()))
    hash1 = run.pattern_hash()
    kinds = []
    ok = all([press_and_check(run, pad, chosen < SAMPLES, "edit mode on", kinds) for pad in range(3)])
    run.blocks(60)
    run.all_up()
    check(ok and run.pattern_hash() == hash1 and not run.open_notes(),
          "edit mode on: the rule's tracks, nothing written to the pattern, no note open", str(kinds))
    check(run.leave_recorder(after_take=False), "leave")
    check(run.instrument() == chosen, "left: the instrument given back")
    run.press(REC)
    check(m.call(m.addr("_Z10isEditModev")) & 0xFF == 0, "edit mode off again")

    # 5. record mode armed on an external clock; 6. no note left open
    record_mode_visit(run, trk_audio=True)
    check(not run.r.failures, f"no failures in the run: {run.r.failures[:4]}")
    del run

    # 7. the mod Off
    run = Inst(axf, project)
    run.m.write(run.m.addr("_ZN4mods8switchesE") + snd.RESAMPLE_ID, bytes([0]))
    run.m.write(run.m.addr("_ZN4mods8resample8settingsE"), bytes([1, 0]))
    check(run.enter_recorder() and to_line_in(run), "the mod Off: the recorder on Line In, Trk Audio stored On")
    run.blocks(LOOP_EVERY)
    plain_ce(run, "the mod Off")
    check(not run.r.failures, f"the mod Off: no failures: {run.r.failures[:4]}")


def one_shot(run):
    """The project's first active 1-Shot sample instrument."""
    m = run.m
    return next((i for i in range(SAMPLES) if m.r8(run.project + i * INSTRUMENT_SIZE)
                 and m.r8(run.project + i * INSTRUMENT_SIZE + PLAY_MODE) == 0), None)


def arm_external(run):
    """Pattern screen, the clock from USB: Rec held and Play arms record mode with nothing playing."""
    run.set_config(CLK_IN, CLOCK_USB)
    run.r.press(REC)
    run.loop_block()
    run.press(PLAY)
    run.r.release(REC)
    run.loop_block()
    return run.is_rec() == 1 and not run.playing()


def record_mode_visit(run, trk_audio):
    """Record mode armed on an external clock before the visit (plan 11.5)."""
    m = run.m
    check(arm_external(run), "external clock: Rec+Play on the Pattern screen arms record mode (isREC), nothing plays")
    check(run.enter_recorder(), "the recorder with record mode armed")
    run.blocks(LOOP_EVERY)
    check(run.is_rec() == 1, "record mode stays armed in the recorder")
    if run.mods and trk_audio:
        check(run.live_on() == 1, "Trk Audio on")
        run.pick(one_shot(run) + SYNTHS)
        run.all_up()
        h = run.pattern_hash()
        kinds = []
        ok = all([press_and_check(run, pad, True, "record mode, Trk Audio on", kinds) for pad in range(2)])
        run.blocks(30)
        run.all_up()
        check(ok and run.pattern_hash() == h and not run.open_notes(),
              "record mode, Trk Audio on: the rule's tracks, nothing written, no note open", str(kinds))
        # Trk Audio Off while a pad is held: its note-off goes the way its note-on went
        run.pad_down(7)
        held_on = run.live_track(7)
        run.press(KEY[1])
        run.blocks(LOOP_EVERY)
        check(run.live_on() == 0, "Trk Audio Off with the pad held")
        run.all_up()
        check(isinstance(held_on, int) and held_on >= 0 and not run.open_notes() and run.pattern_hash() == h,
              f"let go after Trk Audio Off: the note on track {held_on} closed, nothing written", str(run.open_notes()))
    h = run.pattern_hash()
    run.pad_down(8)
    run.blocks(30)
    run.all_up()
    wrote = run.pattern_hash() != h
    label = "Trk Audio off" if run.mods else "plain CE"
    check(wrote, f"{label}: CE's record branch writes the pad into the pattern (11.5)")
    check(not run.open_notes(), f"{label}: no note open after", str(run.open_notes()))
    if run.mods:
        # Trk Audio On while a record-branch pad is held
        run.pad_down(9)
        run.press(KEY[1])
        run.blocks(LOOP_EVERY)
        check(run.live_on() == 1, "Trk Audio On with the pad held")
        run.all_up()
        check(not run.open_notes(), "let go after Trk Audio On: no note open", str(run.open_notes()))
    check(run.leave_recorder(after_take=False), "leave")
    run.press(REC)
    run.set_config(CLK_IN, CLOCK_INTERNAL)
    check(run.is_rec() == 0, "Rec on the Pattern screen: record mode off")


def plain_ce(run, label):
    run.r.press(INSTR)
    run.loop_block()
    check(run.popup() == POPUP_ONLY_MIDI, f"{label}: Instr shows CE's MIDI-only list ({run.popup()})")
    run.r.release(INSTR)
    run.loop_block()
    check(MIDI_START <= run.instrument() < SYNTH_START and run.live_on() == 0,
          f"{label}: the recorder's MIDI instrument ({run.instrument()})")


def test_ref(axf, project):
    run = Inst(axf, project)
    check(run.enter_recorder() and to_line_in(run), "plain CE: the recorder on Line In")
    plain_ce(run, "plain CE")
    check(run.leave_recorder(after_take=False), "plain CE: leave")
    record_mode_visit(run, trk_audio=False)
    check(not run.r.failures, f"plain CE: no failures: {run.r.failures[:4]}")


def main():
    mods_axf, ref_axf, project = sys.argv[1:4]
    test_mods(Path(mods_axf), Path(project))
    test_ref(Path(ref_axf), Path(project))
    print("failures:", snd.failures)
    return 1 if snd.failures else 0


if __name__ == "__main__":
    sys.exit(main())
