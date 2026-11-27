"""Open one card project in the boot rig, as the unit's File > Open does, and print what the hardware
steps depend on: the mods switched on, track lengths, Master FX slots and labels, Synth FX slots,
synth engines, and what plays.

    python tools/emu/verify_card.py <repo> <axf> <card folder> <project folder name> [--play N]

The card folder is what goes on the SD card: Projects/<name>, System/Synths and MasterFX. Run it on every
device project with the image that goes on the card, and write the steps from what it prints.
"""
import json
import struct
import sys
from pathlib import Path

REPO, AXF, CARD, NAME = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]), sys.argv[4]
PLAY = int(sys.argv[sys.argv.index("--play") + 1]) if "--play" in sys.argv else 200
sys.path.insert(0, str(REPO / "tools" / "emu"))
from boot_rig import BootRig  # noqa: E402
from machine import SCRATCH  # noqa: E402

MODS = ["colorsong", "humanize", "interpolate", "masterfx", "swing", "visualizer", "condtrig", "synths",
        "legato", "copypattern", "slicethru", "resample", "tracklen", "songloop", "chords", "synthfx"]
PLAY_BUTTON = 23

r = BootRig(AXF)
m = r.m
sd = r.sd
# the card's own files: the projects folder as copied, Synths and MasterFX as on the card
r.card_from(CARD / "Projects" / NAME, NAME, workspace=False)
# keyed as the firmware's own paths reach the fake card: CE's Synths without a slash, the mods'
# /MasterFX with one (as the pack files the firmware writes there)
# CE's engines are in System/Synths since CE's 94cdc96 (SdPaths::synthsDir); a card folder made before
# it has them in Synths
sd.dirs.add("System")
for sub, key in (("System/Synths", "System/Synths"), ("Synths", "System/Synths"), ("MasterFX", "/MasterFX")):
    if not (CARD / sub).is_dir():
        continue
    sd.dirs.add(key)
    for f in (CARD / sub).iterdir():
        if f.is_file():
            sd.files[f"{key}/{f.name}"] = f.read_bytes()


def sym(*parts):
    hits = [n for n in r.syms if all(p in n for p in parts)]
    assert len(hits) == 1, (parts, hits[:5])
    return r.syms[hits[0]].addr


def cstr(addr, n=64):
    return m.read(addr, n).split(b"\0")[0].decode("latin-1") if addr else None


def text(symbol, part):
    result = SCRATCH + 0xFF0000
    m.call(m.addr(symbol), result, r.synth, part)
    return m.read(m.r32(result), m.r32(result + 4)).decode()


def step():
    r.loop()
    return r.interrupt()


notes = [0]
r.watch(m.addr("_ZN9Sequencer10sendNoteOnEhPNS_9strPlayer14strPlayerTrack11strSendStepE"),
        lambda mm: notes.__setitem__(0, notes[0] + 1))
updates = [0]
r.watch(m.addr("_ZN14cPatternEditor6updateEv"), lambda mm: updates.__setitem__(0, updates[0] + 1))

r.fill_stack()
r.boot()
for i in range(3000):
    step()
    if updates[0] > 5:
        break
r.open_project(NAME)
before, quiet = len(sd.calls), 0
for i in range(20000):
    step()
    quiet = quiet + 1 if len(sd.calls) == before else 0
    before = len(sd.calls)
    if quiet > 200:
        break
for _ in range(50):                      # the mods' main-loop steps after the open (plugins made)
    step()

out = {"project": NAME, "failures": r.failures}
sw = m.read(m.addr("_ZN4mods8switchesE"), 16)
out["mods_on"] = [MODS[i] for i in range(16) if sw[i]]

# track lengths: the table the mod keeps per pattern number (0: the whole pattern)
if "_ZN4mods8tracklen12_GLOBAL__N_15tableE" in r.syms or any("tracklen" in n and "table" in n for n in r.syms):
    t = sym("tracklen", "table")
    lengths = {}
    for p in range(1, 256):
        row = list(m.read(t + 16 * p, 16))
        if any(row):
            lengths[p] = row
    out["track_lengths"] = {p: lengths[p] for p in list(lengths)[:3]}
    out["track_length_patterns"] = len(lengths)

# Master FX: what the Master page shows (masterfx.cpp's getters)
if any("masterfx" in n and "choiceNames" in n for n in r.syms):
    page = {}
    column = [n for n in r.syms if "masterfx" in n and "choiceColumnName" in n]
    for slot, word in ((0, "Reverb"), (1, "Delay"), (2, "Limiter")):
        # the list's line the project picked, as the Master page's list reads it (choiceName itself
        # may be inlined away)
        names = m.call(sym("4mods8masterfx11choiceNamesEh"), slot)
        name = cstr(m.r32(names + 4 * m.call(sym("4mods8masterfx6choiceEh"), slot)))
        w = m.alloc(16, 4)
        m.write(w, word.encode() + b"\0")
        title = cstr(m.call(sym("4mods8masterfx9pageTitleEhPKc"), slot, w))
        page[word] = {"choice": name, "title": title}
        if column:
            page[word]["column"] = cstr(m.call(r.syms[column[0]].addr, slot))
    out["master_page"] = page
    st = m.addr("_ZN4mods8masterfx8settingsE")
    out["limiter_own_knobs"] = list(struct.unpack("<2f", m.read(st + 56 * 2 + 48, 8)))
    kt = m.alloc(32, 4)
    m.call(sym("_ZN4mods8masterfx11ownKnobTextEhhPcj"), 2, 0, kt, 32)
    lab = 0
    hits = [n for n in r.syms if "masterfx" in n and "ownKnobLabel" in n]
    if hits:
        lab = m.call(r.syms[hits[0]].addr, 2, 0)
    out["limiter_knob1"] = (cstr(lab, 24) if lab else None, cstr(kt, 32))
    names = m.call(sym("4mods8masterfx9duckNamesEv"))
    out["ducks_list"] = [cstr(m.r32(names + 4 * i), 16) for i in range(12)] if names else None

# Synth FX slots and the synth parts
if any("synthfx" in n and "5slotsE" in n for n in r.syms):
    slots = m.addr("_ZN4mods7synthfx5slotsE")
    stride = m.sym("_ZN4mods7synthfx5slotsE").size // 6
    files = [cstr(slots + stride * i, 48) for i in range(6)]
    made = [m.call(m.addr("_ZN4mods7synthfx12slotInstanceEii"), i // 2, i % 2) < 0x80000000 for i in range(6)]
    out["synthfx"] = [(f, mk) for f, mk in zip(files, made)]
out["parts"] = [(text("_ZN5Synth12getSynthNameB5cxx11Ei", p), text("_ZN5Synth12getPatchNameB5cxx11Ei", p))
                for p in range(3)]

# Play: notes, the sound, Bypassed
r.press(PLAY_BUTTON)
step()
r.release(PLAY_BUTTON)
peak = 0
for i in range(PLAY):
    left, right = step()
    peak = max(peak, max(map(abs, left)), max(map(abs, right)))
out["played_blocks"] = PLAY
out["notes"] = notes[0]
out["peak"] = peak
if "master_page" in out:
    out["bypassed"] = [bool(m.call(sym("_ZN4mods8masterfx8bypassedEh"), s) & 1) for s in range(3)]
if "track_lengths" in out:
    out["active_lengths"] = list(m.read(sym("tracklen", "activeLengths"), 16))
out["failures"] = r.failures
print("RESULT " + json.dumps(out))
