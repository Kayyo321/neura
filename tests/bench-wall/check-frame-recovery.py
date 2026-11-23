#!/usr/bin/env python3
"""Compare physical frames recovered by seeded native deoptimization.

The corpus mode consumes a prepared manifest of unchanged NVR images and
frozen held-out sources with their existing gate receipts. Fixture mode uses
one small checked-in program to exercise both optimized and diagnostic modes.
The runner uses the same candidate SDK for both arms and keeps raw output.
"""

import argparse
from dataclasses import dataclass
from hashlib import file_digest, sha256
import json
import os
from pathlib import Path
import re
import subprocess


BENCH = Path(__file__).resolve().parent
GATE_SIZES = re.compile(r"^// sizes: quick (\d+) full (\d+) long (\d+)$", re.M)
RUN_STATS = re.compile(rb"^stats: steps (\d+), peak heap cells (\d+)", re.M)
TEST_STATS = re.compile(rb"^(PASS .+?) \(steps (\d+), peak heap cells (\d+)(?:, peak stack cells \d+)?\)$", re.M)
RECOVERY = b"frame-recovery "
SUMMARY = b"frame-recovery-summary "
NATIVE = ("--gpu=off", "--tier=2", "--jit=on", "--stats")
SEEDS = (277, 70123, 8675309)
STANDARD, PROFILE, CHECKABLE, SPARSE_FRAMES = range(4)


@dataclass(frozen=True)
class Case:
    name: str
    command: tuple[str | Path, ...]
    cwd: Path
    executions: int = 1
    output_sha256: str | None = None
    counters: tuple = ()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


def read_verified(entry):
    path = Path(entry["path"])
    require(digest(path) == entry["sha256"], f"checked input changed: {path}")
    return path


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def gate_cases(prepared, size, work):
    rows = read_json(read_verified(prepared["gate_receipt"]))["results"]
    sources = {path.stem: path for path in (BENCH / "neura").glob("*.nra")}
    require(len(rows) == len(sources) == 17 and {row["name"] for row in rows} == sources.keys(),
            "gate receipts must cover the 17 checked-in programs")
    cases = []
    for row in rows:
        name = row["name"]
        baseline = row["before"]
        archive = prepared["images"][f"gate/{name}"]
        image = read_verified(archive)
        require(archive["sha256"] == baseline["image_sha256"] and archive["arguments"] == [str(row["size"])],
                f"gate image or argument differs from its receipt: {name}")
        sizes = GATE_SIZES.search(sources[name].read_text(encoding="utf-8"))
        require(sizes is not None and int(sizes[3]) == row["size"], f"gate size differs from its receipt: {name}")
        argument = sizes[("quick", "full", "long").index(size) + 1]
        command = ("run", *NATIVE, "--runtime=off", "--parallel-workers", "1",
                   "--max-steps", str(10**12), "--max-heap-cells", str(2**32), image, "--", argument)
        expected = row["output_sha256"] if size == "long" else None
        counters = ((baseline["steps"], baseline["peak_heap_cells"]),) if expected else ()
        cases.append(Case(f"gate-{name}", command, work, output_sha256=expected, counters=counters))
    return cases


def held_cases(prepared):
    held = read_json(read_verified(prepared["held_receipt"]))
    manifest = read_json(read_verified(prepared["held_manifest"]))
    require(manifest == read_json(BENCH / "held-out.json"), "prepared held-out manifest differs from the checked-in manifest")
    rows = held["results"]
    require(len(rows) == 10 and [row["fixture"] for row in rows] == manifest["programs"] and
            held["fixture_revision"] == manifest["fixture_revision"], "held-out receipts do not cover the frozen corpus")
    frozen = Path(prepared["held_frozen_root"]).resolve(strict=True)
    cases = []
    for row in rows:
        fixture = row["fixture"]
        name = fixture["name"]
        for source, expected in fixture["sha256"].items():
            path = (frozen / source).resolve(strict=True)
            require(path.is_relative_to(frozen) and digest(path) == expected, f"frozen held-out source changed: {source}")
        limits = (*NATIVE, "--max-steps", "4000000000", *fixture["options"])
        test_counters = row["before"]["test_counters"]
        counters = tuple((item["steps"], item["peak_heap_cells"]) for item in test_counters)
        if fixture["mode"] == "test":
            command = ("test", *limits, fixture["source"])
        else:
            archive = prepared["images"][f"held/{name}"]
            require(archive["arguments"] == fixture["arguments"], f"held-out arguments changed: {name}")
            image = read_verified(archive)
            command = ("run", "--runtime=off", *limits, image, "--", *fixture["arguments"])
        cases.append(Case(f"held-{name}", command, frozen, len(counters), row["output_sha256"], counters))
    return cases


def number(value):
    return type(value) is int and value >= 0


def parse_recovery(payload):
    event = json.loads(payload)
    require(all(number(event[key]) for key in ("function", "block", "step", "omitted", "variant")),
            "invalid frame-recovery identity")
    require(isinstance(event["frames"], list) and event["frames"], "frame recovery emitted no frames")
    for frame in event["frames"]:
        require(number(frame["function"]) and number(frame["block"]), "invalid recovered frame identity")
        flags, values, dead = (frame[key] for key in ("initialized", "values", "dead"))
        require(len(flags) == len(values) and all(type(flag) is bool for flag in flags), "malformed frame flags")
        require(all(number(slot) and slot < len(values) for slot in dead) and len(dead) == len(set(dead)),
                "malformed dead-slot mask")
        dead_slots = set(dead)
        for slot, value in enumerate(values):
            if slot in dead_slots or not flags[slot]:
                require(value is None, f"frame exposed dead or uninitialized slot {slot}")
            else:
                require(number(value) and value < 2**64, f"frame omitted initialized live slot {slot}")
    return event


def parse_stderr(stderr):
    trace, summaries, ordinary = [], [], []
    for line in stderr.splitlines(keepends=True):
        if line.startswith(RECOVERY):
            trace.append(parse_recovery(line[len(RECOVERY):]))
        elif line.startswith(SUMMARY):
            summary = json.loads(line[len(SUMMARY):])
            require(all(number(summary[k]) for k in ("captured", "forced", "omitted")) and
                    summary["captured"] <= summary["forced"], "invalid frame-recovery summary")
            summaries.append(summary)
        else:
            ordinary.append(line)
    return trace, summaries, b"".join(ordinary)


def command_env(seed=None, overrides=None):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("NEURA_TIER", "NEURA_JIT", "NEURA_RUN")) and key not in {"LD_PRELOAD", "LD_LIBRARY_PATH"}}
    env.update(LC_ALL="C.UTF-8", NEURA_GPU="off", NEURA_TIER3="native", NEURA_TIER3_AFTER="1")
    if seed is not None:
        env["NEURA_TIER3_TEST_FRAME_SEED"] = str(seed)
    if overrides:
        env.update(overrides)
    return env


def invoke(args, command, cwd, log, *, seed=None, env=None):
    if args.stop_file is not None and args.stop_file.exists():
        raise SystemExit(4)
    result = subprocess.run((args.candidate, *command), cwd=cwd, env=command_env(seed, env),
                            capture_output=True, timeout=args.timeout)
    log.with_suffix(".stdout").write_bytes(result.stdout)
    log.with_suffix(".stderr").write_bytes(result.stderr)
    return result


def sample(args, case, seed, name, *, env=None, flags=(), expected_variant=None):
    command = (case.command[0], *flags, *case.command[1:])
    log = args.out_dir / f"{case.name}-{seed}-{name}"
    result = invoke(args, command, case.cwd, log, seed=seed, env=env)
    try:
        trace, summaries, ordinary = parse_stderr(result.stderr)
        # One summary for every top-level run or test. A shadow or parallel
        # item may also own a State and emit a summary.
        require(len(summaries) >= case.executions,
                f"expected at least {case.executions} execution summaries, got {len(summaries)}")
        require(sum(row["captured"] for row in summaries) == len(trace), "frame capture count does not match trace")
        require(sum(row["omitted"] for row in summaries) == sum(row["omitted"] for row in trace),
                "omitted slot count does not match trace")
        if expected_variant is not None:
            require(all(row["variant"] == expected_variant for row in trace), f"expected frame variant {expected_variant}")
        if expected_variant in (STANDARD, PROFILE, CHECKABLE):
            require(all(row["omitted"] == 0 for row in (*trace, *summaries)), "diagnostic or full-state mode omitted slots")
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"{log}: {error}") from error
    return (result.returncode, result.stdout, ordinary), trace, summaries


def check_corpus_result(case, outcome):
    status, stdout, stderr = outcome
    require(status == 0, f"{case.name} exited {status}")
    if case.command[0] == "test":
        rows = TEST_STATS.findall(stdout)
        counters = tuple((int(step), int(heap)) for _, step, heap in rows)
        output = TEST_STATS.sub(rb"\1", stdout)
        require(len(rows) == case.executions, f"{case.name}: missing per-test counters")
    else:
        counters = tuple((int(step), int(heap)) for step, heap in RUN_STATS.findall(stderr))
        output = stdout
        require(len(counters) == 1, f"{case.name}: missing runtime counters")
    if case.output_sha256:
        require(sha256(output).hexdigest() == case.output_sha256, f"{case.name}: frozen output changed")
        require(counters == case.counters, f"{case.name}: frozen steps or heap changed")


def comparable(rows):
    return [{key: value for key, value in row.items() if key not in {"variant", "omitted"}} for row in rows]


def require_equal_frames(case, seed, sparse, full):
    actual, expected = comparable(sparse), comparable(full)
    require(len(actual) == len(expected), f"{case.name}, seed {seed}: different frame capture counts")
    for index, (row, reference) in enumerate(zip(actual, expected), 1):
        require(row == reference, f"{case.name}, seed {seed}: first frame mismatch at event {index}")


def pair(args, case, seed):
    sparse, trace, summaries = sample(args, case, seed, "compact", expected_variant=SPARSE_FRAMES)
    full, reference, totals = sample(args, case, seed, "full", expected_variant=STANDARD,
                                     env={"NEURA_TIER3_TEST_FULL_FRAMES": "1"})
    require(sparse == full, f"{case.name}, seed {seed}: full and compact execution outcomes differ")
    require_equal_frames(case, seed, trace, reference)
    require(comparable(summaries) == comparable(totals), f"{case.name}, seed {seed}: probe summaries differ")
    return sparse, trace, summaries


def corpus(args, seeds):
    prepared = read_json(args.inputs)
    cases = gate_cases(prepared, args.gate_size, args.out_dir) + held_cases(prepared)
    if args.case:
        names = set(args.case)
        require(names <= {case.name for case in cases}, "--case names an unknown workload")
        cases = [case for case in cases if case.name in names]
    evidence = []
    for case in cases:
        print(case.name, flush=True)
        for seed in seeds:
            outcome, trace, summaries = pair(args, case, seed)
            check_corpus_result(case, outcome)
            evidence.append({"case": case.name, "seed": seed, "argv": tuple(map(str, case.command)),
                             "cwd": str(case.cwd), "captured": len(trace),
                             "forced": sum(row["forced"] for row in summaries),
                             "omitted_in_captures": sum(row["omitted"] for row in trace)})
            (args.out_dir / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    for case in cases:
        captured = sum(row["captured"] for row in evidence if row["case"] == case.name)
        if not captured:
            print(f"{case.name}: no native frame captures, execution outcome matched", flush=True)
            require(not case.name.startswith("gate-"), f"{case.name}: no forced deopt coverage at the chosen size/seeds")
    require(any(row["omitted_in_captures"] for row in evidence), "corpus did not exercise compact frame recovery")
    print(f"{len(cases)} workloads, {len(evidence)} pairs, {sum(row['captured'] for row in evidence)} matched frames")



def diagnostic_outcome(outcome):
    status, stdout, stderr = outcome
    # Tier statistics and profiling reports describe the execution engine.
    # Compare the program's output, trap and ordinary runtime counters.
    require(b"NEURA_TIER_CHECK=2 is not applied" not in stderr, "native shadow was refused")
    details = b"".join(line for line in stderr.splitlines(keepends=True)
                       if not line.startswith((b"tier: ", b"profile: ")) and
                       not re.match(rb"^\s+\d+\s+f\d+\s", line))
    return status, stdout, details


def build_fixture(args, source):
    image = args.out_dir / f"{source.stem}.nvr"
    built = invoke(args, ("build", source, "-o", image), args.out_dir, args.out_dir / f"build-{source.stem}")
    require(built.returncode == 0, f"could not build {source}")
    decoded = invoke(args, ("disassemble", image), args.out_dir, args.out_dir / f"layout-{source.stem}")
    require(decoded.returncode == 0, f"could not inspect frame types in {image}")
    types = {int(index): kind for index, kind in re.findall(rb"^type (\d+) (.+)$", decoded.stdout, re.M)}
    frame_types = {int(function): tuple(types[int(index)] for index in frame.split())
                   for function, frame in re.findall(rb"^function (\d+) .* frame \[([^]]*)\]$", decoded.stdout, re.M)}
    require(frame_types, f"missing frame types in {image}")
    return image, frame_types


def recovered_values(trace, layouts):
    float_bits = set()
    live_roots = 0
    for event in trace:
        for frame in event["frames"]:
            types = layouts[frame["function"]]
            dead = set(frame["dead"])
            values = frame["values"]
            require(len(values) == len(types), "recovered frame does not match its image layout")
            for slot, (kind, value) in enumerate(zip(types, values)):
                if kind.split()[0] in {b"text", b"bytes", b"object", b"array", b"list"}:
                    require(slot not in dead, "recovered frame masks a reference slot")
                    live_roots += bool(value)
                elif kind == b"float 64" and value is not None:
                    float_bits.add(value)
    return float_bits, live_roots


def fixture_cases(args):
    source = BENCH / "fixtures" / "function-entry-loops.nra"
    image, layouts = build_fixture(args, source)
    for entry in read_json(source.with_suffix(".json"))["cases"]:
        argument = entry["argument"]
        if argument not in {"resume", "after", "nan", "zero"}:
            continue
        runtime = ("--runtime=off", "--gpu=off", "--stats", "--parallel-workers", "1")
        with_arg = (image, "--", argument)
        reference = invoke(args, ("run", *runtime, "--tier=0", "--jit=off", *with_arg), args.out_dir,
                           args.out_dir / f"fixture-{argument}-reference", env={"NEURA_TIER3": "off"})
        expected = (reference.returncode, reference.stdout, reference.stderr)
        output = "".join(line + "\n" for line in entry["output"]).encode()
        require(expected[0] == entry.get("status", 0) and expected[1] == output,
                f"fixture {argument}: tier 0 result differs from fixture definition")
        require("trap" not in entry or entry["trap"].encode() in reference.stderr,
                f"fixture {argument}: wrong reference trap")
        case = Case(f"fixture-{argument}", ("run", *NATIVE, "--runtime=off", *with_arg),
                    args.out_dir)
        yield case, expected, layouts
        if entry.get("budget"):
            stats = RUN_STATS.search(reference.stderr)
            require(stats is not None, f"fixture {argument}: missing tier 0 steps")
            limits = ("--max-steps", str(int(stats[1]) - 1))
            limited = invoke(args, ("run", *runtime, "--tier=0", "--jit=off", *limits, *with_arg), args.out_dir,
                             args.out_dir / f"fixture-{argument}-limit-reference", env={"NEURA_TIER3": "off"})
            require(limited.returncode != 0 and b"step limit exceeded" in limited.stderr,
                    f"fixture {argument}: expected a step limit trap before the original fault")
            yield Case(f"fixture-{argument}-limit", ("run", *limits, *case.command[1:]), args.out_dir), (
                limited.returncode, limited.stdout, limited.stderr), layouts

    source = BENCH.parent / "backend" / "tier" / "region-gc.nra"
    image, layouts = build_fixture(args, source)
    limits = ("--runtime=off", "--max-heap-cells", "1700")
    reference = invoke(args, ("run", "--gpu=off", "--stats", "--tier=0", "--jit=off", *limits, image),
                       args.out_dir, args.out_dir / "fixture-region-gc-reference", env={"NEURA_TIER3": "off"})
    require(reference.returncode == 0 and reference.stdout == b"sum 348\n",
            "region-gc: unexpected outcome under the bounded heap")
    case = Case("fixture-region-gc", ("run", *NATIVE, *limits, image), args.out_dir)
    yield case, (reference.returncode, reference.stdout, reference.stderr), layouts


def diagnostics(args, seeds):
    omissions = 0
    captured_bits = {"fixture-nan": set(), "fixture-zero": set()}
    gc_roots = 0
    for case, expected, layouts in fixture_cases(args):
        for seed in seeds:
            outcome, trace, _ = pair(args, case, seed)
            require(outcome == expected, f"{case.name}, seed {seed}: differs from tier 0")
            omissions += sum(row["omitted"] for row in trace)
            bits, roots = recovered_values(trace, layouts)
            if case.name in captured_bits:
                captured_bits[case.name].update(bits)
            if case.name == "fixture-region-gc":
                gc_roots += roots
            if case.name != "fixture-resume":
                continue
            modes = (
                ("tier-stats", STANDARD, ("--tier-stats",), {}),
                ("tier-stats-parallel", STANDARD, ("--tier-stats", "--parallel-workers", "2"), {}),
                ("profile", PROFILE, ("--profile",), {}),
                ("native-shadow", STANDARD, (), {"NEURA_TIER_CHECK": "2", "NEURA_TIER_CHECK_HEAP": "1"}),
                ("verify", STANDARD, (), {"NEURA_TIER3_VERIFY": "1"}),
                ("region-check", CHECKABLE, (), {"NEURA_TIER_CHECK": "1"}),
            )
            for name, variant, flags, env in modes:
                outcome, events, _ = sample(args, case, seed, name, flags=flags, env=env, expected_variant=variant)
                require(diagnostic_outcome(outcome) == expected, f"{name}, seed {seed}: differs from tier 0")
                if name in {"tier-stats", "tier-stats-parallel"}:
                    timed = re.search(rb"^tier: time: ", outcome[2], re.M) is not None
                    require(timed == (name == "tier-stats"), f"{name}: unexpected tier clock state")
                if events:
                    print(f"{name}, seed {seed}: {len(events)} captures with full frames")
                    continue
                # The region checker can execute Tier 3 IR without native
                # probes. Require program execution or a check-specific
                # refusal in a separate diagnostic before accepting it.
                require(name == "region-check", f"{name}, seed {seed}: no diagnostic frame coverage")
                witnessed, _, _ = sample(args, case, seed, "region-check-stats", flags=("--tier-stats",),
                                          env=env, expected_variant=variant)
                require(diagnostic_outcome(witnessed) == expected and
                        re.search(rb"^tier: programs [1-9]\d* .*steps in programs [1-9]\d*", witnessed[2], re.M),
                        f"{name}, seed {seed}: no checkable program coverage")
                print(f"{name}, seed {seed}: checked program execution without native frame probes")
    require(omissions > 0, "fixture did not exercise compact frame recovery")
    require(0x7FF8000000000000 in captured_bits["fixture-nan"], "fixture did not recover a live canonical NaN")
    require(0x8000000000000000 in captured_bits["fixture-zero"], "fixture did not recover a live negative zero")
    require(gc_roots > 0, "bounded collection fixture did not recover an initialized reference")
    print(f"fixture: {omissions} omissions, NaN and negative-zero bits, and {gc_roots} recovered roots")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True, type=Path)
    workload = parser.add_mutually_exclusive_group(required=True)
    workload.add_argument("--inputs", type=Path, help="prepared corpus manifest with pinned receipts and images")
    workload.add_argument("--fixture", action="store_true", help="check diagnostic modes and traps on a tracked fixture")
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--gate-size", choices=("quick", "full", "long"), default="long")
    parser.add_argument("--seed", type=int, action="append", help="repeatable uint64 seed")
    parser.add_argument("--case", action="append", help="focus on a named gate- or held- workload")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--stop-file", type=Path)
    args = parser.parse_args()
    seeds = args.seed or SEEDS
    if len(seeds) != len(set(seeds)) or any(not 0 <= seed < 2**64 for seed in seeds):
        parser.error("seeds must be distinct uint64 values")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    args.candidate = args.candidate.resolve(strict=True)
    args.out_dir = args.out_dir.resolve()
    args.out_dir.mkdir(parents=True)
    try:
        run = diagnostics if args.fixture else corpus
        run(args, seeds)
    except (KeyError, OSError, TypeError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"frame recovery: {error}; raw logs in {args.out_dir}\n")


if __name__ == "__main__":
    main()
