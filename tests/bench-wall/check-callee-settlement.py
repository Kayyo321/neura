#!/usr/bin/env python3
"""Check one Tier 3 successor build and equality with Tier 0 at both versions."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

STATS = re.compile(rb"^stats: steps (\d+), peak heap cells (\d+)", re.MULTILINE)
REBUILDS = re.compile(rb"^tier 3 program (\d+) of function (\d+) \(callee settlement rebuild\):", re.MULTILINE)
COMPILATION = re.compile(rb"^tier: native programs \d+ \(\d+ bytes of code, (\d+) calls inlined", re.MULTILINE)
INLINE_REFUSAL = re.compile(rb"^tier: inline refusal [1-9]\d*: callee program not settled$", re.MULTILINE)
LOOP_REFUSAL = re.compile(rb"^tier: loop hoist refusal [1-9]\d*: .+$", re.MULTILINE)


def run(binary, mode, image, *args, program_args=()):
    environment = os.environ.copy()
    environment.update(NEURA_TIER_EAGER="1", NEURA_TIER3_AFTER="1", NEURA_TIER_DUMP="1")
    tier = ("--tier=0",)
    if mode != "reference":
        tier = ()
        environment["NEURA_TIER3"] = mode
    command = [str(binary), "run", "--runtime=off", "--stats", "--tier-stats", *tier, *args, str(image)]
    if program_args:
        command.extend(("--", *program_args))
    result = subprocess.run(command, env=environment, capture_output=True, timeout=30)
    stats = STATS.search(result.stderr)
    assert stats, ("missing step and heap statistics", mode, result.stderr[-1000:])
    return result, (result.returncode, result.stdout, *stats.groups())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neura", type=Path)
    parser.add_argument("--fixture", type=Path, default=Path(__file__).with_name("fixtures") / "callee-settlement.nra")
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--refusals-base", type=Path, help="compare new refusal counters against a binary without them")
    args = parser.parse_args()
    binary = args.neura.resolve()
    mode = "native" if args.native else "interp"
    with tempfile.TemporaryDirectory(prefix="callee-settlement-", dir=Path.cwd()) as directory:
        image = Path(directory) / "fixture.nvr"
        subprocess.run([str(binary), "build", "--no-optimize", str(args.fixture.resolve()), "-o", str(image)],
                       check=True, timeout=30)

        result, observed = run(binary, mode, image)
        _, expected = run(binary, "reference", image)
        assert observed == expected, (mode, observed, expected)
        assert observed[:2] == (0, b"300\n600\n"), observed
        rebuilds = REBUILDS.findall(result.stderr)
        assert rebuilds, ("no rebuild observed", result.stderr[-1000:])
        assert len(rebuilds) == len(set(rebuilds)), ("a program was rebuilt twice", rebuilds)
        for program, function in rebuilds:
            assert f"tier 3 program {program.decode()} of function {function.decode()}:".encode() in result.stderr
        compilation = COMPILATION.search(result.stderr)
        assert compilation and int(compilation[1]) > 0, "settlement did not unlock an inline"
        if args.refusals_base:
            assert INLINE_REFUSAL.search(result.stderr), "no unsettled callee refusal was reported"
            assert LOOP_REFUSAL.search(result.stderr), "no skipped natural loop was reported"
            base, old = run(args.refusals_base.resolve(), mode, image)
            assert old == expected, (mode, old, expected)
            assert b"tier: inline refusal" not in base.stderr, "baseline already reports inline refusals"
            assert b"tier: loop hoist refusal" not in base.stderr, "baseline already reports loop refusals"

        # A step limit before the first result or after the missing callee is
        # called, and overflow in the newly eligible inlined arithmetic.
        for limit in ("900", "2400"):
            _, observed = run(binary, mode, image, "--max-steps", limit)
            _, expected = run(binary, "reference", image, "--max-steps", limit)
            assert observed == expected and observed[0] != 0, (limit, observed, expected)
        _, observed = run(binary, mode, image, program_args=("overflow",))
        _, expected = run(binary, "reference", image, program_args=("overflow",))
        assert observed == expected and observed[0] != 0, (observed, expected)
        profiled, observed = run(binary, mode, image, "--profile")
        assert observed[0] == 0 and observed[1] == b"300\n600\n", observed
        assert not REBUILDS.search(profiled.stderr), "profile variant unexpectedly rebuilt"
        print(f"Matched Tier 0 output, steps, heap, limits and overflow in {mode} mode.")


if __name__ == "__main__":
    main()
