#!/usr/bin/env python3
"""Check an optimizer fixture across runtime tiers and optionally against a predecessor."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MODES = {
    "Tier 0": ({"NEURA_TIER3": "off"}, ["--tier=0", "--jit=off"]),
    "Tier 1": ({"NEURA_TIER3": "off"}, ["--tier=1", "--jit=off"]),
    "Tier 2 interpreter": ({"NEURA_TIER3": "off"}, ["--tier=2", "--jit=off"]),
    "Tier 2 native": ({"NEURA_TIER3": "off"}, ["--tier=2", "--jit=on"]),
    "Tier 3 interpreter": ({"NEURA_TIER3": "interp"}, ["--tier=2", "--jit=off"]),
    "Tier 3 native": ({"NEURA_TIER3": "native", "NEURA_TIER3_VERIFY": "1"}, ["--tier=2", "--jit=on"]),
}
CASES = {
    "nil-result": [("0", None), ("1", None), ("2", None), ("0", 44000), ("0", 44008)],
}


def execution(neura, image, mode, argument, limit):
    env, options = MODES[mode]
    limits = ["--max-steps", str(limit)] if limit is not None else []
    command = [str(neura), "run", "--runtime=off", "--gpu=off", "--stats", *options,
               *limits, str(image), "--", argument]
    process = subprocess.run(command, cwd=ROOT, env=dict(os.environ, **env), capture_output=True)
    return process.returncode, process.stdout, process.stderr


def verify_nil_inlining(neura, image, functions, expect_inlined):
    caller = functions["run_cases"]
    callee = functions["compute"]
    env = dict(os.environ, NEURA_TIER_EAGER="1", NEURA_TIER3="native", NEURA_TIER_DUMP="1",
               NEURA_TIER3_VERIFY="1")
    run = subprocess.run(
        [str(neura), "run", "--runtime=off", "--gpu=off", "--tier=2", "--jit=on", str(image), "--", "0"],
        cwd=ROOT, env=env, capture_output=True, check=True, timeout=120,
    )
    programs = re.split(r"^tier 3 program \d+ of function ", run.stderr.decode(), flags=re.M)
    graphs = [graph for graph in programs[1:] if re.match(rf"{caller}(?: |:)", graph)]
    if not graphs:
        raise SystemExit(f"{neura}: fixture did not build a Tier 3 program for run_cases")
    for graph in graphs:
        nil_values = set(re.findall(r"^  %(\d+) = const ref #0$", graph, re.M))
        # The join first merges the tag and string payload, then the item
        # payload. On the error edge the latter is a reference-typed nil.
        joins = re.findall(r"^  %\d+ = phi i32 \[\d+ \d+\]\n"
                           r"  %\d+ = phi ref \[\d+ \d+\]\n"
                           r"  %\d+ = phi ref \[(\d+) (\d+)\]$", graph, re.M)
        if any(nil_values.intersection(operands) for operands in joins):
            return
        if not expect_inlined:
            # An older optimizer can leave the call in place. Its second
            # auxiliary result must still be the item reference word.
            calls = re.findall(rf"^  %(\d+) = call i32 .* #{callee} state \d+$", graph, re.M)
            if any(re.search(rf"^  %\d+ = resultword ref %{call} #2$", graph, re.M) for call in calls):
                return
    expected = "a reference-typed nil in the inlined result" if expect_inlined else "an item reference result"
    raise SystemExit(f"{neura}: expected {expected} in the Tier 3 program for run_cases")


def verify(base, candidate, fixture, build):
    image = build / (fixture + ".nvr")
    source = ROOT / "tests" / "bench-wall" / "fixtures" / (fixture + ".nra")
    reference = base or candidate
    subprocess.run([str(reference), "build", str(source), "-o", str(image)], cwd=ROOT, check=True)
    inspection = subprocess.check_output([str(candidate), "inspect-image", "--json", str(image)], cwd=ROOT)
    functions = {entry["name"]: entry["id"] for entry in json.loads(inspection)["functions"]}
    if base:
        verify_nil_inlining(base, image, functions, expect_inlined=False)
    verify_nil_inlining(candidate, image, functions, expect_inlined=True)
    binaries = [("base", base), ("candidate", candidate)] if base else [("candidate", candidate)]
    for argument, limit in CASES[fixture]:
        expected = execution(reference, image, "Tier 0", argument, limit)
        expected_status = 0 if argument == "0" and limit is None else 1
        if (expected[0] == 0) != (expected_status == 0):
            raise SystemExit(f"{fixture}: unexpected reference status for {argument}, limit {limit}: {expected}")
        if re.search(br"stats: steps \d+, peak heap cells \d+", expected[2]) is None:
            raise SystemExit(f"{fixture}: missing reference counters")
        for label, executable in binaries:
            for mode in MODES:
                actual = execution(executable, image, mode, argument, limit)
                if actual != expected:
                    raise SystemExit(f"{fixture} input {(argument, limit)}: {label} {mode} differs\n"
                                     f"expected {expected}\nreceived {actual}")
    print(f"{fixture}: {len(CASES[fixture])} cases agree across {len(MODES)} modes and {len(binaries)} binaries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--fixture", choices=CASES, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        verify(args.base.resolve() if args.base else None, args.candidate.resolve(), args.fixture, Path(temporary))


if __name__ == "__main__":
    main()
