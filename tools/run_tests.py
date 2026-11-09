#!/usr/bin/env python3
"""Run the mods' emulator tests at a chosen depth, in parallel, skipping what already passed.

    python tools/run_tests.py <mods.axf> <reference.axf> [--tier quick|release|deep] [--rig 120_rig.elf]
        [--diag diagnostic.axf] [--project "<project>"] [--jobs N] [--no-cache] [--only name]

Carried over from Modular-Trackular's runner, for CE: there are no patch mixes here, one firmware to test.

  quick    Each mod's tests, the UI and the graph. A few minutes.
  release  Quick, plus what no firmware goes to the device without: the real boot, Open and Play with
           the audio interrupt firing inside main-loop code, the worst-case project, the cost limits,
           long stress and the lockup recorder. --project is a real project to build the test projects
           from (read only) and --diag the diagnostic build. About an hour.
  deep     Release, with more seeds and longer runs, and nothing from the cache. Hours.

The cache (build/test_cache.json, local only): a job's key is the SHA-256 of the firmware files it runs on,
its arguments, and the code of the test and every module of tools/ it imports. A key that passed before
doesn't run again.

What these can't tell you, whatever they say: anything that depends on real time (overload, what a long
interrupt does to the screen, the card and the buttons), the real screen, the card's own timing, or the
cache. That's what docs/mt/DEVICE_TEST_LADDER.md is for. A pass here isn't "safe on the unit".
"""
import argparse
import ast
import concurrent.futures
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EMU = ROOT / "tools" / "emu"
sys.path.insert(0, str(EMU))
import cpu_limit  # noqa: E402
CACHE = ROOT / "build" / "test_cache.json"
OUT = ROOT / "build" / "test_out"


def imported_files(path, seen=None):
    """The file and every module of tools/ it imports, at any depth."""
    seen = set() if seen is None else seen
    if path in seen:
        return seen
    seen.add(path)
    for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
        names = []
        if isinstance(node, ast.Import):
            names = [a.name for a in node.names]
        elif isinstance(node, ast.ImportFrom) and node.module:
            names = [node.module]
        for name in names:
            for folder in (EMU, ROOT / "tools"):
                f = folder / (name.split(".")[0] + ".py")
                if f.exists():
                    imported_files(f, seen)
    return seen


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Job:
    def __init__(self, name, script, *args, inputs=()):
        self.name = name
        self.script = script
        self.args = [str(a) for a in args]
        self.inputs = [Path(p) for p in inputs]

    def key(self):
        parts = [self.name, " ".join(Path(a).name if Path(a).exists() else a for a in self.args)]
        for f in sorted(imported_files(self.script)):
            parts.append(sha(f.read_bytes()))
        for f in self.inputs:
            if f.is_dir():
                for g in sorted(p for p in f.rglob("*") if p.is_file()):
                    parts.append(sha(g.read_bytes()))
            else:
                parts.append(sha(f.read_bytes()))
        return sha("\n".join(parts).encode())

    def run(self):
        OUT.mkdir(parents=True, exist_ok=True)
        log = OUT / (self.name.replace(" ", "_") + ".log")
        started = time.time()
        with open(log, "w", encoding="utf-8") as out:
            code = subprocess.run([sys.executable, str(self.script), *self.args], stdout=out,
                                  stderr=subprocess.STDOUT, cwd=ROOT).returncode
        return code, time.time() - started, log


def jobs_for(args, projects):
    mods, ref = args.mods, args.reference
    both = (mods, ref)
    e = EMU
    jobs = [
        Job("step0", e / "test_step0.py", mods, inputs=[mods]),
        Job("colorsong", e / "test_colorsong.py", mods, ref, inputs=both),
        Job("humanize", e / "test_humanize.py", mods, inputs=[mods]),
        Job("swing", e / "test_swing.py", mods, ref, inputs=both),
        Job("tracklen", e / "test_tracklen.py", mods, ref, inputs=both),
        Job("copypattern", e / "test_copypattern.py", mods, ref, inputs=both),
        Job("songloop", e / "test_songloop.py", mods, ref, inputs=both),
        Job("sound rig", e / "test_sound_rig.py", mods, ref, inputs=both),
        Job("synths", e / "test_synths.py", mods, inputs=[mods]),
        Job("masterfx objects", e / "test_masterfx.py", mods, ref, *([args.rig] if args.rig else []),
            inputs=both + ((args.rig,) if args.rig else ())),
    ]
    for section in ("ui", "graph", "stress", "debounce", "project"):
        jobs.append(Job(f"masterfx {section}", e / "test_masterfx_rig.py", mods, ref, section, inputs=both))
    if args.tier == "quick":
        return jobs

    boot = e / "boot_rig.py"
    # Each interrupt renders a whole block, so closer gaps cost time and nothing a device does: it takes
    # one per block. 100,1500 made a 300 block run about 9,500 blocks, and about an hour each. On the
    # worst project one main loop is long enough for about 220 interrupts even at 300,3000, so that
    # project gets fewer main loops: 20 of them is about 4,400 blocks, and 300 would be over 5 hours.
    play_gaps = "300,3000" if args.tier == "release" else "100,1500"
    worst_loops = {"held": 300, "worst": 20 if args.tier == "release" else 60}
    jobs.append(Job("masterfx play", e / "test_masterfx_rig.py", mods, ref, "play", inputs=both))
    seeds = (1,) if args.tier == "release" else (1, 2, 3, 4)
    blocks = 20000 if args.tier == "release" else 100000
    jobs.append(Job("masterfx long stress", e / "stress_masterfx.py", mods, "--blocks", blocks,
                    *[x for s in seeds for x in ("--seed", s)], inputs=[mods]))
    if args.diag:
        jobs.append(Job("lockup recorder", e / "test_lockup_recorder.py", args.diag, inputs=[args.diag]))
        if projects:
            jobs.append(Job("diagnostic build, worst case", boot, args.diag, projects["worst"], "--blocks", worst_loops["worst"],
                            "--preempt", "3000,120000", "--preempt-play", play_gaps,
                            inputs=[args.diag, projects["worst"]]))
    if projects:
        for label, folder in projects.items():
            if label == "tracklen":
                continue
            for firmware, axf in (("mods", mods), ("reference", ref)):
                jobs.append(Job(f"boot {label} {firmware}", boot, axf, folder, "--blocks", 300, inputs=[axf, folder]))
                jobs.append(Job(f"preempt {label} {firmware}", boot, axf, folder, "--blocks", worst_loops[label],
                                "--preempt", "3000,120000", "--preempt-play", play_gaps, inputs=[axf, folder]))
        jobs.append(Job("send leak", e / "test_send_leak.py", mods, ref, projects["held"],
                        inputs=[mods, ref, projects["held"]]))
        jobs.append(Job("cost", e / "test_bundle_cost.py", mods, ref, projects["worst"], inputs=[mods, ref, projects["worst"]]))
        short = projects["tracklen"]
        jobs.append(Job("boot tracklen", boot, mods, short, "--blocks", 300, "--expect-tracklen", inputs=[mods, short]))
        jobs.append(Job("preempt tracklen", boot, mods, short, "--blocks", worst_loops["worst"], "--expect-tracklen",
                        "--preempt", "3000,120000", "--preempt-play", play_gaps, inputs=[mods, short]))
        jobs.append(Job("cost tracklen", e / "test_bundle_cost.py", mods, ref, short, inputs=[mods, ref, short]))
    return jobs


def make_projects(source, parent):
    """The test projects, in a temporary folder: 8 voices held, and the worst case. Both with their effects.
    The third is the worst case with a track length on every track of every pattern, from a 1.2.0 tracklen.bin."""
    import make_masterfx_test_project as maker
    from test_tracklen import side_file
    short = maker.make(source, parent, "tracklen", keep_masterfx=True, worst=True)
    (short / "mods.bin").unlink(missing_ok=True)
    (short / "tracklen.bin").write_bytes(side_file([(p, {t: t % 7 + 1 for t in range(16)}) for p in range(1, 256)]))
    return {"held": maker.make(source, parent, "held", keep_masterfx=True),
            "worst": maker.make(source, parent, "worst", keep_masterfx=True, worst=True),
            "tracklen": short}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mods", type=Path)
    ap.add_argument("reference", type=Path)
    ap.add_argument("--tier", default="quick", choices=("quick", "release", "deep"))
    ap.add_argument("--rig", type=Path)
    ap.add_argument("--diag", type=Path)
    ap.add_argument("--project", type=Path)
    ap.add_argument("--jobs", type=int, default=cpu_limit.allowed())
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--only")
    args = ap.parse_args()
    cpu_limit.apply()
    args.jobs = min(args.jobs, cpu_limit.allowed())

    missing = []
    if args.tier != "quick":
        if not args.diag:
            missing.append("--diag: the lockup recorder wouldn't be tested")
        if not args.project:
            missing.append("--project: the real Play path wouldn't be tested")
    if missing:
        print("The release and deep tiers need:\n  " + "\n  ".join(missing))
        return 2

    cache = json.loads(CACHE.read_text()) if CACHE.exists() else {}
    use_cache = not args.no_cache and args.tier != "deep"
    temp = Path(tempfile.mkdtemp(prefix="mt_tests_"))
    try:
        projects = make_projects(args.project, temp) if args.project else {}
        jobs = jobs_for(args, projects)
        if args.only:
            jobs = [j for j in jobs if args.only in j.name]
        to_run = []
        for job in jobs:
            job.cache_key = job.key()
            if use_cache and cache.get(job.cache_key) == "pass":
                print(f"  cached   {job.name}")
            else:
                to_run.append(job)
        print(f"{len(to_run)} to run of {len(jobs)}, tier {args.tier}", flush=True)
        failed = []
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            running = {pool.submit(j.run): j for j in to_run}
            for done in concurrent.futures.as_completed(running):
                job = running[done]
                code, seconds, log = done.result()
                print(f"  {'pass' if code == 0 else 'FAIL'}     {job.name} ({seconds:.0f} s)", flush=True)
                if code == 0:
                    cache[job.cache_key] = "pass"
                    CACHE.parent.mkdir(parents=True, exist_ok=True)
                    CACHE.write_text(json.dumps(cache, indent=0))
                else:
                    failed.append((job.name, log))
    finally:
        shutil.rmtree(temp, ignore_errors=True)

    for name, log in failed:
        print(f"\n--- {name}: {log}")
        print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-12:]))
    print(f"\n{len(failed)} failed of {len(to_run)} run" if to_run else "\nAll cached.")
    print("A pass here says nothing about real time. See docs/mt/DEVICE_TEST_LADDER.md.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
