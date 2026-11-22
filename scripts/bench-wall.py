#!/usr/bin/env python3
r"""Compare the fixed workloads in tests/bench-wall.

The cold track measures prebuilt programs in fresh processes. The warmed
track runs a fresh fork with fixed warmup and sample counts: Neura shares a
TierCache, HotSpot uses JMH, and CoreCLR uses BenchmarkDotNet. Host console
output is outside the warmed timer. All results must agree byte for byte.
C# uses workstation, nonconcurrent GC. Each JDK also has a Serial GC run.

Each source declares its quick, full (default), and long input sizes. Pick
idiomatic or checked arithmetic for the managed counterparts. Neura keeps
its ordinary checked semantics in both comparisons. Build time is separate.
The .NET project has one shared build time. Each warmed managed tool uses
its own pinned benchmark harness. Optional user-mode instruction counts
come from separate cold perf runs. JSON retains individual samples.

    python3 scripts/bench-wall.py --quick
    python3 scripts/bench-wall.py --track warmed --jdk jdk21=JDK21 --dotnet DOTNET
    python3 scripts/bench-wall.py --long --candidate build/new/neura --no-java --no-dotnet
    python3 scripts/bench-wall.py --arithmetic checked --out-dir build/bench-checked

The runner targets Linux. It keeps all generated files in --out-dir.
"""

import argparse
import base64
import datetime
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "tests" / "bench-wall"
SIZES = re.compile(r"^// sizes: quick (\d+) full (\d+) long (\d+)$", re.MULTILINE)
SIZES_FORMAT = "// sizes: quick <n> full <n> long <n>"
MODES = ("quick", "full", "long")
STATS = re.compile(r"^stats: steps (\d+), peak heap cells (\d+)", re.MULTILINE)
CALLS = re.compile(r"^tier: programs .*?; calls (\d+),", re.MULTILINE)
INSTRUCTIONS = re.compile(r"^(\d+),[^,]*,instructions:u,", re.MULTILINE)
MAVEN_CENTRAL = "https://repo.maven.apache.org/maven2"
JMH_DEPS = (
    ("org/openjdk/jmh/jmh-core/1.37/jmh-core-1.37.jar",
     "dc0eaf2bbf0036a70b60798c785d6e03a9daf06b68b8edb0f1ba9eb3421baeb3"),
    ("org/openjdk/jmh/jmh-generator-annprocess/1.37/jmh-generator-annprocess-1.37.jar",
     "6a5604b5b804e0daca1145df1077609321687734a8b49387e49f10557c186c77"),
    ("net/sf/jopt-simple/jopt-simple/5.0.4/jopt-simple-5.0.4.jar",
     "df26cc58f235f477db07f753ba5a3ab243ebe5789d9f89ecf68dd62ea9a66c28"),
    ("org/apache/commons/commons-math3/3.6.1/commons-math3-3.6.1.jar",
     "1e56d7b058d28b65abd256b8458e3885b674c1d588fa43cd7d1cbb9c7ef2b308"),
)


def java_class(name):
    """The Java class of a program: its name in PascalCase."""
    special = {"nbody": "NBody", "sha256": "Sha256"}
    return special.get(name, "".join(part.capitalize() for part in name.split("-")))


def timed(command, cwd, env=None):
    """Returns wall seconds, the completed process, child peak RSS in KiB, and CPU seconds."""
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        started = time.perf_counter()
        child = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr)
        _, status, usage = os.wait4(child.pid, 0)
        elapsed = time.perf_counter() - started
        child.returncode = os.waitstatus_to_exitcode(status)
        stdout.seek(0)
        stderr.seek(0)
        process = subprocess.CompletedProcess(command, child.returncode, stdout.read(), stderr.read())
    # Linux reports ru_maxrss in KiB. macOS reports bytes.
    rss_kib = usage.ru_maxrss // 1024 if sys.platform == "darwin" else usage.ru_maxrss
    return elapsed, process, rss_kib, usage.ru_utime + usage.ru_stime


def fail(message):
    sys.exit(f"bench-wall: {message}")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--neura", default=str(ROOT / "build" / "neura"), help="the neura executable")
    parser.add_argument("--ref", help="Git reference for the supplied Neura build")
    parser.add_argument("--candidate", help="a second Neura build, measured alternately with --neura")
    parser.add_argument("--candidate-ref", help="Git reference for --candidate")
    parser.add_argument("--runs", type=int, default=5, help="runs per program and language (default 5)")
    sizes = parser.add_mutually_exclusive_group()
    sizes.add_argument("--quick", action="store_true", help="use each program's quick size")
    sizes.add_argument("--long", action="store_true", help="use each program's long size")
    parser.add_argument("--only", help="comma-separated program names")
    parser.add_argument("--size", action="append", default=[], metavar="NAME=N",
                        help="override one program's size; repeatable")
    parser.add_argument("--max-steps", type=int, default=10**12,
                        help="--max-steps for neura run (default 10^12; neura's own default is 10^8)")
    parser.add_argument("--max-heap-cells", type=int, default=2**32,
                        help="--max-heap-cells for neura run (default 2^32; neura's own default is 2^26, which "
                             "some long sizes exceed)")
    parser.add_argument("--no-java", action="store_true", help="skip Java even when it is installed")
    parser.add_argument("--jdk", action="append", default=[], metavar="LABEL=DIR",
                        help="add a named JDK installation containing bin/java and bin/javac; repeatable")
    parser.add_argument("--dotnet", help="path to the dotnet executable (default: look on PATH)")
    parser.add_argument("--no-dotnet", action="store_true", help="skip .NET even when it is installed")
    parser.add_argument("--no-perf", action="store_true", help="skip separate perf instruction captures")
    parser.add_argument("--diagnostics", action="store_true", help="capture separate --tier-stats runs")
    parser.add_argument("--arithmetic", choices=("idiomatic", "checked"), default="idiomatic",
                        help="arithmetic comparison to run; reports stay separate")
    parser.add_argument("--track", choices=("cold", "warmed"), default="cold")
    parser.add_argument("--warmups", type=int, default=3, help="workload invocations before warmed samples")
    parser.add_argument("--run-option", action="append", default=[], metavar="OPTION",
                        help="pass OPTION to neura run, for example --run-option=--tier=0 (repeatable)")
    parser.add_argument("--compare-tiers", action="store_true",
                        help="compare tiers 0, 1, 2 interpreted, 2 native, and 3 native where available")
    parser.add_argument("--out-dir", default=str(ROOT / "build" / "bench-wall"),
                        help="where images, classes, and results go (default build/bench-wall)")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    if args.warmups < 1:
        parser.error("--warmups must be positive")
    if args.compare_tiers and (args.candidate or args.track != "cold"):
        parser.error("--compare-tiers requires one Neura and the cold track")
    if args.candidate_ref and not args.candidate:
        parser.error("--candidate-ref requires --candidate")
    args.mode = "quick" if args.quick else "long" if args.long else "full"
    return args


def programs(args):
    overrides = {}
    for item in args.size:
        name, _, value = item.partition("=")
        if not value.isdigit():
            fail(f"--size expects NAME=N, got {item!r}")
        overrides[name] = int(value)
    selected = set(args.only.split(",")) if args.only else None
    found = []
    for source in sorted((BENCH / "neura").glob("*.nra")):
        name = source.stem
        if selected is not None and name not in selected:
            continue
        match = SIZES.search(source.read_text())
        if not match:
            fail(f"{source} has no '{SIZES_FORMAT}' line")
        size = overrides.get(name, int(match.group(MODES.index(args.mode) + 1)))
        found.append((name, source, size))
    if selected is not None:
        missing = selected - {name for name, _, _ in found}
        if missing:
            fail(f"no such program: {', '.join(sorted(missing))}")
    unknown = set(overrides) - {name for name, _, _ in found}
    if unknown:
        fail(f"--size names no selected program: {', '.join(sorted(unknown))}")
    return found


def find_java(args):
    if args.no_java:
        return None, "skipped (--no-java)"
    javac, java = shutil.which("javac"), shutil.which("java")
    if not javac or not java:
        return None, "not run: javac and java are not both on PATH"
    version = subprocess.run([java, "-version"], capture_output=True, text=True)
    first = (version.stderr or version.stdout).strip().splitlines()
    return (javac, java), first[0] if first else "unknown version"


def timed_build(command, cwd, label, env=None):
    """Runs one build command: its wall time in seconds."""
    elapsed, process, _, _ = timed(command, cwd, env)
    if process.returncode != 0:
        fail(f"{label} failed\n{process.stdout.decode(errors='replace')}{process.stderr.decode(errors='replace')}")
    return elapsed


def checked(command, cwd, label, env=None):
    process = subprocess.run(command, cwd=cwd, env=env, capture_output=True)
    if process.returncode:
        fail(f"{label} exited {process.returncode}\n{process.stderr.decode(errors='replace')}")
    return process


def git_ref(ref):
    if not ref:
        return None
    return checked(["git", "rev-parse", "--verify", f"{ref}^{{commit}}"], ROOT, "Git reference").stdout.decode().strip()


def java_variants(args):
    if args.no_java:
        return {}
    variants = {}
    requested = []
    for item in args.jdk:
        label, separator, home = item.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z0-9-]+", label) or not home:
            fail("--jdk expects LABEL=DIR")
        bin_dir = Path(home).resolve() / "bin"
        requested.append((label, str(bin_dir / "java"), str(bin_dir / "javac")))
    if not requested:
        found, _ = find_java(args)
        if found:
            requested.append(("java", found[1], found[0]))
    for label, java, javac in requested:
        if label in variants:
            fail(f"duplicate JDK label {label}")
        if not (Path(java).is_file() and Path(javac).is_file()):
            fail(f"{label}: java and javac are required")
        version = checked([java, "-version"], ROOT, label).stderr.decode().splitlines()[0]
        variants[label] = (java, javac, version)
    return variants


def find_dotnet(args):
    if args.no_dotnet:
        return None
    tool = args.dotnet or shutil.which("dotnet")
    if not tool:
        return None
    binary = Path(tool).resolve()
    env = dict(os.environ, PATH=os.pathsep.join([str(binary.parent), os.environ.get("PATH", "")]),
               DOTNET_ROOT=str(binary.parent), DOTNET_gcServer="0", DOTNET_gcConcurrent="0",
               DOTNET_CLI_TELEMETRY_OPTOUT="1")
    version = checked([str(binary), "--version"], ROOT, ".NET version", env).stdout.decode().strip()
    return str(binary), env, version


def find_perf(args, out):
    if args.no_perf:
        return None
    perf = shutil.which("perf")
    if perf is None:
        return None
    probe = subprocess.run([perf, "stat", "-x,", "-e", "instructions:u", "true"], cwd=out,
                           capture_output=True, env=dict(os.environ, LC_ALL="C"))
    return perf if probe.returncode == 0 and INSTRUCTIONS.search(probe.stderr.decode()) else None


def summary(samples):
    data = {}
    for name in ("seconds", "cpu_seconds", "peak_rss_kib", "instructions"):
        values = [sample[name] for sample in samples if sample.get(name) is not None]
        if values:
            data[f"median_{name}"] = statistics.median(values)
            data[name] = values
    data["samples"] = samples
    return data


def cold_runs(commands, out, args, perf):
    samples = {label: [] for label in commands}
    expected = None
    for index in range(args.runs):
        labels = list(commands)
        if index % 2:
            labels.reverse()
        for label in labels:
            command, env, _, is_neura = commands[label]
            elapsed, process, rss, cpu = timed(command, out, env)
            if process.returncode:
                fail(f"{label} exited {process.returncode}\n{process.stderr.decode(errors='replace')}")
            if expected is None:
                expected = process.stdout
            if process.stdout != expected:
                fail(f"{label}: output differs\nexpected {expected!r}\nreceived {process.stdout!r}")
            sample = {"seconds": elapsed, "cpu_seconds": cpu, "peak_rss_kib": rss}
            if is_neura:
                stats = STATS.search(process.stderr.decode())
                if stats is None:
                    fail(f"{label}: missing execution statistics")
                sample["steps"], sample["peak_heap_cells"] = (int(value) for value in stats.groups())
            samples[label].append(sample)
    # One separately instrumented process per variant. Headline timings are uninstrumented.
    if perf:
        for label, (command, env, _, _) in commands.items():
            capture = checked([perf, "stat", "-x,", "-e", "instructions:u", "--", *command], out, label,
                              dict(env or os.environ, LC_ALL="C"))
            if capture.stdout != expected:
                fail(f"{label}: perf run output differs")
            match = INSTRUCTIONS.search(capture.stderr.decode())
            if match is not None:
                samples[label][0]["instructions"] = int(match[1])
    rows = {}
    for label, values in samples.items():
        command, env, build_seconds, is_neura = commands[label]
        row = summary(values)
        row["build_seconds"] = build_seconds
        if is_neura:
            for key in ("steps", "peak_heap_cells"):
                if len({sample[key] for sample in values}) != 1:
                    fail(f"{label}: {key} differs between runs")
                row[key] = values[0][key]
            if args.diagnostics:
                proc = checked(command[:2] + ["--tier-stats"] + command[2:], out, label, env)
                text = proc.stderr.decode()
                match = CALLS.search(text)
                row["calls"] = int(match[1]) if match else 0
                row["tier_stats"] = [line for line in text.splitlines() if line.startswith("tier: ")]
                if proc.stdout != expected:
                    fail(f"{label}: diagnostic run output differs")
        rows[label] = row
    return rows, expected.decode()


def machine_details():
    machine = {"system": platform.system(), "machine": platform.machine(), "kernel": platform.release(),
               "cpus": os.cpu_count(), "python": platform.python_version()}
    if hasattr(os, "sched_getaffinity"):
        machine["affinity"] = sorted(os.sched_getaffinity(0))
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        model = re.search(r"^model name\s*:\s*(.*)$", cpuinfo.read_text(), re.MULTILINE)
        if model:
            machine["cpu"] = model[1]
    governors = sorted({path.read_text().strip()
                        for path in Path("/sys/devices/system/cpu").glob("cpu*/cpufreq/scaling_governor")})
    if governors:
        machine["governors"] = governors
    return machine


def build_warm_neura(neura, out, label):
    prefix = neura.parent
    lib = next(iter(sorted((prefix / "lib/neura").glob("libneura-core-*.so"))), None)
    compiler = shutil.which("clang++")
    if lib is None or compiler is None:
        fail("warmed Neura needs the Linux shared core beside the executable and clang++")
    runner = out / f"warm-{label}"
    timed_build([compiler, "-std=c++20", "-O2", "-I", str(ROOT / "src"),
                 str(BENCH / "warm-neura.cpp"), str(lib), f"-Wl,-rpath,{lib.parent}", "-o", str(runner)],
                out, label)
    return runner


def prepare_managed(args, out, javas, dotnet):
    java_sources = BENCH / "java"
    if javas and args.arithmetic == "checked":
        java_sources = out / "checked-java"
        java_sources.mkdir(exist_ok=True)
        java = next(iter(javas.values()))[0]
        checked([java, str(BENCH / "java/harness/CheckedSources.java"), str(BENCH / "java"), str(java_sources)],
                out, "generate checked Java")
    jars = []
    if javas and args.track == "warmed":
        deps = out / "jmh-deps"
        deps.mkdir(exist_ok=True)
        for relative, sha256 in JMH_DEPS:
            jar = deps / relative.rsplit("/", 1)[-1]
            if not jar.is_file():
                with urllib.request.urlopen(f"{MAVEN_CENTRAL}/{relative}", timeout=30) as response:
                    data = response.read()
                if hashlib.sha256(data).hexdigest() != sha256:
                    fail(f"digest mismatch for {jar.name}")
                jar.write_bytes(data)
            elif hashlib.sha256(jar.read_bytes()).hexdigest() != sha256:
                fail(f"digest mismatch for {jar.name}")
            jars.append(jar)
        for label, (_, javac, _) in javas.items():
            wrapper = out / f"wrapper-{label}"
            wrapper.mkdir(exist_ok=True)
            checked([javac, "-cp", str(jars[0]), "-processorpath", os.pathsep.join(map(str, jars[:2])),
                     "-d", str(wrapper), str(BENCH / "java/harness/Workload.java"),
                     str(BENCH / "java/harness/ProcessCpuTime.java")], out, f"{label}: compile JMH wrapper")
    dotnet_build = None
    if dotnet:
        binary, env, _ = dotnet
        env.setdefault("NUGET_PACKAGES", str(out / "nuget-packages"))
        env.setdefault("DOTNET_CLI_HOME", str(out / "dotnet-home"))
        # Keep the restore, obj files and package logs out of the source tree.
        project = out / "csharp"
        shutil.copytree(BENCH / "csharp", project, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("bin", "obj"))
        config = "Checked" if args.arithmetic == "checked" else "Release"
        checked([binary, "restore", "--nologo", f"-p:Configuration={config}"], project, ".NET restore", env)
        dotnet_build = timed_build([binary, "build", "--no-restore", "--nologo", "-c", config],
                                   project, ".NET build", env)
    return java_sources, jars, dotnet_build


def managed_commands(args, out, javas, dotnet, name, size, prepared):
    commands = {}
    klass = java_class(name)
    java_sources, _, dotnet_build = prepared
    source = java_sources / f"{klass}.java"
    for label, (java, javac, _) in javas.items():
        classes = out / f"classes-{label}"
        classes.mkdir(exist_ok=True)
        build = timed_build([javac, "-d", str(classes), str(source)], out, f"{name}: {label} build")
        for suffix, options in [("", []), ("-serial", ["-XX:+UseSerialGC"])]:
            commands[label + suffix] = ([java, *options, "-cp", str(classes), klass, str(size)], None, build, False)
    if dotnet:
        binary, env, _ = dotnet
        config = "Checked" if args.arithmetic == "checked" else "Release"
        dll = out / "csharp/bin" / config / "net10.0/BenchWall.dll"
        commands["dotnet"] = [binary, str(dll), name, str(size)], env, dotnet_build, False
    return commands


def verify_text_edges(neura, managed, out):
    image = out / "images/text-split-edge.nvr"
    checked([str(neura), "build", str(BENCH / "fixtures/text-split-edge.nra"), "-o", str(image)],
            out, "build text edge cases")
    expected = "1 0 \n1 6 single\n3 2 ||\n3 7 é|猫|\n".encode()
    if checked([str(neura), "run", str(image)], out, "Neura text edge cases").stdout != expected:
        fail("Neura: text edge output differs")
    for label, (command, env, _, _) in managed.items():
        if label.endswith("-serial"):
            continue
        edge = command[:2] + ["--verify-text-edges"] if label == "dotnet" else command[:-1] + ["--verify-edges"]
        if checked(edge, out, f"{label}: text edge cases", env).stdout != expected:
            fail(f"{label}: text edge output differs")


def warmed_managed(args, out, javas, dotnet, name, size, expected, prepared):
    built = managed_commands(args, out, javas, dotnet, name, size, prepared)
    rows = {}
    retained = out / "managed-samples" / name
    retained.mkdir(parents=True, exist_ok=True)
    for label, (java, _, _) in javas.items():
        cp = os.pathsep.join(map(str, [out / f"wrapper-{label}", out / f"classes-{label}", *prepared[1]]))
        for suffix, options in [("", []), ("-serial", ["-jvmArgsAppend=-XX:+UseSerialGC"])]:
            key = label + suffix
            with tempfile.TemporaryDirectory(dir=out) as scratch:
                output = Path(scratch) / "jmh.json"
                checked([java, "-cp", cp, "org.openjdk.jmh.Main", "benchwall.jmh.Workload.run",
                         "-p", f"program={java_class(name)}", "-p", f"size={size}",
                         "-p=checksum=" + base64.b64encode(expected.encode()).decode(),
                         "-bm", "ss", "-f", "1", "-wi", str(args.warmups), "-i", str(args.runs),
                         "-bs", "1", "-wbs", "1", "-foe", "true", "-prof", "benchwall.jmh.ProcessCpuTime",
                         *options, "-rf", "json", "-rff", str(output)], out, key)
                data = json.loads(output.read_text())[0]
            wall = data["primaryMetric"]["rawData"][0]
            cpu = data["secondaryMetrics"]["processCpu"]["rawData"][0]
            samples = [{"seconds": w / 1000, "cpu_seconds": c / 1000} for w, c in zip(wall, cpu, strict=True)]
            if len(samples) != args.runs:
                fail(f"{key}: incomplete JMH samples")
            rows[key] = {**summary(samples), "build_seconds": built[key][2]}
            (retained / f"{key}.json").write_text(json.dumps(samples, indent=2) + "\n")
    if dotnet:
        (binary, dll, _, _), env, build, _ = built["dotnet"]
        expected_file = retained / "checksum.txt"
        expected_file.write_bytes(expected.encode())
        sample_dir = retained / "dotnet"
        checked([binary, dll, "--warmed", name, str(size), str(args.runs), str(args.warmups),
                 str(sample_dir), str(expected_file)], out / "csharp", name, env)
        data = json.loads((sample_dir / "samples.json").read_text())
        samples = [{"seconds": w, "cpu_seconds": c}
                   for w, c in zip(data["wall_seconds"], data["cpu_seconds"], strict=True)]
        if len(samples) != args.runs:
            fail(f"{name}: incomplete BenchmarkDotNet samples")
        rows["dotnet"] = {**summary(samples), "build_seconds": build}
    return rows


def warmed_neura_run(runner, image, size, args, out, label):
    command = [str(runner), str(image), str(size), str(args.warmups), str(args.runs), str(args.max_steps),
               str(args.max_heap_cells)]
    proc = checked(command, out, label)
    text = proc.stderr.decode()
    samples = [{"seconds": float(wall), "cpu_seconds": float(cpu)}
               for wall, cpu in re.findall(r"^sample (\S+) (\S+)$", text, re.MULTILINE)]
    stats = STATS.search(text)
    if len(samples) != args.runs or stats is None:
        fail(f"{label}: incomplete warmed samples")
    return {**summary(samples), "steps": int(stats[1]), "peak_heap_cells": int(stats[2])}, proc.stdout.decode()


def main():
    args = parse_args()
    os.environ.setdefault("NEURA_GPU", "off")
    neura = Path(args.neura).resolve()
    tools = {"neura": neura}
    if args.candidate:
        tools["candidate"] = Path(args.candidate).resolve()
    for label, tool in tools.items():
        if not tool.is_file():
            fail(f"{label} executable does not exist")
    if args.track == "warmed" and args.run_option:
        fail("warmed Neura uses the shared-cache runner and does not accept --run-option")
    out = Path(args.out_dir).resolve()
    images = out / "images"
    images.mkdir(parents=True, exist_ok=True)
    javas = {} if args.compare_tiers else java_variants(args)
    dotnet = None if args.compare_tiers else find_dotnet(args)
    perf = find_perf(args, out) if args.track == "cold" else None
    if args.compare_tiers:
        args.diagnostics = True
    runtime_versions = {label: checked([str(tool), "--version"], out, label).stdout.decode().strip()
                        for label, tool in tools.items()}
    runtime_versions.update({label: version for label, (_, _, version) in javas.items()})
    if dotnet:
        runtime_versions["dotnet"] = dotnet[2]
    report = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "mode": args.mode, "track": args.track, "arithmetic": args.arithmetic, "runs": args.runs,
        "warmups": args.warmups if args.track == "warmed" else None,
        "versions": runtime_versions, "git": {"neura": git_ref(args.ref), "candidate": git_ref(args.candidate_ref)},
        "max_steps": args.max_steps, "max_heap_cells": args.max_heap_cells, "run_options": args.run_option,
        "gpu": os.environ["NEURA_GPU"], "instructions": bool(perf), "machine": machine_details(), "results": [],
        "neura_version": runtime_versions["neura"],
    }
    prepared = prepare_managed(args, out, javas, dotnet)
    runners = {label: build_warm_neura(tool, out, label) for label, tool in tools.items()} if args.track == "warmed" else {}

    native = None
    for name, source, size in programs(args):
        commands = {}
        warm_rows = {}
        expected = None
        for label, tool in tools.items():
            image = images / f"{label}-{name}.nvr"
            build_seconds = timed_build([str(tool), "build", str(source), "-o", str(image)], out, f"{name}: {label} build")
            command = [str(tool), "run", "--stats", "--max-steps", str(args.max_steps), "--max-heap-cells",
                       str(args.max_heap_cells), *args.run_option, str(image), "--", str(size)]
            if args.track == "warmed":
                row, output = warmed_neura_run(runners[label], image, size, args, out, label)
                row["build_seconds"] = build_seconds
                if expected is not None and output != expected:
                    fail(f"{label}: warmed output differs")
                expected = output
                warm_rows[label] = row
            else:
                commands[label] = command, None, build_seconds, True
        if args.compare_tiers:
            if native is None:
                native = native_available(neura, out, image)
            rows, expected = cold_runs(tier_commands(command, build_seconds, native), out, args, perf)
            metrics = {(row["steps"], row["peak_heap_cells"]) for row in rows.values()}
            if len(metrics) != 1:
                fail(f"{name}: tiers disagree on steps or peak heap cells")
            entry = {"name": name, "size": size, "output": expected, **rows}
        else:
            if args.track == "warmed":
                warm_rows.update(warmed_managed(args, out, javas, dotnet, name, size, expected, prepared))
                rows = warm_rows
            else:
                managed = managed_commands(args, out, javas, dotnet, name, size, prepared)
                if name == "text-split":
                    verify_text_edges(neura, managed, out)
                commands.update(managed)
                rows, expected = cold_runs(commands, out, args, perf)
            entry = {"name": name, "size": size, "output": expected, **rows}
        medians = "  ".join(f"{label} {row['median_seconds']:.6f}s" for label, row in rows.items())
        print(f"{name:16} {size:>10}  {medians}", flush=True)
        report["results"].append(entry)

    (out / "bench-wall.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "bench-wall.md").write_text(markdown(report))
    return 0


def native_available(neura, cwd, image):
    """Whether `neura run --jit=on` can run native code on this machine."""
    process = subprocess.run([str(neura), "run", "--jit=on", "--max-steps", "1", str(image)], cwd=cwd,
                             capture_output=True)
    return b"--jit=on" not in process.stderr


def tier_commands(command, build_seconds, native):
    modes = [
        ("Tier 0", "0", "off", "off"),
        ("Tier 1", "1", "off", "off"),
        ("Tier 2 interpreter", "2", "off", "off"),
    ]
    if native:
        modes.append(("Tier 2 native", "2", "on", "off"))
        if sys.platform == "linux" and platform.machine() == "x86_64":
            modes.append(("Tier 3 native", "2", "on", "native"))
    return {label: (command[:2] + [f"--tier={tier}", f"--jit={jit}"] + command[2:],
                    dict(os.environ, NEURA_TIER3=tier3), build_seconds, True)
            for label, tier, jit, tier3 in modes}


def markdown(report):
    machine = report["machine"]
    lines = [
        f"# {report['track'].capitalize()} benchmarks, {report['arithmetic']} arithmetic",
        "",
        f"{report['mode'].capitalize()} inputs, median of {report['runs']} samples "
        f"({report['date']}, {machine['system']} {machine['kernel']} {machine['machine']}).",
        "",
        f"- CPU: {machine.get('cpu', '')}, affinity {machine.get('affinity', '')}, "
        f"governors {', '.join(machine.get('governors', []))}",
        f"- Limits: {report['max_steps']} steps, {report['max_heap_cells']} heap cells",
    ]
    if report["warmups"]:
        lines.append(f"- Neura warmups: {report['warmups']} full workload invocations per process")
    for label, version in report["versions"].items():
        ref = report["git"].get(label)
        lines.append(f"- {label}: {version}" + (f", {ref}" if ref else ""))
    lines.extend([
        "",
        "| Program | Size | Runtime | Wall median (s) | Wall range (s) | CPU median (s) | Calls | Steps | Peak heap cells | Instructions:u | Build (s) |",
        "| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for entry in report["results"]:
        for label, row in entry.items():
            if not isinstance(row, dict):
                continue
            samples = row["seconds"]
            cpu = f"{row['median_cpu_seconds']:.6f}" if "median_cpu_seconds" in row else ""
            build = f"{row['build_seconds']:.6f}" if "build_seconds" in row else ""
            lines.append(f"| {entry['name']} | {entry['size']} | {label} | {row['median_seconds']:.6f} | "
                         f"{min(samples):.6f}–{max(samples):.6f} | {cpu} | {row.get('calls', '')} | "
                         f"{row.get('steps', '')} | {row.get('peak_heap_cells', '')} | "
                         f"{row.get('median_instructions', '')} | {build} |")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.exit(main())
