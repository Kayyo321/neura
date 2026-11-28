#!/usr/bin/env python3
"""Check portable Tier 2 identity and the default Windows fallback."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().parent / "tier" / "region-counters.nra"
OUTPUT = b"up 4096\ndown 4096\nunsigned 96\n"
STEPS = re.compile(rb"^stats: steps (\d+), peak heap cells \d+", re.M)
REGION_STEPS = re.compile(rb"^tier: steps (\d+): tier 2 [^\n]+ \(in regions ([1-9]\d*)\)", re.M)
REGIONS = re.compile(rb"^tier: regions [1-9]\d* \([^\n]+\); entered ([1-9]\d*), [^\n]+; "
                     rb"deoptimized: budget (\d+), exits \d+, checks (\d+),", re.M)


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def invoke(neura, environment, *arguments):
    return subprocess.run([neura, *arguments], env=environment, capture_output=True, timeout=90)


def outcome(process):
    # Preserve the exact trap site and all run statistics. Tier diagnostics
    # are checked separately because only the portable run executes regions.
    ordinary = b"".join(line for line in process.stderr.splitlines(keepends=True) if not line.startswith(b"tier: "))
    return process.returncode, process.stdout, ordinary


def check_case(neura, image, environment, name, trap_switch, trap=None, limit=None):
    boundary = () if limit is None else ("--max-steps", str(limit))
    command = ("run", "--runtime=off", "--gpu=off", "--stats", "--tier-stats")
    workload = (*boundary, image, "--", "4096", trap_switch)
    reference = invoke(neura, environment, *command, "--tier=0", "--jit=off", *workload)
    expected = outcome(reference)
    reported = STEPS.search(expected[2])
    require(reported and expected[1] == OUTPUT, f"{name}: incorrect reference output or missing counters: {expected!r}")
    require((expected[0] == 0 and b": trap: " not in expected[2]) if trap is None
            else (expected[0] != 0 and trap in expected[2]),
            f"{name}: unexpected reference completion or trap: {expected!r}")
    steps = int(reported[1])
    if limit is not None:
        require(steps == limit, f"{name}: expected the reference to stop at {limit} steps, got {steps}")

    modes = [("portable", dict(environment, NEURA_TIER3="off"), ("--tier=2", "--jit=off"), b"tier: jit off\n")]
    if os.name == "nt":
        modes.append(("automatic", environment, (), b"tier: jit auto, not used: Windows has no JIT"))
    for mode, env, flags, jit in modes:
        actual = invoke(neura, env, *command, *flags, *workload)
        observed = outcome(actual)
        label = f"{name}, {mode}"
        require(observed == expected, f"{label}: differs from Tier 0\nexpected {expected!r}\nobserved {observed!r}")
        executed = REGION_STEPS.search(actual.stderr)
        regions = REGIONS.search(actual.stderr)
        require(executed and int(executed[1]) == steps and regions,
                f"{label}: missing or inconsistent entered region counters\n{actual.stderr.decode(errors='replace')}")
        require(jit in actual.stderr and not re.search(rb"^tier: (?:programs|native programs) ", actual.stderr, re.M),
                f"{label}: expected portable regions\n{actual.stderr.decode(errors='replace')}")
        entries, budget, checks = map(int, regions.groups())
        if limit is not None:
            require(budget > 0, f"{label}: the limit crossed no region budget boundary")
        elif trap is not None:
            require(checks > 0, f"{label}: the arithmetic fault crossed no region check boundary")
        print(f"{label}: {steps} steps, {entries} region entries, {int(executed[2])} region steps, "
              f"{budget} budget and {checks} check deoptimizations, exit {actual.returncode}")
    return steps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neura", type=Path)
    neura = parser.parse_args().neura.resolve(strict=True)
    environment = {name: value for name, value in os.environ.items()
                   if not name.upper().startswith(("NEURA_TIER", "NEURA_JIT", "NEURA_RUN", "NEURA_GPU"))}
    with tempfile.TemporaryDirectory(prefix="neura-tier2-fallback-") as scratch:
        image = Path(scratch) / "region-counters.nvr"
        built = invoke(neura, environment, "build", SOURCE, "-o", image)
        require(built.returncode == 0, f"could not build region-counters:\n{built.stderr.decode(errors='replace')}")
        check_case(neura, image, environment, "completion", "0")
        fault = check_case(neura, image, environment, "arithmetic trap", "1",
                           trap=b": trap: source arithmetic failure (trap reason 1)")
        check_case(neura, image, environment, "step boundary", "1",
                   trap=b": trap: step limit exceeded:", limit=fault - 1)
        if os.name == "nt":
            refused = invoke(neura, environment, "run", "--runtime=off", "--gpu=off", "--stats", "--jit=on",
                             image, "--", "4096", "0")
            require(refused.returncode != 0 and not refused.stdout and STEPS.search(refused.stderr) is None
                    and b"--jit=on: Windows has no JIT" in refused.stderr,
                    f"Windows forced JIT did not refuse before execution: {refused!r}")
            print("Windows forced JIT refused before execution")


if __name__ == "__main__":
    main()
