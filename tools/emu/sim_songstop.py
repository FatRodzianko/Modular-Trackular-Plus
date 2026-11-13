#!/usr/bin/env python3
# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Does a mod make CE's song stop at a pattern end? The targeted test from docs/mt/SIMULATOR_PLAN.md
section 1, on the stage 1 simulator.

    python tools/emu/sim_songstop.py <firmware.axf> <source project> --mod colorsong|copytonew|none
        [--card-scale 8] [--tempo 800] [--patterns 48] [--length 16] [--play-after-ms 40] [--seconds 6]
        [--press-every-ms 137] [--seed 1] [--phase-ns 0] [--log run.jsonl]

The project made from the source (only read) has a song of --patterns patterns, each --length steps,
at --tempo, every pattern marked as used and no Color Song colours saved, so Color Song reads each
pattern file once. The run opens it, switches the mod On (on a build that has it), opens the Song
screen, presses Play --play-after-ms later, and plays for --seconds of virtual time. With copytonew it
then goes to the Pattern screen and starts Copy to New every --press-every-ms (the job the button
starts, called directly), so each press checks the card and writes a pattern.

Since CE's 15c3d23 and c476ce1, the next pattern is read in the main loop after the audio interrupt
asks for it, and must be in memory by the end of that step, or CE stops the song
(stopForMissingNextPattern). --card-scale multiplies the card's placeholder per-call and per-byte
times, so the sweep shows the card speed at which each build first stops.

Prints, as JSON: the stop (if any); each pattern request with the time to its load start, to the
pattern being in memory and to the switch; and the mod's card accesses that started near it.
Results so far: docs/mt/SIMULATOR_STAGE1.md, "Targeted test".

Timing is placeholder (sim_kernel.PLACEHOLDERS) apart from what --card-scale sets, so the result
compares builds at the same settings; it says nothing about a given card on the unit.
"""
import argparse
import json
import shutil
import struct
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from sim_kernel import Log, Params  # noqa: E402
from sim_rig import PLAY, Sim  # noqa: E402

SONG = 17                                   # interfaceButtonSong
PATTERN_UPDATE = "_ZN14cPatternEditor6updateEv"
SONG_UPDATE = "_ZN11cSongEditor6updateEv"
PATTERN_BUTTON = 22                         # interfaceButtonPattern
SWITCHES = "_ZN4mods8switchesE"
MOD_IDS = {"colorsong": 0, "copytonew": 9}  # mods::Mod
COPY_PRESSED = "_ZN4mods18copypatternPressedEv"
# Copy to New's card check, and in the update that finds the target empty, its write
COPY_CHECK = "_ZN12cFileManager14isPatternEmptyEh"
# stopForMissingNextPattern is inlined, so the stop is Sequencer::stop called in the audio interrupt
# while playing: with no MIDI and no button pressed, nothing else stops the song there.
STOP = "_ZN9Sequencer4stopENS_11StopContextE"
REQUEST = "_ZN12cFileManager20loadWorkspacePatternEh"
LOAD_START = "_ZN12cFileManager25startRequestedPatternLoadEv.part.0"
SWITCH = "_ZN9Sequencer20switchRamPatternsNowEv"
# The pattern is in memory once the file manager has copied it into the sequencer's spare buffer:
# preloadedPattern is set when this returns (loadPatternFromWorkspace).
LOADED = "_ZN9Sequencer25copyBufferToBufferPatternEPh"
LOAD_FROM = "_ZN12cFileManager24loadPatternFromWorkspaceEh"
# Color Song's reads (CeSong::readTracks is inlined into its update): CE's reader into the temp buffer,
# from the main loop. Nothing else calls it in this scenario.
TEMP_READ = "_ZN12cFileManager32loadWorkspacePatternToTempBufferEh"

# project.mt (CE's strMtProjectRemote after a 16-byte header): song.playlist[255], playlistPos, then
# strMtValues. Offsets worked out from mtStructs.h; globalTempo agrees with make_masterfx_test_project.
PLAYLIST = 0x10
VALUES = 0x110
TEMPO = VALUES + 176
PATTERN_BITMASK = VALUES + 521
PATTERN_BITMASK_MIDI = PATTERN_BITMASK + 255
HEADER, TRACKS, TRACK_SIZE = 28, 16, 1 + 128 * 8


def make_project(source, parent, name, patterns, length, tempo):
    out = Path(parent) / name
    shutil.copytree(source, out)
    for f in ("colorsong.bin", "mods.bin"):          # no saved colours; the switch is set in RAM
        (out / f).unlink(missing_ok=True)
    pattern = bytearray((Path(source) / "patterns" / "pattern_01.mtp").read_bytes())
    for t in range(TRACKS):
        pattern[HEADER + t * TRACK_SIZE] = length - 1
    for f in (out / "patterns").glob("pattern_*.mtp"):
        f.unlink()
    for p in range(1, patterns + 1):
        (out / "patterns" / f"pattern_{p:02d}.mtp").write_bytes(pattern)
    mt = bytearray((out / "project.mt").read_bytes())
    mt[PLAYLIST:PLAYLIST + 256] = bytes(range(1, patterns + 1)) + bytes(256 - patterns)
    struct.pack_into("<f", mt, TEMPO, float(tempo))
    mt[PATTERN_BITMASK:PATTERN_BITMASK + 255] = b"\x01" * patterns + bytes(255 - patterns)
    mt[PATTERN_BITMASK_MIDI:PATTERN_BITMASK_MIDI + 255] = bytes(255)
    (out / "project.mt").write_bytes(mt)
    return out


def copy_return(sim):
    """The return address of loadPatternFromWorkspace's call to copyBufferToBufferPattern (open CE code):
    preloadedPattern is set right after it."""
    from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
    caller, callee = sim.syms[LOAD_FROM], sim.syms[LOADED]
    start = caller.addr & ~1
    code = bytes(sim.m.read(start, caller.size))
    for insn in Cs(CS_ARCH_ARM, CS_MODE_THUMB).disasm(code, start):
        if insn.mnemonic == "bl" and int(insn.op_str.lstrip("#"), 0) == callee.addr & ~1:
            return insn.address + insn.size
    raise SystemExit("loadPatternFromWorkspace doesn't call copyBufferToBufferPattern directly in this build")


def run(args):
    p = Params(seed=args.seed, phase_ns=args.phase_ns)
    p.t_card_call_ns = int(p.t_card_call_ns * args.card_scale)
    p.t_card_byte_ps = int(p.t_card_byte_ps * args.card_scale)
    tmp = tempfile.mkdtemp(prefix="songstop_")
    try:
        project = make_project(args.project, tmp, "SONGSTOP", args.patterns, args.length, args.tempo)
        sim = Sim(args.axf, p, Log(args.log))
        sim.card_from(project, project.name, workspace=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    k = sim.kernel
    syms = sim.syms
    ev = {"request": [], "load_start": [], "loaded": [], "switch": [], "stop_missing": [], "read": [], "check": []}
    screens = {"pattern": 0, "song": 0}

    def mark(kind):
        return lambda mm: ev[kind].append(k.now())
    sim.watch(sim.m.addr(PATTERN_UPDATE), lambda mm: screens.__setitem__("pattern", screens["pattern"] + 1))
    sim.watch(sim.m.addr(SONG_UPDATE), lambda mm: screens.__setitem__("song", screens["song"] + 1))
    for kind, name in (("request", REQUEST), ("load_start", LOAD_START), ("loaded", LOADED), ("switch", SWITCH),
                       ("stop", STOP), ("read", TEMP_READ), ("check", COPY_CHECK)):
        if name not in syms and kind in ("read", "check"):
            continue                                # the mods' readers: not in a reference build
        if name not in syms:
            raise SystemExit(f"{name} isn't in this build (inlined?): the test can't see it")
        if kind == "stop":
            sim.watch(syms[name].addr, lambda mm: ev["stop_missing"].append(k.now()) if k.context != "main" else None)
        elif kind == "loaded":
            sim.watch(copy_return(sim), mark("loaded"))
        else:
            sim.watch(syms[name].addr, mark(kind))

    t0 = time.perf_counter()
    sim.run(seconds=40.0, stop=lambda: screens["pattern"] > 5)
    if screens["pattern"] <= 5 or sim.failures:
        return {"result": "FAILED: the project didn't open", "failures": sim.failures[:5]}
    has_mod = SWITCHES in syms
    if has_mod and args.mod != "none":
        sim.m.write(syms[SWITCHES].addr + MOD_IDS[args.mod], b"\x01")
    open_vt = k.vt
    sim.press(SONG)
    sim.run(seconds=args.play_after_ms / 1000.0)
    reads_before_play = len(ev["read"])
    sim.press(PLAY)
    play_vt = k.vt
    stopped = lambda: bool(ev["stop_missing"])  # noqa: E731
    presses = 0
    if args.mod == "copytonew":
        # Back to the Pattern screen with the song playing, then Copy to New every --press-every-ms: the
        # job the button starts (mods::copypatternPressed), called between slices. Each press copies the
        # pattern on screen to the next empty one, which is a check and a 16 KB write on the card.
        sim.run(seconds=0.05)
        sim.press(PATTERN_BUTTON)
        while k.vt - play_vt < args.seconds * 1e9 and not stopped():
            sim.run(seconds=args.press_every_ms / 1000.0, stop=stopped)
            if has_mod and not stopped():
                sim.m.call(syms[COPY_PRESSED].addr)
                presses += 1
    else:
        sim.run(seconds=args.seconds, stop=stopped)
    end_vt = k.vt

    step_ns = 60e9 / (args.tempo * 4)
    pattern_ns = step_ns * args.length
    loads = []
    for r in ev["request"]:
        if r < play_vt:
            continue
        start = next((s for s in ev["load_start"] if s >= r), None)
        loaded = next((s for s in ev["loaded"] if s >= r), None)
        switch = next((s for s in ev["switch"] if s > r), None)
        # a Color Song read that started up to one step before the request can still be under way
        reads = [x for x in ev["read"] + ev["check"] if r - step_ns <= x <= (loaded or end_vt)]
        loads.append({"request_ms": round((r - play_vt) / 1e6, 2),
                      "to_load_start_ms": round((start - r) / 1e6, 2) if start else None,
                      "to_loaded_ms": round((loaded - r) / 1e6, 2) if loaded else None,
                      "to_switch_ms": round((switch - r) / 1e6, 2) if switch else None,
                      "reads_near": [round((x - r) / 1e6, 2) for x in reads]})
    stop = ev["stop_missing"][0] if ev["stop_missing"] else None
    out = {
        "axf": Path(args.axf).name, "mod": args.mod if has_mod or args.mod == "none" else "none (not in build)",
        "card_scale": args.card_scale, "tempo": args.tempo, "step_ms": round(step_ns / 1e6, 2),
        "pattern_ms": round(pattern_ns / 1e6, 1), "patterns": args.patterns, "seed": args.seed,
        "phase_ns": args.phase_ns, "play_after_ms": args.play_after_ms,
        "open_virtual_s": round(open_vt / 1e9, 3),
        "song_screen_updates": screens["song"],
        "reads_before_play": reads_before_play,
        "reads_during_play": len([x for x in ev["read"] if x >= play_vt]),
        "copy_presses": presses,
        "copy_checks_during_play": len([x for x in ev["check"] if x >= play_vt]),
        "copies_on_card": len([f for f in sim.sd.files if f.startswith("Workspace/patterns/")]) - args.patterns,
        "played_virtual_s": round((end_vt - play_vt) / 1e9, 3),
        "pattern_requests": len(loads),
        "stopped_missing_pattern": stop is not None,
        "stop_at_ms": round((stop - play_vt) / 1e6, 2) if stop else None,
        "loads": loads[:40],
        "worst_to_loaded_ms": max((x["to_loaded_ms"] or 0 for x in loads), default=None),
        "failures": sim.failures[:5], "invariants": sim.invariants[:5],
        "sd_unexpected": sorted(set(sim.sd.unexpected))[:8],
        "wall_s": round(time.perf_counter() - t0, 1),
    }
    out["result"] = "stopped" if stop else "played"
    if sim.failures or sim.invariants:
        out["result"] = "FAILED: a failure or a broken invariant"
    elif not loads:
        out["result"] = "FAILED: no pattern end was reached"
    sim.kernel.log.close()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("axf")
    ap.add_argument("project")
    ap.add_argument("--mod", choices=["colorsong", "copytonew", "none"], default="colorsong")
    ap.add_argument("--press-every-ms", type=float, default=137.0, help="copytonew: time between presses")
    ap.add_argument("--card-scale", type=float, default=1.0)
    ap.add_argument("--tempo", type=float, default=800.0)
    ap.add_argument("--patterns", type=int, default=16)
    ap.add_argument("--length", type=int, default=16)
    ap.add_argument("--play-after-ms", type=float, default=40.0)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--phase-ns", type=int, default=0)
    ap.add_argument("--log", type=Path)
    args = ap.parse_args()
    print("UNCALIBRATED: timing is placeholder; this compares builds at the same settings", flush=True)
    out = run(args)
    print(json.dumps(out, indent=1), flush=True)
    return 0 if not out["result"].startswith("FAILED") else 1


if __name__ == "__main__":
    sys.exit(main())
