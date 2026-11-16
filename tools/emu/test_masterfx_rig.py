#!/usr/bin/env python3
"""masterfx on the sound rig: the Master page, the real switch, the real graph with the synths in.

Usage: python tools/emu/test_masterfx_rig.py <mods.axf> <reference.axf> [only: ui|graph|dynamics|ducks|bus|stress|debounce|project|play]

test_masterfx.py checks each effect against 1.2.0 on its own. This one checks what's around them:
- ui: CE's real cMasterParams with display commands caught at API_*. With the mod Off the page
  makes the same display calls as the reference build. On: the lists, stops, names, and the
  page's display bytes counted against the limit. On firmware with the Limiter list (masterfx-2.0's
  step 5): that list, the Limiter page's title and Compressor's own knobs, Missing and Bypassed;
  Glue's page (its own Attack, Release and Threshold, the Tracks list).
- bus: Glue on chosen tracks: only they change, exactly; the synth parts a pass behind.
- graph: the switch through mods::projectMasterfxSwitchChanged, the boot gate, and synth notes
  through CE's sends to the chosen effect.
- stress: 8 synth voices with the heaviest pair, then switches, knobs and notes while playing.
- debounce: the encoder's wait before an effect changes.
- project: make_masterfx_test_project.py's output through the firmware's readers.
- play: that project opened through CE's load steps, Play pressed, and TrackerImpl's real audio
  interrupt run, so the sequencer plays the notes. On both builds, with each effect.

The mods build may be either kind of firmware, and the checks speak in masterfx-1.x's list indices
(0 is Stock). Up to masterfx-1.x the record holds those indices and activeReverb and activeDelay say
which effect runs. From masterfx-2.0 the record holds the pack's file for each index, the lists come
from the card and the hook's byte only says a plugin runs: these helpers write and read the files,
and what plays is the runtime's installed file. The card's /MasterFX is listed as the firmware lists
it, through mods::update (the main loop's step that starts the runtime once the mod is On), with the
mods' file access pointed at the rig's card as mods::init does.
"""
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from masterfx_runtime import DELAY_FILES, REVERB_FILES, START, has_plugins  # noqa: E402
from sound_rig import REPO, PlayRig, SoundRig, find_project_path, peak, set_names  # noqa: E402
from machine import after_hooked_store  # noqa: E402
from unicorn import UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_S0  # noqa: E402

SWITCHES = "_ZN4mods8switchesE"
SETTINGS = "_ZN4mods8masterfx8settingsE"
MASTERFX = 3                        # mods::Mod::masterfx
ENGINE_IDS = (1, 2, 4, 5, 6, 7, 8)  # Synth::kSynthTypes, as project.mt's models number them
LIMIT = 8172                        # kFrameControlBytes (cDisplay::composeFrame)
CONTROLS = 65                       # controlsCount

BUTTON = {"reverb": 1, "delay": 2, "limiter": 3, "eq": 4, "knob5": 5, "knob6": 6, "cancel": 7,
          "up": 26, "delete": 27, "left": 30, "down": 31, "right": 32}
# The Master page's Limiter list and the own knobs (masterfx-2.0, step 5)
LIMITER_WORDS = "_ZN13cMasterParams20showMasterfxOwnKnobsEhh"
REVERBS = ("Stock", "Freeverb", "Plate", "Spring", "Shimmer", '"Gated"', "Reverse")
DELAYS = ("Stock", "Tape", "BBD")

failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


OWN_KNOBS = 5                       # masterfx::kOwnKnobs (record version 3; 2.0 had 2)
SLOT_SIZE = 48 + 4 * OWN_KNOBS      # masterfx::SlotSetting: the file (48), the own knobs
FILE_IO = "_ZN4mods12_GLOBAL__N_16fileIoE"
SLOTS = "_ZN4mods8masterfx7plugins12_GLOBAL__N_15slotsE"    # the runtime's SlotState[3]
KNOB_VALUES = 16                    # SlotState.value[0]: after 8 bytes and dirty[5], aligned


def files_of(slot):
    return REVERB_FILES if slot == 0 else DELAY_FILES


def main_loop(r):
    """mods::update, the main loop's step, on the plugin firmware: it lists the card and applies the
    project's plugins once the mod is On. The mods' file access on the rig's card first, as
    mods::init leaves it (a SoundRig doesn't run init)."""
    m = r.m
    if not has_plugins(m):
        return
    if m.r32(m.addr(FILE_IO) + 4) == 0:
        m.w32(m.addr(FILE_IO) + 4, r.sd.obj)
    m.call(m.addr("_ZN4mods6updateEv"), max_insns=400_000_000)


def write_choice(r, slot, index):
    """masterfx-1.x's index into settings: the index itself, or on the plugin firmware the pack's file
    for it (an index past the list: a file the card doesn't have)"""
    m = r.m
    if not has_plugins(m):
        m.write(m.addr(SETTINGS) + slot, bytes([index]))
        return
    files = files_of(slot)
    name = files[index] if index < len(files) else f"Gone{index}.ptmfx"
    m.write(m.addr(SETTINGS) + SLOT_SIZE * slot, name.encode() + b"\0" * (48 - len(name)))


def set_choice(r, slot, index):
    """A choice as the test wrote it into settings before an effect page opened: on the plugin
    firmware the page's words come from the installed plugin, so the choice is applied too"""
    write_choice(r, slot, index)
    if has_plugins(r.m):
        r.m.call(r.m.addr("_ZN4mods8masterfx5applyEv"), max_insns=400_000_000)


def set_mod(r, on, reverb=None, delay=None):
    """The switch as Project Settings changes it, and the choices as Config's lines do."""
    m = r.m
    if reverb is not None:
        write_choice(r, 0, reverb)
    if delay is not None:
        write_choice(r, 1, delay)
    m.write(m.addr(SWITCHES) + MASTERFX, bytes([1 if on else 0]))
    if "_ZN4mods28projectMasterfxSwitchChangedEv" in r.syms:
        m.call(m.addr("_ZN4mods28projectMasterfxSwitchChangedEv"), max_insns=400_000_000)
    main_loop(r)


def settings(r):
    """The record's choices as masterfx-1.x's indices (-1: a file that isn't the pack's)"""
    m = r.m
    if not has_plugins(m):
        return tuple(m.read(m.addr(SETTINGS), 2))
    out = []
    for slot in (0, 1):
        name = m.cstr(m.addr(SETTINGS) + SLOT_SIZE * slot, 48)
        out.append(files_of(slot).index(name) if name in files_of(slot) else -1)
    return tuple(out)


def slot_file(r, slot):
    """A slot's file in settings ("" is Stock)"""
    return r.m.cstr(r.m.addr(SETTINGS) + SLOT_SIZE * slot, 48)


def runtime_slot(r, slot):
    """The runtime's SlotState for a slot: its address"""
    slots = r.syms[SLOTS]
    return slots.addr + slots.size // 3 * slot


def playing(r):
    """What the reverb and delay objects' hooks run, as masterfx-1.x's indices (0: CE's own). Reads
    memory only, so a watch can call it while the firmware runs: on the plugin firmware, the
    runtime's slot (its active byte, then the file at the end of its SlotState, as plugins::file
    reads it)."""
    m = r.m
    active = (m.r8(m.addr("_ZN4mods8masterfx12activeReverbE")), m.r8(m.addr("_ZN4mods8masterfx11activeDelayE")))
    if not has_plugins(m):
        return active
    slots = r.syms[SLOTS]
    size = slots.size // 3
    out = []
    for slot in (0, 1):
        base = slots.addr + size * slot
        name = m.cstr(base + size - 48, 48) if active[slot] and m.r8(base) else ""
        out.append(files_of(slot).index(name) if name in files_of(slot) else -1)
    return tuple(out)


class MasterPage:
    """cMasterParams on the rig. Buttons press what the page itself registered. Another page
    (test_synthfx_page.py) changes PAGE, INIT and RIG."""
    PAGE = "masterParams"
    INIT = "_ZN13cMasterParams19initDisplayControlsEv"
    RIG = SoundRig

    def __init__(self, axf):
        self.r = r = self.RIG(axf)
        self.m = m = r.m
        self.log = []
        self.buttons = {}
        self.pot = None
        self.bytes = 0
        self.texts = []
        self.ys = []                 # each API_VERTEX2F's y, as the controls draw
        self._catch_display_commands()
        r.stub(m.addr("_ZN7Buttons15isButtonPressedEi"), lambda mm: 0)
        for name, sym in r.syms.items():
            # the pads' LEDs, and the on-screen keyboard this page never opens here
            if name.startswith(("_ZN16cMtPadsBacklight", "_ZN17mtKeyboardManager")) and sym.is_func:
                r.stub(sym.addr, lambda mm: 0)
        r.stub(m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: r.platform)
        m.w32(m.addr("_ZN10ModuleBase2ipE"), m.addr("ip"))
        m.w32(m.addr("_ZN10ModuleBase7buttonsE"), m.alloc(256, 4))
        m.w32(m.addr("_ZN10ModuleBase8titleBarE"), m.addr("titleBar"))
        m.w32(m.addr("_ZN10ModuleBase8platformE"), r.platform)
        m.w32(m.addr("_ZN10ModuleBase5synthE"), r.synth)
        m.w32(m.addr("_ZN10ModuleBase10eventFunctE"), m.add_stub(lambda mm: 0))
        self.display = m.addr("display")
        m.call(m.addr("_ZN8cDisplay8setThemeEh"), self.display, 0)
        self._watch_inputs()
        m.call(m.addr("_ZN8TitleBar12initControlsEv"), m.addr("titleBar"), max_insns=5_000_000)
        self.page = m.addr(self.PAGE)
        m.call(m.addr(self.INIT), self.page, max_insns=20_000_000)
        self._watch_display_calls()
        if has_plugins(m) and self.PAGE == "masterParams":
            # On the unit the first screen has started masterfx before this page can open, and the
            # plugin firmware's pages show what is installed, which waits for that
            m.call(m.addr("_ZN4mods15masterfxStartedEv"), max_insns=50_000_000)
        self._find_controls()

    def _catch_display_commands(self):
        def counted(name):
            short = name.split("API_")[1]

            def f(mm):
                if short.startswith("CMD_TEXT"):
                    text = mm.cstr(mm.arg(4), 128)
                    self.texts.append(text)
                    self.bytes += 24 + 12 * len(text)
                elif short.startswith("CMD_GRADIENT"):
                    self.bytes += 120
                elif short.startswith("VERTEX2F"):
                    self.ys.append(mm.arg(1))
                    self.bytes += 4
                else:
                    self.bytes += 4
                return 0
            return f
        for name, s in self.r.syms.items():
            if name.startswith("_Z") and "API_" in name and s.is_func:
                self.r.stub(s.addr, counted(name))

    def _watch_inputs(self):
        m, r = self.m, self.r

        def button(mode):
            def f(mm):
                self.buttons[mm.arg(1)] = (mode, mm.arg(3 if mode in (1, 2) else 2))
            return f
        # setButtonObj(id, state, void(*)()), (id, state, void(*)(u8)), (id, void(*)(u8)), (id, void(*)(u8, u8))
        for name, mode in (("_ZN14InputProcessor12setButtonObjEhhPFvvE", 1),
                           ("_ZN14InputProcessor12setButtonObjEhhPFvhE", 2),
                           ("_ZN14InputProcessor12setButtonObjEhPFvhE", 3),
                           ("_ZN14InputProcessor12setButtonObjEhPFvhhE", 4)):
            r.watch(m.addr(name), button(mode))

        def pot(mm):
            if mm.arg(1) == 0:
                self.pot = mm.arg(2)
        r.watch(m.addr("_ZN14InputProcessor9setPotObjEhPFvsEb"), pot)

    def _watch_display_calls(self):
        m, r = self.m, self.r

        def words(a):
            return tuple(struct.unpack("<4I", m.read(a, 16))) if a else None

        def add(kind, fn):
            r.watch(m.addr(f"_ZN8cDisplay{kind}"), lambda mm: self.log.append(fn(mm)))
        add("14setControlShowEa", lambda mm: ("show", mm.arg(1)))
        add("14setControlHideEa", lambda mm: ("hide", mm.arg(1)))
        add("14setControlTextEaPKc", lambda mm: ("text", mm.arg(1), mm.cstr(mm.arg(2)) if mm.arg(2) else None))
        add("15setControlText2EaPKc", lambda mm: ("text2", mm.arg(1), mm.cstr(mm.arg(2)) if mm.arg(2) else None))
        add("15setControlValueEai", lambda mm: ("value", mm.arg(1), mm.arg(2)))
        add("16setControlColorsEaPKm", lambda mm: ("colors", mm.arg(1), words(mm.arg(2))))
        add("14setControlDataEaPv", lambda mm: ("data", mm.arg(1)))
        add("14setControlSizeEass", lambda mm: ("size", mm.arg(1), mm.arg(2), mm.arg(3)))
        add("18setControlPositionEass", lambda mm: ("position", mm.arg(1), mm.arg(2), mm.arg(3)))
        r.watch(m.addr("_ZN8TitleBar8setTitleEPKc"), lambda mm: self.log.append(("title", mm.cstr(mm.arg(1)))))

    def _find_controls(self):
        """controlsTable: the first run of heap pointers in cDisplay. And which bit of which word
        setControlShow sets, by doing it."""
        m, r = self.m, self.r
        size = m.sym("display").size
        heap = (r.heap_base, r.heap_base + 138_752)
        for off in range(0, size, 4):
            if heap[0] <= m.r32(self.display + off) < heap[1]:
                self.table = self.display + off
                break
        first = m.r32(self.table)
        hide, show = m.addr("_ZN8cDisplay14setControlHideEa"), m.addr("_ZN8cDisplay14setControlShowEa")
        before = len(self.log)
        original = m.read(first, 64)
        m.call(hide, self.display, 0)
        a = m.read(first, 64)
        m.call(show, self.display, 0)
        b = m.read(first, 64)
        del self.log[before:]
        for i in range(0, 64, 4):
            x, y = struct.unpack("<I", a[i:i + 4])[0], struct.unpack("<I", b[i:i + 4])[0]
            if x != y:
                self.style_offset, self.show_bit = i, x ^ y
                self.was_shown = bool(struct.unpack("<I", original[i:i + 4])[0] & (x ^ y))
        m.call(show if self.was_shown else hide, self.display, 0)
        del self.log[before:]

    def start(self):
        self.m.call(self.m.addr("_ZN13cMasterParams5startEm"), self.page, 1 << 8, max_insns=50_000_000)

    def update(self):
        self.m.call(self.m.addr("_ZN13cMasterParams6updateEv"), self.page, max_insns=20_000_000)

    def press(self, name):
        mode, fn = self.buttons[BUTTON[name]]
        if mode == 1:
            self.m.call(fn, max_insns=30_000_000)
        elif mode == 2:
            self.m.call(fn, BUTTON[name], max_insns=30_000_000)
        elif mode == 3:
            self.m.call(fn, 1, max_insns=30_000_000)
        else:
            self.m.call(fn, BUTTON[name], 1, max_insns=30_000_000)

    def encoder(self, value):
        self.m.call(self.pot, value & 0xFFFFFFFF, max_insns=30_000_000)

    def take_log(self):
        out, self.log = self.log, []
        return out

    def handle_of(self, vtable):
        """The only control of a class on this page."""
        want = self.m.addr(vtable) + 8
        return next(i for i in range(CONTROLS)
                    if self.m.r32(self.table + 4 * i) and self.m.r32(self.m.r32(self.table + 4 * i)) == want)

    def place(self, frame):
        """Where the frame is: the last value set on the frame control."""
        values = [e[2] for e in self.take_log() if e[0] == "value" and e[1] == frame]
        return values[-1] if values else None

    def shown(self):
        out = []
        for i in range(CONTROLS):
            c = self.m.r32(self.table + 4 * i)
            if c and self.m.r32(c + self.style_offset) & self.show_bit:
                out.append(i)
        return out

    def bar_y(self, handle):
        """One frame of a list control: where its bar is (the top of the outline CE's NewList::drawBar
        draws, y1 y1 y2 y2 y1 with a row's 27 px), or None"""
        c = self.m.r32(self.table + 4 * handle)
        vt = self.m.r32(c)
        self.ys = []
        for stage in range(5):
            self.m.call(self.m.r32(vt + 4 * stage), c, max_insns=5_000_000)
        y = self.ys
        for i in range(len(y) - 4):
            if y[i] == y[i + 1] == y[i + 4] and y[i + 2] == y[i + 3] == y[i] + 27:
                return y[i]
        return None

    def page_bytes(self):
        """What one full redraw of the shown controls would append, slot by slot."""
        total = {}
        for i in self.shown():
            c = self.m.r32(self.table + 4 * i)
            vt = self.m.r32(c)
            self.bytes = 0
            for stage in range(5):
                self.m.call(self.m.r32(vt + 4 * stage), c, max_insns=5_000_000)
            total[i] = self.bytes
        return total


def last(log, kind):
    """The text each control ended up with."""
    return {e[1]: e[2] for e in log if e[0] == kind}


def label0_of(final, second):
    """label[0]'s handle, from column 7's Back: its first line, or its second on a ducking plugin's
    Limiter page (Gain Reduce over Back)"""
    h = next((h for h, t in final.items() if t == "Back"), None)
    if h is None:
        h = next(h for h, t in second.items() if t == "Back")
    return h - 7


def bar_of(label0, i):
    """barControl[i]'s handle: cMasterParams::start makes label 0 to 7, bgLabel, then for each column
    trackNameLabel, trackVolumeBar and barControl"""
    return label0 + 11 + 3 * i


def drive(page):
    """One walk over the page: every stop, every direction, the two effect pages and back."""
    page.start()
    for name in ("right", "up", "down", "right", "down", "right", "delete", "right", "up", "right",
                 "left", "left", "down", "left", "up", "left", "left", "left"):
        page.press(name)
    page.encoder(1)
    page.encoder(-1)
    for name in ("reverb", "right", "up", "down", "cancel", "delay", "right", "up", "down", "cancel",
                 "limiter", "cancel", "eq", "cancel"):
        if BUTTON[name] in page.buttons:
            page.press(name)


def test_ui(mods, ref):
    print("-- ui --")
    a, b = MasterPage(mods), MasterPage(ref)
    set_mod(a.r, False)
    a.take_log()
    drive(a)
    drive(b)
    keep = lambda log: [e for e in log if e[0] != "data"]  # showDelayScreen gives its lists back
    x, y = keep(a.take_log()), keep(b.take_log())
    check(len(y) > 500 and x == y, f"mod Off: the same {len(y)} display calls as the reference build")
    if x != y:
        for i, (p, q) in enumerate(zip(x, y)):
            if p != q:
                print(f"  first difference at {i}: {p} / {q}")
                break
    check(a.page_bytes() == b.page_bytes(), "mod Off: the same display bytes as the reference build")
    check(not a.r.failures and not b.r.failures, "no asserts")

    p = MasterPage(mods)
    set_mod(p.r, True, 0, 0)
    p.start()
    log = p.take_log()
    frame = p.handle_of("_ZTV6cFrame")
    limiter_list = LIMITER_WORDS in p.r.syms
    stops = []
    for _ in range(8):
        p.press("right")
        stops.append(p.place(frame))
    want = [1, 2, 3, 5, 6, 7, 7, 7] if limiter_list else [1, 2, 5, 6, 7, 7, 7, 7]
    check(stops == want, f"Right stops on {sorted(set(want))} ({stops})")
    back = []
    for _ in range(6):
        p.press("left")
        back.append(p.place(frame))
    want = [6, 5, 3, 2, 1, 0] if limiter_list else [6, 5, 2, 1, 0, 0]
    check(back == want, f"Left stops on {want} ({back})")

    p.press("right")
    p.take_log()
    p.encoder(4)
    check(settings(p.r) == (4, 0), f"encoder on the Reverb list chooses Shimmer {settings(p.r)}")
    log = p.take_log()
    check(("text2", next(e[1] for e in log if e[0] == "text2"), "Shimmer") in log, "the label's second line says Shimmer")
    p.press("right")
    p.encoder(2)
    check(settings(p.r) == (4, 2), f"encoder on the Delay list chooses BBD {settings(p.r)}")
    p.encoder(1)
    check(settings(p.r) == (4, 2), "and stops at the end of the list")
    p.press("up")
    check(settings(p.r) == (4, 1), "Up moves up the list")
    p.press("down")
    check(settings(p.r) == (4, 2), "Down moves down the list")
    p.press("delete")
    check(settings(p.r) == (4, 0), "Delete: Delay back to Stock")
    p.press("left")
    p.press("delete")
    check(settings(p.r) == (0, 0), "Delete: Reverb back to Stock")
    if limiter_list:
        p.press("right")
        p.press("right")
        p.take_log()
        p.encoder(1)
        check(slot_file(p.r, DYNAMICS) == "Glue.ptmfx", f"encoder on the Limiter list chooses Glue ({slot_file(p.r, DYNAMICS)!r})")
        log = p.take_log()
        check(any(e[0] == "text2" and e[2] == "Glue" for e in log), "the Limiter label's second line says Glue")
        p.encoder(1)
        check(slot_file(p.r, DYNAMICS) == "Compressor.ptmfx", "then Compressor")
        p.encoder(1)
        check(slot_file(p.r, DYNAMICS) == "Compressor.ptmfx", "and stops at the end of the list")
        p.press("delete")
        check(slot_file(p.r, DYNAMICS) == "", "Delete: Limiter back to Stock")
        p.press("left")
        p.press("left")

    # the effect pages' names
    words = {
        1: ("Reverb: Freeverb", "Size", "Damping", "Diffusion"),
        2: ("Reverb: Plate", "Decay", "Damping", "Diffusion"),
        3: ("Reverb: Spring", "Decay", "Damping", "Drip"),
        4: ("Reverb: Shimmer", "Decay", "Tone", "Shimmer"),
        5: ('Reverb: "Gated"', "Length", "Tone", "Release"),
        6: ("Reverb: Reverse", "Length", "Tone", "Release"),
    }
    for choice, (title, size, damp, diffusion) in words.items():
        set_choice(p.r, 0, choice)
        p.take_log()
        p.press("reverb")
        log = p.take_log()
        titles = [e[1] for e in log if e[0] == "title"]
        final = last(log, "text")
        label0 = next(h for h, t in final.items() if t == "Back") - 7
        said = [final[label0], final[label0 + 1], final[label0 + 3]]
        check(titles[-1] == title and said == [size, damp, diffusion], f"{REVERBS[choice]}: title and knob names ({titles[-1]}, {said})")
        second = last(log, "text2")
        check(second.get(label0 + 1) not in REVERBS and second.get(label0 + 2) not in DELAYS,
              f"{REVERBS[choice]}: the Master page's choice isn't left in a label's second line")
        p.press("cancel")
    set_choice(p.r, 0, 0)
    p.take_log()
    p.press("reverb")
    log = p.take_log()
    stock_title = [e[1] for e in log if e[0] == "title"]
    p.press("cancel")

    words = {1: ("Delay: Tape", "Feedback", "Filter"), 2: ("Delay: BBD", "Feedback", "Filter")}
    for choice, (title, feedback, tone) in words.items():
        set_choice(p.r, 1, choice)
        p.take_log()
        p.press("delay")
        log = p.take_log()
        titles = [e[1] for e in log if e[0] == "title"]
        final = last(log, "text")
        label0 = next(h for h, t in final.items() if t == "Back") - 7
        said = [final[label0 + 4], final[label0 + 5]]
        check(titles[-1] == title and said == [feedback, tone], f"{DELAYS[choice]}: title and knob names ({titles[-1]}, {said})")
        lists = 3 if limiter_list else 2
        check(sum(1 for e in log if e[0] == "data") == lists, f"{DELAYS[choice]}: the Delay page takes its {lists} lists back")
        if limiter_list:
            check(any(e[0] == "position" and e[2] == 2 for e in log), f"{DELAYS[choice]}: and PingPong back in column 0")
        p.press("cancel")

    # the same pages, stock, have CE's own words in the reference
    q = MasterPage(ref)
    q.start()
    q.take_log()
    q.press("reverb")
    ref_title = [e[1] for e in q.take_log() if e[0] == "title"]
    check(stock_title == ref_title, f"Stock reverb page: CE's title ({stock_title})")

    # the lists are gone from every other page
    set_choice(p.r, 0, 4)
    set_choice(p.r, 1, 2)
    p.take_log()
    master = set(p.shown())
    ref_page = MasterPage(ref)
    ref_page.start()
    extra = master - set(ref_page.shown())
    lists = 3 if limiter_list else 2
    check(len(extra) == lists, f"Master page: {lists} more controls shown than CE's ({sorted(extra)})")
    for name in ("reverb", "limiter", "eq"):
        p.press(name)
        check(not (extra & set(p.shown())), f"{name} page: the lists aren't left on screen")
        p.press("cancel")
    for name in ("_ZN13cMasterParams13switchToMixerEv", "_ZN13cMasterParams24switchToMixerDelayReverbEv"):
        p.m.call(p.m.addr(name), p.page, max_insns=30_000_000)
        check(not (extra & set(p.shown())), f"{name}: the lists aren't left on screen")
        p.m.call(p.m.addr("_ZN13cMasterParams14switchToMasterEv"), p.page, max_insns=30_000_000)
        check(extra <= set(p.shown()), "and back on the Master page they are")
    p.press("delay")
    check(extra <= set(p.shown()), "Delay page shows them as its own Sync and Rate")
    p.press("cancel")
    p.take_log()
    p.press("delay")
    log = p.take_log()
    check(all(any(e[0] == "value" and e[1] == h for e in log) for h in extra), "and sets their values itself")
    p.press("cancel")

    # the mod turned Off with the frame on a list
    p = MasterPage(mods)
    set_mod(p.r, True, 0, 0)
    p.start()
    p.press("right")
    set_mod(p.r, False)
    p.take_log()
    p.m.call(p.m.addr("_ZN13cMasterParams14switchToMasterEv"), p.page, max_insns=30_000_000)
    p.m.call(p.m.addr("_ZN13cMasterParams20activateLabelsBorderEv"), p.page, max_insns=5_000_000)
    check(p.place(p.handle_of("_ZTV6cFrame")) == 0, "mod turned Off with the frame on a list: the frame goes to Volume")

    if limiter_list:
        limiter_page(mods, ref)
        glue_page(mods)
        lists_and_card(mods)

    # display bytes, the heaviest choices
    p = MasterPage(mods)
    set_mod(p.r, True, 5, 2)
    if limiter_list:
        set_dynamics(p.r, "Glue.ptmfx")
    p.start()
    r_page = MasterPage(ref)
    r_page.start()
    ours, theirs = p.page_bytes(), r_page.page_bytes()
    a_total, b_total = sum(ours.values()), sum(theirs.values())
    print(f"     Master page: {b_total} B as CE, {a_total} B with the lists, against {LIMIT}")
    check(a_total < LIMIT, f"Master page fits ({a_total} of {LIMIT} B, upper bound)")
    added = {i: ours[i] - theirs.get(i, 0) for i in ours if ours[i] != theirs.get(i, 0)}
    print(f"     added per slot: {added}")
    for name in ("reverb", "delay") + (("limiter",) if limiter_list else ()):
        p.press(name)
        total = sum(p.page_bytes().values())
        check(total < LIMIT, f"{name} page fits ({total} of {LIMIT} B, upper bound)")
        p.press("cancel")
    if limiter_list:
        for file in ("Glue.ptmfx", "Compressor.ptmfx"):
            set_dynamics(p.r, file)
            p.press("limiter")
            total = sum(p.page_bytes().values())
            print(f"     the Limiter page with {file}: {total} B")
            check(total < LIMIT, f"the Limiter page with {file} fits ({total} of {LIMIT} B, upper bound)")
            p.press("cancel")


def limiter_page(mods, ref):
    """The Limiter page with Compressor: its title, CE's knobs' labels (Compressor has none of its own), its own
    knobs in columns 5 and 6, then Stock, a missing file and Bypassed"""
    p = MasterPage(mods)
    m = p.m
    set_mod(p.r, True, 0, 0)
    set_dynamics(p.r, "Compressor.ptmfx")
    p.start()
    p.take_log()
    stored = []
    p.r.watch(m.addr("_ZN4mods7project11markChangedEv"), lambda mm: stored.append(1))
    p.press("limiter")
    log = p.take_log()
    titles = [e[1] for e in log if e[0] == "title"]
    final, second = last(log, "text"), last(log, "text2")
    label0 = label0_of(final, second)
    check(titles[-1] == "Compressor", f"Compressor: the page's title, without Limiter ({titles[-1]})")
    said = [final[label0 + i] for i in range(3)]
    check(said == ["Attack", "Release", "Threshold"], f"Compressor: CE's knobs without Limiter in their names ({said})")
    # the page's last words (CE sets its own first, the hook replaces them before the frame is drawn)
    limiter = [t for t in list(final.values()) + list(second.values()) + titles[-1:] if "Limiter" in t]
    check(not limiter, f"Compressor: nothing on its page says Limiter ({limiter})")
    own = [(final.get(label0 + 5), second.get(label0 + 5)), (final.get(label0 + 6), second.get(label0 + 6))]
    check(own == [("Ratio", "4.0:1"), ("Makeup", "+0.0 dB")], f"Compressor: its own knobs in columns 5 and 6 at their defaults ({own})")
    check(all(math.isnan(k) for k in own_knobs(p.r)), "and settings leave them unset")
    # The source, then what it turns down: CE's Sidechain list in column 3, the Ducks list in 4, so
    # the Gain Reduce meter is above Back
    columns = {i: (final.get(label0 + i), second.get(label0 + i)) for i in (3, 4, 7)}
    check(columns == {3: ("Sidechain", "Disable"), 4: ("Ducks", "Master"), 7: ("Gain Reduce", "Back")},
          f"Compressor: CE's Sidechain in column 3, Ducks in 4, Gain Reduce over Back in 7 ({columns})")
    check(("value", bar_of(label0, 0), 10) in log, "the bars are where the rig looks (CE's Attack bar at 10)")
    shown = set(p.shown())
    check(bar_of(label0, 7) in shown and bar_of(label0, 3) not in shown,
          "the meter is column 7's bar, column 3's bar hidden")
    # blankSpaceCover[0] is moved with its y left as it is (-1); the bars get y 29
    cover = {e[1] for e in log if e[0] == "position" and e[2] == (800 // 8) * 7 + 2 and e[3] == 0xFFFFFFFF}
    check(cover and not cover & shown,
          f"the cover is off: nothing empty between Makeup and the meter (moved {sorted(cover)}, shown {sorted(cover & shown)})")

    frame = p.handle_of("_ZTV6cFrame")
    stops = []
    for _ in range(6):
        p.press("right")
        stops.append(p.place(frame))
    check(stops == [1, 2, 3, 4, 5, 6], f"Right stops on 1, 2, 3, 4, 5, 6 ({stops})")
    p.take_log()
    p.encoder(1)
    log = p.take_log()
    check(any(e[0] == "text2" and e[2] == "+0.5 dB" for e in log), "encoder on Makeup: +0.5 dB on the page")
    k = own_knobs(p.r)
    check(k[1] == 0.5 and math.isnan(k[0]), f"and in settings, Ratio still unset {k}")
    check(bool(stored), "and the project marked changed")
    p.press("left")
    p.encoder(2)
    check(own_knobs(p.r)[:2] == (5.0, 0.5), f"encoder on Ratio, two steps: 5.0 {own_knobs(p.r)}")
    value = struct.unpack("<f", m.read(runtime_slot(p.r, DYNAMICS) + KNOB_VALUES, 4))[0]
    check(value == 5.0, f"the plugin is given 5.0 ({value})")
    p.press("up")
    p.take_log()
    p.press("delete")
    log = p.take_log()
    check(own_knobs(p.r)[:2] == (4.0, 0.5) and any(e[0] == "text2" and e[2] == "4.0:1" for e in log),
          f"Delete on Ratio: its default, 4.0 {own_knobs(p.r)}")
    p.press("knob6")
    check(p.place(frame) == 6, "the button under column 6 selects Makeup")
    p.press("knob5")
    check(p.place(frame) == 5, "the button under column 5 selects Ratio")

    # the time guard stops it: the page says so
    m.write(runtime_slot(p.r, DYNAMICS) + 2, b"\x01")
    p.take_log()
    p.update()
    titles = [e[1] for e in p.take_log() if e[0] == "title"]
    check(titles == ["Compressor (bypassed)"], f"bypassed: the title says so ({titles})")
    p.update()
    check(not [e for e in p.take_log() if e[0] == "title"], "and only once")
    p.press("cancel")
    log = p.take_log()
    check(any(e[0] == "text2" and e[2] == "Bypassed" for e in log), "the Master page's Limiter column says Bypassed")
    check(not p.r.failures, "no asserts")
    space = p.m.r32(p.m.r32(p.table + 4 * bar_of(label0, 7)) + p.style_offset)

    # Stock: CE's page, no own knobs
    q = MasterPage(mods)
    set_mod(q.r, True, 0, 0)
    set_dynamics(q.r, "")
    q.start()
    q.take_log()
    stock_space = q.m.r32(q.m.r32(q.table + 4 * bar_of(label0, 7)) + q.style_offset)
    check(space == stock_space, f"back on the Master page, Space's bar has CE's style again ({space:#x}, {stock_space:#x})")
    q.press("limiter")
    log = q.take_log()
    r_page = MasterPage(ref)
    r_page.start()
    r_page.take_log()
    r_page.press("limiter")
    ref_log = r_page.take_log()
    title = [e[1] for e in log if e[0] == "title"]
    check(title == [e[1] for e in ref_log if e[0] == "title"], f"Stock: CE's Limiter title ({title})")
    check(last(log, "text") == last(ref_log, "text"), "Stock: CE's labels")
    frame = q.handle_of("_ZTV6cFrame")
    stops = []
    for _ in range(4):
        q.press("right")
        stops.append(q.place(frame))
    check(stops == [1, 2, 4, 4], f"Stock: Right stops on 1, 2, 4 ({stops})")
    q.press("knob5")
    check(q.place(frame) is None, "Stock: the button under column 5 does nothing")
    q.press("cancel")

    # a file the card doesn't have: CE's limiter, and the title says so
    set_dynamics(q.r, "Gone.ptmfx")
    q.take_log()
    q.press("limiter")
    title = [e[1] for e in q.take_log() if e[0] == "title"]
    check(title[-1:] == ["Limiter: Missing: Gone.ptmfx"], f"missing: the title says so ({title})")
    check(not q.r.failures, "no asserts")


def glue_page(mods):
    """The Limiter page with Glue: its own Attack, Release and Threshold in CE's columns 0 to 2 (CE's
    knobs untouched), the Tracks list in column 4 always, Ratio and Makeup in 5 and 6"""
    p = MasterPage(mods)
    m = p.m
    set_mod(p.r, True, 0, 0)
    set_dynamics(p.r, "Glue.ptmfx")
    p.start()
    ce_calls = []
    for name in ("_Z19changeLimiterAttacks", "_Z20changeLimiterReleases", "_Z21changeLimiterTresholds",
                 "_Z23setDefaultLimiterAttackv"):
        if name in p.r.syms:
            p.r.watch(m.addr(name), lambda mm, n=name: ce_calls.append(n))
    p.take_log()
    p.press("limiter")
    log = p.take_log()
    titles = [e[1] for e in log if e[0] == "title"]
    final, second = last(log, "text"), last(log, "text2")
    label0 = next(h for h, t in final.items() if t == "Back") - 7
    check(titles[-1] == "Glue", f"Glue: the page's title, without Limiter ({titles[-1]})")
    cols = [(final.get(label0 + i), second.get(label0 + i)) for i in range(7)]
    want = [("Attack", "10.0 ms"), ("Release", "200 ms"), ("Threshold", "-12.0 dB"), ("Gain Reduce", ""),
            ("Tracks", "Master"), ("Ratio", "4.0:1"), ("Makeup", "+0.0 dB")]
    check(cols == want, f"Glue: its knobs in columns 0 to 2, Tracks in 4, Ratio and Makeup in 5 and 6 ({cols})")
    frame = p.handle_of("_ZTV6cFrame")
    stops = []
    for _ in range(6):
        p.press("right")
        stops.append(p.place(frame))
    check(stops == [1, 2, 4, 5, 6, 6], f"Right stops on 1, 2, 4, 5, 6 ({stops})")
    for _ in range(6):
        p.press("left")
    check(p.place(frame) == 0, "back on Attack")
    p.take_log()
    p.encoder(1)
    log = p.take_log()
    k = own_knobs(p.r)
    check(k[0] == 41.0 and any(e[0] == "text2" and e[2] == "11.2 ms" for e in log),
          f"encoder on Attack: one step, 11.2 ms, in settings as step 41 {k}")
    p.encoder(-100)
    check(own_knobs(p.r)[0] == 0.0, f"Attack stops at 0.1 ms (step 0) {own_knobs(p.r)}")
    p.take_log()
    p.press("delete")
    log = p.take_log()
    check(own_knobs(p.r)[0] == 40.0 and any(e[0] == "text2" and e[2] == "10.0 ms" for e in log),
          f"Delete on Attack: its default, 10.0 ms {own_knobs(p.r)}")
    p.press("right")
    p.encoder(100)
    log = p.take_log()
    check(own_knobs(p.r)[1] == 32.0 and any(e[0] == "text2" and e[2] == "2.00 s" for e in log),
          f"Release at its end: 2.00 s {own_knobs(p.r)}")
    p.press("right")
    p.encoder(-4)
    log = p.take_log()
    check(own_knobs(p.r)[2] == -14.0 and any(e[0] == "text2" and e[2] == "-14.0 dB" for e in log),
          f"Threshold, four steps down: -14.0 dB {own_knobs(p.r)}")
    check(not ce_calls, f"CE's Attack, Release and Threshold aren't touched ({ce_calls})")
    value = struct.unpack("<f", m.read(runtime_slot(p.r, DYNAMICS) + KNOB_VALUES + 4 * 2, 4))[0]
    check(value == -14.0, f"the plugin is given -14.0 for its Threshold ({value})")

    # The Tracks list: always up in column 4, the same set as the Ducks list
    p.press("right")
    check(p.place(frame) == 4, "Right from Threshold: the Tracks list")
    p.encoder(1)
    p.press("eq")
    p.encoder(2)
    p.press("eq")
    log = p.take_log()
    ducks = struct.unpack("<H", m.read(m.addr(SETTINGS) + DUCKS_AT, 2))[0]
    check(ducks == 0b101 and last(log, "text2").get(label0 + 4) == "2 tracks",
          f"Track 1 and Track 3 marked: 2 tracks ({ducks:#x})")
    bus = struct.unpack("<H", m.read(m.addr(BUS_MASK), 2))[0]
    check(bus == 0b101 and duck_mask(p.r) == 0, f"the bus at once: Track 1 and 3, nothing ducked ({bus:#x})")
    shift = [1]
    p.r.stub(m.addr("_ZN7Buttons15isButtonPressedEi"), lambda mm: shift[0])
    p.take_log()
    p.press("eq")
    shift[0] = 0
    check(last(p.take_log(), "text").get(label0 + 4) in (None, "Tracks"),
          "Shift + column 4: still the Tracks list (no Ducks list for a bus plugin)")
    p.press("delete")
    ducks = struct.unpack("<H", m.read(m.addr(SETTINGS) + DUCKS_AT, 2))[0]
    check(ducks == 0 and struct.unpack("<H", m.read(m.addr(BUS_MASK), 2))[0] == 0,
          f"Delete on Tracks: back to the master ({ducks:#x})")
    total = sum(p.page_bytes().values())
    check(total < LIMIT, f"Glue's page fits ({total} of {LIMIT} B, upper bound)")
    p.press("cancel")
    p.take_log()
    p.press("limiter")
    log = p.take_log()
    check(last(log, "text").get(label0 + 4) == "Tracks" and last(log, "text2").get(label0 + 2) == "-14.0 dB",
          "opened again: the Tracks list, and the knobs as left")
    check(not p.r.failures, "no asserts")

    # Another dynamics plugin picked (masterfx::choose, as the Master page's list and Config call it):
    # the set back to the master
    m.write(m.addr(SETTINGS) + DUCKS_AT, struct.pack("<H", 0b101))
    m.call(m.addr("_ZN4mods8masterfx5applyEv"), max_insns=400_000_000)
    m.call(m.addr("_ZN4mods8masterfx6chooseEhh"), DYNAMICS, 2, max_insns=50_000_000)
    m.call(m.addr("_ZN4mods8masterfx5applyEv"), max_insns=400_000_000)
    ducks = struct.unpack("<H", m.read(m.addr(SETTINGS) + DUCKS_AT, 2))[0]
    check(slot_file(p.r, DYNAMICS) == "Compressor.ptmfx" and ducks == 0,
          f"Glue to Compressor: the set back to the master ({slot_file(p.r, DYNAMICS)}, {ducks:#x})")
    p.press("cancel")
    p.take_log()
    p.press("limiter")
    log = p.take_log()
    final, second = last(log, "text"), last(log, "text2")
    columns = {i: (final.get(label0 + i), second.get(label0 + i)) for i in (3, 4, 7)}
    check(columns == {3: ("Sidechain", "Disable"), 4: ("Ducks", "Master"), 7: ("Gain Reduce", "Back")},
          f"then the Limiter page: CE's Sidechain list in 3, Ducks in 4, the meter above Back ({columns})")
    check(not p.r.failures, "no asserts")


def lists_and_card(mods):
    """What the first device check found: a step in one list moved the others' bars to Stock and slid
    them back (each list was given its data again, from the top); a file copied to /MasterFX after
    the start stayed Missing until a restart; the Missing text, with the file's name, spilled out of
    its column"""
    p = MasterPage(mods)
    set_mod(p.r, True, 4, 2)                      # Shimmer + BBD, Stock in the dynamics slot
    p.start()
    given = [e[1] for e in p.take_log() if e[0] == "data"][-3:]
    names = dict(zip(given, ("Reverb", "Delay", "Limiter")))
    reverb, delay, dynamics = given
    opened = {names[h]: [p.bar_y(h) for _ in range(4)] for h in given}
    check(all(None not in f and len(set(f)) == 1 for f in opened.values()),
          f"the page opens with each list's bar on its choice, still from the first frame ({opened})")
    settled = {h: opened[names[h]][-1] for h in given}
    for _ in range(3):
        p.press("right")                          # to the Limiter list
    for step, what in ((1, "Glue"), (-1, "back to Stock"), (-1, "Up at Stock")):
        p.encoder(step)
        log = p.take_log()
        frames = {names[h]: [p.bar_y(h) for _ in range(4)] for h in (reverb, delay)}
        check(all(f == [settled[h]] * 4 for h, f in zip((reverb, delay), frames.values())),
              f"Limiter list, {what}: the Reverb and Delay bars don't move ({frames})")
        check(not any(e[0] == "data" for e in log), f"Limiter list, {what}: no list is given its data again")
    moved = [p.bar_y(dynamics) for _ in range(6)]
    check(moved[-1] == settled[dynamics], f"and the Limiter bar is back on Stock ({moved})")

    # a file copied to the card after the start: the Master page lists it when it opens, and plays it
    ducker = REPO / "build" / "masterfx" / "examples" / "Ducker.ptmfx"
    if not ducker.exists():
        print(f"     no {ducker} (tools/masterfx/build_pack.py builds it): the card checks skipped")
        return
    q = MasterPage(mods)
    set_mod(q.r, True, 0, 0)
    set_dynamics(q.r, "Ducker.ptmfx")
    q.start()
    log = q.take_log()
    column = [e for e in log if e[0] == "text2" and e[2] and e[2].startswith("Missing")]
    check([e[2] for e in column][-1:] == ["Missing"], f"missing: the Limiter column says Missing, no file name ({[e[2] for e in column]})")
    q.press("limiter")
    title = [e[1] for e in q.take_log() if e[0] == "title"]
    check(title[-1:] == ["Limiter: Missing: Ducker.ptmfx"], f"and the Limiter page's title names the file ({title})")
    q.press("cancel")
    q.r.sd.files["/MasterFX/Ducker.ptmfx"] = ducker.read_bytes()
    q.take_log()
    q.press("cancel")                             # nothing lists the card but the page opening
    check(slot_file(q.r, DYNAMICS) == "Ducker.ptmfx" and q.m.r8(runtime_slot(q.r, DYNAMICS)) == 0,
          "copied while the page is open: not yet listed")
    q.start()
    log = q.take_log()
    active = q.m.r8(runtime_slot(q.r, DYNAMICS))
    check(active == 1, f"the Master page opens: Ducker is listed and plays ({active})")
    check(any(e[0] == "text2" and e[2] == "Ducker" for e in log), "and the Limiter column says Ducker")

    # and the open of a project: masterfx::load lists the card again
    w = MasterPage(mods)
    set_mod(w.r, True, 0, 0)
    set_dynamics(w.r, "Ducker.ptmfx")
    w.m.call(w.m.addr("_ZN4mods18masterfxKnobEditedEv"), max_insns=50_000_000)    # into the project's record
    w.r.sd.files["/MasterFX/Ducker.ptmfx"] = ducker.read_bytes()
    w.m.call(w.m.addr("_ZN4mods8registry15workspaceLoadedERKNS_5StoreE"),
             w.m.addr("_ZN4mods12_GLOBAL__N_112projectStoreE"), max_insns=400_000_000)
    active = w.m.r8(runtime_slot(w.r, DYNAMICS))
    check(slot_file(w.r, DYNAMICS) == "Ducker.ptmfx" and active == 1,
          f"copied before a project's open: it plays once the project opens ({slot_file(w.r, DYNAMICS)!r}, {active})")

    # a long name is cut to the column's width; the list's own line is cut by the list
    data = bytearray(ducker.read_bytes())
    long_name = b"Ducker, with a much longer name"
    data[12:44] = long_name + b"\0" * (32 - len(long_name))
    z = MasterPage(mods)
    # The display's start fills fonts[]' widths from the fonts it unpacks, and the rig doesn't run
    # it: Font2, the label's second line, gets 9 px a character, so 94 px holds 10
    fonts = z.m.sym("fonts")
    z.m.write(fonts.addr + 1 * (fonts.size // 5) + 32, bytes([9]) * 96)   # fonts[1]; 5 fonts (displayFontCount)
    z.r.sd.files["/MasterFX/LongName.ptmfx"] = bytes(data)
    set_mod(z.r, True, 0, 0)
    set_dynamics(z.r, "LongName.ptmfx")
    z.start()
    shown = [e[2] for e in z.take_log() if e[0] == "text2" and e[2] and e[2].startswith("Ducker")]
    check(shown[-1:] == [long_name.decode()[:10]],
          f"a 31-character name: cut to the column's 94 px, 10 characters at 9 px ({shown[-1:]!r})")
    check(not (p.r.failures or q.r.failures or w.r.failures or z.r.failures), "no asserts")


class Graph(SoundRig):
    """A rig with the effects' knobs up and a synth part sending to them."""

    def __init__(self, axf, reverb_send=0, delay_send=0, engine=2):
        super().__init__(axf)
        m = self.m
        self.stub(m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: self.platform)
        if "_ZN4mods15masterfxStartedEv" in self.syms:
            m.call(m.addr("_ZN4mods15masterfxStartedEv"), max_insns=50_000_000)
        self.knobs()
        self.set_instrument(0, engine)
        # All three: a mixer input is at unity until engine.update sets it, and the rig calls
        # updateAudioBuffers without that. Twice: AudioMixer ramps from the gain before the last
        # one, and a first note would get a block of that
        for _ in range(2):
            for part, reverb, delay in ((0, reverb_send, delay_send), (1, 0, 0), (2, 0, 0)):
                self.reverb_send(part, reverb)
                self.delay_send(part, delay)

    def _float(self, name, value):
        self.m.uc.reg_write(UC_ARM_REG_S0, struct.unpack("<I", struct.pack("<f", value))[0])
        self.m.call(self.m.addr(name), self.engine, max_insns=5_000_000)

    def knobs(self, size=0.7, damp=0.3, predelay=0.1, diffusion=0.6, time=200, feedback=50, filt=8000,
              params=0x80):
        m = self.m
        self._float("_ZN11audioEngine13setReverbSizeEf", size)
        self._float("_ZN11audioEngine13setReverbDampEf", damp)
        self._float("_ZN11audioEngine17setReverbPredelayEf", predelay)
        self._float("_ZN11audioEngine18setReverbDiffusionEf", diffusion)
        m.call(m.addr("_ZN11audioEngine12setDelayTimeEt"), self.engine, time)
        m.call(m.addr("_ZN11audioEngine16setDelayFeedbackEh"), self.engine, feedback)
        m.call(m.addr("_ZN11audioEngine23setDelayFilterFrequencyEt"), self.engine, filt)
        m.call(m.addr("_ZN11audioEngine14setDelayParamsEh"), self.engine, params)

    def choose(self, on, reverb, delay):
        set_mod(self, on, reverb, delay)


def phrase(r, on_blocks=40, tail=260):
    """A note and what's left after it."""
    r.note(0, 60, True)
    a = r.blocks(on_blocks)
    r.note(0, 60, False)
    b = r.blocks(tail)
    return a, b


def test_graph(mods, ref):
    print("-- graph --")
    # the switch and the boot gate
    r = SoundRig(mods)
    r.stub(r.m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: r.platform)
    set_mod(r, True, 4, 2)

    def active():
        return playing(r)
    check(active() == (0, 0), "before the first screen: the choice waits, Stock runs")
    r.m.call(r.m.addr("_ZN4mods15masterfxStartedEv"), max_insns=50_000_000)
    check(active() == (4, 2), f"the first screen starts it {active()}")

    # blocked first, new effect set, then the clear
    order = []
    r.watch(r.m.addr("_ZN19AudioEffectPolyverb5resetEv"), lambda mm: order.append(("reset", active()[0])))
    r.watch(r.m.addr("_ZN21AudioEffectShortDelay11blockUpdateEv"), lambda mm: order.append(("block", active()[1])))
    r.watch(r.m.addr("_ZN21AudioEffectShortDelay5clearEv"), lambda mm: order.append(("clear", active()[1])))
    set_mod(r, True, 2, 1)
    check(active() == (2, 1), f"the switch path changes the effects {active()}")
    check(("reset", 2) in order and ("clear", 1) in order, f"CE's clears run with the new choice set ({order})")
    if ("clear", 1) in order:
        check(("block", 2) in order[:order.index(("clear", 1))], "the delay is blocked before the choice changes, and cleared after")
    set_mod(r, False)
    check(active() == (0, 0), "mod Off: Stock")
    set_mod(r, True, 200, 200)
    check(active() == (0, 0), "a damaged choice: Stock")

    # Off, and On with Stock: CE, with notes through both sends
    base = phrase(Graph(ref, 50, 50))
    off = Graph(mods, 50, 50)
    off.choose(False, 4, 2)
    check(phrase(off) == base, "mod Off with Shimmer and BBD chosen: reference build, sample for sample")
    stock = Graph(mods, 50, 50)
    stock.choose(True, 0, 0)
    check(phrase(stock) == base, "mod On, Stock and Stock: reference build, sample for sample")

    # the synth's send reaches the chosen effect
    dry = phrase(Graph(ref))
    for reverb in range(1, 7):
        g = Graph(mods)
        g.choose(True, reverb, 0)
        check(phrase(g) == dry, f"{REVERBS[reverb]}: send at 0, the dry sound and nothing else")
        g = Graph(mods, 80, 0)
        g.choose(True, reverb, 0)
        a, b = phrase(g)
        late = [p - q for p, q in zip(b[0][128 * 60:], dry[1][0][128 * 60:])]
        check(peak(late) > 8, f"{REVERBS[reverb]}: a send reaches it, and it rings on after the note (peak {peak(late)} over the dry sound)")
        check(not g.failures, f"{REVERBS[reverb]}: no asserts")
    for delay in (1, 2):
        g = Graph(mods)
        g.choose(True, 0, delay)
        check(phrase(g) == dry, f"{DELAYS[delay]}: send at 0, the dry sound and nothing else")
        g = Graph(mods, 0, 80)
        g.choose(True, 0, delay)
        a, b = phrase(g)
        late = [p - q for p, q in zip(b[0][128 * 60:], dry[1][0][128 * 60:])]
        check(peak(late) > 8, f"{DELAYS[delay]}: a send reaches it, and it repeats after the note (peak {peak(late)} over the dry sound)")
        check(b[0] != b[1], f"{DELAYS[delay]}: PingPong On, left and right differ")


DYNAMICS = 2                        # the dynamics slot
LIMITERS = "_ZN4mods8masterfx8limitersE"
ACTIVE_DYNAMICS = "_ZN4mods8masterfx14activeDynamicsE"
PROCESS = "_ZN4mods8masterfx7plugins7processENS1_4SlotER10PtmfxBlockPf"


UNSET = struct.pack("<I", 0x7FC00000)  # masterfx.cpp's unset(): a knob at the plugin's default


def set_dynamics(r, file, knobs=None):
    """The dynamics slot's file in settings, as masterfx::choose leaves it (the own knobs unset, or
    `knobs`), then applied"""
    m = r.m
    knobs = list(knobs or ()) + [None] * (OWN_KNOBS - len(knobs or ()))
    own = b"".join(UNSET if k is None else struct.pack("<f", k) for k in knobs)
    m.write(m.addr(SETTINGS) + SLOT_SIZE * DYNAMICS, file.encode() + b"\0" * (48 - len(file)) + own)
    m.call(m.addr("_ZN4mods8masterfx5applyEv"), max_insns=400_000_000)


def own_knobs(r):
    """The dynamics slot's own knobs as the record holds them"""
    return struct.unpack(f"<{OWN_KNOBS}f", r.m.read(r.m.addr(SETTINGS) + SLOT_SIZE * DYNAMICS + 48, 4 * OWN_KNOBS))


def limiter_knobs(r, threshold=32767, sidechain=0):
    """CE's Limiter page, through the engine's setters (the same on both builds)"""
    m = r.m
    m.call(m.addr("_ZN11audioEngine19setLimiterThresholdEt"), r.engine, threshold, max_insns=5_000_000)
    m.call(m.addr("_ZN11audioEngine19setLimiterSidechainEh"), r.engine, sidechain, max_insns=5_000_000)


def meter(r):
    """What the Limiter page's Gain Reduce meter reads from each object"""
    m = r.m
    out = []
    for i in (0, 1):
        obj = m.r32(m.addr(LIMITERS) + 4 * i)
        m.call(m.addr("_ZN18AudioEffectLimiter20getAverageGainChangeEv"), obj)
        out.append(struct.unpack("<f", struct.pack("<I", m.uc.reg_read(UC_ARM_REG_S0) & 0xFFFFFFFF))[0])
    return out


def test_dynamics(mods, ref):
    print("-- dynamics --")
    syms = SoundRig(mods).syms
    if START not in syms:
        print("SKIP the dynamics slot: firmware from before the plugins (masterfx-1.x)")
        return
    if LIMITERS not in syms:
        check(False, "the plugin firmware has the dynamics slot")
        return
    loud = 6000                      # CE's threshold knob, well under the note's peak

    def graph(axf, file=None, threshold=32767, sidechain=0):
        g = Graph(axf)
        if axf == mods:
            set_mod(g, True, 0, 0)
            if file is not None:
                set_dynamics(g, file)
        limiter_knobs(g, threshold, sidechain)
        return g

    # Stock: CE's limiter, untouched
    base = phrase(graph(ref))
    check(phrase(graph(mods, "")) == base, "Stock in the dynamics slot: the reference build, sample for sample")
    hot = phrase(graph(ref, threshold=loud))
    check(phrase(graph(mods, "", threshold=loud)) == hot, "Stock with the threshold down: the reference build, sample for sample")
    note_peak = peak(base[0][0])
    check(note_peak > 2000, f"the note plays (peak {note_peak} at the output, after CE's master volume)")

    # Compressor, the threshold at the top: nothing to do. CE's limiter delays the master by 5 samples
    # (LIMITER_DELAY) and the plugin doesn't, so the plugin's master is CE's 5 samples sooner.
    g = graph(mods, "Compressor.ptmfx")
    m = g.m
    check(m.r8(m.addr(ACTIVE_DYNAMICS)) == 1, "Compressor chosen: the dynamics hook runs the plugin")
    check(all(math.isnan(k) for k in own_knobs(g)), f"its own knobs left unset in settings: Compressor's defaults play {own_knobs(g)}")
    calls = []
    g.watch(m.addr(PROCESS), lambda mm: calls.append(mm.arg(0)) if mm.arg(0) == DYNAMICS else None)
    a, b = phrase(g)
    blocks = len(a[0]) // 128 + len(b[0]) // 128
    check(calls.count(DYNAMICS) == blocks, f"once a block, from the left object only ({calls.count(DYNAMICS)} calls in {blocks} blocks)")
    for side in (0, 1):
        mine = a[side] + b[side]
        ce = base[0][side] + base[1][side]
        check(mine[:-5] == ce[5:], f"threshold at the top, side {side}: CE's master 5 samples sooner, sample for sample")
    check(not g.failures, "Compressor: no asserts")

    # Compressor, the threshold down: both sides turned down, the meter shows it
    g = graph(mods, "Compressor.ptmfx", threshold=loud)
    a, b = phrase(g, tail=40)
    gains = meter(g)
    for side in (0, 1):
        check(peak(a[side][128 * 20:]) < 0.8 * peak(base[0][side][128 * 20:]),
              f"threshold down, side {side}: the held note turned down (peak {peak(a[side][128 * 20:])} against {peak(base[0][side][128 * 20:])})")
    check(gains[0] == gains[1] and 0.0 < gains[0] < 0.95, f"the meter: the plugin's gain on both objects {gains}")

    # Over silence (CE's 7482cc8): the master sends no block while it's silent, and the slot runs the
    # plugin on a zeroed one, so the plugin's level releases. The sound is cut at once
    # (Synth::silencePart, as a patch load does; a CUT step does the same to a sample track), with the
    # level still high. With the plugin skipped over the silence (before the fix, 2026-11-16) the next
    # note's first six blocks peaked at 1,232 against 17,002 with 2.0's Glue (Compressor in 2.1): about
    # 23 dB down, swelling back as it released. A note that fades out lets it release before the
    # silence, so it doesn't show this. Both dynamics plugins of the pack: Compressor, and Glue on
    # the master. Glue's own Release is 200 ms by default, so its silence is 350 blocks (1 s, five of
    # its times): a Glue that releases over the silence is back, and one skipped over it (its level
    # frozen) isn't, however long. After 19 blocks it is still turning down (the next note 4.7 dB
    # down, 2026-11-16), and after 59 with its fastest Release, 50 ms, 2.6 dB: the next note's own
    # attack is in the first 6 blocks too, so the silence is long enough for any Release up to 200 ms.
    for name, quiet in (("Compressor", 20), ("Glue", 350)):
        g = graph(mods, f"{name}.ptmfx", threshold=loud)
        lm = g.syms["_ZN18AudioEffectLimiter10modsUpdateEv"]
        made = [0]

        def on_allocate(mm):
            lr = mm.uc.reg_read(UC_ARM_REG_LR) & ~1
            if lm.addr & ~1 <= lr < (lm.addr & ~1) + lm.size:
                made[0] += 1
        g.watch(g.m.addr("_ZN11AudioStream8allocateEv"), on_allocate)
        g.note(0, 60, True)
        first = g.blocks(40)
        g.m.call(g.m.addr("_ZN5Synth11silencePartEi"), g.synth, 0, max_insns=50_000_000)
        g.blocks(quiet)
        silent_blocks = made[0] // 2
        g.note(0, 60, True)
        second = g.blocks(40)
        head = slice(0, 128 * 6)
        p1, p2 = peak(first[0][head]), peak(second[0][head])
        check(silent_blocks >= quiet - 5 and p2 >= 0.8 * p1,
              f"{name}: cut at once, then {silent_blocks} blocks of a silent master: the next note comes in as "
              f"the first did (its first 6 blocks peak {p2} against {p1})")

    # A key that is silent: nothing to duck, though the master is over the threshold
    g = graph(mods, "Compressor.ptmfx", threshold=loud, sidechain=1)   # sample track 1, which plays nothing
    a, b = phrase(g)
    for side in (0, 1):
        mine = a[side] + b[side]
        ce = base[0][side] + base[1][side]
        check(mine[:-5] == ce[5:], f"a silent key, side {side}: the master untouched, CE's 5 samples sooner")

    # Bypassed by the time guard (its byte set, as the interrupt sets it; tools/masterfx/test_guards.py
    # makes the guard trip): the master passes through untouched, the threshold down or not
    g = graph(mods, "Compressor.ptmfx", threshold=loud)
    g.m.write(runtime_slot(g, DYNAMICS) + 2, b"\x01")
    a, b = phrase(g)
    for side in (0, 1):
        mine = a[side] + b[side]
        ce = base[0][side] + base[1][side]
        check(mine[:-5] == ce[5:], f"Compressor bypassed, side {side}: the master untouched, CE's 5 samples sooner")

    # A bypassed reverb or delay returns silence: the output is the dry sound alone, as with no send
    for slot, reverb, delay in ((0, 4, 0), (1, 0, 2)):
        name = "Shimmer" if slot == 0 else "BBD"
        g = Graph(mods, 60 if slot == 0 else 0, 60 if slot == 1 else 0)
        set_mod(g, True, reverb, delay)
        played_on = phrase(g)
        g = Graph(mods, 60 if slot == 0 else 0, 60 if slot == 1 else 0)
        set_mod(g, True, reverb, delay)
        g.m.write(runtime_slot(g, slot) + 2, b"\x01")
        bypassed = phrase(g)
        g = Graph(mods, 0, 0)
        set_mod(g, True, 0, 0)
        dry = phrase(g)
        check(played_on != dry, f"{name} playing: it adds to the dry sound (the check below can fail)")
        check(bypassed == dry, f"{name} bypassed: no return, the dry sound sample for sample")

    # CE's resetLimiter (Sample Editor, Sample Recorder): the slot's memory cleared with the master
    # passed through, then the plugin starts over
    g = graph(mods, "Compressor.ptmfx", threshold=loud)
    m = g.m
    seen = []
    g.watch(m.addr("_ZN4mods8masterfx19clearDynamicsMemoryEv"), lambda mm: seen.append(("clear", m.r8(m.addr(ACTIVE_DYNAMICS)))))
    g.watch(m.addr("_ZN4mods8masterfx7plugins7clearedENS1_4SlotE"),
            lambda mm: seen.append(("cleared", mm.arg(0), m.r8(m.addr(ACTIVE_DYNAMICS)))))
    m.call(m.addr("_ZN11audioEngine12resetLimiterEv"), g.engine, max_insns=50_000_000)
    check(seen == [("clear", 2), ("cleared", DYNAMICS, 2)] and m.r8(m.addr(ACTIVE_DYNAMICS)) == 1,
          f"resetLimiter: memory cleared and the plugin reset with the master passed through, then it runs again {seen}")

    # Back to Stock: CE's limiter plays again, from its power-up state
    g = graph(mods, "Compressor.ptmfx")
    phrase(g)
    set_dynamics(g, "")
    check(g.m.r8(g.m.addr(ACTIVE_DYNAMICS)) == 0, "Stock again: CE's limiter")
    again = phrase(g)
    fresh = graph(mods, "")
    phrase(fresh)
    check(again == phrase(fresh), "Stock after Compressor: as if Compressor had never played")

    # The mod Off: CE's limiter
    g = graph(mods, "Compressor.ptmfx")
    set_mod(g, False)
    check(g.m.r8(g.m.addr(ACTIVE_DYNAMICS)) == 0, "mod Off: CE's limiter")

    # A file that isn't there: CE's limiter
    g = graph(mods, "Gone.ptmfx")
    check(g.m.r8(g.m.addr(ACTIVE_DYNAMICS)) == 0, "a missing file: CE's limiter")


DUCK_MASK = "_ZN4mods8masterfx8duckMaskE"
DUCK_LIVE = "_ZN4mods8masterfx8duckLiveE"
DUCK_UPDATE = "_ZN14AudioAmplifier14modsDuckUpdateEv"
DUCK_COMPUTE = "_ZN4mods8masterfx12_GLOBAL__N_111duckComputeEPKsS3_"
DUCK_BLOCK = "_ZN4mods8masterfx9duckBlockEPs"
INPUT = "_ZN11audioEngine23updateInputAudioBuffersEPvS0_i"
DUCKS_AT = 3 * SLOT_SIZE            # masterfx::Settings.ducks, after the three slots
LINE_IN_L, SYNTH_2 = 9, 12          # CE's Sidechain list: Line in L, Synth 2
SYNTH_1_BIT = 1 << 8                # masterfx's destinations: 0 to 7 the tracks, 8 to 10 the synth parts


def set_ducks(r, mask):
    """The record's ducks, then applied"""
    m = r.m
    m.write(m.addr(SETTINGS) + DUCKS_AT, struct.pack("<H", mask))
    m.call(m.addr("_ZN4mods8masterfx5applyEv"), max_insns=400_000_000)


def duck_mask(r):
    return struct.unpack("<H", r.m.read(r.m.addr(DUCK_MASK), 2))[0]


class LineIn:
    """The I2S input buffer, as the interrupt hands it to the engine (updateInputAudioBuffers),
    filled before each block: interleaved left and right, a ~920 Hz sine at the given level."""

    def __init__(self, r):
        self.r = r
        m = r.m
        self.buf = m.alloc(4 * 128, 4)
        m.write(self.buf, bytes(4 * 128))
        m.call(m.addr(INPUT), r.engine, self.buf, self.buf, 128, max_insns=1_000_000)
        self.n = 0

    def blocks(self, levels, each=None):
        left, right = [], []
        for level in levels:
            s = [int(level * math.sin(2 * math.pi * (self.n * 128 + i) / 48)) for i in range(128)]
            self.r.m.write(self.buf, struct.pack("<256h", *[v for x in s for v in (x, x)]))
            self.n += 1
            a, b = self.r.block()
            left += a
            right += b
            if each:
                each()
        return left, right


def first_ducked(mine, ce):
    """The first block where the mod's master isn't CE's (CE's limiter, threshold at the top,
    delays it 5 samples; the plugin's hook doesn't)"""
    for i in range(len(mine) - 5):
        if mine[i] != ce[i + 5]:
            return i // 128
    return None


def watch_gain(g, track_blocks=None):
    """Who works out each block's gain: a ducked track's amplifier (the key made before it, this
    block's gain) or the limiter (the key made after it: the track had the last block's). With
    track_blocks, also each time a ducked track had a block to duck ("block")"""
    m = g.m
    s = g.syms[DUCK_BLOCK]
    calls = []

    def f(mm):
        lr = mm.uc.reg_read(UC_ARM_REG_LR) & ~1
        calls.append("track" if s.addr <= lr < s.addr + s.size else "limiter")
    g.watch(m.addr(DUCK_COMPUTE), f)
    if track_blocks is not None:
        g.watch(s.addr, lambda mm: track_blocks.append("block"))
    return calls


def test_ducks(mods, ref):
    """The dynamics slot ducking tracks (Tracker+): synth part 0 holds a note, the key is the Line
    in (made before the synth parts' amplifiers) or synth part 1 (made after them)"""
    print("-- ducks --")
    if DUCK_MASK not in SoundRig(mods).syms:
        print("SKIP the ducking: firmware from before it")
        return
    loud = 6000
    quiet, key, after = 30, 50, 60
    levels = [0] * quiet + [16000] * key + [0] * after

    def run(axf, file=None, ducks=0, threshold=32767, sidechain=LINE_IN_L, synth_key=False, watch=None):
        g = Graph(axf)
        if synth_key:
            g.set_instrument(1, 2)
        if axf == mods:
            set_mod(g, True, 0, 0)
            m = g.m
            m.write(m.addr(SETTINGS) + DUCKS_AT, struct.pack("<H", ducks))
            set_dynamics(g, file if file is not None else "")
        limiter_knobs(g, threshold, sidechain)
        line = LineIn(g)
        g.note(0, 48, True)
        if not synth_key:
            out = line.blocks(levels, watch and (lambda: watch(g)))
        else:
            out = line.blocks([0] * quiet)
            g.note(1, 72, True)
            more = line.blocks([0] * key)
            g.note(1, 72, False)
            rest = line.blocks([0] * after)
            out = (out[0] + more[0] + rest[0], out[1] + more[1] + rest[1])
        return g, out

    _, ce = run(ref, sidechain=LINE_IN_L)
    held = peak(ce[0][128 * quiet:128 * (quiet + key)])
    check(held > 2000, f"the held note plays through the key (peak {held})")
    _, ce_quiet = run(ref, sidechain=0)
    print(f"     (the Line in {'is' if ce[0] != ce_quiet[0] else 'is not'} heard on the master here)")

    # Compressor ducking Synth 1 by the Line in
    seen = []
    who = {}

    def each(gg):
        if "calls" not in who:
            who["blocks"] = []
            who["calls"] = watch_gain(gg, who["blocks"])
            return
        seen.append((gg.m.r8(gg.m.addr(DUCK_LIVE)), meter(gg)[0], tuple(who["calls"]), bool(who["blocks"])))
        who["calls"].clear()
        who["blocks"].clear()
    g, mine = run(mods, "Compressor.ptmfx", SYNTH_1_BIT, loud, watch=each)
    check(duck_mask(g) == SYNTH_1_BIT, f"Synth 1 in the record: the mask the interrupt reads ({duck_mask(g):#x})")
    check(all(live == 1 for live, _, _, _ in seen), "a key and a ducking plugin: ducking every block")
    # Since CE's 7482cc8 a track with no sound sends no block, so its amplifier has nothing to duck;
    # the limiter then works the gain out (it runs on silence for the plugin), so the gain follows
    # the key on every block
    by_track = {c for _, _, c, had in seen if had}
    by_limiter = {c for _, _, c, had in seen if not had}
    check(by_track == {("track",)} and by_limiter <= {("limiter",)},
          f"a key made before the track (Line in): this block's gain, worked out once, by the track when it has "
          f"a block ({by_track}), else by the limiter ({by_limiter}, {sum(not had for *_, had in seen)} blocks)")
    for side in (0, 1):
        before = 128 * quiet - 5
        check(mine[side][:before] == ce[side][5:before + 5],
              f"side {side}, before the key: the master untouched, CE's 5 samples sooner")
        inside = slice(128 * (quiet + 20), 128 * (quiet + key))
        a, b = peak(mine[side][inside]), peak(ce[side][inside])
        check(a < 0.8 * b, f"side {side}, under the key: Synth 1 ducked (peak {a} against {b})")
    first = first_ducked(mine[0], ce[0])
    print(f"     (the first block ducked: {first}, the key from {quiet})")
    meters = [gain for _, gain, _, _ in seen]
    check(meters[quiet - 3] == 1.0 and min(meters[quiet:quiet + key]) < 0.95,
          f"the meter: 1 before the key, under 0.95 with it ({meters[quiet - 3]}, {min(meters[quiet:quiet + key])})")
    tail = peak(mine[0][-128 * 10:]), peak(ce[0][-128 * 10:])
    check(tail[0] > 0.9 * tail[1], f"the key gone: Synth 1 back (peak {tail[0]} against {tail[1]})")
    check(not g.failures, "no asserts")

    # A silent destination: the master and Synth 1 untouched though the key is loud
    _, silent = run(mods, "Compressor.ptmfx", 1 << 0, loud)
    check(first_ducked(silent[0], ce[0]) is None and first_ducked(silent[1], ce[1]) is None,
          "ducking Track 1, which is silent: the master untouched all through, CE's 5 samples sooner")

    # The key made after the track (synth part 1: PolySynth runs after the panning amplifiers):
    # the gain the block before kept, one block late
    _, ce_synth = run(ref, sidechain=SYNTH_2, synth_key=True)
    _, late = run(mods, "Compressor.ptmfx", SYNTH_1_BIT, loud, SYNTH_2, synth_key=True)
    g2 = Graph(mods)
    g2.set_instrument(1, 2)
    set_mod(g2, True, 0, 0)
    set_ducks(g2, SYNTH_1_BIT)
    set_dynamics(g2, "Compressor.ptmfx")
    limiter_knobs(g2, loud, SYNTH_2)
    line = LineIn(g2)
    g2.note(0, 48, True)
    g2.note(1, 72, True)
    line.blocks([0] * 3)
    late_calls = watch_gain(g2)
    per_block = []
    for _ in range(5):
        line.blocks([0])
        per_block.append(tuple(late_calls))
        late_calls.clear()
    check(set(per_block) == {("limiter",)},
          f"a key made after the track (Synth 2): worked out once a block, by the limiter; the track has the last block's {set(per_block)}")
    print(f"     (the first block ducked: {first_ducked(late[0], ce_synth[0])}, the key from {quiet})")
    inside = slice(128 * (quiet + 20), 128 * (quiet + key))
    a, b = rms(late[0][inside]), rms(ce_synth[0][inside])
    check(a < 0.9 * b, f"and Synth 1 ducked under it (rms {a:.0f} against {b:.0f})")

    # Each destination is its own pair of amplifiers (left, right): bit by bit, the ones the hook
    # takes over in a block
    g = Graph(mods)
    set_mod(g, True, 0, 0)
    set_ducks(g, SYNTH_1_BIT)
    set_dynamics(g, "Compressor.ptmfx")
    limiter_knobs(g, loud, LINE_IN_L)
    line = LineIn(g)
    m = g.m
    taken = []
    g.watch(m.addr(DUCK_UPDATE), lambda mm: taken.append(mm.arg(0)))
    owners = []
    for d in range(11):
        m.write(m.addr(DUCK_MASK), struct.pack("<H", 1 << d))
        line.blocks([16000])
        taken.clear()
        line.blocks([16000])
        owners.append((len(taken), frozenset(taken)))
    counts = [n for n, _ in owners]
    check(counts == [2] * 11 and len(frozenset().union(*[o for _, o in owners])) == 22,
          f"each of the 11 destinations: its own two amplifiers, once a block ({counts})")

    # No ducking: Stock, the mod Off, no key, a plugin without the flag
    g = Graph(mods)
    set_mod(g, True, 0, 0)
    set_ducks(g, SYNTH_1_BIT)
    set_dynamics(g, "")
    check(duck_mask(g) == 0, "Stock in the dynamics slot: nothing ducked, though the record says Synth 1")
    set_dynamics(g, "Compressor.ptmfx")
    check(duck_mask(g) == SYNTH_1_BIT, "Compressor again: Synth 1")
    set_mod(g, False)
    check(duck_mask(g) == 0, "the mod Off: nothing ducked")

    _, plain = run(mods, "Compressor.ptmfx", 0, loud)
    check(plain != mine, "ducks against the master: what plays differs")
    g, keyless = run(mods, "Compressor.ptmfx", SYNTH_1_BIT, loud, sidechain=0)
    _, plain_keyless = run(mods, "Compressor.ptmfx", 0, loud, sidechain=0)
    check(keyless == plain_keyless and g.m.r8(g.m.addr(DUCK_LIVE)) == 0,
          "Sidechain off: Compressor on the master, as without ducks")

    g = Graph(mods)
    set_mod(g, True, 0, 0)
    card = g.sd.files
    glue = bytearray(card["/MasterFX/Compressor.ptmfx"])
    glue[6] = 0x01                  # PTMFX_FLAG_FLOAT only
    card["/MasterFX/NoDuck.ptmfx"] = bytes(glue)
    g.m.call(g.m.addr("_ZN4mods8masterfx7plugins4scanEv"), max_insns=400_000_000)  # the card listed again
    set_ducks(g, SYNTH_1_BIT)
    set_dynamics(g, "NoDuck.ptmfx")
    m = g.m
    check(m.r8(m.addr(ACTIVE_DYNAMICS)) == 1 and duck_mask(g) == 0,
          "a dynamics plugin without PTMFX_FLAG_DUCK: it plays on the master, nothing ducked")

    ducks_page(mods)


PLAY_BUTTON = 23                   # the Play button, as hardware.cpp reports it
QUIET_DRUMS = 10                    # of 100: the bus test's kick and hat step volume
BUS_MASK = "_ZN4mods8masterfx7busMaskE"
BUS_LIVE = "_ZN4mods8masterfx7busLiveE"
BUS_MIXERS = "_ZN4mods8masterfx9busMixersE"
SET_SATURATION = "_ZN11audioEngine17setSaturationTypeE14SaturationType"
HARD_CLIP, SATURATE = 0, 2          # CE's SaturationType
SYNTH_2_BIT = 1 << 9
MAKEUP_6 = 10 ** (6 / 20)           # Glue's Makeup at +6 dB, linear


def bus_update(syms):
    """AudioMixer<11>::modsBusUpdate<11>(), the tracks mixer's bus (a member template: found by name)"""
    names = [n for n in syms if "AudioMixer" in n and "modsBusUpdate" in n]
    return names[0] if len(names) == 1 else None


def ce_saturate(x):
    """CE's kSaturate (dspApplyGain.cpp saturate), on one int32 sample: 3x/2 - x^3/2 at 16 bits,
    with C's integer division and the shifts' rounding (multiply_32x32_rshift(a, b, z) is the
    64-bit product shifted right by 31 - z)"""
    if abs(x) > 32767:
        return 32767 if x >= 0 else -32768
    half = 3 * x
    half = half // 2 if half >= 0 else -((-half) // 2)      # C: toward zero
    out = half - ((((x * x) >> 16) * x) >> 15)
    return max(-32768, min(32767, out))


def through_mixers(x):
    """CE's tracks mixer and dry mix mixer, both at kSaturate, on the tracks mixer's int32 sum (no
    sends, so the dry mix mixer has only the tracks mixer)"""
    return ce_saturate(ce_saturate(x))


def bus_close(got, want, most=2):
    """Each sample within `most` of what it should be: the master's two roundings (the bus's, CE's
    master volume) are the only difference. The largest difference."""
    return max((abs(a - b) for a, b in zip(got, want)), default=0) <= most and len(got) == len(want), \
        max((abs(a - b) for a, b in zip(got, want)), default=0)


def test_bus(mods, ref):
    """Glue on chosen tracks (the Tracks list): the plugin works on their sum in CE's tracks mixer, and
    the mixer sums the rest + what it made of it before its saturation. Checked by superposition where
    CE's mixers are linear (hard clip, under full scale), so the exact sample timing is checked too,
    and against CE's saturation curve where they aren't."""
    print("-- bus --")
    if BUS_MASK not in SoundRig(mods).syms:
        print("SKIP the bus: firmware from before it")
        return
    on_blocks = 60

    def synths(tracks, knobs, parts=(0, 1)):
        """Synth 1 holds a low note and Synth 2 a high one (each part on its own), through Glue. Three
        silent blocks first: the bus starts the pass after the limiter first takes it, and the synth
        parts' sum a pass after that, so from the note on the plugin hears the same either way."""
        g = Graph(mods)
        g.set_instrument(1, 2)
        for part in (0, 1):
            # PolySynth::gainDB, CE's host: each part alone reaches full scale, and the sum of two
            # mustn't clip CE's int16 mix, or the parts don't add up whatever Glue does
            g._send("_ZN11audioEngine12setSynthGainEif", part, -20.0)
        set_mod(g, True, 0, 0)
        m = g.m
        m.write(m.addr(SETTINGS) + DUCKS_AT, struct.pack("<H", tracks))
        set_dynamics(g, "Glue.ptmfx", knobs)
        g.blocks(3)
        for part, note in ((0, 48), (1, 72)):
            if part in parts:
                g.note(part, note, True)
        out = g.blocks(on_blocks)
        return g, out

    null = (None, None, None, 1.0, 0.0)         # Ratio 1:1, no makeup: Glue changes nothing
    lift = (None, None, None, 1.0, 6.0)         # Ratio 1:1, +6 dB: the bus times MAKEUP_6, nothing more
    hard = (0.0, None, -40.0, 10.0, 0.0)        # 0.1 ms, -40 dB, 10:1: it squashes

    g, both = synths(0, null)
    check(g.m.r8(g.m.addr(ACTIVE_DYNAMICS)) == 1 and struct.unpack("<H", g.m.read(g.m.addr(BUS_MASK), 2))[0] == 0,
          "Glue on the master: the plugin plays, no bus")
    _, one = synths(0, null, parts=(0,))
    _, two = synths(0, null, parts=(1,))
    check(peak(one[0]) > 1000 and peak(two[0]) > 1000 and peak(both[0]) < 30000,
          f"both synth parts play, and together don't clip ({peak(one[0])}, {peak(two[0])}, {peak(both[0])})")
    for side in (0, 1):
        ok, worst = bus_close(both[side], [a + b for a, b in zip(one[side], two[side])])
        check(ok, f"side {side}: the two parts add up on the master, as the bus test needs (off by {worst} at most)")

    # Synth 1 lifted alone: the master gains (makeup - 1) * Synth 1, Synth 2 untouched
    g, lifted = synths(1 << 8, lift)
    m = g.m
    check(struct.unpack("<H", m.read(m.addr(BUS_MASK), 2))[0] == 1 << 8 and m.r8(m.addr(BUS_LIVE)) == 1,
          "Synth 1 in the Tracks list: the bus is live")
    for side in (0, 1):
        want = [round(a + (MAKEUP_6 - 1) * c) for a, c in zip(both[side], one[side])]
        check(peak(want) < 30000, f"side {side}: the lifted mix stays under full scale ({peak(want)}), so it can add up")
        ok, worst = bus_close(lifted[side], want)
        check(ok, f"side {side}: +6 dB on Synth 1 alone, Synth 2 as it was, sample for sample "
                  f"(off by {worst} at most)")
    check(not g.failures, "no asserts")

    # The same with Synth 1 squashed: the master is Synth 2 + Glue's Synth 1 played alone
    _, squashed_alone = synths(0, hard, parts=(0,))
    _, squashed = synths(1 << 8, hard)
    for side in (0, 1):
        want = [a + b for a, b in zip(two[side], squashed_alone[side])]
        ok, worst = bus_close(squashed[side], want)
        check(ok, f"side {side}: Glue on Synth 1 is Glue on Synth 1 alone, plus Synth 2 untouched (off by {worst})")
        check(rms(squashed_alone[side][128 * 20:]) < 0.7 * rms(one[side][128 * 20:]),
              f"side {side}: and it does squash ({rms(squashed_alone[side][128 * 20:]):.0f} against {rms(one[side][128 * 20:]):.0f})")

    # The tracks mixer's left object runs the bus once a block, for both sides
    g = Graph(mods)
    set_mod(g, True, 0, 0)
    m = g.m
    m.write(m.addr(SETTINGS) + DUCKS_AT, struct.pack("<H", 1 << 8))
    set_dynamics(g, "Glue.ptmfx")
    name = bus_update(g.syms)
    check(name is not None, "the tracks mixer's bus update is in the build")
    if name is not None:
        left = m.r32(m.addr(BUS_MIXERS))
        right = m.r32(m.addr(BUS_MIXERS) + 4)
        objects = []
        g.watch(m.addr(name), lambda mm: objects.append(mm.arg(0)))
        g.blocks(2)
        objects.clear()
        g.blocks(5)
        check(left != 0 and right != 0 and left != right and objects == [left] * 5,
              f"once a block, from the left tracks mixer only ({len(objects)} in 5 blocks)")

    # Not a bus: Stock, the mod Off, Compressor
    set_dynamics(g, "Compressor.ptmfx")
    check(struct.unpack("<H", m.read(m.addr(BUS_MASK), 2))[0] == 0, "Compressor: no bus, though the record has a set")
    set_dynamics(g, "")
    check(struct.unpack("<H", m.read(m.addr(BUS_MASK), 2))[0] == 0, "Stock: no bus")
    set_dynamics(g, "Glue.ptmfx")
    set_mod(g, False)
    check(struct.unpack("<H", m.read(m.addr(BUS_MASK), 2))[0] == 0 and m.r8(m.addr(ACTIVE_DYNAMICS)) == 0,
          "the mod Off: CE's limiter, no bus")

    # The sample tracks, through a project booted from the card and played: M5's kick (Track 1) and
    # hat (Track 3) and a held synth voice (Track 8, Synth 1). A boot, as PlayRig loads no samples.
    source = source_project()
    if source is None:
        print("     no project beside the repo to copy: the sample-track checks skipped")
        return
    import tempfile
    import make_masterfx_test_project as maker
    blocks = 200
    drums = (1 << 0) | (1 << 2)
    with tempfile.TemporaryDirectory() as temp:
        def play(name, knobs, tracks, synth=True, drums_on=True, sends=False, saturation=HARD_CLIP):
            # The drums quieter than M5's (its kick is at full scale) and no sends: the mixes don't
            # clip, and the sums below are of the tracks' own sound. M5 itself is played as made last.
            # saturation: CE's, forced after the boot (None: the project's own)
            folder = maker.make_masterfx_glue(source, Path(temp), name, knobs, tracks, synth, drums_on,
                                              None if sends else QUIET_DRUMS)[0]
            if not sends:
                no_sends(folder)
            out, r = booted(mods, folder, blocks, saturation)
            check(slot_file(r, DYNAMICS) == "Glue.ptmfx" and r.m.r8(r.m.addr(ACTIVE_DYNAMICS)) == 1 and not r.failures,
                  f"{name}: Glue from the project, playing, no asserts {r.failures}")
            return out, r

        # Hard clip under full scale: CE's mixers add, so the tracks' sounds add up
        full, _ = play("all null", null, 0)
        alone, _ = play("drums null", null, 0, synth=False)
        held, _ = play("synth null", null, 0, drums_on=False)
        check(peak(alone[0]) > 1000 and peak(held[0]) > 500 and peak(full[0]) < 30000,
              f"the drums and the synth play, and together don't clip ({peak(alone[0])}, {peak(held[0])}, {peak(full[0])})")
        for side in (0, 1):
            ok, worst = bus_close([a + h for a, h in zip(alone[side], held[side])], full[side])
            check(ok, f"side {side}: the drums and the synth add up to the full mix (off by {worst}), so the sums "
                      "below are the tracks'")
        lifted, r = play("drums lifted", lift, drums)
        check(struct.unpack("<H", r.m.read(r.m.addr(BUS_MASK), 2))[0] == drums, "the project's Tracks: Track 1 and 3")
        synth_lifted, _ = play("synth lifted", lift, 1 << 8)
        for side in (0, 1):
            # From the second block: the bus starts the pass after the limiter first takes it
            for got, part, what in ((lifted, alone, "Track 1 and 3"), (synth_lifted, held, "Synth 1")):
                want = [round(a + (MAKEUP_6 - 1) * d) for a, d in zip(full[side], part[side])]
                check(peak(want) < 30000, f"side {side}: {what} lifted stays under full scale ({peak(want)}), so it adds up")
                ok, worst = bus_close(got[side][128:], want[128:])
                check(ok, f"side {side}: +6 dB on {what} alone, the rest as it was, sample for sample from the "
                          f"second block (off by {worst} at most)")

        # CE's Saturate: the mixers aren't linear (x 1.5 at low level, each), so a bus taken after them
        # is wrong. Glue that changes nothing changes nothing; Glue's +6 dB on the drums is CE's two
        # mixers on the drums' sum times the makeup.
        sat_full, _ = play("all null, Saturate", null, 0, saturation=SATURATE)
        sat_null, _ = play("drums on a null bus, Saturate", null, drums, saturation=SATURATE)
        for side in (0, 1):
            ok, worst = bus_close(sat_null[side], sat_full[side], most=0)
            check(ok, f"side {side}: Saturate, a bus Glue leaves alone: the master exactly as without one (off by {worst})")
        # The drums alone: on Hard Clip the output is the tracks mixer's sum itself (they don't clip),
        # so CE's Saturate curve twice on it is what Saturate gives, Glue or not
        hard_lifted, _ = play("drums lifted, Hard Clip", lift, drums, synth=False)
        sat_alone, _ = play("drums null, Saturate", null, 0, synth=False, saturation=SATURATE)
        sat_lifted, _ = play("drums lifted, Saturate", lift, drums, synth=False, saturation=SATURATE)
        for side in (0, 1):
            ok, worst = bus_close(sat_alone[side], [through_mixers(x) for x in alone[side]], most=0)
            check(ok, f"side {side}: Saturate is CE's curve twice on the drums' sum, as the rig models it (off by {worst})")
            ok, worst = bus_close(sat_lifted[side][128:], [through_mixers(x) for x in hard_lifted[side][128:]], most=0)
            gain = rms(sat_lifted[side][128:]) / max(rms(sat_alone[side][128:]), 1)
            check(ok, f"side {side}: Saturate, +6 dB on the drums is the makeup before CE's saturation, from the second "
                      f"block (off by {worst}; {gain:.3f}x the drums)")

        # The project's own saturation: Glue squashes the drums (no makeup, so it can only turn down)
        own_alone, _ = play("drums null, the project's saturation", null, 0, synth=False, saturation=None)
        squashed, _ = play("drums squashed, the project's saturation", hard, drums, synth=False, saturation=None)
        for side in (0, 1):
            check(rms(squashed[side][128 * 20:]) < 0.7 * rms(own_alone[side][128 * 20:]),
                  f"side {side}: Glue on the drums turns them down ({rms(squashed[side][128 * 20:]):.0f} against "
                  f"{rms(own_alone[side][128 * 20:]):.0f})")
        _, r = play("M5 as made, with its sends", maker.MASTERFX_GLUE_KNOBS, drums, sends=True, saturation=None)
        check(struct.unpack("<H", r.m.read(r.m.addr(BUS_MASK), 2))[0] == drums and r.m.r8(r.m.addr(BUS_LIVE)) == 1,
              "M5 as the device gets it: the bus live on Track 1 and 3")


def no_sends(folder):
    """The project's instruments and synth parts with no reverb or delay send: the returns aren't the
    bus, and the sums above are of the tracks' own sound"""
    import make_masterfx_test_project as maker
    for path in (folder / "instruments").glob("*.pti"):
        data = bytearray(path.read_bytes())
        data[maker.INSTRUMENT_DELAY_SEND] = data[maker.INSTRUMENT_REVERB_SEND] = 0
        path.write_bytes(data)
    mt = bytearray((folder / "project.mt").read_bytes())
    for slot in range(3):
        record = maker.ce.SLOT_RECORDS + slot * maker.ce.SLOT_RECORD_SIZE
        struct.pack_into("<f", mt, record + maker.SLOT_REVERB_SEND, 0.0)
        struct.pack_into("<f", mt, record + maker.SLOT_DELAY_SEND, 0.0)
    mt[maker.PROJECT_CRC:maker.PROJECT_CRC + 4] = bytes(4)     # as the maker leaves it
    (folder / "project.mt").write_bytes(mt)


def booted(axf, folder, blocks, saturation=None):
    """The project as the unit plays it: a boot from a card with it in the Workspace, its samples
    loaded by CE's file manager in the main loop, then Play. What comes out, and the rig.
    saturation: CE's SaturationType set on the mixers before Play, as the Master page does (None: the
    project's own)."""
    from boot_rig import BootRig
    r = BootRig(axf)
    m = r.m
    screen = [0]
    r.watch(m.addr("_ZN14cPatternEditor6updateEv"), lambda mm: screen.__setitem__(0, screen[0] + 1))
    r.card_from(folder, "Glue", workspace=True)
    r.boot()
    for _ in range(3000):
        r.loop()
        r.interrupt()
        if screen[0] > 5:
            break
    if saturation is not None:
        m.call(m.addr(SET_SATURATION), m.addr("engine"), saturation)
    for _ in range(2):              # press, release
        (r.press if _ == 0 else r.release)(PLAY_BUTTON)
        r.loop()
        r.interrupt()
    left, right = [], []
    for i in range(blocks):
        if i % 8 == 0:
            r.loop()
        a, b = r.interrupt()
        left += a
        right += b
    return (left, right), r


def ducks_page(mods):
    """The Limiter page with Compressor: CE's Sidechain list (the source) in column 3, the Ducks list
    (what it turns down) in column 4, each with its own button"""
    p = MasterPage(mods)
    m = p.m
    set_mod(p.r, True, 0, 0)
    set_dynamics(p.r, "Compressor.ptmfx")
    p.start()
    stored = []
    p.r.watch(m.addr("_ZN4mods7project11markChangedEv"), lambda mm: stored.append(1))
    p.press("limiter")
    log = p.take_log()
    label0 = label0_of(last(log, "text"), last(log, "text2"))
    label3, label4 = label0 + 3, label0 + 4
    frame = p.handle_of("_ZTV6cFrame")

    def ducks():
        return struct.unpack("<H", m.read(m.addr(SETTINGS) + DUCKS_AT, 2))[0]

    def said(log, h=label4):
        return last(log, "text").get(h), last(log, "text2").get(h)

    now = said(log)
    check(now == ("Ducks", "Master"), f"the Ducks list in column 4, on the master {now}")
    check(said(log, label3) == ("Sidechain", "Disable"), f"CE's Sidechain list in column 3, before it {said(log, label3)}")
    bytes_up = sum(p.page_bytes().values())
    check(bytes_up < LIMIT, f"the page with the Sidechain and Ducks lists and the meter: {bytes_up} bytes of {LIMIT}")

    # column 3: CE's Sidechain list, with CE's button behaviour. The rig's project has no track
    # names (CE's list shows them), so the value is read where CE sets it
    keys = []
    p.r.watch(m.addr("_ZN11audioEngine19setLimiterSidechainEh"), lambda mm: keys.append(mm.arg(1) & 0xFF))
    p.press("limiter")                  # the button under column 3
    check(p.place(frame) == 3, "the button under column 3 selects the Sidechain list")
    p.take_log()
    p.press("limiter")
    p.take_log()
    check(keys[-1:] == [1], f"pressed again: the key steps to Track 1, as CE's button does ({keys})")
    p.encoder(1)
    p.take_log()
    check(keys[-1:] == [2], f"the encoder steps it in column 3: Track 2 ({keys})")
    p.press("delete")
    check(said(p.take_log(), label3)[1] == "Disable" and keys[-1:] == [0], f"Delete: CE's default, Disable ({keys})")

    # column 4: the Ducks list
    p.press("eq")                       # the button under column 4
    check(p.place(frame) == 4 and ducks() == 0, f"the button under column 4 selects the Ducks list, marks nothing ({ducks():#x})")
    p.encoder(3)
    p.press("eq")
    now = said(p.take_log())
    check(ducks() == 1 << 2 and now[1] == "Track 3", f"three down and its button: Track 3 marked ({ducks():#x}, {now})")
    check(duck_mask(p.r) == 1 << 2, "and ducked at once")
    check(bool(stored), "and the project marked changed")
    p.encoder(6)
    p.press("eq")
    now = said(p.take_log())
    check(ducks() == (1 << 2) | (1 << 8) and now[1] == "2 tracks", f"Synth 1 too: 2 tracks ({ducks():#x}, {now})")
    p.encoder(20)
    p.press("eq")
    check(ducks() == (1 << 2) | (1 << 8) | (1 << 10), f"the cursor stops on Synth 3 ({ducks():#x})")
    p.take_log()
    p.press("delete")
    now = said(p.take_log())
    check(ducks() == 0 and now[1] == "Master", f"Delete: back to the master {now}")
    p.press("up")
    p.press("eq")
    check(ducks() == 1 << 9, f"Up then its button: Synth 2 ({ducks():#x})")
    p.press("left")
    check(p.place(frame) == 3, "Left: the Sidechain list")
    p.press("right")
    check(p.place(frame) == 4, "Right: the Ducks list again")
    p.press("cancel")
    p.take_log()
    p.press("limiter")
    log = p.take_log()
    now = said(log)
    check(now == ("Ducks", "Synth 2") and said(log, label3)[0] == "Sidechain",
          f"opened again: Sidechain in 3, the Ducks list in 4, Synth 2 marked {now}")
    check(not p.r.failures, "no asserts")

    # Stock: Shift + column 4 is CE's
    q = MasterPage(mods)
    set_mod(q.r, True, 0, 0)
    set_dynamics(q.r, "")
    q.start()
    q.press("limiter")
    q.take_log()
    q.r.stub(q.m.addr("_ZN7Buttons15isButtonPressedEi"), lambda mm: 1)
    q.press("eq")
    check("Ducks" not in last(q.take_log(), "text").values(), "Stock: Shift + column 4 is CE's")
    check(not q.r.failures, "no asserts")


def buffers(g):
    """Each effect buffer's [start, end) from the symbols that hold them."""
    out = []
    for name in ("_ZL21internalRamReverbBuff", "externalRamReverbBuff", "externalRamDelayBuff"):
        s = g.syms.get(name)
        if s and s.size:
            out.append((name, s.addr, s.addr + s.size))
    return out


def test_stress(mods, ref):
    print("-- stress --")
    # the heaviest pair, 8 voices, against Stock
    pools = {}
    for name, (reverb, delay) in {"Stock": (0, 0), "Shimmer and BBD": (4, 2)}.items():
        g = Graph(mods, 80, 80)
        g.set_voices(0, 8)
        g.choose(True, reverb, delay)
        for n in range(8):
            g.note(0, 48 + 3 * n, True)
        g.blocks(40)
        pools[name] = g.pool_peak()
        check(not g.failures and g.pool_peak() <= 80, f"{name}, 8 voices: audio pool peak {g.pool_peak()} of 80")
    check(pools["Shimmer and BBD"] == pools["Stock"], f"the effects take no more of the pool than CE's ({pools})")

    # switches, knobs and notes while it plays
    random.seed(1234)
    g = Graph(mods, 60, 60)
    g.set_voices(0, 8)
    bad = []
    ours = [(s.addr & ~1, (s.addr & ~1) + s.size) for n, s in g.syms.items()
            if s.size and s.is_func and n.startswith(("_ZN4mods8masterfx", "_ZN21AudioEffectShortDelay", "_ZN19AudioEffectPolyverb"))]

    def on_write(uc, access, address, size, value, user):
        after_hooked_store(uc)
        pc = uc.reg_read(UC_ARM_REG_PC)
        if any(start <= address < end for _, start, end in guarded):    # the reverb's starts where the delay's ends
            return
        if any(a <= pc < b for a, b in ours):   # our code only: nothing from a closed library is looked at
            bad.append((hex(address), hex(pc)))
    guarded = buffers(g)
    for name, start, end in guarded:
        g.m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=end, end=end + 63)
        g.m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=start - 64, end=start - 1)
    print(f"     buffers guarded: {[b[0] for b in guarded]}")
    notes = set()
    switches = 0
    for block in range(900):
        roll = random.random()
        if roll < 0.04:
            g.choose(random.random() > 0.1, random.randrange(7), random.randrange(3))
            switches += 1
        elif roll < 0.10:
            g.knobs(random.random(), random.random(), random.random(), random.random(),
                    random.randrange(20, 1000), random.randrange(0, 100), random.randrange(100, 15000),
                    random.choice((0, 0x80)))
        elif roll < 0.20:
            n = random.randrange(36, 84)
            if n in notes:
                g.note(0, n, False)
                notes.discard(n)
            elif len(notes) < 8:
                g.note(0, n, True)
                notes.add(n)
        g.block()
    print(f"     {switches} switches in 900 blocks")
    check(not g.failures, "stress: no asserts")
    check(not bad, f"stress: nothing written past an effect's buffers ({bad[:4]})")
    check(g.pool_peak() <= 80, f"stress: audio pool peak {g.pool_peak()} of 80")
    for n in notes:
        g.note(0, n, False)
    g.blocks(100)
    g.choose(False, 0, 0)
    g.m.call(g.m.addr("_ZN11audioEngine10clearDelayEv"), g.engine, max_insns=50_000_000)
    g.m.call(g.m.addr("_ZN11audioEngine11clearReverbEv"), g.engine, max_insns=50_000_000)
    g.reverb_send(0, 0)
    g.delay_send(0, 0)
    g.blocks(500)   # the synths' own releases
    tail = g.blocks(10)
    check(peak(tail[0]) <= 2 and peak(tail[1]) <= 2, f"stress: back to Stock, cleared, silence (peak {peak(tail[0])})")
    g.knobs()
    g.reverb_send(0, 50)
    g.delay_send(0, 50)
    g.note(0, 60, True)
    check(peak(g.blocks(20)[0]) > 100, "and it still plays")


def test_debounce(mods, ref):
    print("-- debounce --")
    p = MasterPage(mods)
    m = p.m
    marks = []
    p.r.watch(m.addr("_ZN4mods7project11markChangedEv"), lambda mm: marks.append(1))
    m.call(m.addr("_ZN4mods15masterfxStartedEv"), max_insns=50_000_000)
    set_mod(p.r, True, 0, 0)
    p.start()
    p.press("right")

    def active():
        return playing(p.r)[0]
    for step in range(3):
        p.encoder(1)
        p.r.now += 50
        p.update()
    check(settings(p.r)[0] == 3 and active() == 0, "turning the encoder: the list moves, the effect waits")
    check(len(marks) == 3, f"each choice marks the project changed ({len(marks)})")
    p.r.now += 99
    p.update()
    check(active() == 0, "149 ms after the last step: still waiting")
    p.r.now += 1
    p.update()
    check(active() == 3, "150 ms after the last step: it changes")
    p.encoder(1)
    check(active() == 3, "another step waits again")
    m.call(m.addr("_ZN13cMasterParams4stopEv"), p.page, max_insns=20_000_000)
    check(active() == 4, "leaving the page changes it at once")
    p.encoder(100)
    before = len(marks)
    p.encoder(100)
    check(settings(p.r)[0] == 6 and len(marks) == before, "at the end of the list nothing changes, so nothing is marked")


def test_project(mods, ref):
    """make_masterfx_test_project.py's output through the firmware's own readers."""
    print("-- device test project --")
    source = Path(__file__).resolve()
    for parent in source.parents:
        if (parent / "hellish downtown" / "project.mt").exists():
            source = parent / "hellish downtown"
            break
    else:
        print("     no project beside the repo to copy: skipped")
        return
    import tempfile
    import make_masterfx_test_project as maker
    with tempfile.TemporaryDirectory() as temp:
        out = maker.make(source, Path(temp), "MFX test", keep_masterfx=True)
        original = {p.relative_to(source): p.read_bytes() for p in source.rglob("*") if p.is_file()}
        copied = {p.relative_to(out): p.read_bytes() for p in out.rglob("*") if p.is_file()}
        changed = [str(k) for k in original if copied.get(k) != original[k]]
        check(all(k == "project.mt" or k.startswith("patterns") for k in map(lambda s: s.replace("\\", "/"), changed)),
              f"only project.mt and patterns differ from the source ({len(changed)} files)")
        files = {str(k).replace("\\", "/"): v for k, v in copied.items()}

    r = SoundRig(mods)
    m = r.m
    fm = m.addr("newFileManager")
    m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), lambda mm: None)
    # the card as it'd be with the project at /Projects/MFX test
    folder = "Projects/MFX test"
    r.sd.files.update({f"{folder}/{k}": v for k, v in files.items()})
    r.sd.dirs.add(folder)
    m.call(m.addr("_ZN4mods4initEP10FileSystem"), r.sd.obj)

    # the pattern, through readPatternFile
    m.call(m.addr("_ZN12cFileManager4initEP10FileSystemP5Synthb"), fm, r.sd.obj, r.synth, 0, max_insns=30_000_000)
    dest = m.alloc(0x8000, 8)
    path = m.put_str(f"{folder}/patterns/pattern_01.mtp")
    ok = m.call(m.addr("_ZN12cFileManager15readPatternFileEPKcPN9Sequencer10strPatternE"), fm, path, dest, max_insns=30_000_000)
    check(ok & 0xFF == 1, "CE's readPatternFile takes the pattern")
    read = m.read(dest, 0x4030)
    expected = maker.pattern()
    check(read == expected, "and reads it as written")

    # the project's record, through mods' own load path
    r.stub(m.addr("_ZN11TrackerImpl11getPlatformEv"), lambda mm: r.platform)
    m.call(m.addr("_ZN4mods15masterfxStartedEv"), max_insns=50_000_000)
    path_at = find_project_path(r)
    set_names(r, path_at, "Projects", "MFX test")
    m.call(m.addr("_ZN4mods17onProjectOpenCopyEv"), max_insns=30_000_000)
    m.call(m.addr("_ZN4mods17onWorkspaceLoadedEv"), max_insns=50_000_000)
    main_loop(r)                            # the plugin firmware lists the card when the project turns it On
    on = m.read(m.addr(SWITCHES) + MASTERFX, 1)[0]
    check(on == 1 and settings(r) == (4, 2), f"the project's 1.2.0 masterfx.bin imports: On, Shimmer and BBD ({on}, {settings(r)})")
    active = playing(r)
    check(active == (4, 2), f"and they play {active}")

    # 8 voices, the patches' models, through Synth's own calls
    g = Graph(mods, 60, 60)
    g.choose(True, 4, 2)
    voices = maker.VOICES
    models = [struct.unpack_from("<i", files["project.mt"], maker.ce.SLOT_RECORDS + s * maker.ce.SLOT_RECORD_SIZE + maker.ce.SLOT_MODEL)[0]
              for s in range(3)]
    print(f"     project's models: {models}")
    for part, (count, model) in enumerate(zip(voices, models)):
        g.set_voices(part, 0)
    for part, (count, model) in enumerate(zip(voices, models)):
        g.set_voices(part, count)
        g.set_instrument(part, ENGINE_IDS[model])
        g.reverb_send(part, maker.SEND)
        g.delay_send(part, maker.SEND)
    for track, part, note in maker.held():
        g.note(part, note, True)
    a = g.blocks(40)
    check(peak(a[0]) > 100 and not g.failures, f"its 8 voices sound, no asserts (peak {peak(a[0])})")
    check(g.pool_peak() <= 80, f"audio pool peak {g.pool_peak()} of 80")


def source_project():
    for parent in Path(__file__).resolve().parents:
        if (parent / "hellish downtown" / "project.mt").exists():
            return parent / "hellish downtown"
    return None


def rms(x):
    return (sum(v * v for v in x) / max(len(x), 1)) ** 0.5


def played(axf, folder, choice, blocks, dynamics=None):
    """Open, choose (and the dynamics slot's file), press Play: what comes out, and the rig."""
    r = PlayRig(axf)
    loaded = r.open_project(folder)
    if choice is not None:
        set_mod(r, *choice)
    if dynamics is not None:
        set_dynamics(r, dynamics)
    r.count_notes()
    low = bytes(r.m.read(0, 0x20000))       # the ITCM: a write through NULL would land here
    r.press_play()
    out = r.interrupts_run(blocks)
    r.itcm_intact = bytes(r.m.read(0, 0x20000)) == low
    r.loaded = loaded
    return out, r


def test_play(mods, ref):
    """The path that locked a device up: open the project, press Play, run the real interrupt."""
    print("-- Play, through the sequencer and TrackerImpl's interrupt --")
    source = source_project()
    if source is None:
        print("     no project beside the repo to copy: skipped")
        return
    import math
    import tempfile
    import make_masterfx_test_project as maker
    with tempfile.TemporaryDirectory() as temp:
        for voices, name in (((1, 4, 0), "five"), (maker.VOICES, "eight")):
            count = sum(voices)
            folder = maker.make(source, Path(temp), name, voices)
            blocks = 300                    # 0.9 s, long enough for the delay to come round

            base, rb = played(ref, folder, None, blocks)
            check(not rb.failures and rb.notes == count,
                  f"{count} voices, reference: {rb.notes} notes through the sequencer, no asserts {rb.failures}")
            print(f"     reference peak {max(peak(base[0]), peak(base[1]))}, rms {rms(base[0]):.0f} {rms(base[1]):.0f}, "
                  f"audio pool {rb.pool_peak()} of 80, patches loaded {rb.loaded}")

            off, r = played(mods, folder, None, blocks)
            check(off == base, f"{count} voices, a new project with no masterfx.bin plays exactly as the reference")
            active = (r.m.read(r.m.addr("_ZN4mods8switchesE") + MASTERFX, 1)[0],)
            check(active == (0,), f"{count} voices: Master FX starts Off ({active})")

            on_stock, r = played(mods, folder, (True, 0, 0), blocks)
            check(on_stock == base, f"{count} voices, On with Stock plays exactly as the reference")

            stock_level = [rms(base[0]), rms(base[1])]
            # The heaviest pair at full load, and each effect at the project's own 5 voices
            pairs = [(4, 2)] if count == 8 else [(reverb, 0) for reverb in range(1, 7)] + [(0, delay) for delay in (1, 2)]
            for reverb, delay in pairs:
                out, r = played(mods, folder, (True, reverb, delay), blocks)
                name = f"{REVERBS[reverb]} + {DELAYS[delay]}"
                check(not r.failures and r.notes == count and len(out[0]) == blocks * 128,
                      f"{count} voices, {name}: Play runs {blocks} blocks, no hang, no asserts {r.failures}")
                check(r.itcm_intact, f"{count} voices, {name}: nothing written over the code")
                # Without this, the level check below would also pass with an effect that hears nothing
                check(out != base, f"{count} voices, {name}: it hears the sends, the output differs from Stock")
                db = [20 * math.log10(max(rms(x), 1) / max(y, 1)) for x, y in zip(out, stock_level)]
                check(max(db) < 1.5, f"{count} voices, {name}: output against Stock {db[0]:+.1f} dB, {db[1]:+.1f} dB "
                                     f"(peak {max(peak(out[0]), peak(out[1]))}, audio pool {r.pool_peak()})")
            if count != 8 or START not in r.syms:
                continue
            # The device check's load (M2): the heaviest pair and Glue in the dynamics slot, Glue's own
            # knobs at their defaults
            pair, _ = played(mods, folder, (True, 4, 2), blocks)
            out, r = played(mods, folder, (True, 4, 2), blocks, "Glue.ptmfx")
            glue = r.m.read(runtime_slot(r, DYNAMICS), 1)[0] == 1 and slot_file(r, DYNAMICS) == "Glue.ptmfx"
            check(glue, "8 voices, Shimmer + BBD + Glue: Glue is installed and active")
            check(not r.failures and r.notes == count and len(out[0]) == blocks * 128,
                  f"8 voices, Shimmer + BBD + Glue: Play runs {blocks} blocks, no hang, no asserts {r.failures}")
            check(r.itcm_intact, "8 voices, Shimmer + BBD + Glue: nothing written over the code")
            check(out != pair, "8 voices, Shimmer + BBD + Glue: Glue runs on the master (the output differs)")
            db = [20 * math.log10(max(rms(x), 1) / max(y, 1)) for x, y in zip(out, stock_level)]
            # Glue's defaults (-12 dB, 4:1, no makeup) can only turn the mix down, and by no more than
            # 4:1 above -12 dB takes off a full-scale peak (9 dB). 2.0's Glue, on CE's knobs, was held
            # within 3 dB of Stock here; that is the Compressor now.
            check(all(-9.0 < d < 0.1 for d in db),
                  f"8 voices, Shimmer + BBD + Glue: Glue turns the mix down, by less than 9 dB: output against Stock {db[0]:+.1f} dB, {db[1]:+.1f} dB "
                  f"(peak {max(peak(out[0]), peak(out[1]))}, audio pool {r.pool_peak()})")


def main():
    mods, ref = Path(sys.argv[1]), Path(sys.argv[2])
    only = sys.argv[3] if len(sys.argv) > 3 else None
    for name, fn in (("ui", test_ui), ("graph", test_graph), ("dynamics", test_dynamics), ("ducks", test_ducks),
                     ("bus", test_bus), ("stress", test_stress), ("debounce", test_debounce),
                     ("project", test_project), ("play", test_play)):
        if only in (None, name):
            fn(mods, ref)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
