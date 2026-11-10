#!/usr/bin/env python3
"""Measure the GPU execution plan's 65,536-member Collatz gate."""

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time


def collatz_steps(value):
    steps = 0
    while value != 1:
        value = value // 2 if value % 2 == 0 else 3 * value + 1
        steps += 1
    return steps


def measure(command, output, error, environment, expected, gpu_prefix):
    print(shlex.join(command), flush=True)
    with output.open("wb") as stdout, error.open("wb") as stderr:
        start = time.perf_counter()
        process = subprocess.run(command, stdout=stdout, stderr=stderr, env=environment)
        elapsed = time.perf_counter() - start
    print(f"status {process.returncode}, wall time {elapsed:.9f} s", flush=True)
    lines = error.read_text(encoding="utf-8").splitlines()
    print("\n".join(lines), flush=True)
    process.check_returncode()
    if output.read_bytes() != expected:
        raise RuntimeError(f"{output} differs from the Collatz oracle")
    gpu_lines = [line for line in lines if line.startswith("gpu: ")]
    stats = [line for line in lines if not line.startswith("gpu: ")]
    if (len(gpu_lines) != 1 or not gpu_lines[0].startswith(gpu_prefix)
            or len(stats) != 1 or not stats[0].startswith("stats: members 65536, ")):
        raise RuntimeError(f"{error} does not contain the expected stats and GPU lines")
    return elapsed, stats[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neura", type=Path, help="path to the Release build of neura")
    parser.add_argument("--cpu", help="CPU affinity list passed to taskset, such as 7")
    parser.add_argument("--output", type=Path, help="directory for the image, input lists, and outputs")
    args = parser.parse_args()

    root = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="neura-collatz-", dir="/tmp"))
    root.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[1] / "backend" / "each" / "collatz.nra"
    executable = str(args.neura.resolve())
    image = root / "collatz.nvr"
    lists = root / "collatz.lists"
    environment = os.environ.copy()
    for variable in ("DISPLAY", "WAYLAND_DISPLAY", "NEURA_GPU"):
        environment.pop(variable, None)
    build = [executable, "build", str(source), "-o", str(image)]
    print(shlex.join(build), flush=True)
    subprocess.run(build, check=True, env=environment)

    values = range(1, 65_537)
    lists.write_bytes("".join(f"{value}\n" for value in values).encode("ascii"))
    expected = "".join(f"{collatz_steps(value)}\n" for value in values).encode("ascii")
    print(f"{len(values)} oracle lines, SHA-256 {hashlib.sha256(expected).hexdigest()}", flush=True)
    affinity = ["taskset", "-c", args.cpu] if args.cpu else []

    def run(mode, name, gpu_prefix):
        command = [*affinity, executable, "run", "--stats", mode, "--each", str(lists), str(image)]
        return measure(command, root / f"{name}.stdout", root / f"{name}.stderr", environment, expected, gpu_prefix)

    off_time, off_stats = run("--no-gpu", "off", "gpu: off")
    auto_time, auto_stats = run("--gpu=auto", "auto", "gpu: auto,")
    if off_stats != auto_stats:
        raise RuntimeError("run statistics differ between off and auto")
    ratio = auto_time / off_time
    print(f"auto/off {ratio:.6f}x, bound 1.0x ({'MET' if ratio <= 1 else 'MISSED'})")
    return 0 if ratio <= 1 else 1


if __name__ == "__main__":
    raise SystemExit(main())
