#!/usr/bin/env python3
"""Check warnings in tests that deliberately pass or dereference nil.

These exercise nil reference traps, the result of equality against nil, and
the negative cases from the flow-sensitive narrowing fixture. Other fixture
suites keep their own diagnostic expectations.
"""

import json
from pathlib import Path
import subprocess
import sys


def main():
    neura = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    manifest = Path(__file__).with_suffix(".json")
    recorded = json.loads(manifest.read_text(encoding="utf-8"))

    for name, expected in recorded.items():
        result = subprocess.run(
            [neura, "check", "--error-format=json", root / name],
            capture_output=True, encoding="utf-8", timeout=120,
        )
        if result.returncode:
            raise AssertionError(f"{name}: check exited {result.returncode}\n{result.stderr}")
        observed = []
        for line in result.stderr.splitlines():
            if not line.startswith("{"):
                continue
            diagnostic = json.loads(line)
            if diagnostic["code"] not in ("W0006", "W0007"):
                continue
            primary = next(span for span in diagnostic["spans"] if span["primary"])
            observed.append([diagnostic["code"], primary["line"], primary["column"], primary["length"]])
        if sorted(observed) != sorted(expected):
            raise AssertionError(f"{name}: expected {expected}, got {observed}\n{result.stderr}")

    print(f"{sum(map(len, recorded.values()))} intentional warning spans matched in {len(recorded)} fixtures")


if __name__ == "__main__":
    main()
