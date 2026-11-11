#!/usr/bin/env python3
"""Measure the wall-clock time of the programs in tests/bench-wall.

Each program in tests/bench-wall/neura/ is built once with `neura build` and
run with `neura run --stats` several times; the result is the median wall
time of the process, the program's output, and the `steps` and `peak heap
cells` it reports. When `javac` and `java` are on PATH, the counterpart in
tests/bench-wall/java/ is compiled and run the same number of times with the
same size, and its output must match the Neura program's byte for byte.
Without them the run is Neura-only and says so.

Every program's first line names two sizes, `// sizes: quick <n> full <n>`.
--quick uses the first, which keeps each program to a few seconds on the
reference evaluator; the default uses the second. The method is described in
docs/Benchmarks.md.

    scripts/bench-wall.py --quick
    scripts/bench-wall.py --neura build/neura --runs 7 --only nbody,sieve
    scripts/bench-wall.py --size sieve=1000000 --out-dir /tmp/bench

Results go to <out-dir>/bench-wall.json and <out-dir>/bench-wall.md.
"""

import argparse
import datetime
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "tests" / "bench-wall"
SIZES = re.compile(r"^// sizes: quick (\d+) full (\d+)$", re.MULTILINE)
STATS = re.compile(r"^stats: steps (\d+), peak heap cells (\d+)", re.MULTILINE)


def java_class(name):
    """The Java class of a program: its name in PascalCase."""
    special = {"nbody": "NBody", "sha256": "Sha256"}
    return special.get(name, "".join(part.capitalize() for part in name.split("-")))


def timed(command, cwd):
    started = time.perf_counter()
    process = subprocess.run(command, cwd=cwd, capture_output=True)
    return time.perf_counter() - started, process


def fail(message):
    sys.exit(f"bench-wall: {message}")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--neura", default=str(ROOT / "build" / "neura"), help="the neura executable")
    parser.add_argument("--runs", type=int, default=5, help="runs per program and language (default 5)")
    parser.add_argument("--quick", action="store_true", help="use each program's quick size")
    parser.add_argument("--only", help="comma-separated program names")
    parser.add_argument("--size", action="append", default=[], metavar="NAME=N",
                        help="override one program's size; repeatable")
    parser.add_argument("--max-steps", type=int, default=10**12,
                        help="--max-steps for neura run (default 10^12; neura's own default is 10^8)")
    parser.add_argument("--no-java", action="store_true", help="skip Java even when it is installed")
    parser.add_argument("--out-dir", default=str(ROOT / "build" / "bench-wall"),
                        help="where images, classes, and results go (default build/bench-wall)")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    return args


def programs(args):
    overrides = {}
    for item in args.size:
        name, _, value = item.partition("=")
        if not value.isdigit():
            fail(f"--size expects NAME=N, got {item!r}")
        overrides[name] = int(value)
    selected = set(args.only.split(",")) if args.only else None
    found = []
    for source in sorted((BENCH / "neura").glob("*.nra")):
        name = source.stem
        if selected is not None and name not in selected:
            continue
        match = SIZES.search(source.read_text())
        if not match:
            fail(f"{source} has no '// sizes: quick <n> full <n>' line")
        size = overrides.get(name, int(match.group(1) if args.quick else match.group(2)))
        found.append((name, source, size))
    if selected is not None:
        missing = selected - {name for name, _, _ in found}
        if missing:
            fail(f"no such program: {', '.join(sorted(missing))}")
    unknown = set(overrides) - {name for name, _, _ in found}
    if unknown:
        fail(f"--size names no selected program: {', '.join(sorted(unknown))}")
    return found


def find_java(args):
    if args.no_java:
        return None, "skipped (--no-java)"
    javac, java = shutil.which("javac"), shutil.which("java")
    if not javac or not java:
        return None, "not run: javac and java are not both on PATH"
    version = subprocess.run([java, "-version"], capture_output=True, text=True)
    first = (version.stderr or version.stdout).strip().splitlines()
    return (javac, java), first[0] if first else "unknown version"


def median_run(command, cwd, runs, label):
    times = []
    output = None
    stats = None
    for index in range(runs):
        elapsed, process = timed(command, cwd)
        if process.returncode != 0:
            fail(f"{label} exited {process.returncode}\n{process.stdout.decode(errors='replace')}"
                 f"{process.stderr.decode(errors='replace')}")
        if output is None:
            output = process.stdout
        elif process.stdout != output:
            fail(f"{label} printed different output on run {index + 1}")
        stderr = process.stderr.decode(errors="replace")
        match = STATS.search(stderr)
        if match:
            current = (int(match.group(1)), int(match.group(2)))
            if stats is not None and current != stats:
                fail(f"{label} reported different stats on run {index + 1}")
            stats = current
        times.append(elapsed)
    return statistics.median(times), times, output, stats


def main():
    args = parse_args()
    neura = Path(args.neura).resolve()
    if not neura.exists():
        fail(f"{neura} does not exist; build it first or pass --neura")
    out = Path(args.out_dir).resolve()
    images = out / "images"
    classes = out / "classes"
    images.mkdir(parents=True, exist_ok=True)
    selected = programs(args)
    java, java_note = find_java(args)
    print(f"neura: {neura}")
    print(f"java: {java_note}")
    print(f"mode: {'quick' if args.quick else 'full'}, {args.runs} runs, median wall time")
    if java:
        classes.mkdir(parents=True, exist_ok=True)
        sources = [str(BENCH / "java" / f"{java_class(name)}.java") for name, _, _ in selected]
        compiled = subprocess.run([java[0], "-d", str(classes), *sources], capture_output=True, text=True)
        if compiled.returncode != 0:
            fail(f"javac failed\n{compiled.stdout}{compiled.stderr}")

    results = []
    for name, source, size in selected:
        image = images / f"{name}.nvr"
        built = subprocess.run([str(neura), "build", str(source), "-o", str(image)], capture_output=True, text=True)
        if built.returncode != 0:
            fail(f"{name}: neura build failed\n{built.stderr}")
        command = [str(neura), "run", "--stats", "--max-steps", str(args.max_steps), str(image), "--", str(size)]
        seconds, times, output, stats = median_run(command, out, args.runs, f"{name} (neura)")
        entry = {
            "name": name,
            "size": size,
            "neura": {"median_seconds": seconds, "seconds": times,
                      "steps": stats[0] if stats else None, "peak_heap_cells": stats[1] if stats else None},
            "output": output.decode(errors="replace"),
        }
        line = f"{name:16} size {size:>8}  neura {seconds:8.3f} s"
        if stats:
            line += f"  steps {stats[0]:>12}"
        if java:
            java_command = [java[1], "-cp", str(classes), java_class(name), str(size)]
            java_seconds, java_times, java_output, _ = median_run(java_command, out, args.runs, f"{name} (java)")
            entry["java"] = {"median_seconds": java_seconds, "seconds": java_times}
            entry["outputs_match"] = java_output == output
            line += f"  java {java_seconds:8.3f} s  ratio {seconds / java_seconds:8.1f}"
            if java_output != output:
                line += "  OUTPUT DIFFERS"
        print(line, flush=True)
        results.append(entry)

    report = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "mode": "quick" if args.quick else "full",
        "runs": args.runs,
        "max_steps": args.max_steps,
        "neura": str(neura),
        "neura_version": subprocess.run([str(neura), "--version"], capture_output=True, text=True).stdout.strip(),
        "java": java_note,
        "machine": {"system": platform.system(), "machine": platform.machine(), "cpus": os.cpu_count(),
                    "python": platform.python_version()},
        "results": results,
    }
    (out / "bench-wall.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "bench-wall.md").write_text(markdown(report))
    print(f"wrote {out / 'bench-wall.json'} and {out / 'bench-wall.md'}")
    if any(entry.get("outputs_match") is False for entry in results):
        fail("a Java program's output differs from its Neura program's")
    return 0


def markdown(report):
    java = any("java" in entry for entry in report["results"])
    lines = [
        f"Wall-clock benchmarks, {report['mode']} sizes, median of {report['runs']} runs "
        f"({report['date']}, {report['machine']['system']} {report['machine']['machine']}, "
        f"{report['machine']['cpus']} CPUs).",
        "",
        f"- Neura: {report['neura_version'] or report['neura']}",
        f"- Java: {report['java']}",
        "",
    ]
    if java:
        lines += ["| Program | Size | Neura (s) | Steps | Java (s) | Neura / Java | Same output |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | --- |"]
    else:
        lines += ["| Program | Size | Neura (s) | Steps | Peak heap cells |", "| --- | ---: | ---: | ---: | ---: |"]
    for entry in report["results"]:
        neura = entry["neura"]
        steps = "" if neura["steps"] is None else str(neura["steps"])
        row = f"| {entry['name']} | {entry['size']} | {neura['median_seconds']:.3f} | {steps} |"
        if java:
            if "java" in entry:
                seconds = entry["java"]["median_seconds"]
                same = "yes" if entry["outputs_match"] else "no"
                row += f" {seconds:.3f} | {neura['median_seconds'] / seconds:.1f} | {same} |"
            else:
                row += " | | |"
        else:
            cells = "" if neura["peak_heap_cells"] is None else str(neura["peak_heap_cells"])
            row += f" {cells} |"
        lines.append(row)
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.exit(main())
