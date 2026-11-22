#!/usr/bin/env python3
"""Exercise a sqrt callee inlined into a caller with no direct kernels."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().with_name("fixtures") / "sqrt-inline.nra"
STATS = re.compile(rb"^stats: steps (\d+), peak heap cells (\d+)", re.M)
TRAP = re.compile(rb": trap: (.+)$", re.M)
OUTPUT = (
    b"0\n8000000000000000\n7ff8000000000000\n7ff8000000000000\n"
    b"4000000000000000\n0\n1e60000000000000\n"
)
MODES = {
    "tier 0": (("--tier=0",), {}),
    "tier 1": (("--tier=1",), {}),
    "tier 2 interpreter": (("--tier=2", "--jit=off"), {"NEURA_TIER3": "off"}),
    "tier 2 native": (("--tier=2", "--jit=on"), {"NEURA_TIER3": "off"}),
    "tier 3 native": (("--tier=2", "--jit=on"), {"NEURA_TIER3_VERIFY": "1"}),
}


def invoke(neura, *arguments, overrides=None):
    env = os.environ.copy()
    for key in ("NEURA_TIER3", "NEURA_TIER_CHECK", "NEURA_TIER_DUMP", "NEURA_TIER3_VERIFY"):
        env.pop(key, None)
    env["NEURA_TIER_EAGER"] = "1"
    if overrides:
        env.update(overrides)
    return subprocess.run((neura, *arguments), env=env, capture_output=True, timeout=120)


def execute(neura, image, tier, case, limit=None, dump=False):
    flags, environment = MODES[tier]
    extra = () if limit is None else ("--max-steps", str(limit))
    args = () if not case else ("--", case)
    if dump:
        environment = {**environment, "NEURA_TIER_DUMP": "1"}
    result = invoke(neura, "run", "--runtime=off", "--gpu=off", "--stats", "--tier-stats",
                    *flags, *extra, image, *args, overrides=environment)
    stats = STATS.search(result.stderr)
    assert stats, (tier, case, limit, result.returncode, result.stderr[-3000:])
    trap = TRAP.search(result.stderr)
    return (result.returncode, result.stdout, stats.groups(), trap.group(1) if trap else None), result.stderr


def image_caller(neura, image):
    result = invoke(neura, "disassemble", image)
    result.check_returncode()
    kernel = re.search(rb'^kernel (\d+) "neura\.math\.sqrt" ', result.stdout, re.M).group(1)
    functions = list(re.finditer(rb"^function (\d+) parameters (\d+)", result.stdout, re.M))
    bodies = [(function.group(1), result.stdout[function.end():following.start()])
              for function, following in zip(functions, functions[1:])
              if function.group(2) == b"4"]
    leaf = next(index for index, body in bodies if b"exit state kernel immediate " + kernel + b" " in body)
    return next(index for index, body in bodies if b"exit call target " + leaf + b" " in body)


def assert_inlined(dump, caller):
    programs = list(re.finditer(rb"^tier 3 program \d+ of function (\d+):\n", dump, re.M))
    for index, program in enumerate(programs):
        if program.group(1) == caller:
            end = programs[index + 1].start() if index + 1 < len(programs) else len(dump)
            assert b" = sqrt f64" in dump[program.end():end], "the caller did not inline the sqrt"
            return
    raise AssertionError("the caller did not compile at Tier 3")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neura", type=Path)
    parser.add_argument("--base", type=Path, help="also compare the previous executable at Tier 3")
    args = parser.parse_args()
    neura = args.neura.resolve()
    with tempfile.TemporaryDirectory(prefix="sqrt-inline-", dir=Path.cwd()) as work:
        image = Path(work) / "sqrt-inline.nvr"
        built = invoke(neura, "build", SOURCE, "-o", image)
        assert built.returncode == 0, built.stderr
        caller = image_caller(neura, image)
        for case, problem in (("", None), ("before", b"index out of bounds"),
                              ("after", b"index out of bounds"), ("overflow", b"source arithmetic failure")):
            expected, _ = execute(neura, image, "tier 0", case)
            if problem is None:
                assert expected[0] == 0 and expected[1] == OUTPUT and expected[3] is None, expected
            else:
                assert expected[0] != 0 and expected[1] == b"0\n" and problem in expected[3], expected
            for tier in MODES:
                actual, dump = execute(neura, image, tier, case, dump=not case and tier == "tier 3 native")
                assert actual == expected, (case, tier, actual, expected)
                if not case and tier == "tier 3 native":
                    assert_inlined(dump, caller)
            if args.base:
                base, _ = execute(args.base.resolve(), image, "tier 3 native", case)
                assert base == expected, (case, base, expected)
            if problem is not None:
                # The final invocation ends at the faulting block. Pull
                # the limit back one charged step into that invocation.
                limit = int(expected[2][0]) - 1
                limited, _ = execute(neura, image, "tier 0", case, limit)
                assert limited[0] != 0 and b"step limit exceeded" in limited[3], (case, limited)
                for tier in MODES:
                    actual, _ = execute(neura, image, tier, case, limit)
                    assert actual == limited, (case, tier, actual, limited)
                if args.base:
                    base, _ = execute(args.base.resolve(), image, "tier 3 native", case, limit)
                    assert base == limited, (case, base, limited)
    print("Checked inlined sqrt, exact float bits, bounds and overflow faults, and tight budgets across all five tiers.")


if __name__ == "__main__":
    main()
