#!/usr/bin/env python3
"""Compare an optimizer fixture's exact results and runtime counts across tiers."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


TIERS = {
    "tier 0": (("--tier=0",), {}),
    "tier 1": (("--tier=1",), {}),
    "tier 2 interpreter": (("--tier=2", "--jit=off"), {"NEURA_TIER3": "off"}),
    "tier 2 native": (("--tier=2", "--jit=on"), {"NEURA_TIER3": "off"}),
    "tier 3 native": (("--tier=2", "--jit=on"), {"NEURA_TIER3_VERIFY": "1"}),
}
NATIVE = "tier 3 native"
STATS = re.compile(rb"^stats: steps (\d+), peak heap cells (\d+)", re.M)
TRAP = re.compile(rb": trap: (.+)$", re.M)
FUNCTIONS = re.compile(r"^function (\d+) parameters (\d+)[^\n]*\n", re.M)
NATIVE_PROGRAMS = re.compile(r"^tier 3 program \d+ of function (\d+):\n", re.M)


def invoke(neura, *args, overrides=None):
    env = os.environ.copy()
    for key in ("NEURA_TIER3", "NEURA_TIER_CHECK", "NEURA_TIER_DUMP", "NEURA_TIER3_ALLOC_TRACE", "NEURA_TIER3_VERIFY"):
        env.pop(key, None)
    env["NEURA_TIER_EAGER"] = "1"
    if overrides:
        env.update(overrides)
    return subprocess.run((neura, *args), env=env, capture_output=True, timeout=120)


def run(neura, image, tier, argument, *, limit=None, dump=False, trace_resume=False):
    flags, defaults = TIERS[tier]
    overrides = {**defaults, "NEURA_TIER_DUMP": "1"} if dump else defaults
    if trace_resume:
        overrides = {**overrides, "NEURA_TIER3_ALLOC_TRACE": "1"}
    boundary = () if limit is None else ("--max-steps", str(limit))
    result = invoke(neura, "run", "--runtime=off", "--gpu=off", "--stats", "--tier-stats",
                    *flags, *boundary, image, "--", argument, overrides=overrides)
    stats = STATS.search(result.stderr)
    assert stats, (tier, argument, limit, result.returncode, result.stderr[-3000:])
    trap = TRAP.search(result.stderr)
    outcome = (result.returncode, result.stdout, tuple(map(int, stats.groups())),
               trap.group(1) if trap else None)
    return outcome, result.stderr


def sections(pattern, text):
    starts = list(pattern.finditer(text))
    for index, start in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        yield start, text[start.end():end]


def verify_mechanism(neura, image, dump, mechanism, baseline):
    disassembly = invoke(neura, "disassemble", image)
    disassembly.check_returncode()
    functions = [match.group(1) for match, body in sections(FUNCTIONS, disassembly.stdout.decode())
                 if int(match.group(2)) == mechanism["parameters"]
                 and all(body.count(marker) >= count for marker, count in mechanism["markers"].items())]
    assert len(functions) == 1, functions
    reports = [body for match, body in sections(NATIVE_PROGRAMS, dump.decode())
               if match.group(1) == functions[0]]
    assert reports, f"function {functions[0]} did not compile at Tier 3"
    matched = any(re.search(mechanism["pattern"], body, re.M) for body in reports)
    assert matched != baseline, (functions[0], mechanism["pattern"], "baseline" if baseline else "candidate")
    if mechanism.get("trace_resume"):
        trace = rf"^tier 3 allocation of function {functions[0]}, unit entries: [1-9][0-9]*$"
        assert re.search(trace, dump.decode(), re.M), f"function {functions[0]} did not generate a lazy resume entry"


def check(neura, image, case, base, mechanism):
    argument = case["argument"]
    expected, _ = run(neura, image, "tier 0", argument)
    exact = "".join(line + "\n" for line in case["output"]).encode()
    assert expected[1] == exact and expected[0] == case.get("status", 0), (argument, expected)
    if "trap" in case:
        assert expected[3] and case["trap"].encode() in expected[3], (argument, expected)
    else:
        assert expected[3] is None, (argument, expected)

    for tier in TIERS:
        dump = tier == NATIVE and mechanism is not None
        actual, detail = run(neura, image, tier, argument, dump=dump,
                             trace_resume=dump and mechanism[0].get("trace_resume", False))
        assert actual == expected, (argument, tier, actual, expected)
        if dump:
            verify_mechanism(neura, image, detail, *mechanism)
    if base:
        old, _ = run(base, image, NATIVE, argument)
        assert old == expected, (argument, "base", old, expected)

    if case.get("budget"):
        # Stop one charged step before the original fault, after warmup.
        limit = expected[2][0] - 1
        limited, _ = run(neura, image, "tier 0", argument, limit=limit)
        assert limited[0] != 0 and limited[3] and b"step limit exceeded" in limited[3], (argument, limited)
        for tier in TIERS:
            actual, _ = run(neura, image, tier, argument, limit=limit)
            assert actual == limited, (argument, tier, limit, actual, limited)
        if base:
            old, _ = run(base, image, NATIVE, argument, limit=limit)
            assert old == limited, (argument, "base", limit, old, limited)
    print(f"{argument}: {expected[2][0]} steps, peak heap {expected[2][1]}, exit {expected[0]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neura", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--base", type=Path, help="also compare the prior executable")
    parser.add_argument("--expect-no-mechanism", action="store_true", help="assert the old optimizer behavior")
    args = parser.parse_args()
    neura = args.neura.resolve()
    base = args.base.resolve() if args.base else None
    fixture = args.fixture.resolve()
    definition = json.loads(fixture.read_text())
    source = fixture.with_suffix(".nra")
    with tempfile.TemporaryDirectory(prefix="tier-fixture-", dir=Path.cwd()) as work:
        image = Path(work) / (source.stem + ".nvr")
        for flags in definition["builds"]:
            print("build", " ".join(flags) if flags else "(default)")
            built = invoke(neura, "build", *flags, source, "-o", image)
            assert built.returncode == 0, built.stderr
            for case in definition["cases"]:
                mechanism = definition["mechanism"]
                enabled = flags == mechanism["flags"] and case["argument"] == mechanism["argument"]
                check(neura, image, case, base, (mechanism, args.expect_no_mechanism) if enabled else None)
    print("Exact output, traps, steps and peak heap agreed across five tiers.")


if __name__ == "__main__":
    main()
