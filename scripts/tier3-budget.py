#!/usr/bin/env python3
"""The budget gate of tier 3 (docs/runtime/NVM.md, "Execution tiers"): the
machine instructions a benchmark spends per block step, in today's default
mode and with NEURA_TIER3=native, against the budget table of the tier 3
design.

For each of the 17 programs of tests/bench-wall/neura, the script builds the
image with the binary under test and counts its instructions under callgrind
at two sizes: the quick size of the program's `// sizes:` line and the
second size given for it in a sizes file (lines `PROGRAM SIZE`), each run as

    valgrind --tool=callgrind neura run --stats --max-steps 1000000000000 \\
        --max-heap-cells 4294967296 IMAGE -- SIZE

with NEURA_GPU=off, once in the default mode and once with
NEURA_TIER3=native (the measurement of the design's raw/measure.sh and
raw/measure2.sh). The marginal cost is the difference in instructions
divided by the difference in `stats: steps`: it cancels loading, proving,
compiling and warm-up, and is the steady-state cost per step.

The marginal cost with NEURA_TIER3=native is compared with the budget table
(the `## A. Summary` table of the budget file: the columns "today Ir/step"
and the stage's column, 3.2 by default). A program passes when its cost is
below today's and at most the stage's figure times 1.1.

With --cpu (the default; --no-cpu skips it), each program also runs at its
long size three times in each mode, without valgrind, and the median CPU time
(user + sys of the child) is reported.

Output: a Markdown table on standard output (and in OUT/budget.md), the
callgrind files and run outputs in OUT. Exit status 1 when a program fails
the gate.

    scripts/tier3-budget.py --neura build/neura --budget wo3/budget.md --sizes wo3/raw/sizes2.txt --out build/budget
    scripts/tier3-budget.py ... --programs collatz,sieve --no-cpu
"""

import argparse
import concurrent.futures
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCHMARKS = ROOT / "tests" / "bench-wall" / "neura"
LIMITS = ["--max-steps", "1000000000000", "--max-heap-cells", "4294967296"]
# "default" is the behavior before tier 3 became the default (NEURA_TIER3=off).
MODES = {"default": {"NEURA_TIER3": "off"}, "native": {"NEURA_TIER3": "native"}}


def sizes_of(source):
    """The quick and long sizes of a benchmark from its `// sizes:` line."""
    text = source.read_text()
    found = re.search(r"^// sizes: quick (\d+) full (\d+) long (\d+)", text, re.M)
    if not found:
        raise SystemExit(f"{source}: no '// sizes:' line")
    return found.group(1), found.group(3)


def second_sizes(path):
    sizes = {}
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) == 2:
            sizes[parts[0]] = parts[1]
    return sizes


def budget_table(path, stage):
    """{program: (today, stage figure)} from the budget file's summary table."""
    lines = Path(path).read_text().splitlines()
    header = None
    table = {}
    for line in lines:
        if not line.startswith("|"):
            if header is not None and table:
                break
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if header is None:
            if cells and cells[0] == "program" and "today Ir/step" in cells:
                header = cells
            continue
        if set(cells[0]) <= set("-: "):
            continue
        row = dict(zip(header, cells))
        table[row["program"]] = (float(row["today Ir/step"]), float(row[stage]))
    if not table:
        raise SystemExit(f"{path}: no summary table with 'today Ir/step' and '{stage}'")
    return table


def environment(mode):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("NEURA_TIER") and key not in ("NEURA_JIT", "NEURA_RUN_OPTIONS")}
    env["NEURA_GPU"] = "off"
    env.update(MODES[mode])
    return env


def callgrind(neura, valgrind, image, size, mode, out):
    """(instructions, steps) of one run under callgrind."""
    stem = out / f"{image.stem}.{mode}.{size}"
    command = [valgrind, "--tool=callgrind", f"--callgrind-out-file={stem}.cg", str(neura), "run", "--stats",
               *LIMITS, str(image), "--", size]
    result = subprocess.run(command, env=environment(mode), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            text=True, check=False)
    (stem.parent / f"{stem.name}.err").write_text(result.stderr)
    steps = re.search(r"^stats: steps (\d+)", result.stderr, re.M)
    total = None
    for line in (stem.parent / f"{stem.name}.cg").read_text().splitlines():
        if line.startswith("summary:") or line.startswith("totals:"):
            total = int(line.split()[1])
            break
    if result.returncode != 0 or steps is None or total is None:
        raise RuntimeError(f"{image.stem} {mode} {size}: the run failed (status {result.returncode}); see {stem}.err")
    return total, int(steps.group(1))


def cpu_time(neura, image, size, mode):
    """User + sys seconds of one run, from the child's resource usage."""
    process = subprocess.Popen([str(neura), "run", *LIMITS, str(image), "--", size], env=environment(mode),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _, status, usage = os.wait4(process.pid, 0)
    if status != 0:
        raise RuntimeError(f"{image.stem} {mode} {size}: status {status}")
    return usage.ru_utime + usage.ru_stime


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--neura", required=True, help="the binary under test")
    parser.add_argument("--budget", required=True, help="the budget file (budget.md, section A)")
    parser.add_argument("--sizes", required=True, help="the second sizes, lines 'PROGRAM SIZE'")
    parser.add_argument("--out", required=True, help="a directory for images, callgrind files and the table")
    parser.add_argument("--stage", default="3.2", help="the budget column the gate holds native code to")
    parser.add_argument("--programs", help="a comma-separated subset of the programs")
    parser.add_argument("--jobs", type=int, default=3, help="callgrind runs at once (default 3)")
    parser.add_argument("--valgrind", default=shutil.which("valgrind") or "valgrind")
    parser.add_argument("--cpu", action=argparse.BooleanOptionalAction, default=True,
                        help="also time the long size, median of 3, both modes")
    args = parser.parse_args()

    neura = Path(args.neura).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    table = budget_table(args.budget, args.stage)
    second = second_sizes(args.sizes)
    programs = sorted(p.stem for p in BENCHMARKS.glob("*.nra"))
    if args.programs:
        wanted = args.programs.split(",")
        programs = [p for p in programs if p in wanted]
    for program in programs:
        if program not in table or program not in second:
            raise SystemExit(f"{program}: not in the budget table or the sizes file")

    images = {}
    for program in programs:
        image = out / f"{program}.nvr"
        subprocess.run([str(neura), "build", str(BENCHMARKS / f"{program}.nra"), "-o", str(image)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        images[program] = image

    jobs = []
    for program in programs:
        quick, _ = sizes_of(BENCHMARKS / f"{program}.nra")
        for size in (quick, second[program]):
            for mode in MODES:
                jobs.append((program, size, mode))
    counts = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(callgrind, neura, args.valgrind, images[p], s, m, out): (p, s, m) for p, s, m in jobs}
        for future in concurrent.futures.as_completed(futures):
            counts[futures[future]] = future.result()
            print(f"measured {' '.join(futures[future])}", file=sys.stderr, flush=True)

    cpu = {}
    if args.cpu:
        for program in programs:
            _, long_size = sizes_of(BENCHMARKS / f"{program}.nra")
            for mode in MODES:
                times = [cpu_time(neura, images[program], long_size, mode) for _ in range(3)]
                cpu[(program, mode)] = (statistics.median(times), times)
                print(f"timed {program} {mode}: {times}", file=sys.stderr, flush=True)

    lines = [f"| program | steps q | steps q2 | default Ir/step | native Ir/step | today | {args.stage} | "
             f"{args.stage} x 1.1 | verdict |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"]
    failed = []
    for program in programs:
        quick, _ = sizes_of(BENCHMARKS / f"{program}.nra")
        marginal = {}
        steps1 = counts[(program, quick, "default")][1]
        steps2 = counts[(program, second[program], "default")][1]
        for mode in MODES:
            ir1, mode_steps1 = counts[(program, quick, mode)]
            ir2, mode_steps2 = counts[(program, second[program], mode)]
            if (mode_steps1, mode_steps2) != (steps1, steps2):
                raise SystemExit(f"{program}: {mode} ran other steps ({mode_steps1}, {mode_steps2})")
            marginal[mode] = (ir2 - ir1) / (steps2 - steps1)
        today, stage = table[program]
        native = marginal["native"]
        reasons = []
        if native >= today:
            reasons.append("not below today")
        if native > stage * 1.1:
            reasons.append(f"above {args.stage} x 1.1")
        verdict = "pass" if not reasons else "FAIL: " + ", ".join(reasons)
        if reasons:
            failed.append(program)
        lines.append(f"| {program} | {steps1:,} | {steps2:,} | {marginal['default']:.2f} | {native:.2f} | {today:.2f} | "
                     f"{stage:.2f} | {stage * 1.1:.2f} | {verdict} |")
    if cpu:
        lines += ["", "| program | long size | default CPU s (median of 3) | native CPU s (median of 3) | "
                  "native / default | default runs | native runs |",
                  "| --- | ---: | ---: | ---: | ---: | --- | --- |"]
        for program in programs:
            _, long_size = sizes_of(BENCHMARKS / f"{program}.nra")
            default, default_runs = cpu[(program, "default")]
            native, native_runs = cpu[(program, "native")]
            lines.append(f"| {program} | {long_size} | {default:.2f} | {native:.2f} | {native / default:.3f} | "
                         f"{', '.join(f'{t:.2f}' for t in default_runs)} | "
                         f"{', '.join(f'{t:.2f}' for t in native_runs)} |")
    lines += ["", f"{len(programs) - len(failed)} of {len(programs)} programs pass the {args.stage} gate"
              + (": " + ", ".join(failed) + " fail" if failed else "")]
    text = "\n".join(lines) + "\n"
    (out / "budget.md").write_text(text)
    print(text, end="")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
