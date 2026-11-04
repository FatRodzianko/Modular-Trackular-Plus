#!/usr/bin/env python3
"""masterfx on the sound rig: the Master page, the real switch, the real graph with the synths in.

Usage: python tools/emu/test_masterfx_rig.py <mods.axf> <reference.axf> [only: ui|graph|stress|debounce]

test_masterfx.py checks each effect against 1.2.0 on its own. This one checks what's around them:
- ui: CE's real cMasterParams with display commands caught at API_*. With the mod Off the page
  makes the same display calls as the reference build. On: the lists, stops, names, and the
  page's display bytes counted against the limit.
- graph: the switch through mods::projectMasterfxSwitchChanged, the boot gate, and synth notes
  through CE's sends to the chosen effect.
- stress: 8 synth voices with the heaviest pair, then switches, knobs and notes while playing.
- debounce: the encoder's wait before an effect changes.
"""
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from sound_rig import BLOCK_MS, SoundRig, peak  # noqa: E402

SWITCHES = "_ZN4mods8switchesE"
SETTINGS = "_ZN4mods8masterfx8settingsE"
MASTERFX = 3                        # mods::Mod::masterfx
LIMIT = 8172                        # cDisplay::step3
CONTROLS = 65                       # controlsCount

BUTTON = {"reverb": 1, "delay": 2, "limiter": 3, "eq": 4, "cancel": 7,
          "up": 26, "delete": 27, "left": 30, "down": 31, "right": 32}
REVERBS = ("Stock", "Freeverb", "Plate", "Spring", "Shimmer", '"Gated"', "Reverse")
DELAYS = ("Stock", "Tape", "BBD")

failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def set_mod(r, on, reverb=None, delay=None):
    """The switch as Project Settings changes it, and the choices as Config's lines do."""
    m = r.m
    if reverb is not None:
        m.write(m.addr(SETTINGS), bytes([reverb]))
    if delay is not None:
        m.write(m.addr(SETTINGS) + 1, bytes([delay]))
    m.write(m.addr(SWITCHES) + MASTERFX, bytes([1 if on else 0]))
    if "_ZN4mods28projectMasterfxSwitchChangedEv" in r.syms:
        m.call(m.addr("_ZN4mods28projectMasterfxSwitchChangedEv"), max_insns=50_000_000)


def settings(r):
    return tuple(r.m.read(r.m.addr(SETTINGS), 2))


class MasterPage:
    """cMasterParams on the rig. Buttons press what the page itself registered."""

    def __init__(self, axf):
        self.r = r = SoundRig(axf)
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
        self.page = m.addr("masterParams")
        m.call(m.addr("_ZN13cMasterParams19initDisplayControlsEv"), self.page, max_insns=20_000_000)
        self._watch_display_calls()
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
    stops = []
    for _ in range(8):
        p.press("right")
        stops.append(p.place(frame))
    check(stops == [1, 2, 5, 6, 7, 7, 7, 7], f"Right stops on 1, 2, 5, 6, 7 ({stops})")
    back = []
    for _ in range(6):
        p.press("left")
        back.append(p.place(frame))
    check(back == [6, 5, 2, 1, 0, 0], f"Left stops on 6, 5, 2, 1, 0 ({back})")

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
        p.m.write(p.m.addr(SETTINGS), bytes([choice]))
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
    p.m.write(p.m.addr(SETTINGS), bytes([0]))
    p.take_log()
    p.press("reverb")
    log = p.take_log()
    stock_title = [e[1] for e in log if e[0] == "title"]
    p.press("cancel")

    words = {1: ("Delay: Tape", "Intensity", "Tone"), 2: ("Delay: BBD", "Feedback", "Tone")}
    for choice, (title, feedback, tone) in words.items():
        p.m.write(p.m.addr(SETTINGS) + 1, bytes([choice]))
        p.take_log()
        p.press("delay")
        log = p.take_log()
        titles = [e[1] for e in log if e[0] == "title"]
        final = last(log, "text")
        label0 = next(h for h, t in final.items() if t == "Back") - 7
        said = [final[label0 + 4], final[label0 + 5]]
        check(titles[-1] == title and said == [feedback, tone], f"{DELAYS[choice]}: title and knob names ({titles[-1]}, {said})")
        check(sum(1 for e in log if e[0] == "data") == 2, f"{DELAYS[choice]}: the Delay page takes its two lists back")
        p.press("cancel")

    # the same pages, stock, have CE's own words in the reference
    q = MasterPage(ref)
    q.start()
    q.take_log()
    q.press("reverb")
    ref_title = [e[1] for e in q.take_log() if e[0] == "title"]
    check(stock_title == ref_title, f"Stock reverb page: CE's title ({stock_title})")

    # the lists are gone from every other page
    p.m.write(p.m.addr(SETTINGS), bytes([4, 2]))
    p.take_log()
    master = set(p.shown())
    ref_page = MasterPage(ref)
    ref_page.start()
    extra = master - set(ref_page.shown())
    check(len(extra) == 2, f"Master page: two more controls shown than CE's ({sorted(extra)})")
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

    # display bytes, the heaviest choices
    p = MasterPage(mods)
    set_mod(p.r, True, 5, 2)
    p.start()
    r_page = MasterPage(ref)
    r_page.start()
    ours, theirs = p.page_bytes(), r_page.page_bytes()
    a_total, b_total = sum(ours.values()), sum(theirs.values())
    print(f"     Master page: {b_total} B as CE, {a_total} B with the lists, against {LIMIT}")
    check(a_total < LIMIT, f"Master page fits ({a_total} of {LIMIT} B, upper bound)")
    added = {i: ours[i] - theirs.get(i, 0) for i in ours if ours[i] != theirs.get(i, 0)}
    print(f"     added per slot: {added}")
    for name in ("reverb", "delay"):
        p.press(name)
        total = sum(p.page_bytes().values())
        check(total < LIMIT, f"{name} page fits ({total} of {LIMIT} B, upper bound)")
        p.press("cancel")


def main():
    mods, ref = Path(sys.argv[1]), Path(sys.argv[2])
    only = sys.argv[3] if len(sys.argv) > 3 else None
    for name, fn in (("ui", test_ui),):
        if only in (None, name):
            fn(mods, ref)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
