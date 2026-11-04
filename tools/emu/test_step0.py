# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Step 0 checks on a full Tracker+ build, with the synth libraries in:

1. Config menu: the real static constructors build Config -> Mods as the 8th top-level
   group, just before Info, starting with the "Mods in build" line and then one line per
   mod; and Project Settings -> Mods as the last item of Project Settings, a group with
   one line per mod and then the "Defaults" line. Each mod's own lines are checked by
   its test (test_humanize.py, test_swing.py).
2. Settings files through CE's FileSystem interface: mods::init reads Mods/mods.cfg on a fake SD card wired into a FileSystem vtable whose
   slots are taken from the image's own FileSystemImpl vtable.

Usage: python tools/emu/test_step0.py build/emu/TrackerPlus_MT_full.axf
"""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine import Machine  # noqa: E402

failures = 0


def check(cond: bool, name: str, detail: str = "") -> None:
    global failures
    print(("PASS " if cond else "FAIL ") + name + (f" ({detail})" if detail and not cond else ""))
    if not cond:
        failures += 1


# -- 1. Config menu ---------------------------------------------------------------------------
# cMenuGroup: type@0, parentMenu@4, childsCount@8, childs@12, childsNames@16,
# childsValues@20, groupName@24, selectedItem@28, slotsUsed@29, slot@30
# cMenuItem:  type@0, itemName@4, itemDesc@8, itemType@12, itemSetup@16
MENU_TYPE_GROUP = 1
MENU_TYPE_ITEM_LABEL = 5
EXPECTED_GROUPS = ["Project Settings", "General", "MIDI", "USB", "Metronome", "Firmware",
                   "Mods", "Info"]


def menu_item_named(m: Machine, group: str, name: str) -> int:
    """A group's item by its name, wherever other mods' lines have put it."""
    a = m.addr(group)
    items = (m.r32(m.r32(a + 12) + 4 * i) for i in range(m.r8(a + 8)))
    return next(i for i in items if m.cstr(m.r32(i + 4)) == name)


def test_menu(m: Machine) -> None:
    m.call(m.addr("_GLOBAL__sub_I_menuBase"))

    base = m.addr("menuBase")
    count, used = m.r8(base + 8), m.r8(base + 29)
    names_ptr = m.r32(base + 16)
    names = [m.cstr(m.r32(names_ptr + 4 * i)) for i in range(min(count, used))]
    check(count == 8 and used == 8, "Config has 8 top-level groups", f"count {count}, used {used}")
    check(names == EXPECTED_GROUPS, "Mods sits just before Info", str(names))

    mods = m.addr("menuMods")
    check(m.r32(base + 12 + 0) != 0 and m.r32(m.r32(base + 12) + 4 * 6) == mods,
          "slot 6 is the menuMods object")
    item = m.r32(m.r32(mods + 12))
    check(m.cstr(m.r32(item + 4)) == "Mods in build", "first item: Mods in build")
    check(m.r32(item + 12) == MENU_TYPE_ITEM_LABEL, "item is a label")
    built = m.cstr(m.r32(m.r32(item + 16)))
    check(built.isdigit(), "label shows a count", built)
    if not built.isdigit():
        return
    built = int(built)
    check(m.r8(mods + 8) == 1 + built and m.r8(mods + 29) == 1 + built,
          "Mods holds the count and one item per mod",
          f"count {m.r8(mods + 8)}, used {m.r8(mods + 29)}, mods {built}")

    project = m.addr("menuProject")
    count, used = m.r8(project + 8), m.r8(project + 29)
    check(count == 13 and used == 13, "Project Settings has CE's 10 items plus Mods, Global Swing and Humanize",
          f"count {count}, used {used}")
    project_mods = m.addr("menuProjectMods")
    check(m.r32(m.r32(project + 12) + 4 * 10) == project_mods,
          "after CE's items comes the Mods group")
    check(m.cstr(m.r32(m.r32(project + 16) + 4 * 10)) == "Mods", "its name")
    check(m.r32(m.r32(project + 12) + 4 * 11) == m.addr("menuProjectSwing")
          and m.r32(m.r32(project + 12) + 4 * 12) == m.addr("menuProjectHumanize"),
          "then Global Swing and Humanize")
    check(m.r32(project_mods) == MENU_TYPE_GROUP and m.r32(project_mods + 4) == project,
          "a group inside Project Settings")
    check(m.r8(project_mods + 8) == built + 1 and m.r8(project_mods + 29) == built + 1,
          "Project Settings -> Mods holds one item per mod and Defaults",
          f"count {m.r8(project_mods + 8)}, used {m.r8(project_mods + 29)}")
    item = m.r32(m.r32(project_mods + 12) + 4 * built)
    check(m.cstr(m.r32(item + 4)) == "Defaults", "last item: Defaults")
    check(m.r32(item + 12) == MENU_TYPE_ITEM_LABEL, "item is a label")
    check(m.cstr(m.r32(m.r32(item + 16))) == "Config > Mods", "label points at Config -> Mods")


# -- 2. Settings files through FileSystem -----------------------------------------------------
class FakeSd:
    """Files and folders, served through stubs placed in a FileSystem vtable."""

    def __init__(self, m: Machine):
        self.m = m
        self.files: dict[str, bytes] = {}
        self.dirs: set[str] = set()
        self.handles: dict[int, list] = {}   # id -> [path, mode, position]
        self.next_handle = 0x100
        self.unexpected: list[str] = []
        self.calls: list[str] = []

        impl_vt = m.addr("_ZTV14FileSystemImpl") + 8
        slots = m.sym("_ZTV14FileSystemImpl").size // 4 - 2
        handlers = {
            "exists": self.exists, "mkdir": self.mkdir, "remove": self.remove,
            "openFile": self.open_file, "readFile": self.read_file,
            "writeFile": self.write_file, "closeFile": self.close_file,
            "getFileSizeEPv": self.size_of_handle,
            "getFileSizeEPKc": self.size_of_path,
        }
        self.vtable = m.alloc(4 * slots, 4)
        for i in range(slots):
            target = m.r32(impl_vt + 4 * i) & ~1
            name = next((n for n, s in m.elf.symbols.items()
                         if s.addr & ~1 == target and n.startswith("_ZN14FileSystemImpl")), "?")
            func = next((h for key, h in handlers.items() if key in name), None)
            if func is None:
                func = self._unexpected(name)
            m.w32(self.vtable + 4 * i, m.add_stub(lambda mm, f=func, n=name: self._log(n, f)))
        self.obj = m.alloc(16, 4)
        m.w32(self.obj, self.vtable)

    def _log(self, name, func):
        self.calls.append(name)
        return func()

    def _unexpected(self, name):
        def f():
            self.unexpected.append(name)
            return 0
        return f

    def exists(self):
        p = self.m.cstr(self.m.arg(1))
        return int(p in self.files or p in self.dirs)

    def mkdir(self):
        self.dirs.add(self.m.cstr(self.m.arg(2)))
        return 1

    def remove(self):
        return int(self.files.pop(self.m.cstr(self.m.arg(1)), None) is not None)

    def open_file(self):
        # std::tuple<bool, FileHandle> openFile(path, mode, FileHandle*): returned in
        # memory (r0), this in r1. libstdc++ lays the tuple out as {FileHandle, bool}.
        ret, path, mode = self.m.arg(0), self.m.cstr(self.m.arg(2)), self.m.arg(3)
        ok = path in self.files or mode != 0
        handle = 0
        if ok:
            handle = self.next_handle
            self.next_handle += 1
            if mode == 3:            # kWriteCreateNew
                self.files[path] = b""
            self.handles[handle] = [path, mode, 0]
            if self.m.arg(4):        # the caller's handle, for files kept open
                self.m.w32(self.m.arg(4), handle)
        self.m.w32(ret, handle)
        self.m.w32(ret + 4, int(ok))
        return ret

    def size_of_handle(self):
        return len(self.files[self.handles[self.m.arg(1)][0]])

    def size_of_path(self):
        return len(self.files[self.m.cstr(self.m.arg(1))])

    def read_file(self):
        buf, n, h = self.m.arg(1), self.m.arg(2), self.m.arg(3)
        path, _mode, pos = self.handles[h]
        data = self.files[path][pos:pos + n]
        self.m.write(buf, data)
        self.handles[h][2] += len(data)
        return len(data)

    def write_file(self):
        data, n, h = self.m.arg(1), self.m.arg(2), self.m.arg(3)
        path = self.handles[h][0]
        self.files[path] += self.m.read(data, n)
        return n

    def close_file(self):
        self.handles.pop(self.m.arg(1), None)
        return 0


def store_image(magic: bytes, records: list[tuple[int, int, bytes]]) -> bytes:
    payload = b"".join(struct.pack("<BBH", i, v, len(d)) + d for i, v, d in records)
    return magic + struct.pack("<HHI", 1, len(payload), zlib.crc32(payload)) + payload


def test_files(m: Machine) -> None:
    # Boot reads mods.cfg. Saving it (mods::defaultsChanged) and the project's set
    # (mods::projectSwitchChanged) are linked in only once a mod adds a switch; until
    # then the host tests cover those paths.
    m.call(m.addr("_GLOBAL__sub_I__ZN4mods4initEP10FileSystem"))
    sd = FakeSd(m)
    init = m.addr("_ZN4mods4initEP10FileSystem")

    m.call(init, sd.obj)
    check(not sd.files and not sd.unexpected, "boot without mods.cfg: nothing read or written",
          f"{sd.files} {sd.unexpected}")

    newer = store_image(b"MTC1", [(0, 1, b"\x01\x00\x01"), (42, 1, b"\x05\x06")])
    sd.files["Mods/mods.cfg"] = newer
    m.call(init, sd.obj)
    check(any("readFile" in c for c in sd.calls), "boot reads mods.cfg through FileSystem")
    check(sd.files["Mods/mods.cfg"] == newer, "boot leaves mods.cfg untouched")
    check(not sd.unexpected, "only expected FileSystem calls", str(sd.unexpected))

    for name, data in (("damaged", newer[:-1]), ("oversized", b"MTC1" + b"\0" * 4000)):
        sd.files["Mods/mods.cfg"] = data
        m.call(init, sd.obj)
        check(not sd.unexpected and sd.files["Mods/mods.cfg"] == data,
              f"{name} mods.cfg read safely", str(sd.unexpected))
    check(not sd.handles, "every file closed", str(sd.handles))


def main() -> int:
    axf = Path(sys.argv[1])
    m = Machine(axf)
    test_menu(m)
    test_files(m)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
