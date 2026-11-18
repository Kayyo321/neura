#!/usr/bin/env python3
"""Launch times and memory of the runtime service (docs/design/Service030.md,
"Gate and acceptance"; docs/runtime/Service.md, "Measurements").

    scripts/bench-launch.py --neura build/neura [--before OLD/neura] --out launch.json
                            [--samples 30] [--work DIR]

Every time is measured end to end by this script, from just before it
starts `neura run` to the end of that process, so it includes the
dispatcher, the loading of libneura-core and, for a service launch, the
client's connection. The phases inside a service launch come from
`--runtime-report` and are relative to the client's start in `cli::run`.

Scenarios, for each workload and mode:

    before   `neura run` of the build given by --before (the milestone's base)
    direct   `neura run`
    cold     `--runtime=required` with no supervisor: an on-demand start
    uncached `--runtime=required` with a supervisor and a ready worker, the
             image dropped from the cache before each sample
    warm     `--runtime=required` with the image cached and a ready worker
             (each sample waits for it; the reported pass must say service-warm)
    burst    eight warm launches at once; each one's time is a sample

Workloads: hello-world, the largest image of tests/benchmarks, a program
with a long startup, and one with tasks; modes: the default,
NEURA_TIER3=off and --jit=off. A native dependency and a terminal are not
measured (docs/runtime/Service.md says why).

It also records the supervisor's resident size without the cache, the
cache bytes, a ready worker's resident size, the wakeups of an idle
supervisor and ready worker in 60 seconds (--idle-seconds), and the sizes
of neurad and libneura-core. It prints a Markdown summary.

The run uses its own XDG_RUNTIME_DIR and XDG_STATE_HOME and shuts down
every supervisor it started.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/service"))
import protocol_client as pc  # noqa: E402

MODES = {"default": ([], {}), "tier3-off": ([], {"NEURA_TIER3": "off"}), "jit-off": (["--jit=off"], {})}
WORKLOADS = {
    "hello-world": (ROOT / "examples/hello-world/hello-world.nra", []),
    "long-startup": (ROOT / "tests/service/programs/long-startup.nra", []),
    "tasks": (ROOT / "tests/concurrency/programs/fan-out.nra", []),
}


# The size argument each benchmark takes, small: the launch is measured, not
# the benchmark (scripts/runtime-identity.py uses larger ones).
BENCHMARK_ARGUMENTS = {"hash-map": "100", "sort": "100", "strings": "100", "json": "10", "tree-map": "100",
                       "sha256": "1024", "array-list": "100", "format": "100", "time-text": "100"}


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def summary(values: list[float]) -> dict:
    return {"p50": round(percentile(values, 0.5), 3), "p95": round(percentile(values, 0.95), 3),
            "mean": round(statistics.fmean(values), 3) if values else None, "samples": len(values)}


def timed(command: list[str], env: dict, cwd: Path) -> tuple[float, subprocess.CompletedProcess]:
    start = time.perf_counter()
    result = subprocess.run(command, capture_output=True, env=env, cwd=cwd, stdin=subprocess.DEVNULL)
    return (time.perf_counter() - start) * 1000, result


def resident_kib(pid: int) -> int:
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    return 0


def switches(pid: int) -> int:
    total = 0
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith(("voluntary_ctxt_switches:", "nonvoluntary_ctxt_switches:")):
            total += int(line.split()[1])
    return total


def service_processes(env: dict) -> dict[str, list[int]]:
    marker = f"XDG_RUNTIME_DIR={env['XDG_RUNTIME_DIR']}".encode()
    found: dict[str, list[int]] = {"supervisor": [], "worker": []}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if marker not in (entry / "environ").read_bytes().split(b"\0"):
                continue
            command = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        if b"neurad" not in command:
            continue
        found["worker" if b"--worker" in command else "supervisor"].append(int(entry.name))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--neura", required=True)
    parser.add_argument("--before")
    parser.add_argument("--out", required=True)
    parser.add_argument("--samples", type=int, default=30)
    parser.add_argument("--idle-seconds", type=int, default=60)
    parser.add_argument("--work")
    args = parser.parse_args()
    neura = Path(args.neura).resolve()
    bin_dir = neura.parent
    work = Path(args.work or tempfile.mkdtemp(prefix="nbl-", dir="/tmp")).resolve()
    for name in ("r", "s", "h", "images"):
        (work / name).mkdir(mode=0o700, parents=True, exist_ok=True)
    base_env = dict(os.environ, XDG_RUNTIME_DIR=str(work / "r"), XDG_STATE_HOME=str(work / "s"),
                    HOME=str(work / "h"), NEURA_GPU="off")
    for name in ("NEURA_JIT", "NEURA_TIER3", "NEURA_TIER", "NEURA_TIER_EAGER", "NEURA_TIER_CHECK"):
        base_env.pop(name, None)

    def build(source: Path, name: str) -> Path:
        image = work / "images" / f"{name}.nvr"
        if not image.exists():
            subprocess.run([str(neura), "build", str(source), "-o", str(image)], check=True, env=base_env,
                           capture_output=True)
        return image

    images = {name: (build(source, name), arguments) for name, (source, arguments) in WORKLOADS.items()}
    largest = None
    for source in sorted((ROOT / "tests/benchmarks").glob("*.nra")):
        image = build(source, f"bench-{source.stem}")
        if largest is None or image.stat().st_size > largest.stat().st_size:
            largest = image
    images["largest-benchmark"] = (largest, [BENCHMARK_ARGUMENTS.get(largest.stem.removeprefix("bench-"), "1")])

    service = pc.Service(bin_dir, base_env)
    report_file = work / "report.json"
    results: dict = {
        "machine": {"cpus": os.cpu_count(), "load": os.getloadavg(), "nice": os.getpriority(os.PRIO_PROCESS, 0),
                    "uname": " ".join(os.uname())},
        "neura": str(neura), "before": args.before, "samples": args.samples, "workloads": {},
    }

    def shutdown() -> None:
        subprocess.run([str(neura), "runtime", "shutdown", "--terminate", "--timeout=10"], env=base_env,
                       capture_output=True)
        for pid in sum(service_processes(base_env).values(), []):
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        service.socket.unlink(missing_ok=True)

    def wait_ready(control) -> None:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            control.send("List", [])
            reply = control.receive()
            if reply and reply.number("ReadyWorkers") == 1:
                return
            time.sleep(0.002)

    for workload, (image, arguments) in images.items():
        tail = [str(image), "--", *arguments] if arguments else [str(image)]
        entry = results["workloads"].setdefault(workload, {"image bytes": image.stat().st_size})
        for mode, (options, variables) in MODES.items():
            env = dict(base_env, **variables)
            row: dict = {}
            phases: dict[str, list[float]] = {}

            def service_sample(label: str, reported: bool = False) -> float:
                # Timed samples write no report, as a direct run writes none;
                # a reported pass gives the phases and checks the path.
                if not reported:
                    elapsed, result = timed([str(neura), "run", "--runtime=required", *options, *tail], env, work)
                    if result.returncode != 0:
                        raise SystemExit(f"{workload} {mode} {label}: {result.stderr.decode()}")
                    return elapsed
                elapsed, result = timed([str(neura), "run", "--runtime=required", f"--runtime-report={report_file}",
                                         *options, *tail], env, work)
                report = json.loads(report_file.read_text())
                if result.returncode != 0 or not report["path"].startswith("service"):
                    raise SystemExit(f"{workload} {mode} {label}: {report} {result.stderr.decode()}")
                if label == "warm" and report["path"] != "service-warm":
                    raise SystemExit(f"{workload} {mode}: a warm sample took {report['path']}")
                if label == "warm":
                    for phase, ns in report["times"].items():
                        phases.setdefault(phase, []).append(ns / 1e6)
                return elapsed

            if args.before:
                row["before"] = summary([timed([args.before, "run", *options, *tail], env, work)[0]
                                         for _ in range(args.samples)])
            row["direct"] = summary([timed([str(neura), "run", *options, *tail], env, work)[0]
                                     for _ in range(args.samples)])
            cold = []
            for _ in range(args.samples):
                shutdown()
                cold.append(service_sample("cold"))
            row["cold"] = summary(cold)
            control = service.hello(purpose=2)
            uncached = []
            for _ in range(args.samples):
                subprocess.run([str(neura), "runtime", "cache", "clear"], env=base_env, capture_output=True)
                wait_ready(control)
                uncached.append(service_sample("uncached"))
            row["uncached"] = summary(uncached)
            warm = []
            service_sample("prime")
            for _ in range(args.samples):
                wait_ready(control)
                warm.append(service_sample("warm"))
            row["warm"] = summary(warm)
            for _ in range(max(5, args.samples // 2)):
                wait_ready(control)
                service_sample("warm", reported=True)
            row["warm phases (ms from the client's start)"] = {phase: summary(values)
                                                                for phase, values in sorted(phases.items())}
            burst = []
            for _ in range(max(1, args.samples // 8)):
                wait_ready(control)
                times: list[float] = []
                lock = threading.Lock()

                def one() -> None:
                    elapsed, result = timed([str(neura), "run", "--runtime=required", *options, *tail], env, work)
                    if result.returncode != 0:
                        raise SystemExit(f"burst: {result.stderr.decode()}")
                    with lock:
                        times.append(elapsed)
                threads = [threading.Thread(target=one) for _ in range(8)]
                for thread in threads:
                    thread.start()
                for thread in threads:
                    thread.join()
                burst.extend(times)
            row["burst of 8"] = summary(burst)
            control.close()
            entry[mode] = row
            print(f"{workload:18} {mode:10} direct {row['direct']['p50']:7.2f} warm {row['warm']['p50']:7.2f} "
                  f"uncached {row['uncached']['p50']:7.2f} cold {row['cold']['p50']:7.2f} ms (p50)", flush=True)

    # Memory and idle wakeups of a supervisor with one ready worker and the
    # hello-world image cached.
    shutdown()
    hello = images["hello-world"][0]
    subprocess.run([str(neura), "runtime", "start"], env=base_env, capture_output=True, check=True)
    subprocess.run([str(neura), "run", "--runtime=required", str(hello)], env=base_env, capture_output=True)
    control = service.hello(purpose=2)
    wait_ready(control)
    control.send("List", [])
    listing = control.receive()
    control.close()
    processes = service_processes(base_env)
    supervisor = processes["supervisor"][0]
    worker = processes["worker"][0]
    before_switches = (switches(supervisor), switches(worker))
    time.sleep(args.idle_seconds)
    after_switches = (switches(supervisor), switches(worker))
    cache_bytes = listing.number("CacheBytes")
    core = sorted((bin_dir / "lib/neura").glob("libneura-core-*"))
    results["memory"] = {
        "supervisor resident KiB (cache in its own memory file, counted apart)": resident_kib(supervisor),
        "cache bytes": cache_bytes,
        "ready worker resident KiB": resident_kib(worker),
        f"supervisor wakeups in {args.idle_seconds} s idle": after_switches[0] - before_switches[0],
        f"ready worker wakeups in {args.idle_seconds} s idle": after_switches[1] - before_switches[1],
    }
    results["sizes"] = {
        "neurad bytes": (bin_dir / "neurad").stat().st_size,
        "libneura-core bytes": core[0].stat().st_size if core else None,
    }
    shutdown()
    Path(args.out).write_text(json.dumps(results, indent=2) + "\n")

    print("\n| Workload | Mode | before | direct | cold | uncached | warm | burst of 8 |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for workload, entry in results["workloads"].items():
        for mode in MODES:
            row = entry[mode]
            cell = lambda name: (f"{row[name]['p50']:.2f} / {row[name]['p95']:.2f}" if name in row else "-")  # noqa
            print(f"| {workload} | {mode} | {cell('before')} | {cell('direct')} | {cell('cold')} | {cell('uncached')} "
                  f"| {cell('warm')} | {cell('burst of 8')} |")
    print("\n" + json.dumps(results["memory"], indent=2) + "\n" + json.dumps(results["sizes"], indent=2))
    if not args.work:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
