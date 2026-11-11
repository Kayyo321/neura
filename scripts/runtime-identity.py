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


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=600)
    return result.returncode, result.stdout, result.stderr


def build_images(neura, images):
    images.mkdir(parents=True, exist_ok=True)
    for name, source, _, _ in PROGRAMS:
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
    build_images(neura, images)
    work = out / "work"
    work.mkdir(exist_ok=True)

    def record(case, command, trace=None):
        status, stdout, stderr = run(command, work)
        (out / f"{case}.status").write_text(f"{status}\n")
        (out / f"{case}.stdout").write_bytes(stdout)
        (out / f"{case}.stderr").write_bytes(stderr)
        if trace is not None:
            (out / f"{case}.trace").write_bytes(trace.read_bytes() if trace.exists() else b"<missing>")

    by_name = {}
    for name, source, arguments, traced in PROGRAMS:
        image = str(images / f"{name}.nvr")
        by_name[name] = (image, arguments)
        tail = [image, "--", *arguments] if arguments else [image]
        record(f"{name}.stats", [neura, "run", "--stats", *tail])
        record(f"{name}.profile", [neura, "run", "--profile", *tail])
        if traced:
            trace = work / f"{name}.trace"
            if trace.exists():
                trace.unlink()
            record(f"{name}.record", [neura, "run", "--record", str(trace), *tail], trace)
    for name in SWEEP_PROGRAMS:
        image, arguments = by_name[name]
        tail = [image, "--", *arguments] if arguments else [image]
        for limit in SWEEP:
            record(f"{name}.sweep{limit[0]}={limit[1]}", [neura, "run", "--stats", *limit, *tail])
    for name in TASK_DUMPS:
        image, arguments = by_name[name]
        record(f"{name}.task-dump", [neura, "run", "--task-dump", image])
        record(f"{name}.task-dump-json", [neura, "run", "--task-dump=json", image])
    for name, source in TEST_FILES:
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
