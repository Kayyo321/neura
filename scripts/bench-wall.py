#!/usr/bin/env python3
"""Measure the wall-clock time of the programs in tests/bench-wall.

Each program in tests/bench-wall/neura/ is built once with `neura build` and
run with `neura run --stats` several times; the result is the median wall
time of the process, the median of its peak resident memory, the program's
output, and the `steps` and `peak heap cells` it reports. When `javac` and
`java` are on PATH, the counterpart in tests/bench-wall/java/ is compiled
on its own and run the same number of times with the same size, and its
output must match the Neura program's byte for byte. Without them the run is
Neura-only and says so. The wall time of the one `neura build` and the one
`javac` of each program is recorded too.

Every program's first line names three sizes,
`// sizes: quick <n> full <n> long <n>`. --quick uses the first, which keeps
each program to a few seconds on the reference evaluator; the default uses
the second, about ten times the work; --long uses the third, which keeps the
Java program busy for a few seconds, so that start-up does not dominate it.
The method is described in docs/Benchmarks.md.

    scripts/bench-wall.py --quick
    scripts/bench-wall.py --neura build/neura --runs 7 --only nbody,sieve
    scripts/bench-wall.py --long --runs 3
    scripts/bench-wall.py --size sieve=1000000 --out-dir /tmp/bench

Results go to <out-dir>/bench-wall.json and <out-dir>/bench-wall.md. Peak
resident memory is the `ru_maxrss` of each measured process alone, read with
os.wait4 when it exits, in KiB in the JSON (`median_peak_rss_kib`) and in MiB
in the table.
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
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "tests" / "bench-wall"
SIZES = re.compile(r"^// sizes: quick (\d+) full (\d+) long (\d+)$", re.MULTILINE)
SIZES_FORMAT = "// sizes: quick <n> full <n> long <n>"
MODES = ("quick", "full", "long")
STATS = re.compile(r"^stats: steps (\d+), peak heap cells (\d+)", re.MULTILINE)


def java_class(name):
    """The Java class of a program: its name in PascalCase."""
    special = {"nbody": "NBody", "sha256": "Sha256"}
    return special.get(name, "".join(part.capitalize() for part in name.split("-")))


def timed(command, cwd):
    """Runs `command` to completion: its wall time, the completed process,
    and its peak resident memory in KiB. The memory is the child's own
    ru_maxrss from os.wait4, not a maximum over every child so far."""
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        started = time.perf_counter()
        child = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr)
        _, status, usage = os.wait4(child.pid, 0)
        elapsed = time.perf_counter() - started
        child.returncode = os.waitstatus_to_exitcode(status)
        stdout.seek(0)
        stderr.seek(0)
        process = subprocess.CompletedProcess(command, child.returncode, stdout.read(), stderr.read())
    # Linux reports ru_maxrss in KiB, macOS in bytes.
    rss_kib = usage.ru_maxrss // 1024 if sys.platform == "darwin" else usage.ru_maxrss
    return elapsed, process, rss_kib


def fail(message):
    sys.exit(f"bench-wall: {message}")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--neura", default=str(ROOT / "build" / "neura"), help="the neura executable")
    parser.add_argument("--runs", type=int, default=5, help="runs per program and language (default 5)")
    sizes = parser.add_mutually_exclusive_group()
    sizes.add_argument("--quick", action="store_true", help="use each program's quick size")
    sizes.add_argument("--long", action="store_true", help="use each program's long size")
    parser.add_argument("--only", help="comma-separated program names")
    parser.add_argument("--size", action="append", default=[], metavar="NAME=N",
                        help="override one program's size; repeatable")
    parser.add_argument("--max-steps", type=int, default=10**12,
                        help="--max-steps for neura run (default 10^12; neura's own default is 10^8)")
    parser.add_argument("--max-heap-cells", type=int, default=2**32,
                        help="--max-heap-cells for neura run (default 2^32; neura's own default is 2^26, which "
                             "some long sizes exceed)")
    parser.add_argument("--no-java", action="store_true", help="skip Java even when it is installed")
    parser.add_argument("--run-option", action="append", default=[], metavar="OPTION",
                        help="pass OPTION to neura run, for example --run-option=--tier=0 (repeatable)")
    parser.add_argument("--compare-tiers", action="store_true",
                        help="run each program at --tier=0, --tier=1, and --tier=2 (regions interpreted, and "
                             "compiled with --jit=on where this machine can) instead of comparing with Java, and "
                             "report the share of steps tiers 1 and 2 ran (docs/runtime/NVM.md, \"Execution tiers\")")
    parser.add_argument("--out-dir", default=str(ROOT / "build" / "bench-wall"),
                        help="where images, classes, and results go (default build/bench-wall)")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    args.mode = "quick" if args.quick else "long" if args.long else "full"
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
            fail(f"{source} has no '{SIZES_FORMAT}' line")
        size = overrides.get(name, int(match.group(MODES.index(args.mode) + 1)))
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
    """Runs `command` `runs` times: the median wall time, every wall time,
    the output, the stats, and every peak resident memory in KiB."""
    times = []
    memory = []
    output = None
    stats = None
    for index in range(runs):
        elapsed, process, rss_kib = timed(command, cwd)
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
        memory.append(rss_kib)
    return statistics.median(times), times, output, stats, memory


def memory_fields(memory):
    """The JSON fields of the peak resident memory of a set of runs."""
    return {"median_peak_rss_kib": statistics.median(memory), "peak_rss_kib": memory}


def timed_build(command, cwd, label):
    """Runs one build command: its wall time in seconds."""
    elapsed, process, _ = timed(command, cwd)
    if process.returncode != 0:
        fail(f"{label} failed\n{process.stdout.decode(errors='replace')}{process.stderr.decode(errors='replace')}")
    return elapsed


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
    java, java_note = (None, "not run: tiers compared") if args.compare_tiers else find_java(args)
    print(f"neura: {neura}")
    print(f"java: {java_note}")
    print(f"mode: {args.mode}, {args.runs} runs, median wall time")
    if java:
        classes.mkdir(parents=True, exist_ok=True)

    results = []
    native = None
    for name, source, size in selected:
        image = images / f"{name}.nvr"
        build_seconds = timed_build([str(neura), "build", str(source), "-o", str(image)], out,
                                    f"{name}: neura build")
        command = [str(neura), "run", "--stats", "--max-steps", str(args.max_steps), "--max-heap-cells",
                   str(args.max_heap_cells), *args.run_option, str(image), "--", str(size)]
        if args.compare_tiers:
            if native is None:
                native = native_available(neura, out, image)
                print(f"native code: {'compared (--jit=on)' if native else 'not available here'}", flush=True)
            entry = compare_tiers(command, out, args.runs, name, size, native)
            entry["build_seconds"] = build_seconds
            results.append(entry)
            continue
        seconds, times, output, stats, memory = median_run(command, out, args.runs, f"{name} (neura)")
        entry = {
            "name": name,
            "size": size,
            "neura": {"median_seconds": seconds, "seconds": times,
                      "steps": stats[0] if stats else None, "peak_heap_cells": stats[1] if stats else None,
                      **memory_fields(memory), "build_seconds": build_seconds},
            "output": output.decode(errors="replace"),
        }
        line = f"{name:16} size {size:>10}  neura {seconds:8.3f} s {mib(memory_fields(memory)):>7} MiB"
        if stats:
            line += f"  steps {stats[0]:>14}"
        if java:
            # Each program is compiled on its own, so the time is that program's.
            java_build = timed_build([java[0], "-d", str(classes), str(BENCH / "java" / f"{java_class(name)}.java")],
                                     out, f"{name}: javac")
            java_command = [java[1], "-cp", str(classes), java_class(name), str(size)]
            java_seconds, java_times, java_output, _, java_memory = median_run(java_command, out, args.runs,
                                                                               f"{name} (java)")
            entry["java"] = {"median_seconds": java_seconds, "seconds": java_times, **memory_fields(java_memory),
                             "build_seconds": java_build}
            entry["outputs_match"] = java_output == output
            line += (f"  java {java_seconds:8.3f} s {mib(memory_fields(java_memory)):>7} MiB  "
                     f"ratio {seconds / java_seconds:8.1f}")
            if java_output != output:
                line += "  OUTPUT DIFFERS"
        print(line, flush=True)
        results.append(entry)

    report = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "mode": args.mode,
        "runs": args.runs,
        "max_steps": args.max_steps,
        "max_heap_cells": args.max_heap_cells,
        "neura": str(neura),
        "neura_version": subprocess.run([str(neura), "--version"], capture_output=True, text=True).stdout.strip(),
        "java": java_note,
        "machine": {"system": platform.system(), "machine": platform.machine(), "cpus": os.cpu_count(),
                    "python": platform.python_version()},
        "results": results,
    }
    (out / "bench-wall.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "bench-wall.md").write_text(tier_markdown(report) if args.compare_tiers else markdown(report))
    print(f"wrote {out / 'bench-wall.json'} and {out / 'bench-wall.md'}")
    if any(entry.get("outputs_match") is False for entry in results):
        fail("a Java program's output differs from its Neura program's")
    return 0


TIER_STEPS = re.compile(r"tier: steps (\d+): tier 2 ([0-9.]+)% .*?, tier 1 ([0-9.]+)%")
JIT_LINE = re.compile(r"tier: jit on: (\d+) regions compiled \((\d+) bytes of code\)")


def compare_tiers(command, cwd, runs, name, size, native):
    """Times one program at tiers 0 and 1, at tier 2 with every region on
    the region interpreter (--jit=off), and, when `native`, at tier 2 with
    regions compiled to native code (--jit=on; docs/runtime/NVM.md, "Native
    compilation"). All must print the same output and report the same steps
    and peak heap cells."""
    at = command.index("run") + 1
    configurations = [("tier0", ["--tier=0"]), ("tier1", ["--tier=1"]), ("tier2", ["--tier=2", "--jit=off"])]
    if native:
        configurations.append(("jit", ["--tier=2", "--jit=on"]))
    timed_runs = {}
    for key, options in configurations:
        timed_runs[key] = median_run(command[:at] + options + command[at:], cwd, runs, f"{name} ({key})")
    seconds = {key: timed_runs[key][0] for key in timed_runs}
    for key in timed_runs:
        if timed_runs[key][2] != timed_runs["tier0"][2] or timed_runs[key][3] != timed_runs["tier0"][3]:
            fail(f"{name}: tier 0 and {key} differ in output or stats")
    shares = {}
    compiled = None
    for key, options in configurations[1:]:
        stats = command[:at] + options + ["--tier-stats"] + command[at:]
        text = subprocess.run(stats, cwd=cwd, capture_output=True).stderr.decode(errors="replace")
        match = TIER_STEPS.search(text)
        shares[key] = (float(match.group(2)), float(match.group(3))) if match else (None, None)
        if key == "jit":
            jit = JIT_LINE.search(text)
            compiled = (int(jit.group(1)), int(jit.group(2))) if jit else None
    line = (f"{name:16} size {size:>8}  tier 0 {seconds['tier0']:8.3f} s  tier 1 {seconds['tier1']:8.3f} s  "
            f"tier 2 {seconds['tier2']:8.3f} s")
    if native:
        line += f"  jit {seconds['jit']:8.3f} s  jit speedup over tier 2 {seconds['tier2'] / seconds['jit']:5.2f}"
    line += (f"  speedup {seconds['tier0'] / seconds['tier1']:5.2f} {seconds['tier0'] / seconds['tier2']:5.2f}  "
             f"tier 1 steps {shares['tier1'][1]}%  tier 2 steps in regions {shares['tier2'][0]}%")
    print(line, flush=True)
    stats0 = timed_runs["tier0"][3]
    entry = {"name": name, "size": size, "steps": stats0[0] if stats0 else None,
             "tier0": {"median_seconds": seconds["tier0"], "seconds": timed_runs["tier0"][1]},
             "tier1": {"median_seconds": seconds["tier1"], "seconds": timed_runs["tier1"][1],
                       "steps_percent": shares["tier1"][1]},
             "tier2": {"median_seconds": seconds["tier2"], "seconds": timed_runs["tier2"][1],
                       "region_percent": shares["tier2"][0], "tier1_percent": shares["tier2"][1]},
             "output": timed_runs["tier0"][2].decode(errors="replace")}
    if native:
        entry["jit"] = {"median_seconds": seconds["jit"], "seconds": timed_runs["jit"][1],
                        "region_percent": shares["jit"][0],
                        "regions_compiled": compiled[0] if compiled else None,
                        "code_bytes": compiled[1] if compiled else None}
    for key in timed_runs:
        entry[key].update(memory_fields(timed_runs[key][4]))
    return entry


def mib(fields):
    """The median peak resident memory of `memory_fields` in MiB, as text."""
    return f"{fields['median_peak_rss_kib'] / 1024:.1f}"


def native_available(neura, cwd, image):
    """Whether `neura run --jit=on` can run native code on this machine."""
    process = subprocess.run([str(neura), "run", "--jit=on", "--max-steps", "1", str(image)], cwd=cwd,
                             capture_output=True)
    return b"--jit=on" not in process.stderr


def tier_markdown(report):
    native = any("jit" in entry for entry in report["results"])
    lines = [
        f"Tiers 0, 1, and 2, {report['mode']} sizes, median of {report['runs']} runs "
        f"({report['date']}, {report['machine']['system']} {report['machine']['machine']}, "
        f"{report['machine']['cpus']} CPUs). Tier 2 runs regions on the region interpreter"
        + ("; Native runs them as native code (--jit=on)." if native else "."),
        "",
        f"- Neura: {report['neura_version'] or report['neura']}",
        "",
    ]
    if native:
        lines += ["| Program | Size | Steps | Tier 0 (s) | Tier 1 (s) | Tier 2 (s) | Native (s) | Speedup 1 | "
                  "Speedup 2 | Speedup native | Native / tier 2 | Steps in regions |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    else:
        lines += ["| Program | Size | Steps | Tier 0 (s) | Tier 1 (s) | Tier 2 (s) | Speedup 1 | Speedup 2 | "
                  "Steps on tier 1 | Steps in regions |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    percent = lambda value: "" if value is None else f"{value:.1f}%"
    for entry in report["results"]:
        t0 = entry["tier0"]["median_seconds"]
        t1 = entry["tier1"]["median_seconds"]
        t2 = entry["tier2"]["median_seconds"]
        if native and "jit" in entry:
            tj = entry["jit"]["median_seconds"]
            lines.append(f"| {entry['name']} | {entry['size']} | {entry['steps']} | {t0:.3f} | {t1:.3f} | {t2:.3f} | "
                         f"{tj:.3f} | {t0 / t1:.2f} | {t0 / t2:.2f} | {t0 / tj:.2f} | {tj / t2:.2f} | "
                         f"{percent(entry['tier2']['region_percent'])} |")
        else:
            lines.append(f"| {entry['name']} | {entry['size']} | {entry['steps']} | {t0:.3f} | {t1:.3f} | {t2:.3f} | "
                         f"{t0 / t1:.2f} | {t0 / t2:.2f} | {percent(entry['tier1']['steps_percent'])} | "
                         f"{percent(entry['tier2']['region_percent'])} |")
    return "\n".join(lines) + "\n"


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
        lines += ["| Program | Size | Neura (s) | Steps | Java (s) | Neura / Java | Same output | Neura RSS (MiB) | "
                  "Java RSS (MiB) | Neura build (s) | Java build (s) |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: |"]
    else:
        lines += ["| Program | Size | Neura (s) | Steps | Peak heap cells | Neura RSS (MiB) | Neura build (s) |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for entry in report["results"]:
        neura = entry["neura"]
        steps = "" if neura["steps"] is None else str(neura["steps"])
        row = f"| {entry['name']} | {entry['size']} | {neura['median_seconds']:.3f} | {steps} |"
        if java:
            if "java" in entry:
                seconds = entry["java"]["median_seconds"]
                same = "yes" if entry["outputs_match"] else "no"
                row += (f" {seconds:.3f} | {neura['median_seconds'] / seconds:.1f} | {same} | {mib(neura)} | "
                        f"{mib(entry['java'])} | {neura['build_seconds']:.3f} | {entry['java']['build_seconds']:.3f} |")
            else:
                row += f" | | | {mib(neura)} | | {neura['build_seconds']:.3f} | |"
        else:
            cells = "" if neura["peak_heap_cells"] is None else str(neura["peak_heap_cells"])
            row += f" {cells} | {mib(neura)} | {neura['build_seconds']:.3f} |"
        lines.append(row)
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.exit(main())
