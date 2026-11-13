# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The IMX platform's vtables, every slot named from the .axf (docs/mt/SIMULATOR_STAGE1.md 1.5).

boot_rig.py writes stubs into vtable slots by number. A CE change to an interface would move a slot and
run the wrong stub without a word. Here every slot of each implementation class is named by the function
symbol it points to, and the simulator registers a model for each name. resolve() refuses a class with a
slot that has no model, so a CE merge that adds a method stops the run until someone decides what it does.

Only the open platform classes (platforms/imx) are read. The closed libraries' vtables are never looked at.

    python tools/emu/vtables.py <firmware.axf>        lists every slot of every class
"""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

# Platform's getters and the classes their objects are, in the order of PlatformImpl's slots. None: no
# implementation class with a vtable (SystemInfo is a plain struct).
CLASSES = {
    "Platform": "PlatformImpl",
    "Timer": "TimerImpl",
    "Buttons": "ButtonsImpl",
    "Pads": "PadsImpl",
    "Encoder": "EncoderImpl",
    "Battery": "BatteryImpl",
    "Leds": "LedsImpl",
    "Codec": "Sgtl5000Impl",
    "Radio": "RadioImpl",
    "UsbAudio": "UsbAudioImpl",
    "UartMidi": "UartMidiImpl",
    "UsbMidi": "UsbMidiImpl",
    "ConfigManager": "ConfigManagerImpl",
    "FileSystem": "FileSystemImpl",
    "Crc": "CrcCalculatorImpl",
    "DisplayDriver": "DisplayDriverImpl",
    "Usb": "UsbImpl",
}


@dataclass(frozen=True)
class Slot:
    index: int
    symbol: str          # the function the real vtable points to
    method: str          # its name in the class: "millis", "getFileSize", "~D1"
    params: str          # its mangled parameter list: "Ev", "EPKc"

    @property
    def key(self):
        """The name a model is registered under when the method is overloaded."""
        return self.method + self.params


@dataclass
class Model:
    """What a slot does in the simulator. func(machine) returns r0 (or None). pure: its answer depends only
    on the clock and the inputs, and it changes nothing (1.5, 1.10). why: one line, for models that do
    nothing."""
    func: object
    pure: bool = False
    why: str = ""


def _method_of(symbol: str, cls: str | None = None):
    """('millis', 'Ev') from _ZN9TimerImpl6millisEv; ('~D1', 'Ev') for a destructor. With cls, None unless
    the method is cls's own; without, a method of any one class (an inherited one: ButtonsBaseImpl's)."""
    if cls is None:
        m = re.match(r"_ZN(\d+)", symbol)
        if not m:
            return None
        cls = symbol[m.end():m.end() + int(m.group(1))]
    prefix = f"_ZN{len(cls)}{cls}"
    if not symbol.startswith(prefix):
        return None
    rest = symbol[len(prefix):]
    if rest[:2] in ("D0", "D1", "D2", "C1", "C2"):
        return "~" + rest[:2], rest[2:]
    m = re.match(r"(\d+)", rest)
    if not m:
        return None
    n = int(m.group(1))
    start = len(m.group(1))
    return rest[start:start + n], rest[start + n:]


def slots(elf, cls: str) -> list[Slot]:
    """Every slot of cls's vtable, named. The vtable symbol is _ZTV<len><cls>: offset to top, typeinfo,
    then the slots."""
    vt = elf.symbols.get(f"_ZTV{len(cls)}{cls}")
    if vt is None or not vt.size:
        raise KeyError(f"no vtable for {cls} in this image")
    by_addr = {}
    for name, s in elf.symbols.items():
        if s.is_func and s.addr:
            by_addr.setdefault(s.addr & ~1, []).append(name)
    out = []
    data = _read(elf, vt.addr + 8, vt.size - 8)
    for i in range(len(data) // 4):
        target = int.from_bytes(data[4 * i:4 * i + 4], "little") & ~1
        names = sorted(by_addr.get(target, []))
        own = [(n, _method_of(n, cls)) for n in names if _method_of(n, cls)]
        inherited = [(n, _method_of(n)) for n in names if _method_of(n)]
        if own or inherited:
            symbol, (method, params) = (own or inherited)[0]
        elif names:                                  # a thunk or a plain function: named by what it is
            symbol, method, params = names[0], names[0], ""
        else:
            symbol, method, params = f"{target:#x}", f"{target:#x}", ""
        out.append(Slot(i, symbol, method, params))
    return out


def _read(elf, addr, size):
    """Bytes at a load address, from the ELF's segments (vtables are in flash)."""
    for vaddr, paddr, blob, _memsz in elf.segments:
        for base in {vaddr, paddr}:
            if base <= addr and addr + size <= base + len(blob):
                return blob[addr - base:addr - base + size]
    raise KeyError(f"{addr:#x} isn't in a loaded segment")


class Unmodelled(Exception):
    """A slot has no model."""


def resolve(elf, cls: str, models: dict[str, Model]) -> list[tuple[Slot, Model]]:
    """Each slot of cls with its model, by method+params first, then by method. Raises Unmodelled naming
    every slot without one, and every model that matches no slot (a stale name after a CE change)."""
    found, missing = [], []
    used = set()
    for s in slots(elf, cls):
        key = s.key if s.key in models else s.method
        if key not in models:
            missing.append(f"{s.index} {s.symbol}")
            continue
        used.add(key)
        found.append((s, models[key]))
    stale = sorted(set(models) - used)
    if missing or stale:
        raise Unmodelled(f"{cls}: slots with no model {missing}; models for no slot {stale}")
    return found


def main():
    sys.path.insert(0, str(Path(__file__).parent))
    from machine import Elf
    elf = Elf(Path(sys.argv[1]))
    for name, cls in CLASSES.items():
        print(f"{name} ({cls}):")
        for s in slots(elf, cls):
            print(f"  {s.index:2} {s.key:40} {s.symbol}")


if __name__ == "__main__":
    main()
