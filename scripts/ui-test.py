#!/usr/bin/env python3
"""Check the rendered diagnostics and the fixes of the UI tests in one directory.

Each test is `<name>.nra` with its expected standard error in `<name>.stderr`
and, when its diagnostics suggest machine-applicable edits, the source after
those edits in `<name>.fixed`. For each test, in a fresh temporary directory:

1. `neura check --error-format=human --color=never <name>.nra` runs with
   COLUMNS=100. Its standard error, with the temporary directory replaced by
   `$DIR`, must equal `<name>.stderr`.
2. `neura check --error-format=json` runs repeatedly; the MachineApplicable
   edits that it reports for the test file are applied back to front until
   none remain (at most 50 rounds). When any edit applied, the result must
   equal `<name>.fixed`, and `neura check` must accept it. A `<name>.fixed`
   without any applicable edit is a failure too.

Usage: ui-test.py --neura <binary> <directory> [--bless]

`--bless` writes the observed output to the expectation files instead of
comparing, and is refused when the CI environment variable is set. The exit
status is 0 when every test passes and 1 otherwise.
"""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROUNDS = 50


def run(neura, args, cwd):
    env = dict(os.environ)
    env["COLUMNS"] = "100"
    env["NEURA_GPU"] = "off"
    env.pop("NO_COLOR", None)
    return subprocess.run([neura, *args], cwd=cwd, env=env, capture_output=True, timeout=120)


def apply_edits(data, edits):
    """Applies byte edits back to front, skipping duplicates and overlaps."""
    unique = sorted({(e["offset"], e["length"], e["replacement"]) for e in edits}, reverse=True)
    limit = len(data)
    applied = 0
    for offset, length, replacement in unique:
        if offset > limit or length > limit - offset:
            continue
        data = data[:offset] + replacement.encode("utf-8") + data[offset + length:]
        limit = offset
        applied += 1
    return data, applied


def machine_edits(stderr, path):
    edits = []
    for line in stderr.decode("utf-8", "replace").splitlines():
        if not line.startswith("{"):
            continue
        diagnostic = json.loads(line)
        for suggestion in diagnostic.get("suggestions", []):
            if suggestion["applicability"] != "MachineApplicable":
                continue
            if all(edit["file"] == path for edit in suggestion["edits"]):
                edits.extend(suggestion["edits"])
    return edits


def check_test(neura, source, bless):
    problems = []
    name = source.stem
    expected_stderr = source.with_suffix(".stderr")
    expected_fixed = source.with_suffix(".fixed")
    with tempfile.TemporaryDirectory(prefix="neura-ui-") as temporary:
        directory = pathlib.Path(temporary).resolve()
        work = directory / source.name
        shutil.copyfile(source, work)

        human = run(neura, ["check", "--error-format=human", "--color=never", source.name], directory)
        if human.returncode not in (0, 1):
            problems.append(f"neura check exited with status {human.returncode}")
        stderr = human.stderr.decode("utf-8", "replace").replace(str(directory) + "/", "$DIR/")
        if bless:
            expected_stderr.write_text(stderr, encoding="utf-8")
        elif not expected_stderr.exists():
            problems.append(f"missing {expected_stderr.name}; observed:\n{stderr}")
        elif stderr != expected_stderr.read_text(encoding="utf-8"):
            problems.append(f"standard error differs from {expected_stderr.name}; observed:\n{stderr}")

        path = str(work)
        data = work.read_bytes()
        changed = False
        for _ in range(ROUNDS):
            result = run(neura, ["check", "--error-format=json", source.name], directory)
            edits = machine_edits(result.stderr, path)
            if not edits:
                break
            data, applied = apply_edits(data, edits)
            if applied == 0:
                break
            changed = True
            work.write_bytes(data)
        else:
            problems.append(f"edits still apply after {ROUNDS} rounds")

        if changed:
            fixed = data.decode("utf-8", "replace")
            if bless:
                expected_fixed.write_text(fixed, encoding="utf-8")
            elif not expected_fixed.exists():
                problems.append(f"machine-applicable edits apply but {expected_fixed.name} is missing; fixed:\n{fixed}")
            elif fixed != expected_fixed.read_text(encoding="utf-8"):
                problems.append(f"the fixed source differs from {expected_fixed.name}; fixed:\n{fixed}")
            accepted = run(neura, ["check", source.name], directory)
            if accepted.returncode != 0:
                problems.append("neura check rejects the fixed source:\n" +
                                accepted.stderr.decode("utf-8", "replace"))
        elif expected_fixed.exists():
            if bless:
                expected_fixed.unlink()
            else:
                problems.append(f"{expected_fixed.name} exists but no machine-applicable edit applies")
    return [f"{source}: {problem}" for problem in problems]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--neura", required=True)
    parser.add_argument("--bless", action="store_true")
    parser.add_argument("directory", type=pathlib.Path)
    options = parser.parse_args()
    if options.bless and os.environ.get("CI"):
        print("ui-test.py: --bless is refused when CI is set", file=sys.stderr)
        return 1
    neura = str(pathlib.Path(options.neura).resolve())
    sources = sorted(options.directory.glob("*.nra"))
    if not sources:
        print(f"ui-test.py: no .nra tests in {options.directory}", file=sys.stderr)
        return 1
    problems = []
    for source in sources:
        problems += check_test(neura, source, options.bless)
    for problem in problems:
        print(problem)
    print(f"{len(sources) - len({p.split(': ', 1)[0] for p in problems})} of {len(sources)} UI tests passed")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
