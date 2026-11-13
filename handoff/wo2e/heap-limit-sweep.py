#!/usr/bin/env python3
"""Compare two `neura` binaries under a sweep of heap-cell limits.

The collector's schedule decides when a collection runs and so how much
garbage a run holds between collections (docs/runtime/NVM.md, "Garbage
collection"). A change of schedule may change `peak_heap_cells` and nothing
else: not whether a run reaches its heap limit, nor any other outcome. This
script checks that claim for every case of the identity corpus of
scripts/runtime-identity.py (its programs and --extended programs, its
`neura test` files, and its `--each` fleets) by running each case with a
base binary and with a changed one at the same limits:

    scripts/heap-limit-sweep.py --base OLD/neura --neura NEW/neura --images DIR --out sweep

For each case the base binary first finds, with a run under a limit of 2^40
cells, the case's unlimited peak P, and then, by bisection, the smallest
limit M at which the case ends without a heap-limit trap (a case that
reaches the heap limit at every limit has none; it sweeps 1 to P instead).
Both binaries then run the case at the default limit, at M - 1, and at
12 limits spaced geometrically from M to P (duplicates removed). Each run
records the exit status, stdout, stderr (`--stats`, which reports steps and
host requests, and the trap text), `--profile` (function steps), and, for
the cases the corpus records, the `--record` trace. The two runs of a
limit must agree byte for byte after `peak heap cells N` is replaced by a
placeholder in stdout and stderr; any other difference is reported.

A case's class says whether its peak may differ as well. An image whose
disassembly has a `transferable-layouts` line (an image with tasks) and a
`--each` fleet run on the base schedule, so their peaks must be equal; a
difference in either is reported. Other `run` cases may differ in the peak.
For `neura test` cases the script does not build the image itself and
reports peak differences without judging them.

The environment is passed on, so `NEURA_TIER_EAGER=1` and the other tier
variables apply to both binaries. Images are built once, with the base
binary, into --images; `--jobs` runs that many cases at once. The summary
counts cases, limits, runs, and differences, and the script exits with
status 1 when any difference is found. `--filter REGEX` keeps the cases
whose names match.
"""

import argparse
import concurrent.futures
import importlib.util
import json
import math
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UNLIMITED = 1 << 40
PEAK = re.compile(rb"peak heap cells \d+")
PEAK_VALUE = re.compile(rb"peak heap cells (\d+)")
HEAP_LIMIT = b"heap limit exceeded"


def identity_module():
    spec = importlib.util.spec_from_file_location("runtime_identity", ROOT / "scripts" / "runtime-identity.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def execute(command, cwd):
    result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=1800, stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout, result.stderr


class Case:
    def __init__(self, name, kind, command_tail, traced, task_image):
        self.name = name
        # "run", "test", or "each".
        self.kind = kind
        self.command_tail = command_tail
        self.traced = traced
        # True: base schedule, the peak must not change. False: it may.
        # None: not judged (a `neura test` case).
        self.task_image = task_image

    def command(self, neura, limit, trace):
        options = [] if limit is None else ["--max-heap-cells", str(limit)]
        if self.kind == "test":
            return [neura, "test", "--stats", *options, *self.command_tail]
        if self.kind == "each":
            return [neura, "run", "--stats", *options, *self.command_tail]
        record = ["--record", str(trace)] if trace is not None else []
        return [neura, "run", "--stats", "--profile", *record, *options, *self.command_tail]


def run_case(case, neura, limit, work, label):
    trace = None
    if case.traced:
        trace = work / f"{case.name}.{label}.{limit}.trace"
        if trace.exists():
            trace.unlink()
    status, stdout, stderr = execute(case.command(neura, limit, trace), work)
    trace_bytes = None
    if trace is not None:
        trace_bytes = trace.read_bytes() if trace.exists() else b"<missing>"
        if trace.exists():
            trace.unlink()
    return status, stdout, stderr, trace_bytes


def reaches_limit(result):
    _, stdout, stderr, _ = result
    return HEAP_LIMIT in stdout or HEAP_LIMIT in stderr


def peaks(result):
    _, stdout, stderr, _ = result
    return [int(m) for m in PEAK_VALUE.findall(stdout) + PEAK_VALUE.findall(stderr)]


def without_peak(result):
    status, stdout, stderr, trace = result
    return status, PEAK.sub(b"peak heap cells <peak>", stdout), PEAK.sub(b"peak heap cells <peak>", stderr), trace


def sweep_limits(case, base, work):
    """The limits a case runs at, found with the base binary."""
    unlimited = run_case(case, base, UNLIMITED, work, "probe")
    found = peaks(unlimited)
    top = max(found) if found else 1
    if reaches_limit(unlimited):
        low, minimum = 1, None
    else:
        # The smallest limit without a heap-limit trap: reaching the limit
        # is monotone in it, and the unlimited peak is enough.
        low, high = 1, max(top, 1)
        while low < high:
            middle = (low + high) // 2
            if reaches_limit(run_case(case, base, middle, work, "probe")):
                low = middle + 1
            else:
                high = middle
        minimum = low
    limits = [None]
    start = minimum if minimum is not None else 1
    if minimum is not None and minimum > 1:
        limits.append(minimum - 1)
    end = max(top, start)
    for i in range(12):
        value = round(start * math.pow(end / start, i / 11)) if end > start else start
        if value not in limits:
            limits.append(value)
    return minimum, top, limits


def sweep_case(case, base, neura, work):
    minimum, top, limits = sweep_limits(case, base, work)
    differences = []
    peak_changes = []
    for limit in limits:
        label = "default" if limit is None else str(limit)
        before = run_case(case, base, limit, work, "base")
        after = run_case(case, neura, limit, work, "new")
        if without_peak(before) != without_peak(after):
            parts = [field for field, a, b in zip(("status", "stdout", "stderr", "trace"),
                                                  without_peak(before), without_peak(after)) if a != b]
            differences.append({"limit": label, "fields": parts,
                                "base": [repr(x)[:400] for x in without_peak(before)],
                                "new": [repr(x)[:400] for x in without_peak(after)]})
        if peaks(before) != peaks(after):
            peak_changes.append({"limit": label, "base": peaks(before), "new": peaks(after)})
    if case.task_image is True and peak_changes:
        differences.append({"limit": "*", "fields": ["peak of a base-schedule case"], "peaks": peak_changes})
    return {"case": case.name, "kind": case.kind, "task_image": case.task_image, "minimum": minimum,
            "unlimited_peak": top, "limits": ["default" if l is None else l for l in limits],
            "differences": differences, "peak_changes": peak_changes}


def has_tasks(base, image):
    status, stdout, stderr = execute([base, "disassemble", str(image)], ROOT)
    if status != 0:
        sys.exit(f"{image}: disassembly failed\n{stderr.decode(errors='replace')}")
    return any(line.startswith(b"transferable-layouts") for line in stdout.splitlines())


def corpus(identity, base, images, work):
    programs = identity.PROGRAMS + identity.extended_programs()
    identity.build_images(base, images, programs)
    cases = []
    for name, source, arguments, traced in programs:
        image = images / f"{name}.nvr"
        tail = [str(image), "--", *arguments] if arguments else [str(image)]
        if name.startswith("x-concurrency-"):
            tail = ["--virtual-time", *tail]
        cases.append(Case(name, "run", tail, traced, has_tasks(base, image)))
    for name, source in identity.TEST_FILES + identity.EXTENDED_TEST_FILES:
        cases.append(Case(name, "test", [str(ROOT / source)], False, None))
    failing = work / "failing-tests.nra"
    failing.write_text(identity.FAILING_TESTS)
    cases.append(Case("test-failing", "test", [failing.name], False, None))
    lists = work / "each-lists.txt"
    lists.write_text("".join(f"{n}\n" for n in (1, 7, 27, 97, 871, 6171, 2, 3, 0, 100)))
    cases.append(Case("wall-collatz.each", "each", ["--each", lists.name, str(images / "wall-collatz.nvr")],
                      False, True))
    each_image = images / "x-each-collatz.nvr"
    if not each_image.exists():
        status, _, stderr = execute([base, "build", str(ROOT / "tests/backend/each/collatz.nra"), "-o",
                                     str(each_image)], ROOT)
        if status != 0:
            sys.exit(f"each collatz: build failed\n{stderr.decode(errors='replace')}")
    cases.append(Case("each-collatz.each", "each", ["--each", lists.name, str(each_image)], False, True))
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", required=True, help="the binary before the change")
    parser.add_argument("--neura", required=True, help="the binary after the change")
    parser.add_argument("--images", required=True, help="where the corpus images are built, or found")
    parser.add_argument("--out", required=True, help="where the results (results.jsonl) are written")
    parser.add_argument("--jobs", type=int, default=1, help="cases run at once")
    parser.add_argument("--filter", default=None, metavar="REGEX", help="only the cases whose names match")
    args = parser.parse_args()
    base = str(Path(args.base).resolve())
    neura = str(Path(args.neura).resolve())
    out = Path(args.out).resolve()
    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)
    identity = identity_module()
    cases = corpus(identity, base, Path(args.images).resolve(), work)
    if args.filter:
        cases = [case for case in cases if re.search(args.filter, case.name)]
    results_path = out / "results.jsonl"
    totals = {"cases": 0, "limits": 0, "differences": 0, "peak_changed_cases": 0}
    with results_path.open("w") as results, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(sweep_case, case, base, neura, work): case for case in cases}
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.write(json.dumps(result) + "\n")
            results.flush()
            totals["cases"] += 1
            totals["limits"] += len(result["limits"])
            totals["differences"] += len(result["differences"])
            totals["peak_changed_cases"] += bool(result["peak_changes"])
            mark = "DIFFERS" if result["differences"] else "same"
            print(f"{result['case']}: {mark}; {len(result['limits'])} limits, minimum {result['minimum']}, "
                  f"peak {result['unlimited_peak']}, peak changed at {len(result['peak_changes'])}", flush=True)
    by_class = {}
    for line in results_path.read_text().splitlines():
        result = json.loads(line)
        key = {True: "base schedule", False: "new schedule", None: "test, not judged"}[result["task_image"]]
        entry = by_class.setdefault(key, [0, 0, 0])
        entry[0] += 1
        entry[1] += len(result["limits"])
        entry[2] += bool(result["peak_changes"])
    print(f"{totals['cases']} cases, {totals['limits']} limits, {2 * totals['limits']} runs per binary pair; "
          f"{totals['differences']} differences; peak changed in {totals['peak_changed_cases']} cases")
    for key, (count, limits, changed) in sorted(by_class.items()):
        print(f"  {key}: {count} cases, {limits} limits, peak changed in {changed}")
    return 1 if totals["differences"] else 0


if __name__ == "__main__":
    sys.exit(main())
