#!/usr/bin/env python3
r"""Compare two Neura builds on the frozen Stage 0B workloads.

Run only at gates or to check a patch's claimed mechanism. Keep the fixture
sources, inputs and digests fixed while optimizing. A patch that improves only
the benchmark that exposed it is benchmark-specific.

For applications and task programs, build a separate image with each SDK,
then time cold processes. Standard library suites use the normal `neura test`
entry point, including compilation. Timings, CPU times and optional perf
counts stay separate from instrumented diagnostics. Outputs must agree.
Step counts and heap peaks must also agree unless --allow-cost-change is set.

    python3 scripts/bench-held-out.py --before build/base/neura --after build/neura \
        --only csv,worker-group --runs 5 --verify-tiers
    python3 scripts/bench-held-out.py --before build/base/neura --after build/neura \
        --phases --include-extensions --only csv,worker-group,join-coverage

Raw samples are written under the ignored build directory by default.
"""

import argparse
import datetime
import hashlib
import importlib.util
import json
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tests" / "bench-wall" / "held-out.json"
EXTENSIONS = MANIFEST.with_name("held-out-extensions.json")
MAX_STEPS = 4_000_000_000
TEST_STATS = re.compile(
    r"^(PASS .+?) \(steps (\d+), peak heap cells (\d+)(?:, peak stack cells \d+)?\)$",
    re.MULTILINE,
)
TIERS = (("0", "off"), ("1", "off"), ("2", "off"), ("2", "on"))


def load_script(name):
    specification = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    harness = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(harness)
    return harness


def options():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--before", required=True, type=Path, help="reference SDK executable")
    parser.add_argument("--after", required=True, type=Path, help="candidate SDK executable")
    parser.add_argument("--before-ref", help="commit of the reference build, if known")
    parser.add_argument("--after-ref", help="commit of the candidate build, if known")
    parser.add_argument("--only", help="comma-separated workload names")
    parser.add_argument("--phases", action="store_true", help="also measure source, prebuilt and warmed phases")
    parser.add_argument("--include-extensions", action="store_true", help="include the separate extension manifest")
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--samples", type=int, default=2, help="warmed samples per fork")
    parser.add_argument("--jdk", type=Path, help="JDK directory for managed twins (default JAVA_HOME)")
    parser.add_argument("--dotnet", type=Path, help=".NET SDK executable for managed twins")
    parser.add_argument("--workers", type=int, default=1, help="default workers for staged measurements")
    parser.add_argument("--cpu7", action="store_true", help="require the quiet CPU 7 preflight and retry disturbed forks")
    parser.add_argument("--aa-results", type=Path, help="matching CPU 7 A/A phased report for T0 trial margins")
    parser.add_argument("--stop-file", type=Path, help="stop before the next command or when a running fork is interrupted")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--tier", choices=("0", "1", "2"), default="2")
    parser.add_argument("--jit", choices=("off", "on"), default="on")
    parser.add_argument("--verify-tiers", action="store_true", help="also check each SDK's output and counters across tiers")
    parser.add_argument("--allow-cost-change", action="store_true", help="allow changes to steps and, in the legacy comparison, peak heap cells")
    parser.add_argument("--no-perf", action="store_true")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build" / "bench-held-out")
    result = parser.parse_args()
    if min(result.runs, result.warmups, result.samples, result.workers) < 1:
        parser.error("runs, warmups, samples and workers must be positive")
    if not result.phases and (result.cpu7 or result.include_extensions or result.aa_results or result.stop_file):
        parser.error("CPU 7 qualification, extensions, A/A receipts and STOP checks require --phases")
    if result.aa_results and not result.cpu7:
        parser.error("A/A trial margins require --cpu7")
    if result.tier != "2" and result.jit == "on":
        parser.error("use --jit=off with --tier=0 or --tier=1")
    return result


def fixture_cases(manifest, selection, verify=True):
    cases = manifest["programs"]
    if selection:
        chosen = set(selection.split(","))
        unknown = chosen - {case["name"] for case in cases}
        if unknown:
            raise SystemExit(f"bench-held-out: unknown workload: {', '.join(sorted(unknown))}")
        cases = [case for case in cases if case["name"] in chosen]
    if verify:
        for case in cases:
            for path, expected in case["sha256"].items():
                if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != expected:
                    raise SystemExit(f"bench-held-out: frozen source has changed: {path}")
    return cases


def command_for(case, executable, image, tier, jit):
    limits = ["--gpu=off", f"--tier={tier}", f"--jit={jit}", "--max-steps", str(MAX_STEPS)]
    if case["mode"] == "test":
        return [str(executable), "test", *limits, *case["options"], case["source"]]
    return [str(executable), "run", "--runtime=off", *limits, *case["options"], str(image),
            "--", *case["arguments"]]


def diagnostic(harness, case, command, expected, tier_stats):
    flags = ["--stats"] + (["--tier-stats"] if tier_stats else [])
    process = harness.checked(command[:2] + flags + command[2:], ROOT, case["name"])
    output = process.stdout.decode()
    stderr = process.stderr.decode()
    if case["mode"] == "test":
        counters = [{"test": match[1].removeprefix("PASS "), "steps": int(match[2]), "peak_heap_cells": int(match[3])}
                    for match in TEST_STATS.finditer(output)]
        output = TEST_STATS.sub(r"\1", output)
        passes = sum(line.startswith("PASS ") for line in expected.splitlines())
        if len(counters) != passes:
            raise SystemExit(f"bench-held-out: {case['name']}: missing per-test counters")
    else:
        match = harness.STATS.search(stderr)
        counters = [{"steps": int(match[1]), "peak_heap_cells": int(match[2])}] if match else []
    if not counters or output != expected:
        raise SystemExit(f"bench-held-out: {case['name']}: diagnostic run changed output or omitted counters")
    calls = harness.CALLS.search(stderr)
    return {
        "steps": sum(entry["steps"] for entry in counters),
        "peak_heap_cells": max(entry["peak_heap_cells"] for entry in counters),
        "test_counters": counters,
        "calls": int(calls[1]) if calls else None,
        "tier_stats": [line for line in stderr.splitlines() if line.startswith("tier: ")],
    }


def measure(harness, case, executables, arguments, output_dir, perf):
    name = case["name"]
    commands = {}
    images = {}
    for label, binary in executables.items():
        image = output_dir / f"{name}-{label}.nvr"
        images[label] = image
        build_seconds = None
        if case["mode"] == "run":
            build_seconds = harness.timed_build([str(binary), "build", case["source"], "-o", str(image)],
                                                ROOT, f"{name}: {label} build")
        command = command_for(case, binary, image, arguments.tier, arguments.jit)
        commands[label] = (command, None, build_seconds, False)
    times, output = harness.cold_runs(commands, ROOT, arguments, perf)
    for label, binary in executables.items():
        observed = diagnostic(harness, case, commands[label][0], output, True)
        times[label].update(observed)
        if arguments.verify_tiers:
            for tier, jit in TIERS:
                if (tier, jit) == (arguments.tier, arguments.jit):
                    continue
                alternate = command_for(case, binary, images[label], tier, jit)
                other = diagnostic(harness, case, alternate, output, False)
                if observed["test_counters"] != other["test_counters"]:
                    raise SystemExit(f"bench-held-out: {name}: {label} differs at tier {tier}, jit {jit}")
    before, after = times["before"], times["after"]
    counters_match = before["test_counters"] == after["test_counters"]
    if not counters_match and not arguments.allow_cost_change:
        raise SystemExit(f"bench-held-out: {name}: candidate steps or peak heap cells differ")
    wall_ratio = after["median_seconds"] / before["median_seconds"]
    cpu_ratio = after["median_cpu_seconds"] / before["median_cpu_seconds"]
    print(f"{name:16} wall {before['median_seconds']:.6f} -> {after['median_seconds']:.6f} s ({wall_ratio:.3f}x)"
          f"   CPU {before['median_cpu_seconds']:.6f} -> {after['median_cpu_seconds']:.6f} s ({cpu_ratio:.3f}x)"
          f"   calls {before['calls']} -> {after['calls']}"
          f"   steps {before['steps']} -> {after['steps']}"
          f"   heap {before['peak_heap_cells']} -> {after['peak_heap_cells']}", flush=True)
    return {"fixture": case, "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            "counters_match": counters_match, "wall_ratio": wall_ratio, "cpu_ratio": cpu_ratio, **times}


def main():
    arguments = options()
    harness = load_script("bench-wall")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if arguments.phases:
        phases = load_script("bench-held-out-phases")
        extensions = json.loads(EXTENSIONS.read_text(encoding="utf-8"))["rows"] if arguments.include_extensions else []
        available = {"programs": manifest["programs"] + [row["fixture"] for row in extensions]}
        cases = fixture_cases(available, arguments.only, verify=False)
        phases.run(harness, arguments, manifest, cases, extensions, command_for, MAX_STEPS)
        return
    cases = fixture_cases(manifest, arguments.only)
    output_dir = arguments.out_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    perf = harness.find_perf(arguments, output_dir)
    binaries = {"before": arguments.before.resolve(), "after": arguments.after.resolve()}
    references = {label: harness.git_ref(ref) for label, ref in
                  (("before", arguments.before_ref), ("after", arguments.after_ref))}
    versions = {label: harness.checked([str(binary), "--version"], ROOT, label).stdout.decode().strip()
                for label, binary in binaries.items()}
    print(f"perf instructions: {'available' if perf else 'unavailable or disabled'}")
    measurements = [measure(harness, case, binaries, arguments, output_dir, perf) for case in cases]
    complete = len(cases) == len(manifest["programs"])
    report = {
        "recorded_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "machine": harness.machine_details(),
        "fixture_revision": manifest["fixture_revision"],
        "references": references,
        "versions": versions,
        "tier": arguments.tier,
        "jit": arguments.jit,
        "runs": arguments.runs,
        "max_steps": MAX_STEPS,
        "verify_tiers": arguments.verify_tiers,
        "allow_cost_change": arguments.allow_cost_change,
        "perf_instructions": perf is not None,
        "complete_frozen_ten": complete,
        "wall_ratio_geomean": statistics.geometric_mean(item["wall_ratio"] for item in measurements) if complete else None,
        "cpu_ratio_geomean": statistics.geometric_mean(item["cpu_ratio"] for item in measurements) if complete else None,
        "results": measurements,
    }
    (output_dir / "held-out-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if complete:
        print(f"Frozen ten geomean candidate/base: wall {report['wall_ratio_geomean']:.3f}x, CPU {report['cpu_ratio_geomean']:.3f}x")
    else:
        print(f"Partial comparison of {len(cases)} frozen workloads")


if __name__ == "__main__":
    main()
