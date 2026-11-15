#!/usr/bin/env python3
"""masterfx on the sound rig: the Master page, the real switch, the real graph with the synths in.

Usage: python tools/emu/test_masterfx_rig.py <mods.axf> <reference.axf> [only: ui|graph|stress|debounce|project|play]

test_masterfx.py checks each effect against 1.2.0 on its own. This one checks what's around them:
- ui: CE's real cMasterParams with display commands caught at API_*. With the mod Off the page
  makes the same display calls as the reference build. On: the lists, stops, names, and the
  page's display bytes counted against the limit. On firmware with the Limiter list (masterfx-2.0's
  step 5): that list, the Limiter page's title and Glue's own knobs, Missing and Bypassed.
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
from sound_rig import PlayRig, SoundRig, find_project_path, peak, set_names  # noqa: E402
from unicorn import UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_S0  # noqa: E402

SWITCHES = "_ZN4mods8switchesE"
SETTINGS = "_ZN4mods8masterfx8settingsE"
MASTERFX = 3                        # mods::Mod::masterfx
ENGINE_IDS = (1, 2, 4, 5, 6, 7, 8)  # Synth::kSynthTypes, as project.mt's models number them
LIMIT = 8172                        # cDisplay::step3
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


SLOT_SIZE = 56                      # masterfx::SlotSetting: the file (48), two own knobs
FILE_IO = "_ZN4mods12_GLOBAL__N_16fileIoE"
SLOTS = "_ZN4mods8masterfx7plugins12_GLOBAL__N_15slotsE"    # the runtime's SlotState[3]


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
        check(slot_file(p.r, DYNAMICS) == "Glue.ptmfx", "and stops at the end of the list")
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


def limiter_page(mods, ref):
    """The Limiter page with Glue: its title, CE's knobs' labels (Glue has none of its own), its own
    knobs in columns 5 and 6, then Stock, a missing file and Bypassed"""
    p = MasterPage(mods)
    m = p.m
    set_mod(p.r, True, 0, 0)
    set_dynamics(p.r, "Glue.ptmfx")
    p.start()
    p.take_log()
    stored = []
    p.r.watch(m.addr("_ZN4mods7project11markChangedEv"), lambda mm: stored.append(1))
    p.press("limiter")
    log = p.take_log()
    titles = [e[1] for e in log if e[0] == "title"]
    final, second = last(log, "text"), last(log, "text2")
    label0 = next(h for h, t in final.items() if t == "Back") - 7
    check(titles[-1] == "Limiter: Glue", f"Glue: the Limiter page's title ({titles[-1]})")
    said = [final[label0 + i] for i in range(3)]
    check(said == ["LimiterAtt", "LimiterRls", "LimiterThr"], f"Glue: CE's labels for Attack, Release, Threshold ({said})")
    own = [(final.get(label0 + 5), second.get(label0 + 5)), (final.get(label0 + 6), second.get(label0 + 6))]
    check(own == [("Ratio", "4.0:1"), ("Makeup", "+0.0 dB")], f"Glue: its own knobs in columns 5 and 6 at their defaults ({own})")
    check(all(math.isnan(k) for k in own_knobs(p.r)), "and settings leave them unset")
    cover = [e for e in log if e[0] == "position" and e[2] == (800 // 8) * 7 + 2]
    check(bool(cover), "the cover moves to column 7, off the own knobs")

    frame = p.handle_of("_ZTV6cFrame")
    stops = []
    for _ in range(6):
        p.press("right")
        stops.append(p.place(frame))
    check(stops == [1, 2, 4, 5, 6, 6], f"Right stops on 1, 2, 4, 5, 6 ({stops})")
    p.take_log()
    p.encoder(1)
    log = p.take_log()
    check(any(e[0] == "text2" and e[2] == "+0.5 dB" for e in log), "encoder on Makeup: +0.5 dB on the page")
    k = own_knobs(p.r)
    check(k[1] == 0.5 and math.isnan(k[0]), f"and in settings, Ratio still unset {k}")
    check(bool(stored), "and the project marked changed")
    p.press("left")
    p.encoder(2)
    check(own_knobs(p.r) == (5.0, 0.5), f"encoder on Ratio, two steps: 5.0 {own_knobs(p.r)}")
    value = struct.unpack("<f", m.read(runtime_slot(p.r, DYNAMICS) + 12, 4))[0]
    check(value == 5.0, f"the plugin is given 5.0 ({value})")
    p.press("up")
    p.take_log()
    p.press("delete")
    log = p.take_log()
    check(own_knobs(p.r) == (4.0, 0.5) and any(e[0] == "text2" and e[2] == "4.0:1" for e in log),
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
    check(titles == ["Limiter: Glue (bypassed)"], f"bypassed: the title says so ({titles})")
    p.update()
    check(not [e for e in p.take_log() if e[0] == "title"], "and only once")
    p.press("cancel")
    log = p.take_log()
    check(any(e[0] == "text2" and e[2] == "Bypassed" for e in log), "the Master page's Limiter column says Bypassed")
    check(not p.r.failures, "no asserts")

    # Stock: CE's page, no own knobs
    q = MasterPage(mods)
    set_mod(q.r, True, 0, 0)
    set_dynamics(q.r, "")
    q.start()
    q.take_log()
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
    own = b"".join(UNSET if k is None else struct.pack("<f", k) for k in (knobs or (None, None)))
    m.write(m.addr(SETTINGS) + SLOT_SIZE * DYNAMICS, file.encode() + b"\0" * (48 - len(file)) + own)
    m.call(m.addr("_ZN4mods8masterfx5applyEv"), max_insns=400_000_000)


def own_knobs(r):
    """The dynamics slot's own knobs as the record holds them"""
    return struct.unpack("<2f", r.m.read(r.m.addr(SETTINGS) + SLOT_SIZE * DYNAMICS + 48, 8))


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

    # Glue, the threshold at the top: nothing to do. CE's limiter delays the master by 5 samples
    # (LIMITER_DELAY) and the plugin doesn't, so the plugin's master is CE's 5 samples sooner.
    g = graph(mods, "Glue.ptmfx")
    m = g.m
    check(m.r8(m.addr(ACTIVE_DYNAMICS)) == 1, "Glue chosen: the dynamics hook runs the plugin")
    check(all(math.isnan(k) for k in own_knobs(g)), f"its own knobs left unset in settings: Glue's defaults play {own_knobs(g)}")
    calls = []
    g.watch(m.addr(PROCESS), lambda mm: calls.append(mm.arg(0)) if mm.arg(0) == DYNAMICS else None)
    a, b = phrase(g)
    blocks = len(a[0]) // 128 + len(b[0]) // 128
    check(calls.count(DYNAMICS) == blocks, f"once a block, from the left object only ({calls.count(DYNAMICS)} calls in {blocks} blocks)")
    for side in (0, 1):
        mine = a[side] + b[side]
        ce = base[0][side] + base[1][side]
        check(mine[:-5] == ce[5:], f"threshold at the top, side {side}: CE's master 5 samples sooner, sample for sample")
    check(not g.failures, "Glue: no asserts")

    # Glue, the threshold down: both sides turned down, the meter shows it
    g = graph(mods, "Glue.ptmfx", threshold=loud)
    a, b = phrase(g, tail=40)
    gains = meter(g)
    for side in (0, 1):
        check(peak(a[side][128 * 20:]) < 0.8 * peak(base[0][side][128 * 20:]),
              f"threshold down, side {side}: the held note turned down (peak {peak(a[side][128 * 20:])} against {peak(base[0][side][128 * 20:])})")
    check(gains[0] == gains[1] and 0.0 < gains[0] < 0.95, f"the meter: the plugin's gain on both objects {gains}")

    # A key that is silent: nothing to duck, though the master is over the threshold
    g = graph(mods, "Glue.ptmfx", threshold=loud, sidechain=1)   # sample track 1, which plays nothing
    a, b = phrase(g)
    for side in (0, 1):
        mine = a[side] + b[side]
        ce = base[0][side] + base[1][side]
        check(mine[:-5] == ce[5:], f"a silent key, side {side}: the master untouched, CE's 5 samples sooner")

    # CE's resetLimiter (Sample Editor, Sample Recorder): the slot's memory cleared with the master
    # passed through, then the plugin starts over
    g = graph(mods, "Glue.ptmfx", threshold=loud)
    m = g.m
    seen = []
    g.watch(m.addr("_ZN4mods8masterfx19clearDynamicsMemoryEv"), lambda mm: seen.append(("clear", m.r8(m.addr(ACTIVE_DYNAMICS)))))
    g.watch(m.addr("_ZN4mods8masterfx7plugins7clearedENS1_4SlotE"),
            lambda mm: seen.append(("cleared", mm.arg(0), m.r8(m.addr(ACTIVE_DYNAMICS)))))
    m.call(m.addr("_ZN11audioEngine12resetLimiterEv"), g.engine, max_insns=50_000_000)
    check(seen == [("clear", 2), ("cleared", DYNAMICS, 2)] and m.r8(m.addr(ACTIVE_DYNAMICS)) == 1,
          f"resetLimiter: memory cleared and the plugin reset with the master passed through, then it runs again {seen}")

    # Back to Stock: CE's limiter plays again, from its power-up state
    g = graph(mods, "Glue.ptmfx")
    phrase(g)
    set_dynamics(g, "")
    check(g.m.r8(g.m.addr(ACTIVE_DYNAMICS)) == 0, "Stock again: CE's limiter")
    again = phrase(g)
    fresh = graph(mods, "")
    phrase(fresh)
    check(again == phrase(fresh), "Stock after Glue: as if Glue had never played")

    # The mod Off: CE's limiter
    g = graph(mods, "Glue.ptmfx")
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


def watch_gain(g):
    """Who works out each block's gain: a ducked track's amplifier (the key made before it, this
    block's gain) or the limiter (the key made after it: the track had the last block's)"""
    m = g.m
    s = g.syms[DUCK_BLOCK]
    calls = []

    def f(mm):
        lr = mm.uc.reg_read(UC_ARM_REG_LR) & ~1
        calls.append("track" if s.addr <= lr < s.addr + s.size else "limiter")
    g.watch(m.addr(DUCK_COMPUTE), f)
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

    # Glue ducking Synth 1 by the Line in
    seen = []
    who = {}

    def each(gg):
        if "calls" not in who:
            who["calls"] = watch_gain(gg)
            return
        seen.append((gg.m.r8(gg.m.addr(DUCK_LIVE)), meter(gg)[0], tuple(who["calls"])))
        who["calls"].clear()
    g, mine = run(mods, "Glue.ptmfx", SYNTH_1_BIT, loud, watch=each)
    check(duck_mask(g) == SYNTH_1_BIT, f"Synth 1 in the record: the mask the interrupt reads ({duck_mask(g):#x})")
    check(all(live == 1 for live, _, _ in seen), "a key and a ducking plugin: ducking every block")
    callers = {c for _, _, c in seen}
    check(callers == {("track",)}, f"a key made before the track (Line in): this block's gain, worked out once, by the track {callers}")
    for side in (0, 1):
        before = 128 * quiet - 5
        check(mine[side][:before] == ce[side][5:before + 5],
              f"side {side}, before the key: the master untouched, CE's 5 samples sooner")
        inside = slice(128 * (quiet + 20), 128 * (quiet + key))
        a, b = peak(mine[side][inside]), peak(ce[side][inside])
        check(a < 0.8 * b, f"side {side}, under the key: Synth 1 ducked (peak {a} against {b})")
    first = first_ducked(mine[0], ce[0])
    print(f"     (the first block ducked: {first}, the key from {quiet})")
    meters = [gain for _, gain, _ in seen]
    check(meters[quiet - 3] == 1.0 and min(meters[quiet:quiet + key]) < 0.95,
          f"the meter: 1 before the key, under 0.95 with it ({meters[quiet - 3]}, {min(meters[quiet:quiet + key])})")
    tail = peak(mine[0][-128 * 10:]), peak(ce[0][-128 * 10:])
    check(tail[0] > 0.9 * tail[1], f"the key gone: Synth 1 back (peak {tail[0]} against {tail[1]})")
    check(not g.failures, "no asserts")

    # A silent destination: the master and Synth 1 untouched though the key is loud
    _, silent = run(mods, "Glue.ptmfx", 1 << 0, loud)
    check(first_ducked(silent[0], ce[0]) is None and first_ducked(silent[1], ce[1]) is None,
          "ducking Track 1, which is silent: the master untouched all through, CE's 5 samples sooner")

    # The key made after the track (synth part 1: PolySynth runs after the panning amplifiers):
    # the gain the block before kept, one block late
    _, ce_synth = run(ref, sidechain=SYNTH_2, synth_key=True)
    _, late = run(mods, "Glue.ptmfx", SYNTH_1_BIT, loud, SYNTH_2, synth_key=True)
    g2 = Graph(mods)
    g2.set_instrument(1, 2)
    set_mod(g2, True, 0, 0)
    set_ducks(g2, SYNTH_1_BIT)
    set_dynamics(g2, "Glue.ptmfx")
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
    set_dynamics(g, "Glue.ptmfx")
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
    set_dynamics(g, "Glue.ptmfx")
    check(duck_mask(g) == SYNTH_1_BIT, "Glue again: Synth 1")
    set_mod(g, False)
    check(duck_mask(g) == 0, "the mod Off: nothing ducked")

    _, plain = run(mods, "Glue.ptmfx", 0, loud)
    check(plain != mine, "ducks against the master: what plays differs")
    g, keyless = run(mods, "Glue.ptmfx", SYNTH_1_BIT, loud, sidechain=0)
    _, plain_keyless = run(mods, "Glue.ptmfx", 0, loud, sidechain=0)
    check(keyless == plain_keyless and g.m.r8(g.m.addr(DUCK_LIVE)) == 0,
          "Sidechain off: Glue on the master, as without ducks")

    g = Graph(mods)
    set_mod(g, True, 0, 0)
    card = g.sd.files
    glue = bytearray(card["/MasterFX/Glue.ptmfx"])
    glue[6] = 0x01                  # PTMFX_FLAG_FLOAT only
    card["/MasterFX/NoDuck.ptmfx"] = bytes(glue)
    g.m.call(g.m.addr("_ZN4mods8masterfx7plugins4scanEv"), max_insns=400_000_000)  # the card listed again
    set_ducks(g, SYNTH_1_BIT)
    set_dynamics(g, "NoDuck.ptmfx")
    m = g.m
    check(m.r8(m.addr(ACTIVE_DYNAMICS)) == 1 and duck_mask(g) == 0,
          "a dynamics plugin without PTMFX_FLAG_DUCK: it plays on the master, nothing ducked")

    ducks_page(mods)


def ducks_page(mods):
    """The Limiter page's Ducks list: Shift + the button under column 4"""
    p = MasterPage(mods)
    m = p.m
    set_mod(p.r, True, 0, 0)
    set_dynamics(p.r, "Glue.ptmfx")
    p.start()
    stored = []
    p.r.watch(m.addr("_ZN4mods7project11markChangedEv"), lambda mm: stored.append(1))
    p.press("limiter")
    final = last(p.take_log(), "text")
    label4 = next(h for h, t in final.items() if t == "Back") - 7 + 4
    shift = [0]
    p.r.stub(m.addr("_ZN7Buttons15isButtonPressedEi"), lambda mm: shift[0])

    def ducks():
        return struct.unpack("<H", m.read(m.addr(SETTINGS) + DUCKS_AT, 2))[0]

    def shifted():
        shift[0] = 1
        p.press("eq")
        shift[0] = 0

    def said(log):
        return last(log, "text").get(label4), last(log, "text2").get(label4)

    shifted()
    now = said(p.take_log())
    check(now == ("Ducks", "Master"), f"Shift + column 4: the Ducks list, on the master {now}")
    bytes_up = sum(p.page_bytes().values())
    check(bytes_up < LIMIT, f"the page with the Ducks list: {bytes_up} bytes of {LIMIT}")
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
    shifted()
    now = said(p.take_log())
    check(now[0] == "Sidechain", f"Shift + column 4 again: CE's Sidechain list {now}")
    check(ducks() == 1 << 9, f"and the ducks kept ({ducks():#x})")
    p.press("cancel")
    p.take_log()
    p.press("limiter")
    check(said(p.take_log())[0] in (None, "Sidechain"), "the page opens on the Sidechain list")
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


def played(axf, folder, choice, blocks):
    """Open, choose, press Play: what comes out, and the rig."""
    r = PlayRig(axf)
    loaded = r.open_project(folder)
    if choice is not None:
        set_mod(r, *choice)
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


def main():
    mods, ref = Path(sys.argv[1]), Path(sys.argv[2])
    only = sys.argv[3] if len(sys.argv) > 3 else None
    for name, fn in (("ui", test_ui), ("graph", test_graph), ("dynamics", test_dynamics), ("ducks", test_ducks),
                     ("stress", test_stress), ("debounce", test_debounce),
                     ("project", test_project), ("play", test_play)):
        if only in (None, name):
            fn(mods, ref)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
