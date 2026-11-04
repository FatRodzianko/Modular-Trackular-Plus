# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Color Song checks on full Tracker+ builds, with the synth libraries in. The second build is
the same source with MT_NO_MODS, as plain CE.

The Song screen's own drawing runs here: cSongPlayer::fillBlocks, with the display commands
caught at CE's API_* functions, each one 4 bytes of display list. Pattern files come through
CE's own reader (loadWorkspacePatternToTempBuffer, cFileTransfer) from a fake SD card, and used
tracks through CE's updatePatternBitmask. What doesn't run: the Song screen's update(), so the
hardware checklist still opens the screen once, and the display itself.

1. Config: the project's switch and the default.
2. Off: the same display commands as plain CE, colours known or not, muted or not.
3. On: colours by content, nothing added to the display list, empty blocks and every other
   command as CE's. One pattern file per update, none while the file manager is busy, one per
   100 ms while playing. Pattern files of both formats give the same colours. Micro-move and
   Volume don't count.
4. Edits: a saved pattern and one in RAM; other colours stay; nothing marks the project changed
   and the colours land in Workspace/mods.bin. updatePatternBitmask only sets flags, since it
   can run in the audio interrupt.
5. A real 1.2.0 project, if `hellish downtown` is beside the repo (read only, held in memory):
   every block of its song has the colour 1.2.0 gives it from its colorsong.bin, through CE's
   translation of its pattern files, also with a mods.bin already there. Save, Save As, New
   Project. 1.2.0's colours are worked out here, from 1.2.0's source, not from the mod's.
6. Themes: a block stands out from the empty ones on each of CE's themes. Writes
   build/emu/colorsong_<theme>.png to look at.

Usage: python tools/emu/test_colorsong.py build/emu/TrackerPlus_MT_full.axf build/emu/TrackerPlus_ref_full.axf
"""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

from unicorn import UC_HOOK_CODE

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402
from test_step0 import FakeSd, menu_item_named  # noqa: E402
from test_swing import Projects  # noqa: E402

COLORSONG_ID = 0
RECORD_ID = COLORSONG_ID + 1
SWITCHES = "_ZN4mods8switchesE"
DEFAULTS = "_ZN4mods8defaultsE"
PATTERN_FILE = "Workspace/patterns/pattern_%02d.mtp"
TRACKS, STEPS = 16, 128
HEADER = 28
ROW_PITCH, TRACK_PITCH, GRID_TOP, GRID_LEFT = 27, 30, 64, 5
ROWS_VISIBLE = 12
DISPLAY_LIMIT = 8172
FX_MICROMOVE, FX_VOLUME, FX_OTHER = 2, 18, 5
THEMES = ["themeDefault", "themeWhiteout", "themeAnniversary"]

SHADE_SAT = (78, 92, 70, 42, 88, 62)            # patch/colorsong/colorsong.c on 1.2.0
SHADE_LIGHT = (50, 70, 34, 58, 80, 26)
HUE_SLOTS = 20

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


# -- 1.2.0's colours, from its source ---------------------------------------------------------
def fnv(h: int, b: int) -> int:
    return ((h ^ (b & 0xFF)) * 16777619) & 0xFFFFFFFF


def track_key(track: bytes, last: int, stride: int) -> tuple[int, int]:
    """(family key, content) of a track, for either step size."""
    instruments = [0] * 8
    used = False
    shade = fnv(2166136261, last)
    for s in range(last + 1):
        note, ins, t1, v1, t2, v2 = track[1 + s * stride:1 + s * stride + 6]
        t1, t2 = (0 if t in (FX_MICROMOVE, FX_VOLUME) else t for t in (t1, t2))
        v1, v2 = (0 if not t1 else v1), (0 if not t2 else v2)
        has_note = note != 0xFF
        if not has_note and not t1 and not t2 and ins <= 0x2F:
            continue
        if has_note or ins > 0x2F:
            instruments[ins >> 5] |= 1 << (ins & 31)
            used = True
        for b in (s, note, ins if has_note else 0, t1, v1, t2, v2):
            shade = fnv(shade, b)
    family = 2166136261
    for word in instruments:
        for bit in (0, 8, 16, 24):
            family = fnv(family, word >> bit)
    return (family if used else 0), (shade ^ shade >> 9 ^ shade >> 18 ^ shade >> 27) & 0x1FF


def hsl_rgb(h: int, s: int, l: int) -> int:
    c = (100 - (2 * l - 100 if 2 * l > 100 else 100 - 2 * l)) * s
    hp = h % 120
    d = hp - 60 if hp >= 60 else 60 - hp
    x = c * (60 - d) // 60
    m = l * 100 - c // 2
    r, g, b = [(c, x, 0), (x, c, 0), (0, c, x), (0, x, c), (x, 0, c), (c, 0, x)][min(h // 60, 5)]
    r, g, b = [((v + m) * 255 + 5000) // 10000 for v in (r, g, b)]
    return r << 16 | g << 8 | b


def color_120(slot: int | None, shade: int, muted: bool) -> int:
    hue = 0 if slot is None else (slot % HUE_SLOTS) * 1375 // 10 % 360
    s = 6 if slot is None else SHADE_SAT[shade % 6]
    l = SHADE_LIGHT[shade % 6]
    if muted:
        s //= 2
        l = max(l * 45 // 100, 14)
    return hsl_rgb(hue, s, l)


def lightness(rgb: int) -> int:
    parts = (rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255)
    return (max(parts) + min(parts)) * 100 // 510


# -- pattern files ----------------------------------------------------------------------------
def pattern_file(tracks: dict[int, list], length: int = 16, version: int = 6) -> bytes:
    """A pattern file. tracks: {track: [(step, note, instrument, fx)]}, fx a list of up to two
    (type, value); with version 6 also the volume and micro-move columns, as extra items."""
    stride = 8 if version >= 6 else 6
    size = HEADER + TRACKS * (1 + STEPS * stride) + 4
    out = bytearray(size)
    out[0:2] = b"PT"
    out[8:12] = bytes([version] * 4)
    out[12:14] = struct.pack("<H", size)
    for t in range(TRACKS):
        at = HEADER + t * (1 + STEPS * stride)
        out[at] = length - 1
        for s in range(STEPS):
            out[at + 1 + s * stride] = 0xFF
            if stride == 8:
                out[at + 1 + s * stride + 6] = 0xFF
                out[at + 1 + s * stride + 7] = 0xFF
    for t, steps in tracks.items():
        at = HEADER + t * (1 + STEPS * stride)
        for step in steps:
            s, note, instrument, fx = step[:4]
            a = at + 1 + s * stride
            out[a] = note & 0xFF
            out[a + 1] = instrument
            for i, (kind, value) in enumerate(fx):
                out[a + 2 + 2 * i] = kind
                out[a + 3 + 2 * i] = value
            if stride == 8 and len(step) > 4:
                out[a + 6], out[a + 7] = step[4] & 0xFF, step[5] & 0xFF
    return bytes(out)


def notes(instrument: int, *steps: int) -> list:
    return [(s, 60, instrument, []) for s in steps]


# -- the Song screen --------------------------------------------------------------------------
class Grid:
    """cSongPlayer on a machine, drawing into a list of commands."""

    def __init__(self, m: Machine):
        self.m = m
        self.commands: list[tuple] = []
        for name, sym in m.elf.symbols.items():
            base = name.split("API_")
            if name.startswith("_Z") and len(base) == 2 and sym.is_func and "API_LIB" not in name:
                m.stub_function(sym.addr, lambda mm, n=name: self._command(n, mm))
        self.player = m.alloc(2048, 8)
        m.call(m.addr("_ZN11cSongPlayerC1EP20strControlProperties"), self.player, 0)
        self.data = m.alloc(64, 4)
        self.list = m.alloc(32, 4)
        self.normal_mute = m.alloc(TRACKS)
        self.midi_mute = m.alloc(TRACKS)
        self.theme_offset = self._find_theme_colors()

    def _command(self, name: str, m: Machine) -> None:
        short = name.split("API_")[1]
        if short.startswith("VERTEX2F"):
            args = tuple(a - 65536 if a & 0x8000 else a for a in (m.arg(i) & 0xFFFF for i in range(2)))
            self.commands.append(("VERTEX2F", *args))
        elif short.startswith("COLOR") and short.endswith("m"):
            self.commands.append(("COLOR", m.arg(0)))
        elif name.endswith("v"):
            self.commands.append((short,))
        else:
            self.commands.append((short, m.arg(0)))

    def _find_theme_colors(self) -> int:
        """Where songPlayerColors is in a Theme, from themeDefault's known values."""
        theme = self.m.sym("themeDefault")
        words = struct.unpack(f"<{theme.size // 4}I", self.m.read(theme.addr, theme.size // 4 * 4))
        for i in range(len(words) - 10):
            if words[i + 2] == 0x575757 and words[i + 3] == 0x0A0A0A and words[i + 8] == 0x121212 \
                    and words[i + 9] != 0:
                return i * 4
        raise RuntimeError("no songPlayerColors in themeDefault")

    def theme_colors(self, theme: str) -> list[int]:
        a = self.m.addr(theme) + self.theme_offset
        return list(struct.unpack("<10I", self.m.read(a, 40)))

    def set_theme(self, theme: str) -> None:
        self.m.call(self.m.addr("_ZN11cSongPlayer9setColorsEPKm"), self.player,
                    self.m.addr(theme) + self.theme_offset)

    def set_mutes(self, muted: set[int]) -> None:
        self.m.write(self.normal_mute, bytes(1 if t in muted else 0 for t in range(8)))
        self.m.write(self.midi_mute, bytes(1 if t + 8 in muted else 0 for t in range(8)))

    def draw(self, length: int, playlist: int, normal: int, midi: int, top: int = 0) -> list[tuple]:
        m, d = self.m, self.data
        m.write(self.list, struct.pack("<hHHxxII", top, max(length, 1), ROWS_VISIBLE, 0, 0))
        m.write(d, b"\0" * 64)
        m.write(d, bytes([length]))
        m.w32(d + 4, playlist)
        m.w32(d + 24, self.list)
        m.write(d + 28, bytes([8, 8]))
        m.w32(d + 32, normal)
        m.w32(d + 36, midi)
        m.w32(d + 40, self.normal_mute)
        m.w32(d + 44, self.midi_mute)
        m.call(m.addr("_ZN11cSongPlayer7setDataEPv"), self.player, d)
        self.commands = []
        m.call(m.addr("_ZN11cSongPlayer10fillBlocksEv"), self.player, max_insns=50_000_000)
        return self.commands


def blocks(commands: list[tuple]) -> dict[tuple[int, int], tuple[int, bool, tuple]]:
    """{(row, track): (colour, filled, rectangle)}."""
    out = {}
    color = None
    corner = None
    for c in commands:
        if c[0] == "COLOR":
            color = c[1]
        elif c[0] == "VERTEX2F":
            if corner is None:
                corner = c[1:]
            else:
                row = (corner[1] - GRID_TOP) // ROW_PITCH
                track = (corner[0] - GRID_LEFT) // TRACK_PITCH
                filled = (corner[1] - GRID_TOP) % ROW_PITCH == 1
                out[(row, track)] = (color, filled, (*corner, *c[1:]))
                corner = None
    return out


def write_png(path: Path, width: int, height: int, background: int, rects: list[tuple]) -> None:
    rows = [bytearray(background.to_bytes(3, "big") * width) for _ in range(height)]
    for color, (x0, y0, x1, y1) in rects:
        pixel = color.to_bytes(3, "big")
        for y in range(y0, min(y1 + 1, height)):
            rows[y][x0 * 3:(x1 + 1) * 3] = pixel * (x1 + 1 - x0)
    raw = b"".join(b"\0" + bytes(r) for r in rows)

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


# -- the machine with the mod -----------------------------------------------------------------
class ModRig:
    """Enough of CE for the mod: the sequencer's state, the file manager's reader, mtProject."""

    FM = "newFileManager"
    PREFIX = "_ZN4mods9colorsong12_GLOBAL__N_16CeSong"

    def __init__(self, axf: Path):
        self.m = m = Machine(axf)
        self.now = 1000
        self._fake_timer()
        self.project_marks = 0
        self.unsaved: set[int] = set()
        m.stub_function(m.addr("_ZN12cFileManager23setProjectStructChangedEv"), self._mark)
        m.stub_function(m.addr("_ZN12cFileManager22isPatternStructChangedEh"),
                        lambda mm: 1 if mm.arg(1) in self.unsaved else 0)

        m.call(m.addr("_GLOBAL__sub_I_sequencer"))
        m.call(m.addr("_ZN9Sequencer4initEv"), m.addr("sequencer"))
        m.call(m.addr("_GLOBAL__sub_I__ZN4mods9colorsong5resetEv"))
        m.call(m.addr("_GLOBAL__sub_I_fileTransfer"))

        self.projects = Projects(self)      # the mods' globals, FakeSd, the project path field
        self.sd = self.projects.sd
        m.call(m.addr("_ZN13cFileTransfer4initEP10FileSystem"), m.addr("fileTransfer"), self.sd.obj)

        self.fm = m.addr(self.FM)
        self.song = m.call(m.addr("_ZN4mods9colorsong6ceSongEv"))
        self.switches = m.addr(SWITCHES)
        self._find_fields()
        self.grid = Grid(m)

    def _fake_timer(self) -> None:
        """elapsedMillis asks a Timer. Set before anything that holds one is constructed."""
        m = self.m
        vtable = m.alloc(4 * 16, 4)
        stub = m.add_stub(lambda mm: self.now)
        for i in range(16):
            m.w32(vtable + 4 * i, stub)
        obj = m.alloc(16, 4)
        m.w32(obj, vtable)
        m.w32(m.addr("_ZN13elapsedMillis5timerE"), obj)

    def _mark(self, m: Machine) -> None:
        self.project_marks += 1

    def _glue(self, name: str, *args: int) -> int:
        return self.m.call(self.m.addr(self.PREFIX + name), self.song, *args)

    def _bisect(self, base: int, size: int, stride: int, fill: bytes, test) -> int:
        """The offset in [base, base + size) that makes test() true when it holds `fill`."""
        m = self.m
        saved = m.read(base, size)
        lo, hi = 0, size // stride
        while hi - lo > 1:
            mid = (lo + hi) // 2
            m.write(base, b"\0" * size)
            m.write(base + lo * stride, fill * (mid - lo))
            if test():
                hi = mid
            else:
                lo = mid
        m.write(base, saved)
        return base + lo * stride

    def _find_fields(self) -> None:
        """mtProject and cFileManager fields, wherever CE's accessors read them from."""
        m = self.m
        project = m.sym("mtProject")
        saved = m.read(project.addr, project.size)

        def spread(which: int) -> None:
            m.write(project.addr, bytes((i >> which) & 0xFF for i in range(project.size)))

        spread(0)
        slot_low, mask_low = self._glue("4slotEj", 7), self._glue("4maskEh", 5)
        spread(8)
        slot_high, mask_high = self._glue("4slotEj", 7), self._glue("4maskEh", 5)
        m.write(project.addr, saved)
        self.playlist = project.addr + (slot_high << 8 | slot_low) - 7
        self.normal = project.addr + ((mask_high & 0xFF) << 8 | (mask_low & 0xFF)) - 4
        self.midi = project.addr + ((mask_high >> 8) << 8 | (mask_low >> 8)) - 4

        self.unsaved = {0x7B}
        self.actual = self._bisect(project.addr, project.size, 1, b"\x7B",
                                   lambda: self._glue("13unsavedTracksEh", 0x7B) != 0)
        self.unsaved = set()

        fm = m.sym(self.FM)
        self.status = self._bisect(fm.addr, fm.size, 1, b"\x01", lambda: self._glue("11mayReadFileEv") == 0)

        self.sd.files[PATTERN_FILE % 1] = pattern_file({})
        loader = m.addr("_ZN12cFileManager32loadWorkspacePatternToTempBufferEh")

        def loads() -> bool:
            try:
                return m.call(loader, self.fm, 1, max_insns=3_000_000) != 0
            except RuntimeError:
                return False
        self.file_system = self._bisect(fm.addr, fm.size, 4, struct.pack("<I", self.sd.obj), loads)
        m.w32(self.file_system, self.sd.obj)
        del self.sd.files[PATTERN_FILE % 1]
        self.sd.handles.clear()
        self.sd.calls.clear()
        self.sd.unexpected.clear()

    # -- what the tests do ------------------------------------------------------------------
    def set_switch(self, on: bool) -> None:
        self.m.write(self.switches + COLORSONG_ID, bytes([1 if on else 0]))

    def set_playlist(self, slots: list[int]) -> None:
        self.m.write(self.playlist, bytes(slots) + bytes(255 - len(slots)))
        self.length = len(slots)

    def bitmask(self, pattern: int, source: int) -> None:
        self.m.call(self.m.addr("_ZN12cFileManager20updatePatternBitmaskEhPN9Sequencer10strPatternE"),
                    self.fm, pattern, source)

    def load(self, pattern: int) -> int:
        return self.m.call(self.m.addr("_ZN12cFileManager32loadWorkspacePatternToTempBufferEh"), self.fm, pattern)

    def put_pattern(self, pattern: int, data: bytes) -> None:
        """A pattern saved, as the file manager does it: the file, then the used tracks."""
        self.sd.files[PATTERN_FILE % pattern] = data
        source = self.load(pattern)
        assert source != 0, f"pattern {pattern} didn't load"
        self.bitmask(pattern, source)

    def update(self) -> int:
        """One Song screen update. The pattern files read."""
        before = sum("readFile" in c for c in self.sd.calls)
        self.m.call(self.m.addr("_ZN4mods15colorsongUpdateEv"), max_insns=30_000_000)
        return sum("readFile" in c for c in self.sd.calls) - before

    def settle(self, limit: int = 600) -> int:
        """Updates until a whole pass reads nothing. The files read."""
        total = quiet = 0
        for _ in range(limit):
            n = self.update()
            total += n
            quiet = 0 if n else quiet + 1
            if quiet > 3:
                return total
        raise RuntimeError("the scan didn't finish")

    def draw(self, top: int = 0, theme: str = "themeDefault") -> list[tuple]:
        self.grid.set_theme(theme)
        return self.grid.draw(self.length, self.playlist, self.normal, self.midi, top)

    def color(self, pattern: int, track: int, muted: bool = False, theme: str = "themeDefault") -> int:
        colors = self.grid.theme_colors(theme)
        mask = self.m.read(self.normal + pattern - 1, 1)[0] | self.m.read(self.midi + pattern - 1, 1)[0] << 8
        return self.m.call(self.m.addr("_ZN4mods9colorsong10blockColorEhhtbmm"), pattern, track, mask,
                           1 if muted else 0, colors[7] if muted else colors[1], colors[8])

    def record(self, name: str = "Workspace/mods.bin") -> bytes | None:
        """The Color Song record's data in a project's mods.bin."""
        data = self.sd.files.get(name)
        if data is None:
            return None
        at = 12
        while at + 4 <= len(data):
            rid, _version, length = struct.unpack_from("<BBH", data, at)
            if rid == RECORD_ID:
                return data[at + 4:at + 4 + length]
            at += 4 + length
        return None


# -- 1. Config --------------------------------------------------------------------------------
def test_config(axf: Path) -> None:
    m = Machine(axf)
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))
    item = menu_item_named(m, "menuMods", "Color Song")
    check(m.r32(m.r32(item + 16)) == m.addr(DEFAULTS) + COLORSONG_ID,
          "Config -> Mods -> Color Song sets the default for new projects")
    item = menu_item_named(m, "menuProjectMods", "Color Song")
    check(m.r32(m.r32(item + 16)) == m.addr(SWITCHES) + COLORSONG_ID,
          "Project Settings -> Mods -> Color Song is the project's switch")


# -- 2 and 3 ----------------------------------------------------------------------------------
def sample_song(r: ModRig) -> None:
    r.put_pattern(1, pattern_file({0: notes(1, 0, 4), 1: notes(2, 0), 9: notes(50, 2)}))
    r.put_pattern(2, pattern_file({0: notes(1, 0, 4), 3: notes(1, 8)}))
    r.put_pattern(3, pattern_file({2: notes(3, 0), 4: [(1, -1, 0, [(FX_OTHER, 30)])]}))
    r.set_playlist([1, 2, 1, 3])


def test_boot_tables(mt: Path) -> None:
    """The tables are in SDRAM, which startup doesn't zero; init must."""
    m = Machine(mt)
    for name in ("colorsong12_GLOBAL__N_18patternsE", "colorsong12_GLOBAL__N_16colorsE"):
        s = m.sym(name)
        check(0x80000000 <= s.addr < 0x84000000, f"{name[-9:]} is in SDRAM", hex(s.addr))
        m.write(s.addr, bytes([0xAA]) * s.size)
    m.call(m.addr("_ZN4mods9colorsong4initEv"))
    for name in ("colorsong12_GLOBAL__N_18patternsE", "colorsong12_GLOBAL__N_16colorsE"):
        s = m.sym(name)
        check(m.read(s.addr, s.size) == bytes(s.size), f"{name[-9:]} zeroed at boot")


def test_off_and_on(mt: Path, ref: Path) -> None:
    r = ModRig(mt)
    r.m.call(r.m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    sample_song(r)

    plain = Machine(ref)
    plain_grid = Grid(plain)
    plain_playlist = plain.alloc(255)
    plain_normal = plain.alloc(255)
    plain_midi = plain.alloc(255)
    plain.write(plain_playlist, r.m.read(r.playlist, 255))
    plain.write(plain_normal, r.m.read(r.normal, 255))
    plain.write(plain_midi, r.m.read(r.midi, 255))

    def plain_draw(theme: str, muted: set[int]) -> list[tuple]:
        plain_grid.set_theme(theme)
        plain_grid.set_mutes(muted)
        return plain_grid.draw(r.length, plain_playlist, plain_normal, plain_midi)

    # Off, before anything is known and with everything known
    r.set_switch(False)
    r.grid.set_mutes(set())
    check(r.draw() == plain_draw("themeDefault", set()), "Off: display commands as plain CE")
    r.set_switch(True)
    first = r.draw()
    check(first == plain_draw("themeDefault", set()), "On, nothing read yet: as plain CE")

    reads = [r.update() for _ in range(3)]
    check(reads[0] == 1 and all(n <= 1 for n in reads), "one pattern file per update", str(reads))
    total = sum(reads) + r.settle()
    check(total == 3, "a song of 3 patterns: 3 reads, 4 slots", str(total))
    check(r.settle() == 0, "with nothing changed, nothing more is read")

    # On
    on = r.draw()
    got = blocks(on)
    plain_commands = plain_draw("themeDefault", set())
    plain_blocks = blocks(plain_commands)
    check(len(on) == len(plain_commands), "the same number of display commands: no bytes added to the display list",
          f"{len(on)} vs {len(plain_commands)}")
    same_shape = all(a == b or (a[0] == "COLOR" and b[0] == "COLOR") for a, b in zip(on, plain_commands))
    check(same_shape, "every other command as CE's")
    colors = r.grid.theme_colors("themeDefault")
    empties = [k for k, v in plain_blocks.items() if not v[1]]
    check(all(got[k][0] == colors[8] and not got[k][1] for k in empties), "empty blocks keep the theme's colour")
    check(got[(0, 0)][0] == hsl_rgb(0, 78, 50), "the first family is red, in the first shade",
          hex(got[(0, 0)][0]))
    check(got[(0, 1)][0] != got[(0, 0)][0], "another instrument, another colour")
    check(got[(1, 0)][0] == got[(0, 0)][0] and got[(2, 0)][0] == got[(0, 0)][0],
          "the same content, the same colour, in other patterns")
    check(got[(1, 3)][0] != got[(1, 0)][0], "the same instrument, other notes: another shade")
    check(got[(3, 4)][0] == hsl_rgb(0, 6, SHADE_LIGHT[0]), "a block with only FX is near-grey", hex(got[(3, 4)][0]))

    # muted: much darker, same hue, and CE's muted colour isn't used
    r.grid.set_mutes({0})
    muted = blocks(r.draw())
    check(muted[(0, 0)][0] == hsl_rgb(0, 39, 22), "a muted track: the same colour, much darker", hex(muted[(0, 0)][0]))
    r.grid.set_mutes(set())

    # Off with everything known, also muted, on each theme
    r.set_switch(False)
    for theme in THEMES:
        for mute in (set(), {0, 2, 9}):
            r.grid.set_mutes(mute)
            check(r.draw(theme=theme) == plain_draw(theme, mute), f"Off: as plain CE, {theme}, muted {sorted(mute)}")
    r.grid.set_mutes(set())

    # a full screen: the count that goes against the display limit
    slots = [1] * 40
    full = pattern_file({t: notes(t + 1, 0) for t in range(16)})
    r.put_pattern(1, full)
    plain.write(plain_normal, r.m.read(r.normal, 255))
    plain.write(plain_midi, r.m.read(r.midi, 255))
    r.set_playlist(slots)
    plain.write(plain_playlist, r.m.read(r.playlist, 255))
    plain_full = plain_draw("themeDefault", set())
    r.set_switch(True)
    r.settle()
    on_full = r.draw()
    fill_bytes = 4 * len(on_full)
    print(f"     a full grid: {len(on_full)} commands, {fill_bytes} bytes On, "
          f"{4 * len(plain_full)} bytes plain CE, limit {DISPLAY_LIMIT} for the whole frame")
    check(len(on_full) == len(plain_full), "a full grid, 12 rows of 16: no bytes added")
    check(len(blocks(on_full)) == ROWS_VISIBLE * TRACKS, "and all of it is drawn", str(len(blocks(on_full))))
    check(len({v[0] for v in blocks(on_full).values()}) == 16, "16 instruments: 16 colours")

    # both file formats
    r2 = ModRig(mt)
    r2.m.call(r2.m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    r2.set_switch(True)
    tracks = {0: notes(1, 0, 4), 1: notes(2, 0), 4: [(1, -1, 0, [(FX_OTHER, 30)])]}
    r2.put_pattern(1, pattern_file(tracks))
    r2.put_pattern(2, pattern_file(tracks, version=5))
    r2.set_playlist([1, 2])
    r2.settle()
    got2 = blocks(r2.draw())
    check(all(got2[(0, t)][0] == got2[(1, t)][0] for t in range(TRACKS)), "a format 5 file and a format 6 file: the same colours")

    # Micro-move and Volume don't count, as columns or as FX
    plain_t = [(0, 60, 1, [])]
    r2.put_pattern(1, pattern_file({0: plain_t}))
    r2.settle()
    before = r2.color(1, 0)
    r2.put_pattern(1, pattern_file({0: [(0, 60, 1, [], 90, 12)]}))
    check(r2.settle() == 1, "a changed pattern is read again")
    check(r2.color(1, 0) == before, "the Volume and micro-move columns don't change a colour")
    r2.put_pattern(1, pattern_file({0: [(0, 60, 1, [(FX_MICROMOVE, 40), (FX_VOLUME, 20)])]}))
    r2.settle()
    check(r2.color(1, 0) == before, "Micro-move and Volume FX don't change a colour")
    r2.put_pattern(1, pattern_file({0: [(0, 60, 1, [(FX_OTHER, 40)])]}))
    r2.settle()
    check(r2.color(1, 0) != before, "another FX does")


def test_reads_wait(mt: Path) -> None:
    r = ModRig(mt)
    r.m.call(r.m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    r.set_switch(True)
    sample_song(r)

    r.m.write(r.status, b"\x01")
    check(sum(r.update() for _ in range(5)) == 0, "the file manager busy: no pattern file is read")
    r.m.write(r.status, b"\x00")
    check(r.update() == 1, "idle again: reading goes on")

    # playing: one file per 100 ms
    seq = r.m.addr("sequencer")
    is_play = r.m.addr("_ZN9Sequencer6isPlayEv")
    player_flag = None
    base, size = seq, r.m.sym("sequencer").size
    saved = r.m.read(base, size)
    for offset in range(size):
        r.m.write(base + offset, b"\x01")
        if r.m.call(is_play, seq) & 0xFF:
            player_flag = base + offset
            r.m.write(base + offset, saved[offset:offset + 1])
            break
        r.m.write(base + offset, saved[offset:offset + 1])
    check(player_flag is not None, "found the sequencer's playing flag")
    r.m.write(player_flag, b"\x01")
    check(r.update() == 0, "playing, just after a read: not yet")
    r.now += 99
    check(r.update() == 0, "after 99 ms: not yet")
    r.now += 1
    check(r.update() == 1, "after 100 ms: the next file")
    check(r.update() == 0, "and then the gap again")
    r.m.write(player_flag, b"\x00")
    check(not r.sd.unexpected and not r.sd.handles, "only expected file calls, every file closed",
          f"{r.sd.unexpected} {r.sd.handles}")


# -- 4. Edits ---------------------------------------------------------------------------------
def test_edits(mt: Path) -> None:
    r = ModRig(mt)
    r.m.call(r.m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    r.set_switch(True)
    sample_song(r)
    r.settle()
    a, b, c = r.color(1, 0), r.color(1, 1), r.color(3, 2)
    check(r.project_marks == 0, "colours handed out don't mark the project changed", str(r.project_marks))

    r.m.call(r.m.addr("_ZN4mods16onWorkspaceSavedEv"))
    first = r.record()
    check(first is not None and first[:4] == b"CSG1", "the colours land in Workspace/mods.bin as a CSG1 image",
          str(first[:8] if first else first))

    # an edit, saved
    r.put_pattern(3, pattern_file({2: notes(3, 0, 1), 4: [(1, -1, 0, [(FX_OTHER, 30)])]}))
    check(r.settle() == 1, "an edited pattern: only its file is read")
    check(r.color(1, 0) == a and r.color(1, 1) == b, "other blocks keep their colours")
    check(r.color(3, 2) != c, "the edited block changes", hex(r.color(3, 2)))
    check(r.project_marks == 0, "still not marked changed", str(r.project_marks))
    r.m.call(r.m.addr("_ZN4mods16onWorkspaceSavedEv"))
    check(r.record() != first, "the new colours land in mods.bin")

    # the edited pattern in RAM
    r.m.write(r.actual, bytes([1]))
    r.unsaved = {1}
    seq = r.m.addr("seq")
    pattern_size = r.m.sym("seq").size // 2
    r.sd.calls.clear()
    actual = r._glue("13unsavedTracksEh", 1) - HEADER
    check(actual in (seq, seq + pattern_size), "the unsaved tracks are the sequencer's actual pattern")
    r.m.write(actual, pattern_file({0: notes(7, 0), 1: notes(2, 0), 9: notes(50, 2)})[:16432])
    r.bitmask(1, actual)
    r.m.write(r.actual, bytes([1]))
    check(r.settle() == 0, "the actual pattern, unsaved: read from RAM, no file")
    check(r.color(1, 0) != a, "its edited block changes")
    check(r.color(1, 1) == b, "and its other blocks don't")
    r.unsaved = set()

    # switching off and on again
    r.set_switch(False)
    r.put_pattern(1, pattern_file({0: notes(1, 0, 4), 1: notes(2, 0), 9: notes(50, 2)}))
    state = r.m.addr("_ZN4mods9colorsong12_GLOBAL__N_115patternsChangedE")
    r.m.write(state, b"\0")
    r.put_pattern(1, pattern_file({0: notes(1, 0, 4), 1: notes(2, 0), 9: notes(50, 2)}))
    check(r.m.r8(state) == 0, "Off: a save tells the mod nothing")
    r.set_switch(True)
    r.m.call(r.m.addr("_ZN4mods29projectColorsongSwitchChangedEv"))
    check(r.settle() >= 1, "turned on: the song is read again")
    check(r.color(1, 0) == a, "and the edit made while Off is seen")

    # the hook on updatePatternBitmask, which can run in the audio interrupt
    patterns = r.m.sym("_ZN4mods9colorsong12_GLOBAL__N_18patternsE")
    colors = r.m.sym("_ZN4mods9colorsong12_GLOBAL__N_16colorsE")
    before_colors = r.m.read(colors.addr, colors.size)
    before_patterns = bytearray(r.m.read(patterns.addr, patterns.size))
    source = r.load(1)
    r.sd.calls.clear()
    r.grid.commands = []
    marks = r.project_marks
    r.bitmask(1, source)
    check(not r.sd.calls and not r.grid.commands and r.project_marks == marks,
          "updatePatternBitmask: no file, display or project calls")
    check(r.m.read(colors.addr, colors.size) == before_colors, "and the colours aren't touched there")
    after = r.m.read(patterns.addr, patterns.size)
    changed = [i for i, (x, y) in enumerate(zip(before_patterns, after)) if x != y]
    check(changed == [2] and r.m.r8(state) == 1, "it only sets flags: the pattern's entry and the changed flag",
          str(changed))
    check(not r.sd.unexpected and not r.sd.handles, "only expected file calls, every file closed",
          f"{r.sd.unexpected} {r.sd.handles}")


# -- 5. A real project ------------------------------------------------------------------------
def project_tables(colorsong: bytes) -> tuple[dict, dict]:
    families, variants = struct.unpack_from("<HH", colorsong, 4)
    fam, var = {}, {}
    for i in range(families):
        key, slot = struct.unpack_from("<IB", colorsong, 12 + 8 * i)
        fam[key] = slot
    for i in range(variants):
        key, content, shade = struct.unpack_from("<IHB", colorsong, 12 + 8 * (families + i))
        var[(key, content)] = shade
    return fam, var


def load_project(r: ModRig, folder: Path, name: str) -> dict[int, bytes]:
    """The project's side files on the SD card and its pattern files in the Workspace, as CE's
    Open copies them, with the used tracks worked out by CE's own function."""
    for f in folder.glob("*.bin"):
        r.sd.files[f"Projects/{name}/{f.name}"] = f.read_bytes()
    patterns = {}
    for f in sorted((folder / "patterns").glob("pattern_*.mtp")):
        number = int(f.stem[8:])
        patterns[number] = f.read_bytes()
        r.sd.files[PATTERN_FILE % number] = patterns[number]
    r.set_playlist([])
    project = (folder / "project.mt").read_bytes()
    slots = list(project[0x10:0x10 + 255])
    if 0 in slots:
        slots = slots[:slots.index(0)]
    r.set_playlist(slots)
    return patterns


def expected_colors(patterns: dict[int, bytes], colorsong: bytes, slots: list[int]) -> dict:
    """{(pattern, track): (1.2.0's colour, its muted colour)}, for filled tracks in the song."""
    fam, var = project_tables(colorsong)
    out = {}
    for p in sorted(set(slots)):
        data = patterns[p]
        stride = 6 if len(data) == 12336 else 8
        size = 1 + STEPS * stride
        last = min(data[HEADER], 127)
        for t in range(TRACKS):
            track = data[HEADER + t * size:HEADER + (t + 1) * size]
            family, content = track_key(track, last, stride)
            shade = var.get((family, content))
            if shade is None:
                continue
            slot = None if family == 0 else fam[family]
            out[(p, t)] = (color_120(slot, shade, False), color_120(slot, shade, True))
    return out


def test_real_project(mt: Path, folder: Path) -> None:
    if not (folder / "colorsong.bin").exists():
        print(f"SKIP a real project: nothing at {folder}")
        return
    colorsong = (folder / "colorsong.bin").read_bytes()
    name = "hellish downtown"
    r = ModRig(mt)
    patterns = load_project(r, folder, name)
    slots = list(r.m.read(r.playlist, r.length))
    p = r.projects
    p.open("Projects", name)
    check(r.m.r8(r.switches + COLORSONG_ID) == 1, "a 1.2.0 project with a colorsong.bin: On")
    check(r.sd.files[f"Projects/{name}/colorsong.bin"] == colorsong, "its colorsong.bin stays as it was")
    for number in sorted(set(slots)):
        r.bitmask(number, r.load(number))
    r.settle()

    want = expected_colors(patterns, colorsong, slots)
    wrong, seen = [], 0
    for number in sorted(set(slots)):
        mask = r.m.read(r.normal + number - 1, 1)[0] | r.m.read(r.midi + number - 1, 1)[0] << 8
        for t in range(TRACKS):
            if not mask & (1 << t):
                continue
            seen += 1
            normal, muted = want.get((number, t), (None, None))
            got = r.color(number, t), r.color(number, t, muted=True)
            if got != (normal, muted):
                wrong.append((number, t, got, (normal, muted)))
    print(f"     {len(set(slots))} patterns in a song of {len(slots)}, {seen} filled blocks")
    check(seen > 0 and not wrong, "every block has the colour 1.2.0 gives it, through CE's reader, muted too", str(wrong[:6]))
    check(r.project_marks == 0, "opening it doesn't mark it changed", str(r.project_marks))

    r.grid.set_mutes(set())
    drawn = blocks(r.draw())
    bad = [k for k, v in drawn.items()
           if v[1] and v[0] != want[(slots[k[0]], k[1])][0]]
    check(len(drawn) == min(len(slots), ROWS_VISIBLE) * TRACKS and not bad, "and the Song screen draws those", str(bad[:6]))
    if len(slots) > ROWS_VISIBLE + 2:
        top = ROWS_VISIBLE + 1
        scrolled = blocks(r.draw(top=top))
        shift = top - (ROWS_VISIBLE - 2)
        bad = [k for k, v in scrolled.items() if v[1] and v[0] != want[(slots[k[0] + shift], k[1])][0]]
        check(not bad, "also scrolled down the song", str(bad[:6]))

    # Save, Save As
    p.save()
    check(r.record(f"Projects/{name}/mods.bin") is not None, "Save: the record in the project's mods.bin")
    check(r.record(f"Projects/{name}/mods.bin") == colorsong, "with nothing edited: the same image as colorsong.bin")
    p.save("Projects", "Copy")
    p.new_project()
    r.set_switch(True)
    theme_fill = r.grid.theme_colors("themeDefault")[1]
    check(all(r.color(n, t) == theme_fill for n, t in want), "New Project: no colours from the last project")
    check(r.record() is None, "and no record")
    p.open("Projects", "Copy")
    check(r.m.r8(r.switches + COLORSONG_ID) == 1, "Save As: the copy has the mod On")
    r.settle()
    check(all(r.color(n, t) == v[0] for (n, t), v in want.items()), "and the same colours")

    # turned off and saved, it stays off, though the colorsong.bin is still there
    r.set_switch(False)
    r.m.call(r.m.addr("_ZN4mods29projectColorsongSwitchChangedEv"))
    p.save()
    p.new_project()
    p.open("Projects", "Copy")
    check(r.m.r8(r.switches + COLORSONG_ID) == 0, "imported once: turned Off and saved, it stays Off")
    p.new_project()
    p.open("Projects", name)
    check(r.m.r8(r.switches + COLORSONG_ID) == 1, "the first project keeps its own switch")
    check(not r.sd.unexpected and not r.sd.handles, "only expected file calls, every file closed",
          f"{r.sd.unexpected} {r.sd.handles}")


# -- 6. Themes --------------------------------------------------------------------------------
def test_themes(mt: Path, folder: Path, out_dir: Path) -> None:
    r = ModRig(mt)
    r.m.call(r.m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    r.set_switch(True)
    if (folder / "colorsong.bin").exists():
        r_name = "hellish downtown"
        load_project(r, folder, r_name)
        r.projects.open("Projects", r_name)
        for number in sorted(set(r.m.read(r.playlist, r.length))):
            r.bitmask(number, r.load(number))
    else:
        sample_song(r)
    r.settle()

    muted = {2, 9}
    r.grid.set_mutes(muted)
    for theme in THEMES:
        colors = r.grid.theme_colors(theme)
        empty = lightness(colors[8])
        drawn = blocks(r.draw(theme=theme))
        filled = {k: v for k, v in drawn.items() if v[1]}
        close = {k: hex(v[0]) for k, v in filled.items() if abs(lightness(v[0]) - empty) < 6}
        check(filled and not close, f"{theme}: every block stands out from the empty ones (lightness {empty})",
              str(list(close.items())[:6]))
        rects = [(v[0], v[2]) for v in drawn.values()]
        write_png(out_dir / f"colorsong_{theme}.png", 490, GRID_TOP + ROWS_VISIBLE * ROW_PITCH + 4, colors[3], rects)
    r.grid.set_mutes(set())


def test_cost(mt: Path) -> None:
    """Instructions for a frame's blocks, with a song of 40 patterns of 16 different tracks."""
    r = ModRig(mt)
    r.m.call(r.m.addr("_ZN4mods17onWorkspaceLoadedEv"))
    r.set_switch(True)
    plain = pattern_file({t: notes(t + 1, 0) for t in range(16)})
    r.put_pattern(1, plain)
    for p in range(2, 41):
        r.put_pattern(p, pattern_file({t: notes(1 + (p + t) % 40, 0, p % 16) for t in range(16)}))
    r.set_playlist(list(range(1, 41)))
    r.settle()
    count = [0]
    hook = r.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: count.__setitem__(0, count[0] + 1))
    r.draw()
    r.m.uc.hook_del(hook)
    with_colors = count[0]
    r.set_switch(False)
    count[0] = 0
    hook = r.m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: count.__setitem__(0, count[0] + 1))
    r.draw()
    r.m.uc.hook_del(hook)
    print(f"     a full grid's blocks: {with_colors} instructions On, {count[0]} Off")
    check(with_colors < 600_000, "a frame's colours stay under 600,000 instructions", str(with_colors))


def main() -> int:
    mt, ref = Path(sys.argv[1]), Path(sys.argv[2])
    here = Path(__file__).resolve().parents
    folder = next((d / "hellish downtown" for d in here if (d / "hellish downtown").exists()), here[0] / "none")
    if len(sys.argv) > 3:
        folder = Path(sys.argv[3])
    test_config(mt)
    test_boot_tables(mt)
    test_off_and_on(mt, ref)
    test_reads_wait(mt)
    test_edits(mt)
    test_real_project(mt, folder)
    test_themes(mt, folder, mt.parent)
    test_cost(mt)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
