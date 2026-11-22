#!/usr/bin/env python3
"""Check an optimizer fixture against its predecessor and every runtime tier."""

import argparse
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


def verify_nil_inlining(neura, image, expected):
    env = dict(os.environ, NEURA_TIER_EAGER="1", NEURA_TIER3="native", NEURA_TIER_DUMP="1",
               NEURA_TIER3_VERIFY="1")
    run = subprocess.run(
        [str(neura), "run", "--runtime=off", "--gpu=off", "--tier=2", "--jit=on", str(image), "--", "0"],
        cwd=ROOT, env=env, capture_output=True, check=True, timeout=120,
    )
    if re.search(rb"^tier 3 program \d+ of function ", run.stderr, re.M) is None:
        raise SystemExit(f"{neura}: fixture did not build Tier 3 programs")
    retyped = re.search(rb"^(?:entry \d+ )?pass inline\.result_nil [1-9]\d*$", run.stderr, re.M)
    if bool(retyped) != expected:
        raise SystemExit(f"{neura}: expected inline.result_nil to {'fire' if expected else 'stay absent'}")


def verify(base, candidate, fixture, build):
    image = build / (fixture + ".nvr")
    source = ROOT / "tests" / "bench-wall" / "fixtures" / (fixture + ".nra")
    subprocess.run([str(base), "build", str(source), "-o", str(image)], cwd=ROOT, check=True)
    verify_nil_inlining(base, image, expected=False)
    verify_nil_inlining(candidate, image, expected=True)
    for argument, limit in CASES[fixture]:
        expected = execution(base, image, "Tier 0", argument, limit)
        expected_status = 0 if argument == "0" and limit is None else 1
        if (expected[0] == 0) != (expected_status == 0):
            raise SystemExit(f"{fixture}: unexpected reference status for {argument}, limit {limit}: {expected}")
        if re.search(br"stats: steps \d+, peak heap cells \d+", expected[2]) is None:
            raise SystemExit(f"{fixture}: missing reference counters")
        for label, executable in (("base", base), ("candidate", candidate)):
            for mode in MODES:
                actual = execution(executable, image, mode, argument, limit)
                if actual != expected:
                    raise SystemExit(f"{fixture} input {(argument, limit)}: {label} {mode} differs\n"
                                     f"expected {expected}\nreceived {actual}")
    print(f"{fixture}: {len(CASES[fixture])} cases agree across {len(MODES)} modes and both binaries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--fixture", choices=CASES, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
        verify(args.base.resolve(), args.candidate.resolve(), args.fixture, Path(temporary))


if __name__ == "__main__":
    main()
