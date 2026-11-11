#!/usr/bin/env python3
"""Capture what a set of programs observably does under one `neura` binary,
or compare two captures byte for byte.

A runtime change that must not change behaviour (docs/runtime/NVM.md: one
image and one input give the same exits, trap text, steps, peak heap cells,
function steps, stdout, and traces in every mode) is checked by capturing
with the binary built before the change and with the one built after it:

    scripts/runtime-identity.py capture --neura OLD/neura --images DIR --out before
    scripts/runtime-identity.py capture --neura NEW/neura --images DIR --out after
    scripts/runtime-identity.py compare before after

The execution tiers (docs/runtime/NVM.md, "Execution tiers") are compared
with one binary: each capture passes its tier to every `run` and `test`, and
--extended adds the wider case list below.

    scripts/runtime-identity.py capture --neura build/neura --images DIR --out t0 --extended --run-option=--tier=0
    scripts/runtime-identity.py capture --neura build/neura --images DIR --out t2 --extended --run-option=--tier=2
    scripts/runtime-identity.py compare t0 t2

Native code for tier 2 regions (docs/runtime/NVM.md, "Native compilation")
is one more mode of the same comparison: --jit=on compiles every region,
--jit=off runs every region on the region interpreter, and --jit MODE is a
shorthand for --run-option=--jit=MODE. A capture with --jit=on first checks
that the binary can run native code here, and stops if it cannot.

    scripts/runtime-identity.py capture --neura build/neura --images DIR --out jit --extended --jit on
    scripts/runtime-identity.py compare t0 jit

The environment is passed on, so NEURA_TIER_EAGER=1 (lift blocks and form
regions at their first execution), NEURA_TIER_CHECK=1 (check every lifted
block and region, native code included, against the slower path), and
NEURA_JIT apply to a capture.

Images are built once, by the first capture, so both runs execute the same
NVR bytes and the comparison isolates the runtime. Each case records the
exit status, stdout, stderr, `--stats`, `--profile`, and for some cases a
`--record` trace. A few small programs are rerun under a sweep of
`--max-steps`, `--max-heap-cells`, and `--max-frames` so that limit traps,
including their detail text and caller lists, are covered.
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (name, source, run arguments, record a trace)
PROGRAMS = [
    ("collections", "tests/backend/programs/collections.nra", [], False),
    ("objects", "tests/backend/programs/objects.nra", [], False),
    ("closures", "tests/backend/programs/closures.nra", [], False),
    ("generics", "tests/backend/programs/generics.nra", [], False),
    ("floats", "tests/backend/programs/floats.nra", [], False),
    ("computation", "tests/backend/programs/computation.nra", [], False),
    ("interfaces", "tests/backend/programs/interfaces.nra", [], False),
    ("integers", "tests/backend/programs/integers.nra", [], False),
    ("trap-deep-recursion", "tests/backend/traps/deep-recursion.nra", [], False),
    ("trap-use-after-destroy", "tests/backend/traps/use-after-destroy.nra", [], True),
    ("trap-uninitialized-field", "tests/backend/traps/uninitialized-field.nra", [], False),
    ("trap-index", "tests/backend/traps/index-out-of-bounds.nra", [], False),
    ("trap-range-heap-limit", "tests/backend/traps/range-heap-limit.nra", [], False),
    ("trap-endless-loop", "tests/backend/traps/endless-loop.nra", [], False),
    ("trap-message", "tests/backend/traps/message-trap.nra", [], False),
    ("trap-nil-receiver", "tests/backend/traps/nil-receiver.nra", [], False),
    ("trap-invalid-cast", "tests/backend/traps/invalid-class-cast.nra", [], False),
    ("trap-overflow", "tests/backend/traps/overflow.nra", [], False),
    ("hello-world", "examples/hello-world/hello-world.nra", ["a", "b"], True),
    ("example-overloads", "examples/types/overloads.nra", [], True),
    ("example-service", "examples/concurrency/service.nra", [], False),
    ("example-module-interface", "examples/modules/module-interface.nra", [], False),
    ("control-and-updates", "tests/backend/programs/control-and-updates.nra", [], False),
    ("bench-hash-map", "tests/benchmarks/hash-map.nra", ["1000"], True),
    ("bench-sort", "tests/benchmarks/sort.nra", ["1000"], False),
    ("bench-strings", "tests/benchmarks/strings.nra", ["10000"], False),
    ("bench-json", "tests/benchmarks/json.nra", ["100"], False),
    ("bench-tree-map", "tests/benchmarks/tree-map.nra", ["1000"], False),
    ("bench-sha256", "tests/benchmarks/sha256.nra", ["10240"], False),
    ("bench-array-list", "tests/benchmarks/array-list.nra", ["1000"], False),
    ("hash-map-retention", "tests/stdlib/hash-map/programs/retention-bound.nra", [], False),
    ("hash-map-stack-reclamation", "tests/stdlib/hash-map/programs/stack-reclamation.nra", [], False),
    ("hash-map-iterator-after-delete", "tests/stdlib/hash-map/traps/iterator-after-delete.nra", [], False),
    ("concurrency-fan-out", "tests/concurrency/programs/fan-out.nra", [], True),
    ("concurrency-select-rotation", "tests/concurrency/programs/select-rotation.nra", [], False),
    ("concurrency-deadlock", "tests/concurrency/programs/deadlock.nra", [], False),
    ("concurrency-child-trap", "tests/concurrency/programs/child-trap.nra", [], False),
    ("concurrency-collect-parked", "tests/concurrency/programs/collect-parked.nra", [], False),
    ("concurrency-worker-group", "tests/concurrency/programs/worker-group.nra", [], False),
    # Float and wide-integer circuits, which the evaluator runs differently
    # from mostly-zero integer circuits.
    ("wall-nbody", "tests/bench-wall/neura/nbody.nra", ["40"], False),
    ("wall-mandelbrot", "tests/bench-wall/neura/mandelbrot.nra", ["10"], False),
    ("wall-spectral-norm", "tests/bench-wall/neura/spectral-norm.nra", ["6"], False),
    ("wall-sha256", "tests/bench-wall/neura/sha256.nra", ["2000"], False),
    ("wall-binary-trees", "tests/bench-wall/neura/binary-trees.nra", ["8"], False),
    ("wall-collatz", "tests/bench-wall/neura/collatz.nra", ["500"], False),
]

# Programs rerun under each limit in the sweep.
SWEEP_PROGRAMS = ["collections", "bench-hash-map", "trap-deep-recursion", "concurrency-fan-out", "wall-nbody"]
SWEEP = [
    ["--max-steps", "1000"], ["--max-steps", "12345"], ["--max-steps", "50001"],
    ["--max-heap-cells", "300"], ["--max-heap-cells", "1000"], ["--max-heap-cells", "5000"],
    ["--max-frames", "5"], ["--max-frames", "50"], ["--max-frames", "500"],
]

# Runs with a task dump: on a trap it lists every task's frames.
TASK_DUMPS = ["concurrency-child-trap", "concurrency-deadlock", "concurrency-fan-out"]

# A test file whose failures are reported at the test's own line, found by
# walking the trap's caller list out of the library.
FAILING_TESTS = """let test = import:Std.Test;

let FailingTests = module (
    let check = func(depth: int) -> void {
        if (depth == 0) {
            test.assert_equal_int32(1, 2);
        } else {
            check(depth - 1);
        }
    };

    export let test_passes = func() -> void {
        test.assert(true);
    };

    export let test_fails_deep = func() -> void {
        check(5);
    };

    export let test_fails_here = func() -> void {
        test.assert(1 > 2);
    };
);
"""

# `neura test` files: compiled and run per test, with --stats.
TEST_FILES = [
    ("test-string-builder", "tests/stdlib/string-builder/string-builder-tests.nra"),
    ("test-hash-map", "tests/stdlib/hash-map/hash-map-tests.nra"),
]

# --extended: more `neura test` files.
EXTENDED_TEST_FILES = [
    ("test-sorting", "tests/stdlib/sorting/sorting-tests.nra"),
    ("test-strings", "tests/stdlib/strings/strings-tests.nra"),
    ("test-json", "tests/stdlib/json/json-tests.nra"),
    ("test-integers", "tests/stdlib/integers/integers-tests.nra"),
    ("test-sha256", "tests/stdlib/sha256/sha256-tests.nra"),
    ("test-big-integer", "tests/stdlib/big-integer/big-integer-tests.nra"),
    ("test-tree-map", "tests/stdlib/tree-map/tree-map-tests.nra"),
    ("test-float64", "tests/stdlib/floats/float64-tests.nra"),
]

# Standard library directories whose programs read the clock, entropy, the
# network, the environment, files, or native code: their output can differ
# from run to run whatever the tier, so --extended leaves them out.
NONDETERMINISTIC_STDLIB = {
    "clock", "system-clock", "instant", "local-date", "local-date-time", "local-time", "zone-offset", "random",
    "net", "process", "environment", "file", "fs", "io", "host", "dynamic-foreign", "language-bindings",
    "logger", "console", "concurrent", "uuid",
}


def first_line_size(source, quick):
    """The size a benchmark runs at: `// sizes: quick <n>` or `// sizes: <n>`."""
    for line in (ROOT / source).read_text().splitlines():
        if line.startswith("// sizes: "):
            words = line.split()
            if quick and "quick" in words:
                return words[words.index("quick") + 1]
            return words[2]
    return None


def extended_programs():
    """The --extended cases: every backend program and trap, every cost
    benchmark, the wall-clock programs at their quick sizes, a spread of
    standard library programs and traps, and every concurrency program."""
    programs = []
    known = {(source, tuple(arguments)) for _, source, arguments, _ in PROGRAMS}

    def add(name, source, arguments, traced=True):
        if (source, tuple(arguments)) not in known:
            known.add((source, tuple(arguments)))
            programs.append((name, source, arguments, traced))

    for directory in ("tests/backend/programs", "tests/backend/traps"):
        for path in sorted((ROOT / directory).glob("*.nra")):
            add(f"x-{path.parent.name}-{path.stem}", f"{directory}/{path.name}", [])
    for path in sorted((ROOT / "tests/benchmarks").glob("*.nra")):
        source = f"tests/benchmarks/{path.name}"
        add(f"x-bench-{path.stem}", source, [first_line_size(source, False)], False)
    for path in sorted((ROOT / "tests/bench-wall/neura").glob("*.nra")):
        source = f"tests/bench-wall/neura/{path.name}"
        add(f"x-wall-{path.stem}", source, [first_line_size(source, True)], False)
    for directory in sorted((ROOT / "tests/stdlib").iterdir()):
        if not directory.is_dir() or directory.name in NONDETERMINISTIC_STDLIB:
            continue
        for kind in ("programs", "traps"):
            for path in sorted((directory / kind).glob("*.nra"))[:3]:
                add(f"x-stdlib-{directory.name}-{kind}-{path.stem}",
                    f"tests/stdlib/{directory.name}/{kind}/{path.name}", [])
    for path in sorted((ROOT / "tests/concurrency/programs").glob("*.nra")):
        add(f"x-concurrency-{path.stem}", f"tests/concurrency/programs/{path.name}", [])
    add("x-tier-regions", "tests/backend/tier/regions.nra", [])
    return programs


# --extended: a step limit at every position of a small loop's first blocks,
# and on programs whose blocks run lifted, so a step limit trap lands on
# every kind of block. x-tier-regions (tests/backend/tier/regions.nra) has
# nested loops with element and field reads and writes: its first 3,000
# steps, every one a step limit, put the trap at every block of those
# loops, inside and at the edges of tier 2 regions.
STEP_SWEEPS = [
    ("x-programs-loops", range(1, 401)),
    ("wall-collatz", list(range(1, 201)) + [997, 4093, 65521, 100003]),
    ("bench-sort", [1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946]),
    ("x-tier-regions", range(1, 3001)),
]
EXTENDED_SWEEP_PROGRAMS = ["x-programs-loops", "bench-sort", "bench-tree-map", "wall-binary-trees",
                           "computation", "concurrency-worker-group", "x-tier-regions"]
# --extended: heap-cell limits around the peak of x-tier-regions, whose
# regions read and write elements and fields and end at allocations, and
# frame limits at each depth it reaches.
REGION_SWEEP = [["--max-heap-cells", str(n)] for n in (400, 800, 1200, 1600, 1800, 2000, 2100, 2200, 2240, 2250,
                                                         2300, 2400)] + \
               [["--max-frames", str(n)] for n in range(1, 7)]
# --extended: the task quantum at every value from 1 (a turn ends at every
# poll) to 12, and 97, for every concurrency program, so a turn ends at
# every poll they take.
QUANTUM_SWEEP = list(range(1, 13)) + [97]
# --extended: `neura test` under explored schedules.
SCHEDULE_TESTS = [("schedules-order", "tests/concurrency/schedules/order-tests.nra"),
                  ("schedules-service", "tests/concurrency/schedules/service-tests.nra")]
EXTENDED_SWEEP = [
    ["--max-heap-cells", "64"], ["--max-heap-cells", "200"], ["--max-heap-cells", "2048"],
    ["--max-heap-cells", "20000"], ["--max-frames", "1"], ["--max-frames", "2"], ["--max-frames", "3"],
    ["--max-frames", "8"], ["--max-frames", "20"],
]


RUN_OPTIONS = []


def run(command, cwd):
    # The capture's run options follow `run` or `test`.
    if len(command) > 1 and command[1] in ("run", "test"):
        command = command[:2] + RUN_OPTIONS + command[2:]
    result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=600, stdin=subprocess.DEVNULL)
    return result.returncode, result.stdout, result.stderr


def build_images(neura, images, programs):
    images.mkdir(parents=True, exist_ok=True)
    for name, source, _, _ in programs:
        target = images / f"{name}.nvr"
        if target.exists():
            continue
        status, _, stderr = run([neura, "build", str(ROOT / source), "-o", str(target)], ROOT)
        if status != 0:
            sys.exit(f"{name}: build failed\n{stderr.decode(errors='replace')}")


def capture(args):
    neura = str(Path(args.neura).resolve())
    images = Path(args.images).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    RUN_OPTIONS[:] = args.run_option + ([f"--jit={args.jit}"] if args.jit else [])
    programs = PROGRAMS + (extended_programs() if args.extended else [])
    build_images(neura, images, programs)
    if "--jit=on" in RUN_OPTIONS:
        # Every case would fail the same way: say why once instead.
        status, _, stderr = run([neura, "run", str(images / "hello-world.nvr")], ROOT)
        if status != 0 and b"--jit=on" in stderr:
            sys.exit(stderr.decode(errors="replace").strip())
    work = out / "work"
    work.mkdir(exist_ok=True)

    def record(case, command, trace=None):
        status, stdout, stderr = run(command, work)
        # A diagnostic names its file by its path, which is under the
        # capture's own directory: name that directory the same way in every
        # capture.
        stdout = stdout.replace(bytes(out), b"<capture>")
        stderr = stderr.replace(bytes(out), b"<capture>")
        (out / f"{case}.status").write_text(f"{status}\n")
        (out / f"{case}.stdout").write_bytes(stdout)
        (out / f"{case}.stderr").write_bytes(stderr)
        if trace is not None:
            (out / f"{case}.trace").write_bytes(trace.read_bytes() if trace.exists() else b"<missing>")

    by_name = {}
    for name, source, arguments, traced in programs:
        image = str(images / f"{name}.nvr")
        by_name[name] = (image, arguments)
        tail = [image, "--", *arguments] if arguments else [image]
        if name.startswith("x-concurrency-"):
            # Under virtual time, timers do not depend on the machine.
            tail = ["--virtual-time", *tail]
        record(f"{name}.stats", [neura, "run", "--stats", *tail])
        record(f"{name}.profile", [neura, "run", "--profile", *tail])
        if traced:
            trace = work / f"{name}.trace"
            if trace.exists():
                trace.unlink()
            record(f"{name}.record", [neura, "run", "--record", str(trace), *tail], trace)
            if args.extended and trace.exists():
                replay_tail = [arg for arg in tail if arg != "--virtual-time"]
                record(f"{name}.replay", [neura, "run", "--replay", str(trace), *replay_tail])
    for name in SWEEP_PROGRAMS:
        image, arguments = by_name[name]
        tail = [image, "--", *arguments] if arguments else [image]
        for limit in SWEEP:
            record(f"{name}.sweep{limit[0]}={limit[1]}", [neura, "run", "--stats", *limit, *tail])
    for name in TASK_DUMPS:
        image, arguments = by_name[name]
        record(f"{name}.task-dump", [neura, "run", "--task-dump", image])
        record(f"{name}.task-dump-json", [neura, "run", "--task-dump=json", image])
    if args.extended:
        for name, steps in STEP_SWEEPS:
            image, arguments = by_name[name]
            tail = [image, "--", *arguments] if arguments else [image]
            for limit in steps:
                record(f"{name}.steps={limit}", [neura, "run", "--stats", "--max-steps", str(limit), *tail])
        for name in EXTENDED_SWEEP_PROGRAMS:
            image, arguments = by_name[name]
            tail = [image, "--", *arguments] if arguments else [image]
            if name.startswith("x-concurrency-"):
                tail = ["--virtual-time", *tail]
            for limit in SWEEP + EXTENDED_SWEEP:
                record(f"{name}.sweep{limit[0]}={limit[1]}", [neura, "run", "--stats", *limit, *tail])
        image, arguments = by_name["x-tier-regions"]
        for limit in REGION_SWEEP:
            record(f"x-tier-regions.region{limit[0]}={limit[1]}", [neura, "run", "--stats", *limit, image])
        for name in sorted(by_name):
            if not name.startswith("x-concurrency-"):
                continue
            image, _ = by_name[name]
            for quantum in QUANTUM_SWEEP:
                record(f"{name}.quantum={quantum}",
                       [neura, "run", "--stats", "--virtual-time", "--task-quantum", str(quantum), image])
        for name, source in SCHEDULE_TESTS:
            record(f"{name}.explored", [neura, "test", "--stats", "--schedules", "25", str(ROOT / source)])
        # One image run once per argument list (a fleet of runs).
        lists = work / "each-lists.txt"
        lists.write_text("".join(f"{n}\n" for n in (1, 7, 27, 97, 871, 6171, 2, 3, 0, 100)))
        image, _ = by_name["wall-collatz"]
        record("wall-collatz.each", [neura, "run", "--stats", "--each", lists.name, image])
        each_image = images / "x-each-collatz.nvr"
        if not each_image.exists():
            status, _, stderr = run([neura, "build", str(ROOT / "tests/backend/each/collatz.nra"), "-o",
                                     str(each_image)], ROOT)
            if status != 0:
                sys.exit(f"each collatz: build failed\n{stderr.decode(errors='replace')}")
        record("each-collatz.each", [neura, "run", "--stats", "--each", lists.name, str(each_image)])
    for name, source in TEST_FILES + (EXTENDED_TEST_FILES if args.extended else []):
        record(name, [neura, "test", "--stats", str(ROOT / source)])
    failing = work / "failing-tests.nra"
    failing.write_text(FAILING_TESTS)
    record("test-failing", [neura, "test", "--stats", "failing-tests.nra"])
    print(f"captured {len(list(out.glob('*.status')))} cases in {out}")


def compare(args):
    left, right = Path(args.left), Path(args.right)
    names = sorted({p.name for p in left.iterdir() if p.is_file()} |
                   {p.name for p in right.iterdir() if p.is_file()})
    different = []
    for name in names:
        a, b = left / name, right / name
        if not a.exists() or not b.exists() or a.read_bytes() != b.read_bytes():
            different.append(name)
    cases = len([n for n in names if n.endswith(".status")])
    if different:
        print(f"{len(different)} of {len(names)} files differ:")
        for name in different:
            print(f"  {name}")
        return 1
    digest = hashlib.sha256()
    for name in names:
        digest.update(name.encode())
        digest.update((left / name).read_bytes())
    print(f"identical: {cases} cases, {len(names)} files, sha256 {digest.hexdigest()[:16]}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    capture_parser = commands.add_parser("capture")
    capture_parser.add_argument("--neura", required=True)
    capture_parser.add_argument("--images", required=True)
    capture_parser.add_argument("--out", required=True)
    capture_parser.add_argument("--extended", action="store_true",
                                help="also capture the wider case list (backend, benchmark, wall-clock, standard "
                                     "library, and concurrency programs, step sweeps, replays, and --each)")
    capture_parser.add_argument("--run-option", action="append", default=[], metavar="OPTION",
                                help="pass OPTION to every run and test, for example --run-option=--tier=0")
    capture_parser.add_argument("--jit", choices=["off", "on", "auto"],
                                help="run tier 2 regions as native code (on), on the interpreter (off), or as the "
                                     "binary decides (auto): --run-option=--jit=MODE")
    compare_parser = commands.add_parser("compare")
    compare_parser.add_argument("left")
    compare_parser.add_argument("right")
    args = parser.parse_args()
    if args.command == "capture":
        capture(args)
        return 0
    return compare(args)


if __name__ == "__main__":
    sys.exit(main())
