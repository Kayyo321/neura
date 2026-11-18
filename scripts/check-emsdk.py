#!/usr/bin/env python3
"""Verify the Emscripten SDK checkout and installed compiler for the web target."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tomllib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emsdk", type=Path, help="Emscripten SDK checkout")
    args = parser.parse_args()
    lock = Path(__file__).resolve().parent.parent / "packaging/components.lock.toml"
    with lock.open("rb") as config:
        data = tomllib.load(config)
    if data["schema"] != 1:
        parser.error(f"unknown component lock schema in {lock}")
    expected = data["toolchains"]["emscripten"]
    emsdk = args.emsdk.resolve()
    emcc = emsdk / "upstream/emscripten/emcc"
    try:
        sdk = subprocess.check_output(["git", "-C", emsdk, "rev-parse", "--verify", "HEAD"], text=True,
                                      stderr=subprocess.STDOUT).strip()
        first_line = subprocess.check_output([emcc, "--version"], text=True, stderr=subprocess.STDOUT).splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError) as error:
        parser.error(f"could not verify {emsdk}: {error}")
    match = re.fullmatch(r"emcc .+ ([\d.]+) \(([0-9a-f]{40})\)", first_line)
    if not match:
        parser.error(f"could not read the compiler version and revision from {emcc}: {first_line}")
    version, compiler = match.groups()
    observed = {"version": version, "sdk_revision": sdk, "compiler_revision": compiler}
    for field, value in observed.items():
        if value != expected[field]:
            print(f"Emscripten {field}: expected {expected[field]}, found {value}", file=sys.stderr)
            return 1
    print(f"Emscripten {version}, SDK {sdk}, compiler {compiler}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
