"""Source, prebuilt, warmed and diagnostic phases of bench-held-out.py."""

import base64
from contextlib import contextmanager, nullcontext
import datetime
import hashlib
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from xml.sax.saxutils import quoteattr


ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "tests" / "bench-wall"
SDK_LABELS = ("before", "after")
GOVERNOR = Path("/sys/devices/system/cpu/cpu7/cpufreq/scaling_governor")
RETRY_BUDGET = 2
ADMISSION_BUDGET = 3
IDLE_SECONDS = 4
SOURCE_CLOCKS = ("source_through_encoding_seconds", "source_through_encoding_cpu_seconds")
VERIFY_MODES = (
    ("tier0-reference", "0", "off", "off"),
    ("tier1-lifted", "1", "off", "off"),
    ("tier2-interpreted", "2", "off", "off"),
    ("tier2-region-jit", "2", "on", "off"),
    ("tier3-native-requested", "2", "on", "native"),
)


def require(test, message):
    if not test:
        raise SystemExit(f"bench-held-out: {message}")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def output_hash(data):
    return hashlib.sha256(data).hexdigest()


def output_bytes(output):
    if output.get("encoding") == "base64":
        return base64.b64decode(output["data"], validate=True)
    return output["output"].encode("utf-8")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def environment(args):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("NEURA_", "LD_", "DOTNET_", "COMPlus_"))
           and k not in {"JAVA_TOOL_OPTIONS", "_JAVA_OPTIONS", "JDK_JAVA_OPTIONS", "CLASSPATH", "COLUMNS"}}
    env.update(LC_ALL="C.UTF-8", TZ="UTC", NEURA_GPU="off", NEURA_TIER3="native",
               DOTNET_MULTILEVEL_LOOKUP="0", DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
    if args.stop_file:
        env["NEURA_HELD_OUT_STOP"] = str(args.stop_file.resolve())
    return env


class StopRequested(Exception):
    pass


def check_stop(env):
    stop = env.get("NEURA_HELD_OUT_STOP")
    if stop and Path(stop).exists():
        raise StopRequested(f"STOP requested at {stop}")


@contextmanager
def alarm_handler(handler):
    previous = signal.signal(signal.SIGALRM, handler)
    try:
        yield
    finally:
        signal.signal(signal.SIGALRM, previous)


def timed(command, env, cwd=ROOT, *, pass_fds=()):
    """Match the existing wall/wait4 boundary with optional STOP interruption."""
    check_stop(env)
    stop_path = env.get("NEURA_HELD_OUT_STOP")
    stop_during_spawn = False

    def defer_stop(_signum, _frame):
        nonlocal stop_during_spawn
        stop_during_spawn = stop_during_spawn or Path(stop_path).exists()

    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        child = None
        try:
            # Preserve the child's signal mask. Latch a stop while Popen is
            # binding its handle, then deliver it where cleanup owns the child.
            with alarm_handler(defer_stop) if stop_path else nullcontext():
                start = time.monotonic_ns()
                child = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=stdout,
                                         stderr=stderr, start_new_session=bool(stop_path), pass_fds=pass_fds)
            if stop_during_spawn:
                raise StopRequested(f"STOP requested at {stop_path}")
            check_stop(env)
            _, status, usage = os.wait4(child.pid, 0)
            end = time.monotonic_ns()
            child.returncode = os.waitstatus_to_exitcode(status)
        except BaseException:
            if child is not None:
                if stop_path:
                    with alarm_handler(signal.SIG_IGN):
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
                elif child.poll() is None:
                    child.kill()
                    child.wait()
            raise
        stdout.seek(0)
        stderr.seek(0)
        process = subprocess.CompletedProcess(command, child.returncode, stdout.read(), stderr.read())
    return {"seconds": (end - start) / 1e9, "cpu_seconds": usage.ru_utime + usage.ru_stime,
            "peak_rss_kib": usage.ru_maxrss, "start_ns": start, "command": command,
            "status": process.returncode, "stdout_sha256": output_hash(process.stdout),
            "stderr_sha256": output_hash(process.stderr)}, process


def checked(command, env, cwd=ROOT):
    record, process = timed(command, env, cwd)
    require(process.returncode == 0,
            f"{command[0]} exited {process.returncode}\n{process.stderr.decode('utf-8', errors='replace')}")
    return record, process


def companion_receipt(command, env, cwd, path, *, allow_failure=False):
    # A memory-backed descriptor can hold a large receipt without blocking
    # the child while the parent waits. Publish the raw receipt after reaping.
    path.unlink(missing_ok=True)
    with os.fdopen(os.memfd_create("neura-held-out-receipt"), "rb") as stream:
        descriptor = stream.fileno()
        record, process = timed([*command[:2], f"--report-fd={descriptor}", *command[2:]], env, cwd,
                                pass_fds=(descriptor,))
        stream.seek(0)
        payload = stream.read()
    if payload:
        temporary = path.with_suffix(".tmp")
        temporary.write_bytes(payload)
        temporary.replace(path)
    require(payload and (process.returncode == 0 or allow_failure),
            f"{path}: {command[1]} companion failed\n{process.stderr.decode(errors='replace')}")
    try:
        receipt = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit(f"bench-held-out: {path}: invalid companion receipt: {error}") from None
    require(isinstance(receipt, dict), f"{path}: invalid companion receipt object")
    return record, process, receipt


def stage_fixtures(manifest, cases, original_names, folder, env):
    sources = {}
    revision = manifest["fixture_revision"]
    for case in cases:
        original = case["name"] in original_names
        for file, expected in case["sha256"].items():
            path = Path(file)
            require(not path.is_absolute() and ".." not in path.parts, f"invalid fixture path {file}")
            if original:
                _, item = checked(["git", "show", f"{revision}:{file}"], env)
                contents = item.stdout
            else:
                contents = (ROOT / path).read_bytes()
            require(output_hash(contents) == expected, f"frozen source digest differs: {file}")
            require(file not in sources or sources[file]["sha256"] == expected, f"conflicting frozen fixture: {file}")
            target = folder / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents)
            sources[file] = {"sha256": expected, "revision": revision if original else None}
    return sources


def linked_libraries(binary, env):
    _, process = checked(["ldd", str(binary)], env)
    text = process.stdout.decode()
    require("not found" not in text, f"missing dependency of {binary}")
    paths = re.findall(r"^\s*libneura-\S+ => (\S+) \(", text, re.MULTILINE)
    return {Path(path).name.split("-", 2)[1]: str(Path(path).resolve()) for path in paths}


def sdk_provenance(binary, ref, env):
    companion = binary.with_name("neura-held-out")
    test = binary.with_name("neura-test")
    compiler = binary.with_name("neurac")
    runner = binary.with_name("neura-run")
    tools = [binary, companion, test, compiler, runner]
    for path in tools:
        require(path.is_file(), f"missing SDK tool {path}; build neura and neura-held-out in its source tree")
    _, version = checked([str(binary), "--version"], env)
    _, companion_version = checked([str(companion), "--version"], env)
    require(version.stdout == companion_version.stdout, f"benchmark companion version differs from {binary}")
    _, abi = checked([str(companion), "--abi"], env)
    abi = json.loads(abi.stdout)
    require(abi["version"] == 1 and re.fullmatch(r"[0-9a-f]{64}", abi["headers_sha256"]),
            f"missing ABI provenance for {companion}")
    require(abi.get("receipt_fd_protocol") == 1, f"missing receipt descriptor protocol for {companion}")
    libs = linked_libraries(test, env)
    require(set(libs) == {"core", "sdk"} and libs == linked_libraries(companion, env) == linked_libraries(compiler, env)
            and {"core": libs["core"]} == linked_libraries(runner, env), f"mixed SDK linkage near {binary}")
    prefix = binary.parent.parent
    stdlib = next((p.resolve() for p in [prefix / "share/neura/stdlib", prefix / "stdlib"]
                   if (p / "std").is_dir()), None)
    require(stdlib is not None, f"cannot locate the SDK's standard library near {binary}; use its installed layout")
    paths = {str(p): digest(p) for p in [*tools, *(Path(p) for p in libs.values())]}
    library_hashes = {p.relative_to(stdlib).as_posix(): digest(p) for p in sorted(stdlib.rglob("*.nra"))}
    return {"neura": str(binary), "companion": str(companion), "ref": ref, "version": version.stdout.decode().strip(),
            "abi": abi, "libraries": libs, "sha256": paths, "stdlib": str(stdlib), "stdlib_sha256": library_hashes}


def worker_count(options):
    for index, option in enumerate(options):
        if option == "--parallel-workers":
            return int(options[index + 1])
    return None


def phase_options(case, args, max_steps):
    options = ["--gpu=off", f"--tier={args.tier}", f"--jit={args.jit}", "--max-steps", str(max_steps), *case["options"]]
    if worker_count(case["options"]) is None:
        options.extend(["--parallel-workers", str(args.workers)])
    return options


def cpu_configuration(require_ready=True):
    affinity = sorted(os.sched_getaffinity(0))
    governor = GOVERNOR.read_text().strip()
    if require_ready:
        require(affinity == [7], "use taskset -c 7 for CPU 7 qualification")
        require(governor == "performance", "set CPU 7 governor to performance")
    return {"affinity": affinity, "governor": governor, "seconds_per_observation": IDLE_SECONDS,
            "maximum_admission_observations": ADMISSION_BUDGET}


def cpu_preflight(env):
    def ticks():
        values = next(line.split()[1:9] for line in Path("/proc/stat").read_text().splitlines()
                      if line.startswith("cpu7 "))
        counts = list(map(int, values))
        return sum(counts), counts[3] + counts[4]

    check_stop(env)
    config = cpu_configuration(require_ready=False)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    before, idle_before = ticks()
    time.sleep(IDLE_SECONDS)
    check_stop(env)
    after, idle_after = ticks()
    busy = 1 - (idle_after - idle_before) / (after - before) if after > before else None
    stable = (config["governor"] == GOVERNOR.read_text().strip() == "performance" and
              config["affinity"] == sorted(os.sched_getaffinity(0)) == [7])
    return {**config, "recorded_at": started, "busy_fraction": busy, "affinity_and_governor_ok": stable,
            "idle_enough": stable and busy is not None and busy < .10}


def distorted(wall, cpu):
    gap = wall - cpu
    return gap > .002 or (gap > .0005 and gap > wall * .01)


def metrics(records, schedule):
    if any(schedule["clean_per_arm"][label] != schedule["expected_per_arm"] for label in SDK_LABELS):
        return None
    return {"wall_ratio": statistics.median(item["seconds"] for item in records["after"]) /
            statistics.median(item["seconds"] for item in records["before"]),
            "cpu_ratio": statistics.median(item["cpu_seconds"] for item in records["after"]) /
            statistics.median(item["cpu_seconds"] for item in records["before"])}


def forks(labels, args, folder, action, samples, env, limit_extra_forks=False, admit=cpu_preflight,
          *, clock_keys=("seconds", "cpu_seconds")):
    wall_key, cpu_key = clock_keys
    order = [(fork, label) for fork in range(args.runs) for label in (labels if fork % 2 == 0 else labels[::-1])]
    accepted = {}
    verified = {label: [] for label in labels}
    attempts = []
    extra_forks = 0

    def run_slot(slot, round_index):
        fork, label = slot
        directory = folder / f"{fork}-{round_index}-{label}"
        directory.mkdir(parents=True)
        admissions = []
        attempt = {"fork": fork, "arm": label, "round": round_index, "admissions": admissions}
        attempts.append(attempt)
        if args.cpu7:
            for _ in range(ADMISSION_BUDGET):
                admissions.append(admit(env))
                write_json(directory / "admission.json", admissions)
                if admissions[-1]["idle_enough"]:
                    break
            else:
                attempt["state"] = "not_admitted"
                return False
        record = action(label, directory)
        record.update(fork=fork, attempt=round_index, arm=label)
        affected = False
        for sample in samples(record):
            sample["disturbed"] = distorted(sample[wall_key], sample[cpu_key]) if args.cpu7 else None
            affected = affected or sample["disturbed"]
        state = "struck" if affected else "clean"
        record["timing_state"] = state
        path = directory / "record.json"
        write_json(path, record)
        attempt.update(state=state, receipt=str(path))
        verified[label].append(record)
        if not affected:
            accepted[slot] = record
        return True

    for round_index in range(RETRY_BUDGET + 1):
        for slot in order:
            if slot in accepted:
                continue
            if round_index and limit_extra_forks and extra_forks >= RETRY_BUDGET:
                break
            ran = run_slot(slot, round_index)
            if round_index and ran:
                extra_forks += 1

    for label, logical in verified.items():
        if logical:
            continue
        # Admission can block every timed fork. Still check the workload and
        # keep build artifacts available for the following phases.
        directory = folder / f"diagnostic-{label}"
        directory.mkdir(parents=True)
        record = action(label, directory)
        record.update(arm=label, timing_state="diagnostic_only")
        logical.append(record)
        write_json(directory / "record.json", record)

    clean = {label: [accepted[fork, label] for fork in range(args.runs) if (fork, label) in accepted]
             for label in labels}
    owed = [{"fork": fork, "arm": label} for fork, label in order if (fork, label) not in accepted]
    schedule = {"status": "owed" if owed else "complete", "expected_per_arm": args.runs,
                "clean_per_arm": {label: len(rows) for label, rows in clean.items()}, "owed_slots": owed,
                "extra_timed_forks": extra_forks, "attempts": attempts,
                "logical_receipts": {label: f"diagnostic-{label}/record.json" for label, rows in verified.items()
                                     if rows[0]["timing_state"] == "diagnostic_only"}}
    write_json(folder / "schedule.json", schedule)
    return clean, verified, schedule


def make_managed(args, ext, folder, env):
    sources = [row["twins"] for row in ext if "twins" in row]
    if not sources:
        return {}
    folder.mkdir(parents=True, exist_ok=True)
    java_home = args.jdk or (Path(env["JAVA_HOME"]) if "JAVA_HOME" in env else None)
    javac = (java_home / "bin/javac").resolve() if java_home else Path(shutil.which("javac") or "/nonexistent")
    javac = javac.resolve()
    java_home = javac.parent.parent
    java = java_home / "bin/java"
    dotnet = (args.dotnet or Path(shutil.which("dotnet") or "/nonexistent")).resolve()
    cc = shutil.which("cc")
    require(java.is_file() and javac.is_file() and dotnet.is_file() and cc,
            "managed rows need a JDK, .NET SDK, and C compiler; use --jdk and --dotnet to select versions")
    for twins in sources:
        for twin in twins.values():
            require(digest(ROOT / twin["source"]) == twin["sha256"], f"frozen managed source changed: {twin['source']}")
    managed = BENCH / "held-out"
    java_driver = managed / "java/TimedWorkload.java"
    java_clock = managed / "java/process-clock.c"
    csharp_driver = managed / "csharp/TimedWorkload.cs"
    classes = folder / "classes"
    classes.mkdir(exist_ok=True)
    checked([str(javac), "-d", str(classes), str(java_driver),
             *(str(ROOT / pair["java"]["source"]) for pair in sources)], env)
    checked([cc, "-shared", "-fPIC", "-O2", "-I", str(java_home / "include"), "-I", str(java_home / "include/linux"),
             str(java_clock), "-o", str(folder / "libheld-out-clock.so")], env)
    dotnet_env = dict(env, DOTNET_ROOT=str(dotnet.parent), DOTNET_CLI_HOME=str(folder),
                      DOTNET_gcServer="0", DOTNET_gcConcurrent="0")
    _, dotnet_version = checked([str(dotnet), "--version"], dotnet_env)
    major = dotnet_version.stdout.decode().strip().partition(".")[0]
    require(major.isdecimal(), "unable to identify the .NET SDK major version")
    project = folder / "TimedWorkload.csproj"
    items = "\n".join(f"    <Compile Include={quoteattr(str(path))} />"
                      for path in [csharp_driver, *(ROOT / pair["csharp"]["source"] for pair in sources)])
    project.write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType><TargetFramework>net{major}.0</TargetFramework>
    <StartupObject>TimedWorkload</StartupObject><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <Nullable>enable</Nullable><InvariantGlobalization>true</InvariantGlobalization>
    <ServerGarbageCollection>false</ServerGarbageCollection><ConcurrentGarbageCollection>false</ConcurrentGarbageCollection>
  </PropertyGroup>
  <ItemGroup>
{items}
  </ItemGroup>
</Project>
''', encoding="utf-8")
    config = folder / "NuGet.Config"
    config.write_text("<configuration><packageSources><clear /></packageSources></configuration>\n", encoding="utf-8")
    checked([str(dotnet), "build", str(project), "-c", "Release", "-o", str(folder), "--nologo",
             "--disable-build-servers", "--configfile", str(config)], dotnet_env)
    _, java_version = checked([str(java), "-version"], env)
    jvm = java_home / "lib/server/libjvm.so"
    _, runtimes = checked([str(dotnet), "--list-runtimes"], dotnet_env)
    _, active_runtime = checked([str(dotnet), str(folder / "TimedWorkload.dll"), "--runtime-info"], dotnet_env)
    coreclr = Path(active_runtime.stdout.decode().strip()) / "libcoreclr.so"
    return {
        "java": {"prefix": [str(java), "-XX:+UseSerialGC", "-Dfile.encoding=UTF-8", f"-Djava.library.path={folder}",
                            "-cp", str(classes), "TimedWorkload"], "version": java_version.stderr.decode().strip(),
                 "sha256": {str(p): digest(p) for p in [java, javac, jvm, java_driver, java_clock,
                                                       folder / "libheld-out-clock.so"]},
                 "classes_sha256": {p.name: digest(p) for p in sorted(classes.glob("*.class"))}},
        "csharp": {"prefix": [str(dotnet), str(folder / "TimedWorkload.dll")],
                   "version": dotnet_version.stdout.decode().strip(), "runtimes": runtimes.stdout.decode().splitlines(),
                   "environment": {k: v for k, v in dotnet_env.items() if k not in env},
                   "sha256": {str(p): digest(p) for p in [dotnet, coreclr, csharp_driver, project,
                       folder / "TimedWorkload.dll", folder / "TimedWorkload.runtimeconfig.json"]}},
    }


def replay_summary(events):
    """Exclusive code-generation wall intervals per thread and their cross-thread union."""
    details = {"inline_fallback", "entry_zero_fallback"}
    threads = {}
    for event in events:
        threads.setdefault(event["thread"], []).append(event)
    spans = []
    summed_wall = 0
    summed_cpu = 0
    for log in threads.values():
        points = []
        for event in log:
            kind = event["kind"]
            if kind in details:
                continue
            sign = -1 if kind == "lock_wait" else 1
            if event["cpu_ns"] is None:
                summed_cpu = None
            elif summed_cpu is not None:
                # A wait's CPU belongs to the timed compiler only when the wait is inside an outer build.
                contained = any(other["kind"] not in details | {"lock_wait"}
                                and other["start_ns"] <= event["start_ns"] <= event["end_ns"] <= other["end_ns"]
                                for other in log) if sign < 0 else True
                if contained:
                    summed_cpu += sign * event["cpu_ns"]
            points.append((event["start_ns"], int(sign > 0), int(sign < 0)))
            points.append((event["end_ns"], -int(sign > 0), -int(sign < 0)))
        building = waiting = previous = 0
        for point, build, wait in sorted(points):
            if building > 0 and not waiting and point > previous:
                spans.extend([(previous, 1), (point, -1)])
                summed_wall += point - previous
            building += build
            waiting += wait
            previous = point
    union = overlap = occupied = previous = 0
    for point, change in sorted(spans):
        if occupied > 0:
            union += point - previous
        if occupied > 1:
            overlap += point - previous
        occupied += change
        previous = point
    return {"compiler_cpu_ns": summed_cpu, "compiler_wall_union_ns": union,
            "compiler_wall_sum_over_threads_ns": summed_wall, "compiler_wall_parallel_overlap_ns": overlap,
            "cache_hits": "outside build intervals", "lock_waits": sum(e["kind"] == "lock_wait" for e in events)}


def tier_observation(stderr):
    text = stderr.decode(errors="replace")
    lines = [line for line in text.splitlines() if line.startswith("tier: ")]
    region = re.search(r"^tier: regions .*?; entered (\d+),", text, re.MULTILINE)
    region_steps = re.search(r"^tier: steps .*?\(in regions (\d+)\)", text, re.MULTILINE)
    jit = re.search(r"^tier: jit (\w+): (\d+) regions compiled .*?; entries native (\d+), interpreted (\d+)",
                    text, re.MULTILINE)
    native = re.search(r"^tier: native programs (\d+) .*?; steps in native code (\d+)$", text, re.MULTILINE)
    return {"lines": lines, "region_entries": int(region[1]) if region else 0,
            "region_steps": int(region_steps[1]) if region_steps else 0,
            "jit_mode": jit[1] if jit else "off" if "tier: jit off" in lines else None,
            "jit_regions_compiled": int(jit[2]) if jit else 0,
            "jit_native_entries": int(jit[3]) if jit else 0,
            "jit_interpreted_entries": int(jit[4]) if jit else None,
            "tier3_programs": int(native[1]) if native else 0, "tier3_native_steps": int(native[2]) if native else 0}


def reference(case, binary, image, options, env, harness, sources, diagnose_tiers=False):
    diagnostics = ["--tier-stats"] if diagnose_tiers else []
    if case["mode"] == "test":
        command = [str(binary), "test", "--reporter=json", *diagnostics, *options, case["source"]]
    else:
        command = [str(binary), "run", "--runtime=off", "--stats", *diagnostics, *options,
                   str(image), "--", *case["arguments"]]
    record, process = timed(command, env, sources)
    if diagnose_tiers:
        record["tier_observation"] = tier_observation(process.stderr)
        record["environment"] = {key: value for key, value in env.items() if key.startswith("NEURA_") or key in {"LC_ALL", "TZ"}}
        require(record["tier_observation"]["lines"], f"{case['name']}: tier verification omitted diagnostic counters")
    if case["mode"] == "test":
        events = [json.loads(line) for line in process.stdout.splitlines()]
        results = [{"id": e["id"], "steps": e["steps"], "completed": e["state"] == "passed",
                    "peak_heap_cells": e["peakHeapCells"], "peak_stack_cells": e.get("peakStackCells"),
                    "heap_limit_trap": e["message"].startswith("heap limit exceeded")}
                   for e in events if e["event"] == "testResult"]
        require(bool(results), f"{case['name']}: ordinary test reporter produced no test results")
        return {**record, "results": results, "test_events": events}
    stderr = process.stderr.decode(errors="replace")
    match = harness.STATS.search(stderr)
    require(match is not None, f"{case['name']}: ordinary run omitted execution counters")
    trap_prefix = f"{image}: trap: "
    trap = next((line.removeprefix(trap_prefix) for line in stderr.splitlines() if line.startswith(trap_prefix)), None)
    return {**record, "results": [{"steps": int(match[1]), "peak_heap_cells": int(match[2]),
                                  "completed": process.returncode == 0, "trap": trap,
                                  "heap_limit_trap": trap is not None and trap.startswith("heap limit exceeded")}]}


def test_streams(events):
    streams = {}
    for event in events:
        if event.get("event", "output") != "output":
            continue
        key = (event["id"], event["stream"])
        streams.setdefault(key, bytearray()).extend(output_bytes(event))
    return {f"{test} {stream}": output_hash(data) for (test, stream), data in streams.items()}


def suite_execution_stderr(raw, compile_diagnostics, name):
    if not compile_diagnostics:
        return raw
    require(raw.endswith(compile_diagnostics), f"{name}: unable to separate ordinary source diagnostics from suite stderr")
    return raw[:-len(compile_diagnostics)]


def validate_ordered_test_output(events, stdout, stderr, name):
    streams = {"stdout": stdout, "stderr": stderr}
    positions = dict.fromkeys(streams, 0)
    for event in events:
        if event.get("event") != "output":
            continue
        stream = event["stream"]
        data = output_bytes(event)
        at = streams[stream].find(data, positions[stream])
        require(at != -1, f"{name}: ordinary suite {stream} differs from ordered reporter output for {event['id']}")
        positions[stream] = at + len(data)


def validate_execution_streams(stdout, stderr, expected_output, expected_error, name):
    require(stdout == expected_output, f"{name}: stdout differs from ordinary CLI")
    require(stderr == expected_error, f"{name}: stderr differs from ordinary CLI execution")


def logical(results):
    return [{key: item[key] for key in ("id", "steps", "completed", "status", "trap_kind") if key in item}
            for item in results]


def compare_reference(reference_result, execution, case):
    results = reference_result["results"]
    observed = execution["results"]
    require(len(results) == len(observed), f"{case['name']}: differs from ordinary runner's entry count")
    for expected, actual in zip(results, observed):
        require(expected["steps"] == actual["steps"] and expected.get("id") == actual.get("id"),
                f"{case['name']}: runner order or step count differs for {expected.get('id', 'program')}")
        require(expected["completed"] == (actual["completed"] and actual["status"] == 0),
                f"{case['name']}: runner completion differs for {expected.get('id', 'program')}")
        require(expected["heap_limit_trap"] == (actual["trap_kind"] == "heap limit exceeded"),
                f"{case['name']}: heap limit trap differs for {expected.get('id', 'program')}")
        if case["mode"] == "run":
            require(expected["trap"] == actual["trap"], f"{case['name']}: ordinary program trap text differs")
    require(reference_result["status"] == execution["status"], f"{case['name']}: ordinary runner status differs")


def measure_managed(language, args, managed, extension, env, expected_path, case):
    require(len(case["arguments"]) == 1, f"{case['name']}: managed twin requires one size argument")
    expected = expected_path.read_bytes()
    sample_pattern = re.compile(rb"^sample (\d+) (\d+) (\d+)$")
    tool = managed[language]
    twin = Path(extension["twins"][language]["source"]).stem
    record, process = checked([*tool["prefix"], twin, *case["arguments"], str(args.warmups), str(args.samples),
                               str(expected_path)], dict(env, **tool.get("environment", {})))
    require(process.stdout == expected, f"{case['name']}: {language} output differs")
    invocations = []
    for line in process.stderr.splitlines():
        match = sample_pattern.fullmatch(line)
        require(match is not None, f"{case['name']}: unexpected {language} sample line: {line!r}")
        index, wall, cpu = map(int, match.groups())
        require(index == len(invocations) and min(wall, cpu) > 0, f"{case['name']}: invalid {language} clocks")
        invocations.append({"index": index, "warmup": index < args.warmups, "seconds": wall / 1e9,
                            "cpu_seconds": cpu / 1e9, "stdout_sha256": record["stdout_sha256"], "status": 0})
    require(len(invocations) == args.warmups + args.samples, f"{case['name']}: incomplete {language} fork")
    record["invocations"] = invocations
    return record


def measure(case, extension, args, sdks, harness, command_for, max_steps, managed, out, env, sources):
    name = case["name"]
    folder = out / name
    folder.mkdir(exist_ok=True)
    suite = case["mode"] == "test"
    options = phase_options(case, args, max_steps)
    workers = worker_count(options)
    same_budget = workers == (worker_count(case["options"]) or 1)

    def build_image(label, directory):
        receipt = directory / "build.json"
        image = directory / "workload.nvr"
        catalog = directory / "suite.json"
        command = [sdks[label]["companion"], "suite-build" if suite else "program-build", str(image)]
        if suite:
            command += [str(catalog), *options]
        else:
            command += case.get("build_options", [])
        record, process, timing = companion_receipt([*command, case["source"]], env, sources, receipt)
        require(timing["version"] == 1 and timing["compile_ns"] > 0, f"{name}: invalid source compile receipt")
        record.update(timing, receipt=str(receipt), image=str(image), image_sha256=digest(image), image_bytes=image.stat().st_size,
                      source_through_encoding_seconds=(timing["encoded_at_ns"] - record["start_ns"]) / 1e9)
        require(0 < record["source_through_encoding_seconds"] <= record["seconds"] and
                (record["source_through_encoding_cpu_seconds"] or 0) > 0, f"{name}: compilation clocks disagree")
        if suite:
            diagnostics = directory / "source-compile.stderr"
            diagnostics.write_bytes(process.stderr)
            record.update(catalog=str(catalog), catalog_sha256=digest(catalog), source_compile_stderr=str(diagnostics))
        return record

    source, source_verified, source_schedule = forks(SDK_LABELS, args, folder / "source", build_image, lambda r: [r], env,
                                                      clock_keys=SOURCE_CLOCKS)
    images = {}
    catalog_paths = {}
    source_diagnostics = {}
    for label, variants in source_verified.items():
        require(len({row["image_sha256"] for row in variants}) == 1, f"{name}: {label} build bytes changed between forks")
        if suite:
            require(len({row["catalog_sha256"] for row in variants}) == 1, f"{name}: {label} catalog changed between forks")
            catalog_paths[label] = variants[0]["catalog"]
            require(len({row["stderr_sha256"] for row in variants}) == 1,
                    f"{name}: {label} source diagnostics changed between forks")
            source_diagnostics[label] = Path(variants[0]["source_compile_stderr"]).read_bytes()
        images[label] = Path(variants[0]["image"])

    # The startup program budget uses an ordinary build. Check that its
    # bytes also match the artifact measured at the encoding boundary.
    startup_images = dict(images)
    ordinary_builds = {}
    if not suite:
        for label, sdk in sdks.items():
            directory = folder / "ordinary-build" / label
            directory.mkdir(parents=True, exist_ok=True)
            image = directory / "workload.nvr"
            command = [sdk["neura"], "build", *case.get("build_options", []), case["source"], "-o", str(image)]
            record, _ = checked(command, env, sources)
            record.update(image=str(image), image_sha256=digest(image))
            require(record["image_sha256"] == source_verified[label][0]["image_sha256"],
                    f"{name}: companion image differs from ordinary {label} build")
            ordinary_builds[label] = record
            startup_images[label] = image

    expected_output = None
    expected_error = None

    def budget(label, directory):
        nonlocal expected_output, expected_error
        binary = sdks[label]["neura"]
        command = command_for(case, binary, startup_images[label], args.tier, args.jit)
        # The historical budget retains its original default worker and command.
        record, proc = checked(command, env, sources)
        errors = suite_execution_stderr(proc.stderr, source_diagnostics[label], f"{name}: {label}") if suite else proc.stderr
        if expected_output is None:
            expected_output, expected_error = proc.stdout, errors
        validate_execution_streams(proc.stdout, errors, expected_output, expected_error, f"{name}: {label} startup budget")
        record["execution_stderr_sha256"] = output_hash(errors)
        if suite:
            raw_errors = directory / "ordinary.stderr"
            raw_errors.write_bytes(proc.stderr)
            record.update(source_compile_stderr_sha256=output_hash(source_diagnostics[label]),
                          ordinary_stderr=str(raw_errors))
        require("output_sha256" not in extension or record["stdout_sha256"] == extension["output_sha256"],
                f"{name}: output differs from the frozen expectation")
        if not suite:
            record["image_sha256"] = ordinary_builds[label]["image_sha256"]
        return record

    startup, startup_verified, startup_schedule = forks(SDK_LABELS, args, folder / "startup", budget, lambda r: [r], env)
    expected_path = folder / "expected.stdout"
    expected_path.write_bytes(expected_output)
    expected_error_path = folder / "expected.stderr"
    expected_error_path.write_bytes(expected_error)
    references = {label: reference(case, sdk["neura"], images[label], options, env, harness, sources)
                  for label, sdk in sdks.items()}
    for label, ref in references.items():
        require(ref["status"] == 0 and all(row["completed"] for row in ref["results"]),
                f"{name}: normal execution reference failed")
        if suite:
            validate_ordered_test_output(ref["test_events"], expected_output, expected_error, f"{name}: {label}")
        else:
            require(ref["stdout_sha256"] == output_hash(expected_output), f"{name}: normal program output differs")
        if "steps" in extension:
            require(sum(row["steps"] for row in ref["results"]) == extension["steps"], f"{name}: frozen steps differ")
    tier_verification = {}
    if args.verify_tiers:
        for label, sdk in sdks.items():
            anchor = references[label]
            modes = {}
            for mode, tier, jit, tier3 in VERIFY_MODES:
                alternate = options.copy()
                alternate[1:3] = [f"--tier={tier}", f"--jit={jit}"]
                mode_env = dict(env, NEURA_TIER3=tier3)
                other = reference(case, sdk["neura"], images[label], alternate, mode_env, harness, sources,
                                  diagnose_tiers=True)
                require(other["status"] == 0 and logical(other["results"]) == logical(anchor["results"]),
                        f"{name}: {label} logical counts differ in {mode}")
                if suite:
                    require(test_streams(other["test_events"]) == test_streams(anchor["test_events"]),
                            f"{name}: {label} per-test output differs in {mode}")
                else:
                    require(other["stdout_sha256"] == output_hash(expected_output), f"{name}: {label} output differs in {mode}")
                modes[mode] = other
            tier_verification[label] = modes

    def execute(label, directory, action, runtime_options=options, heap_probe=False):
        receipt = directory / "execution.json"
        command = [sdks[label]["companion"], action]
        if action == "warm":
            command += [str(args.warmups), str(args.samples)]
        if suite:
            command += ["--suite", catalog_paths[label], "test"]
        else:
            command += ["run", "--runtime=off"]
        command += [*runtime_options, str(images[label])]
        if not suite:
            command += ["--", *case["arguments"]]
        record, process, raw = companion_receipt(command, env, sources, receipt, allow_failure=heap_probe)
        require(raw["version"] == 1 and raw["parallel_workers"] == workers, f"{name}: bad execution receipt or worker count")
        require(len(raw["invocations"]) == (args.warmups + args.samples if action == "warm" else 1),
                f"{name}: incomplete {action} fork")
        if heap_probe:
            require(raw["max_heap_cells"] == 1, f"{name}: heap probe limit was not applied")
        for index, invocation in enumerate(raw["invocations"]):
            require(invocation["index"] == index and invocation["warmup"] == (action == "warm" and index < args.warmups),
                    f"{name}: wrong invocation index or warmup state")
            invocation["seconds"] = invocation.pop("wall_ns") / 1e9
            invocation["cpu_seconds"] = invocation.pop("cpu_ns") / 1e9
            require(min(invocation["seconds"], invocation["cpu_seconds"]) > 0, f"{name}: invalid invocation clock")
            stdout = process.stdout if action == "cold" else output_bytes(invocation.pop("stdout"))
            stderr = process.stderr if action == "cold" else output_bytes(invocation.pop("stderr"))
            invocation.update(stdout_sha256=output_hash(stdout), stderr_sha256=output_hash(stderr))
            if not heap_probe:
                compare_reference(references[label], invocation, case)
                validate_execution_streams(stdout, stderr, expected_output, expected_error, f"{name}: {label} {action}")
            if action == "replay":
                events = invocation["compile_events"]
                invocation["compiler_summary"] = replay_summary(events) if events is not None else None
                if suite:
                    invocation["test_output_sha256"] = test_streams(invocation.pop("test_output"))
                    require(invocation["test_output_sha256"] == test_streams(references[label]["test_events"]),
                            f"{name}: replay per-test output differs from normal reporter")
        require(action != "cold" or raw["invocations"][0]["status"] == process.returncode,
                f"{name}: cold process exit differs from its receipt")
        return {**record, **raw, "image_sha256": digest(images[label]), "receipt": str(receipt)}

    cold, cold_verified, cold_schedule = forks(SDK_LABELS, args, folder / "prebuilt",
                                                lambda l, d: execute(l, d, "cold"), lambda r: [r], env)
    def all_samples(record):
        return [entry for entry in record["invocations"] if not entry["warmup"]]

    def warm_arm(label, directory):
        if label in sdks:
            return execute(label, directory, "warm")
        return measure_managed(label, args, managed, extension, env, expected_path, case)

    labels = SDK_LABELS + (tuple(managed) if "twins" in extension else ())
    warm_all, _, warm_schedule = forks(labels, args, folder / "warm", warm_arm, all_samples, env,
                                       limit_extra_forks=extension.get("warmed", False))
    warm = {label: warm_all[label] for label in SDK_LABELS}
    twins = {label: warm_all[label] for label in labels if label not in sdks}
    debugger = {}
    probes = {}
    for label, sdk in sdks.items():
        for action, result in (("replay", debugger), ("heap-limit", probes)):
            directory = folder / action / label
            directory.mkdir(parents=True, exist_ok=True)
            if action == "replay":
                result[label] = execute(label, directory, action)
            else:
                limit = [*options, "--max-heap-cells", "1"]
                ref = reference(case, sdk["neura"], images[label], limit, env, harness, sources)
                probe = execute(label, directory, "cold", limit, heap_probe=True)
                invocation = probe["invocations"][0]
                compare_reference(ref, invocation, case)
                if suite:
                    command = [sdk["neura"], "test", *limit, case["source"]]
                    plain, proc = timed(command, env, sources)
                    errors = suite_execution_stderr(proc.stderr, source_diagnostics[label], f"{name}: {label} heap probe")
                    validate_ordered_test_output(ref["test_events"], proc.stdout, errors, f"{name}: {label} heap probe")
                    require(plain["status"] == invocation["status"] and plain["stdout_sha256"] == invocation["stdout_sha256"]
                            and output_hash(errors) == invocation["stderr_sha256"],
                            f"{name}: {label} prebuilt heap probe differs from ordinary suite output")
                    ref["ordinary_terminal"] = plain
                else:
                    require(ref["stdout_sha256"] == invocation["stdout_sha256"], f"{name}: heap probe output differs")
                result[label] = {"reference": ref, "prebuilt": probe,
                                 "heap_limit_traps": sum(row["trap_kind"] == "heap limit exceeded" for row in invocation["results"])}

    observed = {label: logical(samples[0]["invocations"][0]["results"]) for label, samples in cold_verified.items()}
    require(observed["before"] == observed["after"] or args.allow_cost_change,
            f"{name}: ordered completion, traps or steps differ across SDKs")
    compiled = {label: [{"seconds": row["source_through_encoding_seconds"],
                         "cpu_seconds": row["source_through_encoding_cpu_seconds"]} for row in records]
                for label, records in source.items()}
    schedules = {"startup_budget": startup_schedule, "source_to_image": source_schedule,
                 "cold_prebuilt": cold_schedule, "warmed": warm_schedule}
    warm_samples = {label: [item for r in records for item in all_samples(r)] for label, records in warm.items()}
    ratios = {key: metrics(records, schedules[key]) for key, records in
              (("startup_budget", startup), ("source_to_image", compiled),
               ("cold_prebuilt", cold), ("warmed", warm_samples))}
    formatted = [f"{key} {value['wall_ratio']:.3f}x" if value else f"{key} owed"
                 for key, value in ratios.items()]
    print(f"{name:22} " + "   ".join(formatted), flush=True)
    medians = {label: {"wall_seconds": statistics.median(s["seconds"] for r in records for s in all_samples(r)),
                       "cpu_seconds": statistics.median(s["cpu_seconds"] for r in records for s in all_samples(r))}
               for label, records in warm_all.items() if records}
    return {"fixture": case, "extension": extension or None, "runtime_options": options, "parallel_workers": workers,
            "options_match_startup_worker_default": same_budget, "startup_budget": startup, "source_to_image": source,
            "ordinary_program_build": ordinary_builds,
            "cold_prebuilt": cold, "warmed": warm, "managed_warmed": twins, "warmed_medians": medians,
            "diagnostic_replay": debugger,
            "heap_limit_probe": probes, "reference": references, "tier_verification": tier_verification,
            "expected_stdout_sha256": output_hash(expected_output), "expected_execution_stderr_sha256": output_hash(expected_error),
            "source_compile_stderr": {label: source_verified[label][0]["source_compile_stderr"] for label in source_diagnostics},
            "expected_logical_results": observed, "ratios": ratios, "schedules": schedules,
            "startup_verified_commands": {label: records[0]["command"] for label, records in startup_verified.items()}}


def render_report(report):
    lines = [f"# Held-out {report['comparison']} phases", "", "Times are median wall / process CPU seconds.", ""]
    if not report["cpu7_preflight"]:
        lines.extend(["This run skipped the CPU 7 timing preflight.", ""])
    columns = ("Workload", "Startup before → after", "Source through encoding before → after",
               "Prebuilt before → after", "Warm before → after", "Warm Java", "Warm C#")
    header = "| " + " | ".join(columns) + " |"
    divider = "| " + " | ".join(["---", *(["---:"] * (len(columns) - 1))]) + " |"

    def pair(wall, cpu):
        return f"{wall:.6f} / {cpu:.6f}"

    def show(row, key, label, value):
        phase = row["schedules"][key]
        if label not in phase["clean_per_arm"]:
            return "—"
        clean = phase["clean_per_arm"][label]
        expected = phase["expected_per_arm"]
        return value if clean == expected else f"{value} ({clean}/{expected} clean)"

    def phase(row, key, wall="seconds", cpu="cpu_seconds"):
        return " → ".join(show(row, key, label,
                                pair(statistics.median(r[wall] for r in records), statistics.median(r[cpu] for r in records))
                                if records else "—") for label, records in row[key].items())

    for extension in (False, True):
        rows = [row for row in report["results"] if bool(row["extension"]) == extension]
        if not rows:
            continue
        lines.extend(["## Extensions" if extension else "## Frozen startup workloads", "", header, divider])
        for row in rows:
            warm = {}
            for arm in (*SDK_LABELS, "java", "csharp"):
                values = row["warmed_medians"].get(arm)
                warm[arm] = show(row, "warmed", arm, pair(values["wall_seconds"], values["cpu_seconds"]) if values else "—")
            label = row["fixture"]["name"]
            if extension:
                label = f"{row['extension']['row']} {label}"
            cells = [label, phase(row, "startup_budget"),
                     phase(row, "source_to_image", "source_through_encoding_seconds", "source_through_encoding_cpu_seconds"),
                     phase(row, "cold_prebuilt"), f"{warm['before']} → {warm['after']}", warm["java"], warm["csharp"]]
            lines.append("| " + " | ".join(cells) + " |")
        lines.append("")

    pooled = report["startup_budget_geomean"]
    if pooled:
        lines.extend([f"The frozen-ten startup geomean is {pooled['wall_ratio']:.4f}× wall and "
                      f"{pooled['cpu_ratio']:.4f}× CPU for after / before.", ""])
    elif report["selected_frozen_ten"]:
        lines.extend(["The frozen-ten startup geomean awaits clean samples for every startup arm.", ""])
    verdict = report["startup_budget_qualification"]
    if verdict:
        lines.extend([f"Startup qualification status is **{verdict['status']}**.", ""])
        for row in verdict["rows"]:
            if row["pass"] is not True:
                reason = "timing slots owed" if row["pass"] is None else "exceeded the row margin"
                lines.append(f"- {row['row']} {reason}.")
        lines.append("")
    lines.append("The JSON report contains the fork admissions, clean and struck samples, and diagnostic receipts.")
    return "\n".join(lines) + "\n"


def qualification(report, args):
    if not args.aa_results:
        return None
    baseline_path = args.aa_results.resolve()
    baseline = read_json(baseline_path)
    require(baseline.get("finished") and baseline["comparison"] == "A/A" and baseline["cpu7_preflight"] and
            report["comparison"] == "A/B",
            "trial margins need a formal A/A report and an A/B comparison")
    for name in ("tier", "jit", "workers", "runs", "warmups", "samples", "fixture_revision"):
        require(report[name] == baseline[name], f"A/A report differs in {name}")
    require(report["sdk_ids"]["before"] == baseline["sdk_ids"]["before"], "A/A report used a different reference SDK")
    require(report.get("driver_sha256") and report["driver_sha256"] == baseline.get("driver_sha256"),
            "A/A benchmark driver or shared logic differs")
    manifest = report["manifest_sha256"]["held-out.json"]
    require(manifest == baseline.get("manifest_sha256", {}).get("held-out.json"), "A/A frozen manifest digest differs")
    for key in ("system", "kernel", "cpu", "affinity"):
        require(report["machine"].get(key) == baseline["machine"].get(key), f"A/A machine differs in {key}")
    aa_rows = {row["fixture"]["name"]: row for row in baseline["results"]}
    rows = []
    for row in report["results"]:
        case = row["fixture"]
        if row["extension"]:
            continue
        name = case["name"]
        aa = aa_rows.get(name)
        if aa is not None:
            require(aa["fixture"] == case and aa["expected_stdout_sha256"] == row["expected_stdout_sha256"],
                    f"unmatched A/A workload {name}")
        for path, expected in case["sha256"].items():
            identity = {"sha256": expected, "revision": report["fixture_revision"]}
            require(report["source_inputs"].get(path) == identity and
                    (aa is None or baseline.get("source_inputs", {}).get(path) == identity),
                    f"unmatched A/A frozen source {path}")
        reference = aa["ratios"]["startup_budget"] if aa else None
        comparison = row["ratios"]["startup_budget"]
        findings = margin_result(reference, comparison, .03)
        rows.append({"row": name, **findings})
    pooled = None
    if report["selected_frozen_ten"]:
        complete = (len(rows) == 10 and report.get("complete_frozen_ten") and baseline.get("complete_frozen_ten"))
        pooled = (margin_result(baseline["startup_budget_geomean"], report["startup_budget_geomean"], .01)
                  if complete else margin_result(None, None, .01))
    results = [row["pass"] for row in rows] + ([pooled["pass"]] if pooled is not None else [])
    if False in results:
        status, outcome = "failed", False
    elif not results or None in results:
        status, outcome = "owed", None
    elif pooled is None:
        status, outcome = "partial", None
    else:
        status, outcome = "passed", True
    return {"aa_report": str(baseline_path), "aa_sha256": digest(baseline_path), "rows": rows, "pooled": pooled,
            "selection": "frozen-ten" if report["selected_frozen_ten"] else "selected-frozen-rows",
            "status": status, "pass": outcome}


def margin_result(reference, comparison, margin):
    spread = abs(reference["wall_ratio"] - 1) if reference else None
    change = comparison["wall_ratio"] - 1 if comparison else None
    return {"aa_spread": spread, "wall_change": change, "margin": margin,
            "pass": change <= spread + margin if spread is not None and change is not None else None}


def output_directory(path):
    out = path.resolve()
    out.mkdir(parents=True, exist_ok=True)
    conflict = f"{out} already contains receipts or build files; use a fresh --out-dir"
    require(not any(out.iterdir()), conflict)
    try:
        with (out / ".held-out-run").open("x", encoding="utf-8") as marker:
            marker.write(f"pid {os.getpid()}\n")
    except FileExistsError:
        raise SystemExit(f"bench-held-out: {conflict}") from None
    return out


def run(harness, args, manifest, cases, extensions, command_for, max_steps):
    require(os.uname().sysname == "Linux", "the phased harness requires Linux")
    if args.cpu7:
        require(args.runs >= 2 and args.runs % 2 == 0, "CPU 7 qualification requires an even number of matched forks")
    out = output_directory(args.out_dir)
    env = environment(args)
    if args.stop_file:
        signal.signal(signal.SIGALRM, lambda _signum, _frame: check_stop(env))
        signal.setitimer(signal.ITIMER_REAL, 1, 1)
    try:
        check_stop(env)
        labels = {"before": (args.before, args.before_ref), "after": (args.after, args.after_ref)}
        sdks = {label: sdk_provenance(binary.resolve(), harness.git_ref(ref), env)
                for label, (binary, ref) in labels.items()}
        ids = {label: output_hash(json.dumps({"files": sorted(sdk["sha256"].values()),
                    "stdlib": sdk["stdlib_sha256"]}, sort_keys=True).encode()) for label, sdk in sdks.items()}
        chosen = {case["name"] for case in cases}
        ext = {row["fixture"]["name"]: row for row in extensions if row["fixture"]["name"] in chosen}
        sources = out / "sources"
        original_names = {case["name"] for case in manifest["programs"]}
        input_files = stage_fixtures(manifest, cases, original_names, sources, env)
        managed = make_managed(args, ext.values(), out / "managed-build", env)
        preflight = cpu_configuration() if args.cpu7 else None
        if not args.no_perf:
            perf = harness.find_perf(args, out)
        else:
            perf = None
        manifest_paths = [BENCH / "held-out.json"]
        if args.include_extensions:
            manifest_paths.append(BENCH / "held-out-extensions.json")
        runner_paths = [Path(__file__), ROOT / "scripts/bench-held-out.py", ROOT / "scripts/bench-wall.py"]
        report = {"schema": 1, "recorded_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
                  "fixture_revision": manifest["fixture_revision"], "machine": harness.machine_details(),
                  "manifest_sha256": {p.name: digest(p) for p in manifest_paths},
                  "driver_sha256": {p.name: digest(p) for p in runner_paths},
                  "tier": args.tier, "jit": args.jit, "workers": args.workers, "runs": args.runs,
                  "warmups": args.warmups, "samples": args.samples, "sdk_ids": ids, "sdks": sdks,
                  "comparison": "A/A" if ids["before"] == ids["after"] else "A/B",
                  "selected_frozen_ten": original_names <= chosen, "complete_frozen_ten": False,
                  "cpu7_preflight": preflight,
                  "verify_tiers": args.verify_tiers, "allow_cost_change": args.allow_cost_change,
                  "max_steps": max_steps, "managed": managed, "source_root": str(sources), "source_inputs": input_files,
                  "environment": {k: v for k, v in env.items() if k.startswith(("NEURA_", "DOTNET_")) or k in {"LC_ALL", "TZ"}},
                  "perf_instructions": bool(perf), "finished": False, "results": []}
        report_path = out / "held-out-phases.json"
        write_json(report_path, report)
        for case in cases:
            result = measure(case, ext.get(case["name"], {}), args, sdks, harness, command_for,
                             max_steps, managed, out, env, sources)
            if perf:
                result["startup_instructions"] = {}
                for label, command in result["startup_verified_commands"].items():
                    record, process = checked([perf, "stat", "-x,", "-e", "instructions:u", "--", *command], env, sources)
                    require(record["stdout_sha256"] == result["expected_stdout_sha256"], f"{case['name']}: perf output differs")
                    match = harness.INSTRUCTIONS.search(process.stderr.decode(errors="replace"))
                    result["startup_instructions"][label] = {**record, "instructions": int(match[1]) if match else None}
            report["results"].append(result)
            write_json(report_path, report)
            if result["extension"]:
                write_json(out / case["name"] / "extension-results.json", result)
        frozen = [row for row in report["results"] if row["fixture"]["name"] in original_names]
        report["timing_complete"] = all(phase["status"] == "complete" for row in report["results"]
                                        for phase in row["schedules"].values())
        report["complete_frozen_ten"] = report["selected_frozen_ten"] and all(
            row["schedules"]["startup_budget"]["status"] == "complete" for row in frozen)
        report["startup_budget_geomean"] = ({metric: statistics.geometric_mean(
            row["ratios"]["startup_budget"][metric] for row in frozen) for metric in ("wall_ratio", "cpu_ratio")}
            if report["complete_frozen_ten"] else None)
        report["startup_budget_qualification"] = qualification(report, args)
        report["finished"] = True
        write_json(report_path, report)
        (out / "held-out-phases.md").write_text(render_report(report), encoding="utf-8")
        verdict = report["startup_budget_qualification"]
        require(verdict is None or verdict["pass"] is not False,
                f"trial exceeds the established margin; see {report_path}")
        if not report["timing_complete"] or (verdict and verdict["status"] == "owed"):
            print(f"Timing slots or qualification remain owed. See {report_path}.", flush=True)
    except StopRequested as error:
        raise SystemExit(str(error)) from None
    finally:
        if args.stop_file:
            signal.setitimer(signal.ITIMER_REAL, 0)
