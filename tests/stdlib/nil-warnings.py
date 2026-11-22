#!/usr/bin/env python3
"""Fail when checking a standard-library module emits a nil warning."""

from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    neura = Path(sys.argv[1]).resolve()
    stdlib = Path(sys.argv[2]).resolve()
    sources = sorted(stdlib.rglob("*.nra"))
    if not sources:
        raise SystemExit(f"no Neura standard-library sources under {stdlib}")
    environment = os.environ | {"NEURA_GPU": "off", "NO_COLOR": "1"}

    def check(source):
        result = subprocess.run(
            [neura, "check", "--error-format=json", "--warn-library", source],
            cwd=source.parent, env=environment, capture_output=True, timeout=90,
            text=True, encoding="utf-8", errors="replace",
        )
        warnings = []
        for line in result.stderr.splitlines():
            if not line.startswith("{"):
                continue
            diagnostic = json.loads(line)
            if diagnostic.get("code") in ("W0006", "W0007"):
                main_span = next(span for span in diagnostic["spans"] if span.get("primary"))
                reported = Path(main_span["file"])
                if not reported.is_absolute():
                    reported = source.parent / reported
                reported = reported.resolve()
                if any(reported.is_relative_to(directory) for directory in excluded):
                    continue
                warnings.append((reported, main_span["offset"], diagnostic["code"], diagnostic["rendered"]))
        return source, result, warnings

    sites = {}
    errors = 0
    jobs = min(4, os.cpu_count() or 1)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for source, result, warnings in pool.map(check, sources):
            if result.returncode != 0:
                errors += 1
                print(f"{source.relative_to(stdlib)} could not be checked (status {result.returncode})",
                      file=sys.stderr)
                print(result.stderr, file=sys.stderr)
            for file, offset, code, rendered in warnings:
                sites[(file, offset, code)] = rendered
    for key in sorted(sites):
        print(sites[key], file=sys.stderr)
    print(f"{len(sources)} stdlib modules checked, {len(sites)} nil-warning sites, {errors} check failures")
    return int(bool(errors or sites))


if __name__ == "__main__":
    raise SystemExit(main())
