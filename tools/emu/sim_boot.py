#!/usr/bin/env python3
# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Boot, Open and Play on the simulator's virtual clock (docs/mt/SIMULATOR_STAGE1.md).

    python tools/emu/sim_boot.py <firmware.axf> <project folder> [--play 1.0] [--seed 1] [--no-skip]
        [--no-input-interrupt] [--interrupts-after-init] [--log run.jsonl] [--record run.json]
    python tools/emu/sim_boot.py --replay run.json

The project goes on the card in /Projects and in the Workspace, so the start-up opens it, as a unit does.
Prints boot_rig.py's figures (heap, stack, notes, pool, peak, failures) and the clock's: interrupts fired,
late, held by masks, the longest main-loop gap, skips, uncertain stops and the speed.

Every timing value is a placeholder: the run says nothing about the unit's timing.
"""
import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from sim_kernel import Log, Params  # noqa: E402
from sim_rig import PLAY, Sim  # noqa: E402

PATTERN_UPDATE = "_ZN14cPatternEditor6updateEv"


def card_hash(sim):
    h = hashlib.sha256()
    for name, data in sorted(sim.sd.files.items()):
        h.update(name.encode() + b"\0" + hashlib.sha256(data).digest())
    return h.hexdigest()[:16]


def run(axf, project, params, play_s, log_path=None, open_limit_s=30.0):
    t0 = time.perf_counter()
    sim = Sim(axf, params, Log(log_path))
    sim.card_from(project, Path(project).name, workspace=True)
    record = {"params": params.to_json(), "axf": str(axf), "project": str(project), "play_s": play_s,
              "axf_sha256": hashlib.sha256(Path(axf).read_bytes()).hexdigest(), "card": card_hash(sim)}
    screen = [0]
    sim.watch(sim.m.addr(PATTERN_UPDATE), lambda mm: screen.__setitem__(0, screen[0] + 1))
    out = {"uncalibrated": sim.uncalibrated(), "setup_wall_s": round(time.perf_counter() - t0, 2)}

    t1 = time.perf_counter()
    k = sim.kernel
    sim.run(seconds=open_limit_s, stop=lambda: sim.booted)
    out["boot"] = {"virtual_s": round(k.vt / 1e9, 3), "wall_s": round(time.perf_counter() - t1, 2),
                   "booted": sim.booted, "free_heap": sim.free_heap() if sim.booted else None,
                   "max_stack": sim.max_stack(), "irqs_during_init": k.c.irqs_out}
    sim.run(seconds=open_limit_s, stop=lambda: screen[0] > 5)
    out["open"] = {"virtual_s": round(k.vt / 1e9, 3), "wall_s": round(time.perf_counter() - t1, 2),
                   "pattern_screen": screen[0] > 5, "booted": sim.booted, "free_heap": None,
                   "max_stack": sim.max_stack(), "failures": sim.failures[:5]}
    if screen[0] <= 5 or sim.failures:
        out["result"] = "FAILED: the Pattern screen never came up" if screen[0] <= 5 else "FAILED"
        return sim, record, out
    out["open"]["free_heap"] = sim.free_heap()

    vt0, blocks0, notes0 = k.vt, sim.blocks, sim.notes
    wall0 = time.perf_counter()
    sim.press(PLAY)
    sim.run(seconds=play_s)
    wall = time.perf_counter() - wall0
    vs = (k.vt - vt0) / 1e9
    out["play"] = {"virtual_s": round(vs, 3), "wall_s": round(wall, 2),
                   "wall_s_per_virtual_s": round(wall / vs, 2) if vs else None,
                   "blocks": sim.blocks - blocks0, "notes": sim.notes - notes0}
    out["final"] = sim.report()
    out["final"]["free_heap"] = sim.free_heap()
    out["state"] = sim.state_hash()
    ok = sim.notes > notes0 and not sim.failures and not sim.invariants
    out["result"] = "ok" if ok else "FAILED: no notes, a failure or a broken invariant"
    return sim, record, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("axf", nargs="?")
    ap.add_argument("project", nargs="?")
    ap.add_argument("--play", type=float, default=1.0, help="virtual seconds of Play")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--phase-ns", type=int, default=0)
    ap.add_argument("--jitter-ns", type=int, default=0)
    ap.add_argument("--no-skip", action="store_true")
    ap.add_argument("--no-input-interrupt", action="store_true")
    ap.add_argument("--interrupts-after-init", action="store_true")
    ap.add_argument("--log", type=Path, help="the event log, as JSON lines")
    ap.add_argument("--record", type=Path, help="write the run's parameters here, for --replay")
    ap.add_argument("--replay", type=Path, help="rerun a recorded run and compare its log hash")
    args = ap.parse_args()

    if args.replay:
        rec = json.loads(args.replay.read_text())
        params = Params.from_json(rec["params"])
        axf, project, play_s = rec["axf"], rec["project"], rec["play_s"]
    else:
        if not (args.axf and args.project):
            ap.error("give a firmware and a project, or --replay")
        params = Params(seed=args.seed, phase_ns=args.phase_ns, jitter_ns=args.jitter_ns, skip=not args.no_skip,
                        input_interrupt=not args.no_input_interrupt,
                        interrupts_during_init=not args.interrupts_after_init)
        axf, project, play_s = args.axf, args.project, args.play
    print("UNCALIBRATED: every timing value is a placeholder; nothing here is the unit's timing", flush=True)
    try:
        sim, record, out = run(axf, project, params, play_s, args.log)
    except RuntimeError as e:
        print(json.dumps({"result": f"STOPPED: {e}"}), flush=True)
        return 1
    if args.replay:
        rec = json.loads(args.replay.read_text())
        same = rec.get("log_hash") == out.get("final", {}).get("log_hash")
        out["replay"] = {"recorded": rec.get("log_hash"), "same": same}
        if not same:
            out["result"] = "FAILED: the replay's log differs (a determinism bug)"
    sim.kernel.log.close()          # the interrupt watchdog thread keeps sim alive at exit, so close it here
    if args.record and "final" in out:
        record["log_hash"] = out["final"]["log_hash"]
        args.record.write_text(json.dumps(record, indent=1))
    print(json.dumps(out, indent=1), flush=True)
    return 0 if out["result"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
