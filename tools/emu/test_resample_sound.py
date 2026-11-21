#!/usr/bin/env python3
"""Resample's Trk Audio on the real boot, with a real project's samples and synths sounding.

Usage: python tools/emu/test_resample_sound.py <mods.axf> <plain CE.axf> <project folder>

Step 3 of docs/mt/RESAMPLE_PORT_PLAN.md. Each run boots the firmware, opens the project from the fake
card and drives the interface with its own keys, the audio interrupt between main loops:
1. Normal playback: Play on the Pattern screen, 600 blocks.
2. The Sample Recorder with Trk Audio on (mods build): Play in the setup gives the same 600 blocks, bit
   for bit. Shift+Play and a MIDI Start are heard. Rec then Play: the pattern is heard during the take,
   the take goes after the loaded samples, which stay byte-identical, and holds the input only. Stopping
   the take stops the pattern and leaves everything as CE's entry leaves it. The project's mix survives
   opening, closing, source changes and leaving. With too little room the take is CE's, the pattern
   stops and stays silent for the rest of the visit. Trk Audio off, and the mod Off, are CE's.
3. The limiter's sidechain on track 1: the recorder against normal playback again, bit for bit.
4. Plain CE: the recorder is silent and the take goes where CE puts it.

The project's mix is set by MIDI CC first (CE's changeMixerOnMidiCC), away from CE's defaults, so a
lost mix shows. The fake codec's input is silence, so a take that holds only the input is all zeros.
What doesn't run here: real time, the real codec and Line In, the screen.
"""
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from boot_rig import BootRig  # noqa: E402
from machine import after_hooked_store  # noqa: E402
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE  # noqa: E402

KEY = list(range(8))                    # interfaceButton0..7, the keys under the screen
SAMPLE_REC, PATTERN, PLAY, REC, SHIFT = 16, 22, 23, 28, 29
RESAMPLE_ID = 11                        # Mod::resample
SOURCES = 9                             # CE's Tracker+ recorder sources
LINE_IN, USB = 3, 8
BLOCKS = 600
SIDECHAIN_TRACK_1 = 1                   # mtProject.values.limiterSidechain: 0 off, then the sources
LOOP_EVERY = 8
MIX_CC = {71 + i: 50 + 6 * i for i in range(11)}    # tracks 1-8, delay, reverb, dry: none at 86 %
# strMtProject: 48 strInstrument of 372 bytes, two counts, strSong (255 + 1), then strMtValues at 4 bytes.
# Playing the song (Shift+Play) moves song.playlistPos and values.actualPattern, and every press marks
# values.projectNotSavedFlag: the visit's own changes, which the gates have nothing to do with.
SONG = 48 * 372 + 2
VALUES = (SONG + 256 + 3) & ~3
PLAYED = {SONG + 255, VALUES + 4, VALUES + 48}      # playlistPos, actualPattern, projectNotSavedFlag
failures = 0


def check(ok, what, detail=""):
    global failures
    print(("PASS " if ok else "FAIL ") + what + (f": {detail}" if detail and not ok else ""), flush=True)
    if not ok:
        failures += 1


class Run:
    def __init__(self, axf, project):
        r = self.r = BootRig(axf)
        m = self.m = r.m
        self.mods = "_ZN4mods8switchesE" in m.elf.symbols
        self.counts = {"pattern": 0, "recorder": 0}
        r.watch(m.addr("_ZN14cPatternEditor6updateEv"), lambda mm: self._count("pattern"))
        r.watch(m.addr("_ZN15cSampleRecorder6updateEv"), lambda mm: self._count("recorder"))
        project_size = m.sym("mtProject").size
        self.project = m.addr("mtProject")
        self.used_memory = self.project + project_size - 36       # strMtProject: ..., used_memory, 16 x u16
        self.bank = m.addr("externalRamSampleMemory")
        self.bank_size = m.sym("externalRamSampleMemory").size
        self.mix = [m.r32(m.addr("_ZL18indexedMixerValues") + 4 * i) for i in range(11)]
        self.block = 0
        r.card_from(project, "Resample", workspace=True)
        r.boot()
        for _ in range(3000):
            self.loop_block()
            if self.counts["pattern"] > 5:
                break
        check(m.r32(self.used_memory - 4) == self.bank_size, "mtProject's layout: max_memory found",
              f"{m.r32(self.used_memory - 4)} {self.bank_size}")
        for cc, value in MIX_CC.items():
            m.call(m.addr("_Z19changeMixerOnMidiCChh"), cc, value, max_insns=10_000_000)
        if self.mods:
            m.write(m.addr("_ZN4mods8switchesE") + RESAMPLE_ID, bytes([1]))
            m.write(m.addr("_ZN4mods8resample8settingsE"), bytes([0, 0]))
        self.loop_block()

    def _count(self, name):
        self.counts[name] += 1

    # -- time ---------------------------------------------------------------------------------
    def loop_block(self):
        self.r.loop()
        return self.r.interrupt()

    def blocks(self, n):
        """n interrupts, a main loop every LOOP_EVERY. The output blocks, left then right."""
        out = []
        for i in range(n):
            self.block += 1
            if i % LOOP_EVERY == 0:
                self.r.loop()
            left, right = self.r.interrupt()
            out.append(left + right)
        return out

    def press(self, button, held=None):
        r = self.r
        if held is not None:
            r.press(held)
        r.press(button)
        self.loop_block()
        r.release(button)
        if held is not None:
            r.release(held)
        self.loop_block()

    # -- state --------------------------------------------------------------------------------
    def snapshot(self):
        m = self.m
        return bytes(m.read(self.project, m.sym("mtProject").size)), bytes(m.read(m.addr("isBlockedNoteOn"), 48))

    def mix_values(self):
        return [self.m.r8(a) for a in self.mix]

    def engine_open(self):
        return self.m.r8(self.m.addr("_ZN4mods8resample10engineOpenE"))

    def trk_audio(self):
        return self.m.r8(self.m.addr("_ZN4mods8resample8settingsE"))

    def playing(self):
        m = self.m
        return m.call(m.addr("_ZN9Sequencer6isPlayEv"), m.addr("sequencer")) & 0xFF

    def recording(self):
        m = self.m
        return m.call(m.addr("_ZN8Recorder17isRecordingActiveEv"), m.addr("recorder")) & 0xFF

    def take(self):
        """The take's left and right addresses and its length in frames."""
        m = self.m
        rec = m.addr("recorder")
        get = m.addr("_ZN8Recorder10getAddressEN10AudioInput13InputChannelsE")
        return m.call(get, rec, 0), m.call(get, rec, 1), m.call(m.addr("_ZN8Recorder9getLengthEv"), rec)

    def watch_sidechain_read(self):
        """mtProject.values.limiterSidechain: the last project byte read before setLimiterSidechain
        while the gates open (connectMixerImpact only writes the project). Returns a getter."""
        m = self.m
        size = m.sym("mtProject").size
        state = {"last": None, "found": None, "h": None}

        def on_read(uc, access, address, sz, value, user):
            if sz == 1:
                state["last"] = address - self.project

        def on_set(mm):
            if state["h"] is not None and state["last"] is not None and state["found"] is None:
                state["found"] = state["last"]
                m.uc.hook_del(state["h"])
                state["h"] = None
        state["h"] = m.uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=self.project, end=self.project + size - 1)
        self.r.watch(m.addr("_ZN11audioEngine19setLimiterSidechainEh"), on_set)
        return lambda: state["found"]

    def set_sidechain(self, at, value):
        """As the Master page's Sidechain list sets it."""
        m = self.m
        m.write(self.project + at, bytes([value]))
        m.call(m.addr("_ZN11audioEngine19setLimiterSidechainEh"), m.addr("engine"), value, max_insns=10_000_000)
        self.loop_block()

    def samples_hash(self, end):
        return hashlib.sha256(bytes(self.m.read(self.bank, end - self.bank))).hexdigest()

    # -- the interface ------------------------------------------------------------------------
    def enter_recorder(self):
        before = self.counts["recorder"]
        self.press(SAMPLE_REC)
        for _ in range(50):
            if self.counts["recorder"] > before + 2:
                return True
            self.loop_block()
        return False

    def leave_recorder(self, after_take):
        """Back to the Pattern screen. After a take CE asks whether it may be lost (key 7 says yes)
        and reloads the samples before it switches."""
        before = self.counts["pattern"]
        self.press(PATTERN)
        if after_take:
            self.press(KEY[7])
        for _ in range(20000):
            if self.counts["pattern"] > before + 5:
                return True
            self.loop_block()
        return False

    def source(self):
        return self.m.r8(self.m.addr("sampleRecorder") + self.source_at)

    def next_source(self):
        """Key 0 with the Source column selected moves to the next source, round the list."""
        self.press(KEY[0])

    def start_take(self):
        """Rec on the setup; through any count-in until the take records."""
        self.press(REC)
        for _ in range(4000):
            if self.recording():
                return True
            self.loop_block()
        return False

    def stop_take(self):
        self.press(REC)
        self.blocks(LOOP_EVERY * 2)

    def back_to_setup(self):
        """The crop screen's key 6 asks whether the take may be lost; key 7 says yes."""
        self.press(KEY[6])
        self.press(KEY[7])
        self.blocks(LOOP_EVERY * 2)

    def peak(self, out):
        return max((max(map(abs, b)) for b in out), default=0)


def to_line_in(run):
    """Key 0 until the source is Line In. recorderConfig.source and mtProject.values.source are the
    first bytes changeSourceSelection writes into the recorder and the project."""
    m = run.m
    rec, rec_size = m.addr("sampleRecorder"), m.sym("sampleRecorder").size
    project_size = m.sym("mtProject").size
    state = {"on": False, "rec": None, "project": None}

    def entered(mm):
        state["on"] = True
    run.r.watch(m.addr("changeSourceSelection"), entered)

    def on_write(uc, access, address, size, value, user):
        after_hooked_store(uc)
        if not state["on"] or size != 1:
            return
        if state["rec"] is None and rec <= address < rec + rec_size:
            state["rec"] = address - rec
        elif state["project"] is None and run.project <= address < run.project + project_size:
            state["project"] = address - run.project
    h = m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    for _ in range(SOURCES + 2):
        run.press(KEY[0])           # selects the Source column if it isn't, else moves it on
        if state["rec"] is not None:
            state["on"] = False
            if m.r8(rec + state["rec"]) == LINE_IN:
                break
            state["on"] = True
    m.uc.hook_del(h)
    state["on"] = False
    if state["rec"] is None:
        return False
    run.source_at = state["rec"]
    run.project_source_at = state["project"]
    return True


def test(mods_axf, ref_axf, project):
    # 1. normal playback
    normal = Run(mods_axf, project)
    normal.press(PLAY)
    played = normal.blocks(BLOCKS)
    check(normal.peak(played) > 1000, f"normal playback sounds: peak {normal.peak(played)}")
    del normal

    # 2. the recorder, mods build
    run = Run(mods_axf, project)
    before, blocked_before = run.snapshot()
    mix_before = run.mix_values()
    check(mix_before == [v * 100 // 127 for v in MIX_CC.values()], f"the project's mix set by CC: {mix_before}")
    check(run.enter_recorder(), "the Sample Recorder starts")
    check(to_line_in(run), "the recorder's source byte found")
    check(run.source() == LINE_IN, f"the source is Line In ({run.source()})")
    check(run.project_source_at == VALUES + 36, "the layout below holds: mtProject.values.source where it was found",
          f"{run.project_source_at} {VALUES + 36}")
    # the project keeps the recorder's source: that byte is the visit's to change
    if run.project_source_at is not None:
        before = bytearray(before)
        before[run.project_source_at] = LINE_IN
        before = bytes(before)
    entry, blocked_entry = run.snapshot()
    check(run.mix_values() == [86] * 11, f"CE's entry sets the mix to its defaults: {run.mix_values()}")
    check(run.engine_open() == 0 and blocked_entry != blocked_before, "Trk Audio off: the gates shut, notes blocked")

    sidechain_at = run.watch_sidechain_read()
    run.press(KEY[1])                                       # Trk Audio On
    run.blocks(LOOP_EVERY)
    check(run.trk_audio() == 1 and run.engine_open() == 1, "Trk Audio On: the gates open")
    run.found_sidechain = sidechain_at()
    check(run.found_sidechain is not None and run.m.r8(run.project + run.found_sidechain) == 0,
          f"opening reads the project's limiter sidechain (off), at {run.found_sidechain}")
    check(run.mix_values() == mix_before, f"open: the project's mix back: {run.mix_values()}")
    _now, blocked_open = run.snapshot()
    check(blocked_open == blocked_before, "open: notes unblocked as outside the recorder")

    run.press(PLAY)
    in_setup = run.blocks(BLOCKS)
    differ = [i for i in range(BLOCKS) if in_setup[i] != played[i]]
    check(not differ, f"Play in the setup: the same {BLOCKS} blocks as normal playback, bit for bit",
          f"{len(differ)} differ, first {differ[:5]}; peaks {run.peak(in_setup)} / {run.peak(played)}")
    run.press(PLAY)                                         # Stop
    run.blocks(200)

    run.press(PLAY, held=SHIFT)                             # Shift+Play: the song
    song = run.blocks(300)
    check(run.peak(song) > 1000 and run.playing(), f"Shift+Play in the setup is heard: peak {run.peak(song)}")
    run.press(PLAY)
    run.blocks(200)

    run.r.in_interrupt = True                               # MIDI Start comes in the audio interrupt
    run.m.call(run.m.addr("_ZN9Sequencer17handleMidiInStartEbb"), run.m.addr("sequencer"), 0, 0,
               max_insns=50_000_000)
    run.r.in_interrupt = False
    midi = run.blocks(300)
    check(run.peak(midi) > 1000, f"a MIDI Start in the setup is heard: peak {run.peak(midi)}")
    run.press(PLAY)
    run.blocks(200)

    # a take after the loaded samples
    used = run.m.r32(run.used_memory)
    loaded_end = run.bank + used
    samples = run.samples_hash(loaded_end)
    run.m.write(loaded_end, bytes([0xA5]) * (run.bank_size - used))
    check(run.start_take(), "Rec: the take starts")
    left, right, _length = run.take()
    expected = (loaded_end + 31) & ~31
    check(left == expected, f"the take starts after the loaded samples: {left:#x}, expected {expected:#x}")
    check(run.engine_open() == 1, "during the take the gates stay open")
    run.press(PLAY)
    during = run.blocks(300)
    check(run.peak(during) > 1000 and run.playing(), f"Play during the take is heard: peak {run.peak(during)}")
    run.stop_take()
    left, right, length = run.take()
    check(length > 200 * 128, f"the take is {length} frames")
    check(not run.playing(), "stopping the take stops the pattern")
    after, blocked_after = run.snapshot()
    differ = [i for i in range(len(entry)) if entry[i] != after[i] and i not in PLAYED]
    check(not differ and blocked_after == blocked_entry and run.engine_open() == 0,
          "after the take: everything as CE's entry left it (but the song's place and the unsaved flag)",
          f"bytes {differ[:8]}")
    check(run.samples_hash(loaded_end) == samples, "the loaded samples are byte-identical")
    take_bytes = bytes(run.m.read(left, 2 * length)) + bytes(run.m.read(right, 2 * length))
    check(take_bytes.count(0) == len(take_bytes), "the take holds the input only (silence), not the tracker")
    check(right == left + (run.bank + run.bank_size - left) // 2 // 256 * 256,
          "the right channel at the half of the moved buffer, in whole blocks")

    run.back_to_setup()
    check(run.engine_open() == 1, "back on the setup after a moved take: the gates open again")

    # the mix through source changes and Trk Audio presses
    for _ in range(SOURCES):
        run.next_source()
        shown = run.source() in (0, 1, 2, 3, 5, 6)
        if run.engine_open() != (1 if shown else 0):
            check(False, f"source {run.source()}: the gates follow it", str(run.engine_open()))
            break
    else:
        check(True, "every source: the gates open for Line Ins and mics only, shut for Radio and USB")
    check(run.source() == LINE_IN, "back on Line In")
    run.press(KEY[1])
    run.press(KEY[1])
    run.press(KEY[1])
    run.press(KEY[1])
    check(run.engine_open() == 1 and run.mix_values() == mix_before, "after four presses: open, the mix the project's")
    check(run.leave_recorder(after_take=False), "leave the recorder with the gates open")
    left_project, blocked_left = run.snapshot()
    differ = [i for i in range(len(before)) if before[i] != left_project[i] and i not in PLAYED]
    check(not differ and blocked_left == blocked_before,
          "left with the gates open: the project as before the visit (but the song's place and the unsaved flag)",
          f"bytes {differ[:8]}")

    # too little room: CE's buffer, the pattern stopped, silent for the rest of the visit
    check(run.enter_recorder(), "the recorder again")
    run.blocks(LOOP_EVERY)
    check(run.engine_open() == 1, "Trk Audio still On: open from the start")
    run.press(PLAY)
    run.blocks(100)
    # Instrument 1's RAM data made to reach almost to the end of sample memory, for the take only:
    # strSampleMemory is 36 bytes (two addresses, four flags, six words), sampleMemory[48] sits
    # before max_memory, and instrument 1 is packed at the start of the bank.
    memory0 = run.used_memory - 4 - 48 * 36
    check(run.m.r32(memory0) == run.bank, "sampleMemory[0] found: instrument 1 at the bank's start",
          f"{run.m.r32(memory0):#x}")
    stereo = run.m.r8(memory0 + 9) or run.m.r32(memory0 + 4) != run.m.r32(memory0)
    channels = 2 if stereo else 1
    ram_frames = memory0 + 12
    old = run.m.r32(ram_frames)
    run.m.write(ram_frames, struct.pack("<I", run.bank_size // (2 * channels) - 1000))
    check(run.start_take(), "Rec with under 10 s free")
    run.m.write(ram_frames, struct.pack("<I", old))
    left, _right, _length = run.take()
    check(left == run.bank, f"the take is CE's: {left:#x}")
    check(run.engine_open() == 0 and not run.playing(), "the gates shut and the pattern stopped before it")
    run.stop_take()
    run.back_to_setup()
    check(run.engine_open() == 0, "back on the setup: still shut, the samples are overwritten")
    run.press(PLAY)
    silent = run.blocks(200)
    check(run.peak(silent) == 0, f"Play is silent for the rest of the visit: peak {run.peak(silent)}")
    run.press(PLAY)
    check(run.leave_recorder(after_take=False), "leave: CE reloads the samples")
    check(run.samples_hash(loaded_end) == samples, "the samples reloaded as they were")

    # Trk Audio off, then the mod Off: CE's
    check(run.enter_recorder(), "the recorder, Trk Audio to be turned off")
    run.press(KEY[1])
    run.blocks(LOOP_EVERY)
    check(run.trk_audio() == 0 and run.engine_open() == 0, "Trk Audio Off: shut")
    run.press(PLAY)
    silent = run.blocks(200)
    run.press(PLAY)
    check(run.peak(silent) == 0, f"Trk Audio off: Play in the setup is silent: peak {run.peak(silent)}")
    check(run.start_take(), "Rec with Trk Audio off")
    check(run.take()[0] == run.bank, "Trk Audio off: the take is CE's")
    run.stop_take()
    check(run.leave_recorder(after_take=True), "leave")
    run.m.write(run.m.addr("_ZN4mods8switchesE") + RESAMPLE_ID, bytes([0]))
    run.m.write(run.m.addr("_ZN4mods8resample8settingsE"), bytes([1, 0]))
    check(run.enter_recorder(), "the recorder with the mod Off and Trk Audio stored On")
    run.blocks(LOOP_EVERY)
    check(run.engine_open() == 0, "the mod Off: shut")
    run.press(PLAY)
    silent = run.blocks(200)
    run.press(PLAY)
    check(run.peak(silent) == 0, f"the mod Off: Play in the setup is silent: peak {run.peak(silent)}")
    check(run.start_take(), "Rec with the mod Off")
    check(run.take()[0] == run.bank, "the mod Off: the take is CE's")
    run.stop_take()
    check(not run.r.failures, f"no failures in the run: {run.r.failures[:4]}")
    sidechain_at = run.found_sidechain
    del run

    # the limiter's sidechain on (track 1), the same comparison: the gates give it back as the
    # project has it, and the limiter needs no reset for it
    if sidechain_at is not None:
        normal = Run(mods_axf, project)
        normal.set_sidechain(sidechain_at, SIDECHAIN_TRACK_1)
        normal.press(PLAY)
        played = normal.blocks(BLOCKS)
        del normal
        run = Run(mods_axf, project)
        run.set_sidechain(sidechain_at, SIDECHAIN_TRACK_1)
        check(run.enter_recorder() and to_line_in(run), "sidechain on: the recorder on Line In")
        run.press(KEY[1])
        run.blocks(LOOP_EVERY)
        run.press(PLAY)
        in_setup = run.blocks(BLOCKS)
        differ = [i for i in range(BLOCKS) if in_setup[i] != played[i]]
        check(not differ, f"sidechain on: Play in the setup gives normal playback's {BLOCKS} blocks, bit for bit",
              f"{len(differ)} differ, first {differ[:5]}")
        check(run.m.r8(run.project + sidechain_at) == SIDECHAIN_TRACK_1, "sidechain on: the project's value kept")
        del run

    # 3. plain CE
    ref = Run(ref_axf, project)
    check(ref.enter_recorder(), "plain CE: the recorder starts")
    ref.press(PLAY)
    silent = ref.blocks(200)
    ref.press(PLAY)
    check(ref.peak(silent) == 0, f"plain CE: Play in the setup is silent: peak {ref.peak(silent)}")
    check(ref.start_take(), "plain CE: Rec")
    check(ref.take()[0] == ref.bank, "plain CE: the take at the start of sample memory")
    ref.stop_take()
    check(not ref.r.failures, f"plain CE: no failures: {ref.r.failures[:4]}")


def main():
    mods_axf, ref_axf, project = sys.argv[1:4]
    test(Path(mods_axf), Path(ref_axf), Path(project))
    print("failures:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
