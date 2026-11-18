#!/usr/bin/env python3
"""The resident runtime service (docs/design/Service030.md), end to end.

    service.py SUITE BIN_DIR

BIN_DIR is the build directory that holds `neura`, its sibling tools and,
from the service milestone, `neurad`. Every suite runs in a fresh directory
with its own XDG_RUNTIME_DIR, XDG_STATE_HOME and HOME, so suites that run
side by side never meet each other's supervisor, and every supervisor a
suite started is shut down and killed when it ends. Each check names the
edge case of the note it covers.

Exit status 0 when every check passes.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if condition:
        print("ok  ", message)
    else:
        failures.append(message)
        print("FAIL", message)


class Work:
    """A directory with its own run, state and home directories."""

    def __init__(self, bin_dir: Path):
        self.bin = bin_dir.resolve()
        self.neura = self.bin / "neura"
        self.temporary = tempfile.TemporaryDirectory(prefix="nsv-", dir="/tmp")
        self.root = Path(self.temporary.name).resolve()
        self.run_dir = self.root / "r"
        self.state = self.root / "s"
        self.home = self.root / "h"
        for directory in (self.run_dir, self.state, self.home):
            directory.mkdir(mode=0o700)
        self.env = dict(os.environ)
        self.env.update(XDG_RUNTIME_DIR=str(self.run_dir), XDG_STATE_HOME=str(self.state), HOME=str(self.home),
                        NEURA_GPU="off")
        # The other XDG base directories default to places under HOME. A
        # caller's own (GitHub's runners set XDG_CONFIG_HOME) would put login
        # start's unit outside this test's home.
        for name in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"):
            self.env.pop(name, None)
        self.env.pop("NEURA_JIT", None)
        self.images: dict[str, Path] = {}

    def image(self, name: str, source: str | None = None, path: Path | None = None) -> Path:
        """Builds a program, given as text or as a path, into an image."""
        if name in self.images:
            return self.images[name]
        if path is None:
            path = self.root / f"{name}.nra"
            path.write_text(source or "")
        image = self.root / f"{name}.nvr"
        status, _, err = self.run(["build", str(path), "-o", str(image)])
        if status != 0:
            raise SystemExit(f"{name}: build failed\n{err.decode(errors='replace')}")
        self.images[name] = image
        return image

    def run(self, arguments: list[str], stdin: bytes | None = None, env: dict | None = None,
            cwd: Path | None = None, timeout: float = 120) -> tuple[int, bytes, bytes]:
        result = subprocess.run([str(self.neura), *arguments], input=stdin if stdin is not None else b"",
                                capture_output=True, env=env or self.env, cwd=cwd or self.root, timeout=timeout)
        return result.returncode, result.stdout, result.stderr

    def files_under(self, directory: Path) -> list[str]:
        if not directory.exists():
            return []
        return sorted(str(p.relative_to(directory)) for p in directory.rglob("*"))

    def close(self) -> None:
        # Stop whatever supervisor this suite started, then remove the files.
        try:
            subprocess.run([str(self.neura), "runtime", "shutdown", "--terminate", "--timeout=5"], env=self.env,
                           capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            pass
        for pid in supervisors(self):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.temporary.cleanup()


def supervisors(work: Work) -> list[int]:
    """The processes whose environment names this suite's run directory."""
    found = []
    marker = f"XDG_RUNTIME_DIR={work.run_dir}".encode()
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if marker not in (entry / "environ").read_bytes().split(b"\0"):
                continue
            command = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        if b"neurad" in command:
            found.append(int(entry.name))
    return found


HELLO = REPOSITORY / "examples/hello-world/hello-world.nra"


# --------------------------------------------------------------------------
# Step 3: the options and the direct default.

def cli_options(work: Work) -> None:
    hello = str(work.image("hello", path=HELLO))
    usage = [
        (["--runtime=bogus"], "--runtime expects off, auto, or required", "case 5: --runtime=bogus"),
        (["--runtime"], "--runtime expects off, auto, or required", "case 5: --runtime without a value"),
        (["--detach"], "--detach needs --runtime=auto or --runtime=required", "case 6: --detach alone"),
        (["--detach", "--runtime=off"], "--detach needs --runtime=auto or --runtime=required",
         "case 6: --detach --runtime=off"),
        (["--detach", "--runtime=auto", "--stdout=out.txt"], "--detach needs --stdout and --stderr",
         "case 6: --detach --runtime=auto without --stderr"),
        (["--stdout=out.txt"], "--stdout, --stderr and --stdin apply only with --detach",
         "--stdout without --detach"),
    ]
    for options, message, label in usage:
        status, out, err = work.run(["run", *options, hello])
        check((status, out, err) == (1, b"", f"neura: runtime: {message}\n".encode()), label)
    for form in ([str(work.neura), "run"], [str(work.bin / "neura-run")], [str(work.bin / "neura-run"), "run"]):
        result = subprocess.run([*form, "--runtime=bogus", hello], capture_output=True, env=work.env, cwd=work.root)
        check(result.returncode == 1 and result.stderr == b"neura: runtime: --runtime expects off, auto, or required\n",
              f"case 5 through {Path(form[0]).name}")
    status, out, err = work.run(["run", "--runtime=off", hello])
    check((status, out, err) == (0, b"Hello, World!\n", b""), "--runtime=off is a direct run")
    status, out, err = work.run(["test", "--runtime=required", str(HELLO)])
    check((status, out) == (1, b"") and err == b"neura: runtime: neura test runs directly; --runtime applies only to run\n",
          "case 59: neura test --runtime=required is a usage error")
    status, _, err = work.run(["build", "--runtime=auto", str(HELLO), "-o", str(work.root / "x.nvr")])
    check(status == 1 and b"--runtime applies only to run" in err, "--runtime applies only to run")
    check(not (work.root / "out.txt").exists(), "no rejected launch opened its output file")


def debug_session(work: Work, program: Path, extra: dict | None = None) -> tuple[bool, list[dict]]:
    """A debug launch run to its end over the adapter protocol."""
    process = subprocess.Popen([str(work.neura), "debug", "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    sequence = 0
    messages: list[dict] = []

    def send(command: str, arguments: dict) -> None:
        nonlocal sequence
        sequence += 1
        data = json.dumps({"seq": sequence, "type": "request", "command": command, "arguments": arguments}).encode()
        process.stdin.write(b"Content-Length: " + str(len(data)).encode() + b"\r\n\r\n" + data)
        process.stdin.flush()

    def receive() -> dict | None:
        headers = {}
        while True:
            line = process.stdout.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            key, value = line.decode().split(":", 1)
            headers[key.lower()] = value.strip()
        return json.loads(process.stdout.read(int(headers["content-length"])))

    try:
        send("initialize", {"adapterID": "neura", "linesStartAt1": True, "columnsStartAt1": True})
        send("launch", {"program": str(program), "cwd": str(work.root), **(extra or {})})
        configured = False
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            message = receive()
            if message is None:
                break
            messages.append(message)
            if message.get("event") == "initialized" and not configured:
                configured = True
                send("configurationDone", {})
            if message.get("type") == "response" and message.get("command") == "launch" and not message["success"]:
                break
            if message.get("event") in ("terminated", "exited"):
                break
        send("disconnect", {})
    finally:
        try:
            process.stdin.close()
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    ended = any(m.get("event") in ("terminated", "exited") for m in messages)
    return ended, messages


def off_default(work: Work) -> None:
    """Cases 1 and 2: the defaults never look for the service."""
    hello = str(work.image("hello", path=HELLO))
    before_run = work.files_under(work.run_dir)
    status, out, err = work.run(["run", hello])
    check((status, out, err) == (0, b"Hello, World!\n", b""), "case 1: plain neura run")
    status, out, err = subprocess.run([str(work.bin / "neura-run"), hello], capture_output=True, env=work.env,
                                      cwd=work.root).returncode, b"", b""
    check(status == 0, "case 1: plain neura-run")
    tests = work.root / "suite.nra"
    tests.write_text("let test = import:Std.Test;\n\nlet Suite = module (\n"
                     "    export let test_adds = func() -> void {\n        test.assert(1 + 1 == 2);\n    };\n);\n")
    status, out, _ = work.run(["test", str(tests)])
    check(status == 0, "case 2: plain neura test")
    ended, _ = debug_session(work, HELLO)
    check(ended, "case 2: a debug launch runs to its end")
    check(work.files_under(work.run_dir) == before_run == [], "cases 1, 2: nothing appears in the run directory")
    check(work.files_under(work.state) == [], "cases 1, 2: nothing appears in the state directory")

    # The run and state directories unwritable, and no neurad next to neura.
    os.chmod(work.run_dir, 0o500)
    os.chmod(work.state, 0o500)
    try:
        copy = work.root / "copy"
        copy.mkdir()
        for entry in work.bin.iterdir():
            if entry.is_file() and os.access(entry, os.X_OK) and (entry.name == "neura" or entry.name.startswith("neura-")
                                                                  or entry.name == "neurac"):
                shutil.copy2(entry, copy / entry.name)
        shutil.copytree(work.bin / "lib", copy / "lib")
        if (work.bin / "stdlib").exists():
            os.symlink(work.bin / "stdlib", copy / "stdlib")
        check(not (copy / "neurad").exists(), "case 2: the copy has no neurad")
        result = subprocess.run([str(copy / "neura"), "run", hello], capture_output=True, env=work.env, cwd=work.root)
        check((result.returncode, result.stdout, result.stderr) == (0, b"Hello, World!\n", b""),
              "case 2: neura run without neurad and with an unwritable run directory")
        result = subprocess.run([str(copy / "neura"), "run", "--runtime=off", hello], capture_output=True,
                                env=work.env, cwd=work.root)
        check(result.returncode == 0, "case 2: --runtime=off without neurad")
    finally:
        os.chmod(work.run_dir, 0o700)
        os.chmod(work.state, 0o700)
    check(work.files_under(work.run_dir) == [], "case 2: still nothing in the run directory")


# --------------------------------------------------------------------------
# Step 5: the worker, driven by a stand-in supervisor.

PROGRAMS = REPOSITORY / "tests/service/programs"
REGIONS = REPOSITORY / "tests/backend/tier/region-storage.nvra"


def program(work: Work, name: str) -> str:
    return str(work.image(name, path=PROGRAMS / f"{name}.nra"))


def region_image(work: Work) -> str:
    image = work.root / "region-storage.nvr"
    if not image.exists():
        status, _, err = work.run(["assemble", str(REGIONS), "-o", str(image)])
        if status != 0:
            raise SystemExit(f"assembling the region image failed\n{err.decode()}")
    return str(image)


def tier_lines(stderr: bytes) -> list[bytes]:
    """The --tier-stats report without its wall times."""
    # The proof and search times of the first line are wall times too.
    return [re.sub(rb"[0-9.]+ ms", b"<t> ms", line) for line in stderr.splitlines()
            if line.startswith(b"tier: ") and not line.startswith(b"tier: time:")]


def without_times(stderr: bytes) -> bytes:
    return b"\n".join(line for line in stderr.splitlines() if not line.startswith(b"tier: time:"))


def worker_suite(work: Work) -> None:
    driver = Path(sys.argv[3]).resolve()
    neurad = work.bin / "neurad"
    report = work.root / "report.json"

    def direct(arguments, env=None, cwd=None, stdin=b"", **kwargs):
        result = subprocess.run([str(work.neura), "run", *arguments], input=stdin, capture_output=True,
                                env=env or work.env, cwd=cwd or work.root, timeout=120, **kwargs)
        return result.returncode, result.stdout, result.stderr

    def driven(arguments, driver_options=(), env=None, cwd=None, stdin=b"", **kwargs):
        if report.exists():
            report.unlink()
        result = subprocess.run([str(driver), str(neurad), "--report", str(report), *driver_options, "--", *arguments],
                                input=stdin, capture_output=True, env=env or work.env, cwd=cwd or work.root,
                                timeout=120, **kwargs)
        return result.returncode, result.stdout, result.stderr

    def ending() -> dict:
        return json.loads(report.read_text()) if report.exists() else {}

    # Case 45: the worker entry run by hand.
    result = subprocess.run([str(neurad), "--worker"], capture_output=True, env=work.env)
    check(result.returncode == 1 and b"cannot be run by hand" in result.stderr and result.stdout == b"",
          "case 45: neurad --worker run by hand exits 1 with a message")
    result = subprocess.run([str(neurad), "--worker", "--channel", "0", "--build", "x", "--supervisor", "1"],
                            capture_output=True, env=work.env, stdin=subprocess.DEVNULL)
    check(result.returncode == 1 and result.stdout == b"", "case 45: a worker with no supervisor's channel exits 1")

    hello = str(work.image("hello", path=HELLO))
    exit_status = program(work, "exit-status")
    startup = program(work, "startup-trap")
    environment = program(work, "environment")
    read_file = program(work, "read-file")
    echo = program(work, "echo")
    binary = program(work, "binary")
    regions = region_image(work)

    # The same output, status and trap as a direct run.
    cases = [
        ("hello", [hello]),
        ("arguments and exit status", [exit_status, "--", "7", "", "a b", "\u00e9"]),
        ("stats and profile", ["--stats", "--profile", exit_status, "--", "0"]),
        ("case 42: a trap in startup code", [startup]),
        ("case 28: --max-stack-cells on an image with regions", ["--max-stack-cells", "159", regions]),
        ("regions under the limit", ["--stats", "--max-stack-cells", "160", regions]),
        ("--max-steps", ["--max-steps", "50", exit_status, "--", "0"]),
        ("--deny-env", ["--deny-env", environment, "--", "HOME"]),
        ("--tier=0 --jit=off", ["--tier=0", "--jit=off", "--stats", exit_status, "--", "0"]),
    ]
    for label, arguments in cases:
        expected = direct(arguments)
        got = driven(arguments)
        check(got == expected, f"{label}: the worker's run is the direct run's {expected[0]}")
    status, _, err = driven([startup])
    first = ending()
    status_again, _, err_again = driven([startup])
    check(first.get("terminal") == "trapped" and ending().get("terminal") == "trapped" and err == err_again
          and status == status_again and first.get("pid") != ending().get("pid"),
          "case 42: the next launch of the image runs startup again in another worker")

    # Cases 26 and 27: each launch has its own mode, from its own environment.
    image = str(work.image("bench", path=REPOSITORY / "tests/service/programs/exit-status.nra"))
    for mode in ("off", "native", "interp"):
        env = dict(work.env, NEURA_TIER3=mode)
        _, _, expected = direct(["--tier-stats", image, "--", "0"], env=env)
        _, _, got = driven(["--tier-stats", image, "--", "0"], env=env)
        check(tier_lines(got) == tier_lines(expected) and tier_lines(got),
              f"case 26: NEURA_TIER3={mode} shows its own mode")
    _, _, got = driven(["--tier-stats", image, "--", "0"], driver_options=["--supervisor-env", "NEURA_JIT=off"])
    # "tier: jit auto: N regions compiled", or where this target's JIT is not
    # used (aarch64), "tier: jit auto, not used: ...": the mode is auto either way.
    check(b"tier: jit auto" in got and b"tier: jit off" not in got,
          "case 27: NEURA_JIT=off in the supervisor's environment does not reach the application")
    _, _, got = driven(["--tier-stats", image, "--", "0"], env=dict(work.env, NEURA_JIT="off"),
                       driver_options=["--supervisor-env", "NEURA_JIT=on"])
    check(b"tier: jit off" in got, "case 27: the caller's NEURA_JIT=off is the application's")
    for mode in ("0", "1"):
        env = dict(work.env, NEURA_TIER=mode)
        expected = direct(["--tier-stats", image, "--", "0"], env=env)
        got = driven(["--tier-stats", image, "--", "0"], env=env, driver_options=["--supervisor-env", "NEURA_TIER=2"])
        check(tier_lines(got[2]) == tier_lines(expected[2]), f"NEURA_TIER={mode} is the caller's")

    # The whole environment is the caller's, and only the caller's.
    env = dict(work.env, LD_LIBRARY_PATH="/nonexistent/caller", NEURA_SERVICE_PROBE="caller")
    expected = direct(["--allow-env", environment, "--", "LD_LIBRARY_PATH", "NEURA_SERVICE_PROBE", "ONLY_SUPERVISOR"],
                      env=env)
    got = driven(["--allow-env", environment, "--", "LD_LIBRARY_PATH", "NEURA_SERVICE_PROBE", "ONLY_SUPERVISOR"],
                 env=env, driver_options=["--supervisor-env", "ONLY_SUPERVISOR=1",
                                          "--supervisor-env", "NEURA_SERVICE_PROBE=supervisor"])
    check(got == expected and b"ONLY_SUPERVISOR unset" in got[1] and b"LD_LIBRARY_PATH=/nonexistent/caller" in got[1],
          "case 29: the caller's environment, LD_LIBRARY_PATH included, replaces the worker's")

    # Case 32: relative paths and --allow-fs=. against the caller's directory.
    caller = work.root / "caller dir"
    caller.mkdir()
    (caller / "data.txt").write_text("from the caller\n")
    expected = direct(["--allow-fs=.", read_file, "--", "data.txt"], cwd=caller)
    got = driven(["--allow-fs=.", read_file, "--", "data.txt"], cwd=caller)
    check(got == expected and got[1] == b"from the caller\n", "case 32: relative paths and --allow-fs=.")
    expected = direct(["--allow-fs=.", read_file, "--", "../h"], cwd=caller)
    got = driven(["--allow-fs=.", read_file, "--", "../h"], cwd=caller)
    check(got == expected and got[0] != 0, "case 32: a path outside the grant is refused as directly")

    # Case 33: the caller's directory deleted before the commit.
    gone = work.root / "gone"
    gone.mkdir()
    script = f'cd "{gone}" && rmdir "{gone}" && exec "{work.neura}" run "{read_file}" -- data.txt'
    result = subprocess.run(["sh", "-c", script], capture_output=True, env=work.env)
    expected = (result.returncode, result.stdout, result.stderr)
    gone.mkdir()
    got = driven([read_file, "--", "data.txt"], driver_options=["--remove-cwd"], cwd=gone)
    check(got == expected and not gone.exists(), "case 33: a deleted working directory, as directly")

    # Case 34: standard output is a closed pipe.
    for ignored in (False, True):
        def closed_pipe(command):
            read_end, write_end = os.pipe()
            os.close(read_end)
            preexec = (lambda: signal.signal(signal.SIGPIPE, signal.SIG_IGN)) if ignored else None
            try:
                result = subprocess.run(command, stdout=write_end, stderr=subprocess.PIPE, env=work.env,
                                        cwd=work.root, preexec_fn=preexec, restore_signals=not ignored)
            finally:
                os.close(write_end)
            return result.returncode, result.stderr
        expected = closed_pipe([str(work.neura), "run", binary, "--", "4"])
        got = closed_pipe([str(driver), str(neurad), "--report", str(report), "--", binary, "--", "4"])
        if ignored:
            check(got == expected, "case 34: with SIGPIPE ignored, the same write error and status")
        else:
            check(expected[0] == -signal.SIGPIPE and ending().get("terminal") == "signaled"
                  and ending().get("signal") == signal.SIGPIPE,
                  "case 34: the program is ended by SIGPIPE, as directly")

    # Case 35: standard input is a terminal.
    import pty
    def through_terminal(command):
        main, secondary = pty.openpty()
        process = subprocess.Popen(command, stdin=secondary, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=work.env, cwd=work.root)
        os.close(secondary)
        os.write(main, b"typed line\n")
        time.sleep(0.3)
        os.write(main, b"\x04")
        out, err = process.communicate(timeout=30)
        os.close(main)
        return process.returncode, out, err
    expected = through_terminal([str(work.neura), "run", echo])
    got = through_terminal([str(driver), str(neurad), "--", echo])
    check(got == expected and got[1] == b"[typed line]\nend\n", "case 35: reads from a terminal; Ctrl-D ends the input")

    # Case 36: binary output of 64 MiB with NUL bytes, to a pipe and to a file.
    expected = direct([binary, "--", "1024"])
    got = driven([binary, "--", "1024"])
    check(got == expected and len(got[1]) == 64 << 20 and b"\0" in got[1][:256],
          "case 36: 64 MiB of binary output to a pipe, identical bytes")
    for name, command in (("direct", [str(work.neura), "run", binary, "--", "1024"]),
                          ("worker", [str(driver), str(neurad), "--", binary, "--", "1024"])):
        with open(work.root / f"{name}.bin", "wb") as output:
            subprocess.run(command, stdout=output, env=work.env, cwd=work.root, check=True)
    check((work.root / "direct.bin").read_bytes() == (work.root / "worker.bin").read_bytes(),
          "case 36: 64 MiB of binary output to a file, identical bytes")

    # The order of assignment: environment, umask, limits, nice, directory,
    # process group, streams, signals, and nothing else of the driver's.
    pid_file = work.root / "worker.pid"
    def settings():
        os.umask(0o027)
        import resource
        resource.setrlimit(resource.RLIMIT_NOFILE, (200, resource.getrlimit(resource.RLIMIT_NOFILE)[1]))
        os.nice(3)
        signal.signal(signal.SIGUSR1, signal.SIG_IGN)
        signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGUSR2})
    extra = os.open(work.root / "extra-descriptor", os.O_CREAT | os.O_WRONLY)
    process = subprocess.Popen([str(driver), str(neurad), "--pid-file", str(pid_file), "--report", str(report), "--",
                                echo], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               env=dict(work.env, PROBE_VARIABLE="caller"), cwd=caller, preexec_fn=settings,
                               pass_fds=(extra,))
    wait_until(lambda: pid_file.exists() and pid_file.read_text().endswith("\n"), 20)
    pid = int(pid_file.read_text())
    proc = Path(f"/proc/{pid}")
    status_lines = dict(line.split(":", 1) for line in (proc / "status").read_text().splitlines() if ":" in line)
    environ = dict(entry.split("=", 1) for entry in (proc / "environ").read_bytes().decode().split("\0") if "=" in entry)
    caller_env = dict(work.env, PROBE_VARIABLE="caller")
    check(environ == caller_env, "the worker's environment is the caller's, whole")
    check(os.readlink(proc / "cwd") == str(caller), "the worker's directory is the caller's")
    check(status_lines["Umask"].strip() == "0027", "the worker has the caller's umask")
    limits = (proc / "limits").read_text()
    check(any(line.startswith("Max open files") and line.split()[3] == "200" for line in limits.splitlines()),
          "the worker has the caller's soft limit of open files")
    check(os.getpriority(os.PRIO_PROCESS, pid) == os.getpriority(os.PRIO_PROCESS, os.getpid()) + 3,
          "the worker has the caller's nice value")
    check(os.getpgid(pid) == pid, "the worker leads a process group of its own")
    check(int(status_lines["SigIgn"], 16) & (1 << (signal.SIGUSR1 - 1)) != 0, "the caller's ignored signals are ignored")
    check(int(status_lines["SigBlk"], 16) & (1 << (signal.SIGUSR2 - 1)) != 0, "the caller's signal mask is the worker's")
    fds = sorted(int(fd) for fd in os.listdir(proc / "fd"))
    check(fds == [0, 1, 2, 3], f"the worker holds its streams and its channel only ({fds})")
    out, err = process.communicate(b"one\n", timeout=30)
    os.close(extra)
    check(process.returncode == 0 and out == b"[one]\nend\n", "the held run ends with its input")

    # A worker runs one application and exits.
    driven([hello], driver_options=["--second-assignment"])
    check(ending().get("second") == "refused", "a worker is never given a second application")

    # The supervisor's end ends the worker.
    if pid_file.exists():
        pid_file.unlink()
    process = subprocess.Popen([str(driver), str(neurad), "--pid-file", str(pid_file), "--exit-after-started", "--",
                                echo], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               env=work.env, cwd=work.root)
    process.wait(timeout=30)
    pid = int(pid_file.read_text()) if pid_file.exists() else 0
    deadline = time.monotonic() + 5
    while pid and Path(f"/proc/{pid}").exists() and time.monotonic() < deadline:
        state = (Path(f"/proc/{pid}") / "stat").read_text().split()[2] if Path(f"/proc/{pid}").exists() else "X"
        if state == "Z":
            break
        time.sleep(0.05)
    alive = pid and Path(f"/proc/{pid}").exists() and (Path(f"/proc/{pid}") / "stat").read_text().split()[2] != "Z"
    check(process.returncode == 99 and pid and not alive, "the worker ends when its supervisor does")
    process.stdin.close()
    process.stdout.close()
    process.stderr.close()


# --------------------------------------------------------------------------
# Step 6: the supervisor, through a client of the protocol written in Python.

sys.path.insert(0, str(Path(__file__).resolve().parent))
import protocol_client as pc  # noqa: E402


def launch(service, connection, image: bytes, path: str, stdout: int, stdin: int | None = None,
           arguments: list[bytes] | None = None, env: dict | None = None, wait: bool = True):
    """Prepare, send the image when asked, commit, and wait for the outcome."""
    connection.send("Prepare", pc.envelope(image, path=path, arguments=arguments, environment=env or service.env))
    prepared = connection.receive()
    if prepared is None or prepared.kind != "Prepared":
        return prepared, None
    token = prepared.get("Token")
    if prepared.number("ImageNeeded"):
        fd = pc.memory_file(image)
        connection.send("Image", [("Token", token), ("Descriptors", pc.u8(1))], [fd])
        os.close(fd)
        reply = connection.receive()
        if reply.kind != "Prepared":
            return reply, None
    cwd = os.open(".", os.O_RDONLY)
    read_end = None
    if stdin is None:
        read_end, write_end = os.pipe()
        os.close(write_end)
        stdin = read_end
    connection.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [cwd, stdin, stdout, 2])
    os.close(cwd)
    if read_end is not None:
        os.close(read_end)
    committed = connection.receive()
    if not wait or committed is None or committed.kind != "Committed":
        return committed, None
    return committed, connection.receive()


def protocol_suite(work: Work) -> None:
    service = pc.Service(work.bin, work.env)
    hello_image = Path(work.image("hello", path=HELLO)).read_bytes()
    hello_path = str(work.images["hello"])
    output = work.root / "out.txt"

    def out_fd():
        return os.open(output, os.O_CREAT | os.O_WRONLY | os.O_APPEND)

    # Case 9: a socket file and no supervisor; the next supervisor removes it.
    service.directory.mkdir(parents=True, mode=0o700)
    os.chmod(service.directory.parent, 0o700)
    stale = socket_file(service.socket)
    check(service.socket.exists() and stale, "case 9: a stale socket file is in place")
    work.env["SERVICE_TEST_MARKER"] = "environment-marker-value"
    service.start(extra_env={"NEURA_TEST_SERVICE_TIMERS": "reservation=1500"})
    connection = service.hello()
    check(connection.welcome is not None and connection.welcome.kind == "Welcome",
          "case 9: a new supervisor replaced the stale socket and answers")
    check(oct(service.socket.stat().st_mode & 0o777) == "0o600" and
          oct(service.directory.stat().st_mode & 0o777) == "0o700", "the socket is 0600 in a 0700 directory")

    # A launch runs, and its record is written.
    fd = out_fd()
    committed, outcome = launch(service, connection, hello_image, hello_path, fd)
    os.close(fd)
    check(committed.kind == "Committed" and outcome is not None and outcome.kind == "Outcome"
          and pc.TERMINALS[outcome.number("Terminal")] == "exited" and output.read_bytes() == b"Hello, World!\n",
          "a launch through the supervisor runs to exited")
    record = json.loads((service.state / "records" / committed.text("ApplicationId")).read_text())
    check(record["state"] == "exited" and record["status"] == 0 and record["digest"] ==
          hashlib.sha256(hello_image).hexdigest(), "its record is written whole")
    check("Hello" not in (service.state / "records" / committed.text("ApplicationId")).read_text()
          and "environment-marker-value" not in json.dumps(record),
          "a record holds no output and no environment value")

    # Case 16: Commit sent twice with one token.
    output.unlink()
    connection.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
    prepared = connection.receive()
    token = prepared.get("Token")
    fd = out_fd()
    cwd = os.open(".", os.O_RDONLY)
    connection.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [cwd, 0, fd, 2])
    first = connection.receive()
    connection.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [cwd, 0, fd, 2])
    messages = [connection.receive(), connection.receive()]
    second = next(m for m in messages if m.kind == "Committed")
    os.close(fd)
    os.close(cwd)
    time.sleep(0.3)
    check(first.kind == "Committed" and second.text("ApplicationId") == first.text("ApplicationId")
          and output.read_bytes() == b"Hello, World!\n", "case 16: a second Commit gets the same Committed")

    # Case 15: Commit after the reservation expired.
    connection.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
    token = connection.receive().get("Token")
    time.sleep(1.8)
    fd = out_fd()
    cwd = os.open(".", os.O_RDONLY)
    connection.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [cwd, 0, fd, 2])
    os.close(fd)
    os.close(cwd)
    reply = connection.receive()
    check(reply.kind == "Rejected" and reply.text("Reason") == "token-expired"
          and reply.number("NotStarted") == 1 and output.read_bytes() == b"Hello, World!\n",
          f"case 15: a late Commit is token-expired and nothing runs ({reply.text('Reason')})")

    # Case 14: seventeen Prepares without Commit; released after the reservation time.
    replies = []
    for _ in range(17):
        connection.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
        replies.append(connection.receive())
    check([r.kind for r in replies[:16]] == ["Prepared"] * 16 and replies[16].kind == "Rejected"
          and replies[16].text("Reason") == "admission-pending", "case 14: the seventeenth Prepare is admission-pending")
    time.sleep(1.8)
    connection.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
    reply = connection.receive()
    check(reply.kind == "Prepared", "case 14: after the reservation time the sixteen are released")
    connection.send("Cancel", [("Token", reply.get("Token"))])

    # Case 20: the bytes are not those the envelope names.
    other = Path(work.image("exit", path=PROGRAMS / "exit-status.nra")).read_bytes()
    connection.send("Prepare", pc.envelope(b"the bytes the client read", path=hello_path, environment=work.env))
    token = connection.receive().get("Token")
    fd = pc.memory_file(b"the bytes the file held later")
    connection.send("Image", [("Token", token), ("Descriptors", pc.u8(1))], [fd])
    os.close(fd)
    reply = connection.receive()
    check(reply.kind == "Rejected" and reply.text("Reason") == "image-mismatch", "case 20: changed bytes are image-mismatch")

    # Case 22: one path, two contents: two digests, two entries.
    output.unlink()
    for image in (other, hello_image):
        fd = out_fd()
        launch(service, connection, image, hello_path, fd, arguments=[b"3"])
        os.close(fd)
    connection.send("List", [])
    listing = connection.receive()
    check(output.read_bytes() == b"argument: 3\nHello, World!\n" and
          listing.number("CacheBytes") == len(other) + len(hello_image),
          "case 22: the same path with two contents runs each its own bytes")

    # Case 24: cache clear while an application runs leaves its entry.
    echo_image = Path(program(work, "echo")).read_bytes()
    read_end, write_end = os.pipe()
    fd = out_fd()
    committed, _ = launch(service, connection, echo_image, "echo.nvr", fd, stdin=read_end, wait=False)
    os.close(read_end)
    control = service.hello(purpose=2)
    control.send("CacheClear", [])
    cleared = control.receive()
    check(committed.kind == "Committed" and cleared.number("CacheBytes") == len(echo_image),
          f"case 24: cache clear keeps the entry a running application uses ({committed.kind}, "
          f"{cleared.number('CacheBytes')}, {len(echo_image)})")
    os.write(write_end, b"x\n")
    os.close(write_end)
    outcome = connection.receive()
    os.close(fd)
    control.send("CacheClear", [])
    check(outcome.kind == "Outcome" and control.receive().number("CacheBytes") == 0,
          "case 24: once it ended, cache clear drops it")

    # Case 25: an image larger than the cache; three of 100 MiB. The two held
    # images must stay reserved while the third is asked for: under the 1.5 s
    # reservation of the cases above, copying and hashing 200 MiB on a loaded
    # machine can outlast the first reservation, which then frees its entry.
    # A new supervisor with the default reservation (30 s) runs this case.
    connection.close()
    service.process.kill()
    service.process.wait()
    service.start()
    connection = service.hello()
    connection.send("Prepare", pc.envelope(size=300 << 20, digest=b"\1" * 32, environment=work.env))
    reply = connection.receive()
    check(reply.kind == "Rejected" and reply.text("Reason") == "image-too-large", "case 25: 300 MiB is image-too-large")
    tokens = []
    for index in range(2):
        data = bytes([index]) * (100 << 20)
        connection.send("Prepare", pc.envelope(data, environment=work.env))
        token = connection.receive().get("Token")
        fd = pc.memory_file(data)
        connection.send("Image", [("Token", token), ("Descriptors", pc.u8(1))], [fd])
        os.close(fd)
        check(connection.receive().kind == "Prepared", f"case 25: 100 MiB image {index + 1} is held")
        tokens.append(token)
    third = bytes([2]) * (100 << 20)
    connection.send("Prepare", pc.envelope(third, environment=work.env))
    reply = connection.receive()
    check(reply.kind == "Rejected" and reply.text("Reason") == "admission-cache",
          "case 25: the third 100 MiB image is admission-cache")
    connection.send("Cancel", [("Token", tokens[0])])
    connection.send("Prepare", pc.envelope(third, environment=work.env))
    reply = connection.receive()
    check(reply.kind == "Prepared", "case 25: it is admitted once the first is released")
    connection.send("Cancel", [("Token", reply.get("Token"))])
    connection.send("Cancel", [("Token", tokens[1])])

    # Case 43: eight launches at once and one ready worker; none refused.
    import threading
    results = []
    def one():
        own = service.hello()
        fd = os.open(os.devnull, os.O_WRONLY)
        committed, outcome = launch(service, own, hello_image, hello_path, fd)
        os.close(fd)
        results.append((committed.kind, committed.number("Path"), outcome.kind if outcome else None))
        own.close()
    time.sleep(0.5)
    threads = [threading.Thread(target=one) for _ in range(8)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    paths = sorted(path for _, path, _ in results)
    check(len(results) == 8 and all(r[0] == "Committed" and r[2] == "Outcome" for r in results)
          and paths.count(1) >= 1, f"case 43: eight at once all run, one or more on a ready worker ({paths})")

    # Case 57: malformed messages; the supervisor keeps serving others.
    bad_cases = [
        ("a message of 2 MiB", pc.u32(2 << 20) + b"\0" * 12),
        ("a field length past the end", pc.frame(1, [(1, b"\1\0")])[:-2] + b"\0\0" if False else
         pc.u32(16) + pc.u16(1) + pc.u16(0) + pc.u64(1) + pc.u16(1) + pc.u32(99) + b"xx"),
        ("an unknown kind", pc.frame(999, [])),
    ]
    for label, data in bad_cases:
        bad = service.hello()
        bad.send_raw(data)
        reply = bad.receive()
        closed = bad.receive()
        check(reply is not None and reply.kind == "Rejected" and reply.text("Reason") == "protocol" and closed is None,
              f"case 57: {label}: one Rejected protocol, then the connection closes")
        bad.close()
    bad = service.hello()
    bad.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
    token = bad.receive().get("Token")
    cwd = os.open(".", os.O_RDONLY)
    bad.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [cwd, 0, 1])
    os.close(cwd)
    reply = bad.receive()
    check(reply is not None and reply.text("Reason") == "protocol" and bad.receive() is None,
          "case 57: a Commit with three descriptors is malformed")
    check(service.hello().welcome.kind == "Welcome", "case 57: the supervisor keeps serving others")
    first = pc.Connection(service.socket)
    first.send("List", [])
    reply = first.receive()
    check(reply is not None and reply.text("Reason") == "protocol", "a first message that is not Hello is malformed")

    # A Hello of another installation, or with no common protocol.
    other_installation = service.hello(installation="0000000000000000")
    check(other_installation.welcome.kind == "Rejected" and other_installation.welcome.text("Reason") == "identity",
          "a client of another installation is identity")
    newer = service.hello(low=2, high=3)
    check(newer.welcome.kind == "Rejected" and newer.welcome.text("Reason") == "unsupported-protocol",
          "no common protocol major is unsupported-protocol")
    other_build = service.hello(build="neura 9.9.9 (other)")
    other_build.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
    reply = other_build.receive()
    check(other_build.welcome.kind == "Welcome" and reply.kind == "Rejected" and reply.text("Reason") == "identity",
          "case 11: a launch from another build is identity; control is still answered")

    # Case 17: a token from before the supervisor restarted.
    connection.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=work.env))
    token = connection.receive().get("Token")
    service.process.kill()
    service.process.wait()
    service.start()
    again = service.hello()
    cwd = os.open(".", os.O_RDONLY)
    again.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [cwd, 0, 1, 2])
    os.close(cwd)
    reply = again.receive()
    check(reply.kind == "Rejected" and reply.text("Reason") == "token-unknown",
          "case 17: a token of an earlier epoch is token-unknown")
    service.process.kill()
    service.process.wait()

    # Case 44: a worker that never reports ready.
    service.start(extra_env={"NEURA_TEST_SERVICE_TIMERS": "ready=1500,never-ready=1"})
    connection = service.hello()
    fd = os.open(os.devnull, os.O_WRONLY)
    committed, _ = launch(service, connection, hello_image, hello_path, fd)
    check(committed.kind == "Rejected" and committed.number("NotStarted") == 1,
          f"case 44: a worker that never reports ready: the launch is refused before commit ({committed.text('Reason')})")
    committed, outcome = launch(service, connection, hello_image, hello_path, fd)
    os.close(fd)
    check(committed.kind == "Committed" and outcome.kind == "Outcome", "case 44: the next launch tries again and runs")
    service.process.kill()
    service.process.wait()


# --------------------------------------------------------------------------
# Step 7: the client of `neura run --runtime=auto|required`.

def copy_build(work: Work, name: str, neurad: str | None = "neurad") -> Path:
    """A copy of the build's tools; `neurad` is left out, or replaced by a script."""
    copy = work.root / name
    copy.mkdir()
    for entry in work.bin.iterdir():
        if entry.is_file() and os.access(entry, os.X_OK) and (entry.name == "neura" or entry.name.startswith("neura-")):
            shutil.copy2(entry, copy / entry.name)
    shutil.copytree(work.bin / "lib", copy / "lib")
    if neurad == "neurad":
        shutil.copy2(work.bin / "neurad", copy / "neurad")
    elif neurad is not None:
        (copy / "neurad").write_text(neurad)
        os.chmod(copy / "neurad", 0o755)
    return copy


def assigned_workers(work: Work) -> list[int]:
    """Workers of this suite that were given an application: they lead a process group."""
    found = []
    for pid in supervisors(work):
        try:
            command = (Path(f"/proc/{pid}") / "cmdline").read_bytes()
            state = (Path(f"/proc/{pid}") / "stat").read_text().split()[2]
        except OSError:
            continue
        if b"--worker" in command and state != "Z" and os.getpgid(pid) == pid:
            found.append(pid)
    return found


def neura_path_of(work: Work) -> Path:
    return work.neura


def kill_supervisors(work: Work, foreground_only: bool = False) -> None:
    """Kills this suite's supervisors (and, unless foreground_only, its workers) with SIGKILL."""
    for pid in supervisors(work):
        try:
            if foreground_only and b"--foreground" not in Path(f"/proc/{pid}/cmdline").read_bytes():
                continue
            os.kill(pid, signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass


def wait_until(condition, timeout: float = 10) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return condition()


def run_suite(work: Work) -> None:
    hello = str(work.image("hello", path=HELLO))
    echo = program(work, "echo")
    exit_status = program(work, "exit-status")
    report = work.root / "report.json"

    def neura_run(arguments, env=None, stdin=b"", neura=None, timeout=60):
        result = subprocess.run([str(neura or work.neura), "run", *arguments], input=stdin, capture_output=True,
                                env=env or work.env, cwd=work.root, timeout=timeout)
        return result.returncode, result.stdout, result.stderr

    def path_taken() -> str | None:
        return json.loads(report.read_text())["path"] if report.exists() else None

    # Cases 3 and 4: no neurad in the installation.
    bare = copy_build(work, "bare", neurad=None)
    status, out, err = neura_run(["--runtime=auto", hello], neura=bare / "neura")
    check((status, out) == (0, b"Hello, World!\n") and err == b"neura: runtime: running directly: the service "
          b"component is not installed; install Neura-runtime, or run without --runtime\n" or
          err.startswith(b"neura: runtime: running directly: the service component is not installed"),
          "case 3: --runtime=auto without neurad runs directly with one line")
    status, out, err = neura_run(["--runtime=required", hello], neura=bare / "neura")
    check((status, out, err) == (5, b"", b"neura: runtime: the service component is not installed; install "
                                          b"Neura-runtime, or run without --runtime\n"),
          "case 4: --runtime=required without neurad is status 5")

    # Case 7: a detached launch when the service cannot start.
    broken = copy_build(work, "broken", neurad="#!/bin/sh\nexit 1\n")
    started = time.monotonic()
    status, out, err = neura_run(["--runtime=auto", "--detach", f"--stdout={work.root / 'd.out'}",
                                  f"--stderr={work.root / 'd.err'}", hello], neura=broken / "neura")
    check(status == 5 and out == b"" and b"did not start within 2 seconds" in err and
          (work.root / "d.out").read_bytes() == b"", "case 7: --detach --runtime=auto never falls back")
    status, out, err = neura_run(["--runtime=auto", hello], neura=broken / "neura")
    check(status == 0 and out == b"Hello, World!\n" and b"running directly: the service did not start" in err,
          "the service cannot start: auto runs directly")
    status, out, err = neura_run(["--runtime=required", hello], neura=broken / "neura")
    check(status == 5 and out == b"", "the service cannot start: required is status 5")
    kill_supervisors(work)

    # Case 8: two clients start the first launch at once.
    processes = [subprocess.Popen([str(work.neura), "run", "--runtime=required", hello], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=work.env, cwd=work.root) for _ in range(2)]
    results = [process.communicate(timeout=60) + (process.returncode,) for process in processes]
    foreground = [pid for pid in supervisors(work) if b"--foreground" in Path(f"/proc/{pid}/cmdline").read_bytes()]
    check(all(r == (b"Hello, World!\n", b"", 0) for r in results) and len(foreground) == 1,
          f"case 8: two first launches at once: both run, one supervisor ({len(foreground)})")

    # The launch through the service, and its report.
    status, out, err = neura_run(["--runtime=required", f"--runtime-report={report}", exit_status, "--", "3", "x y"])
    data = json.loads(report.read_text())
    check((status, out, err) == (3, b"argument: 3\nargument: x y\n", b"") and data["schema"] == 1 and
          data["path"] in ("service-warm", "service-fresh") and data["state"] == "exited" and data["status"] == 3 and
          data["application"].startswith("dev."), "a service launch reports its path, application and outcome")
    status, out, err = neura_run([f"--runtime-report={report}", hello])
    check(status == 0 and path_taken() == "direct", "--runtime-report of a direct run says direct")

    # Case 58: what the service does not support.
    lists = work.root / "lists.txt"
    lists.write_text("a\n")
    trace = work.root / "hello.trace"
    for label, options in (("--record", ["--record", str(trace)]), ("--each", ["--each", str(lists)])):
        status, out, err = neura_run(["--runtime=required", *options, hello])
        check(status == 5 and out == b"" and err == f"neura: runtime: {label} is not supported by the service; run "
              f"without --runtime\n".encode(), f"case 58: {label} with required is status 5")
        status, out, err = neura_run(["--runtime=auto", f"--runtime-report={report}", *options, hello])
        check(status == 0 and err.startswith(f"neura: runtime: running directly: {label} is not supported".encode())
              and path_taken() == "direct-fallback", f"case 58: {label} with auto runs directly with the line")
    status, out, err = neura_run(["--runtime=required", "--replay", str(trace), hello])
    check(status == 5 and b"--replay is not supported" in err, "case 58: --replay with required is status 5")

    # An image inside a native executable: the client extracts it.
    native = work.root / "native-exit"
    built = subprocess.run([str(neura_path_of(work)), "build", "--native", str(PROGRAMS / "exit-status.nra"), "-o",
                            str(native)], capture_output=True, env=work.env, cwd=work.root)
    if built.returncode == 0:
        expected = neura_run([str(native), "--", "3", "x"])
        got = neura_run(["--runtime=required", f"--runtime-report={report}", str(native), "--", "3", "x"])
        check(got == expected and expected[0] == 3 and path_taken().startswith("service"),
              "an image inside a native executable runs through the service as directly")
    else:
        print("skip: no native launcher in this build:", built.stderr.decode()[:200])

    # A graph device is not supported; --gpu=off makes a GPU build's run eligible.
    no_gpu = {k: v for k, v in work.env.items() if k != "NEURA_GPU"}
    status, out, err = neura_run(["--runtime=required", "--gpu=off", hello], env=no_gpu)
    check((status, out, err) == (0, b"Hello, World!\n", b""), "--gpu=off runs through the service")
    status, out, err = neura_run(["--runtime=required", "--gpu=always", hello], env=no_gpu)
    # Only a GPU build's message names --gpu=off (docs/design/Service030.md):
    # a build without a graph device uses one only when the run asks for it.
    gpu_build = os.environ.get("NEURA_TEST_GPU_BUILD") == "1"
    check(status == 5 and b"a graph device" in err and (b"--gpu=off" in err) == gpu_build,
          f"case 58: a graph device with required is status 5{' and names --gpu=off' if gpu_build else ''} ({err!r})")

    # Case 23: an image that fails validation.
    bad = work.root / "bad.nvr"
    bad.write_bytes(b"NEUR" + b"\xff" * 64)
    expected = neura_run([str(bad)])
    got = neura_run(["--runtime=required", f"--runtime-report={report}", str(bad)])
    check(got == expected and got[0] == 1 and json.loads(report.read_text())["state"] == "load-failed",
          "case 23: an invalid image is load-failed with the direct run's message and status")

    # Case 31: an environment over the 1 MiB message limit. exec() takes at
    # most ARG_MAX (often 2 MiB) of arguments and environment together and
    # 128 KiB per string, so the test uses 1.5 MiB in 100 KiB variables.
    big = dict(work.env, **{f"BIG{index}": "x" * (100 << 10) for index in range(15)})
    status, out, err = neura_run(["--runtime=auto", hello], env=big)
    check(status == 0 and out == b"Hello, World!\n" and b"running directly: " in err,
          "case 31: a 1.5 MiB environment runs directly under auto")
    status, out, err = neura_run(["--runtime=required", hello], env=big)
    check(status == 5 and out == b"", "case 31: a 1.5 MiB environment is status 5 under required")

    # Case 21: the image file replaced after the commit.
    moving = work.root / "moving.nvr"
    shutil.copy2(echo, moving)
    process = subprocess.Popen([str(work.neura), "run", "--runtime=required", str(moving)], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    wait_until(lambda: assigned_workers(work))
    shutil.copy2(hello, moving)
    out, err = process.communicate(b"still echo\n", timeout=30)
    check(process.returncode == 0 and out == b"[still echo]\nend\n",
          "case 21: a file replaced after the commit: the committed bytes run")

    # Case 39: the worker killed with SIGKILL.
    process = subprocess.Popen([str(work.neura), "run", "--runtime=required", f"--runtime-report={report}", echo],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env,
                               cwd=work.root)
    check(wait_until(lambda: assigned_workers(work)), "case 39: the application runs")
    for pid in assigned_workers(work):
        os.kill(pid, signal.SIGKILL)
    out, err = process.communicate(timeout=30)
    check(process.returncode == 137 and err == b"neura: runtime: the application was ended by signal 9\n" and
          json.loads(report.read_text())["state"] == "worker-lost", "case 39: a killed worker is worker-lost, status 137")

    # Signals: Ctrl-C and SIGTERM reach the application only.
    for number in (signal.SIGINT, signal.SIGTERM):
        # A shell may start the test with SIGINT ignored, which a direct run
        # and the application would inherit: give the client the default.
        process = subprocess.Popen([str(work.neura), "run", "--runtime=required", f"--runtime-report={report}", echo],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=work.env, cwd=work.root,
                                   preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL))
        wait_until(lambda: assigned_workers(work))
        process.send_signal(number)
        # Its input stays open until the client ends, so only the signal can end it.
        process.wait(timeout=30)
        out, err = process.communicate(timeout=30)
        check(process.returncode == 128 + number and json.loads(report.read_text())["state"] == "signaled",
              f"signal {number} is forwarded to the application, which it ends ({process.returncode}, {err!r}, "
              f"{report.read_text() if report.exists() else ''})")

    # Case 37: the client killed during a foreground run.
    process = subprocess.Popen([str(work.neura), "run", "--runtime=required", echo], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    wait_until(lambda: assigned_workers(work))
    workers = assigned_workers(work)
    process.kill()
    process.wait(timeout=30)
    # Its input stays open: only the client's end can end the application.
    check(workers and wait_until(lambda: not assigned_workers(work), 12),
          "case 37: a killed client ends its application within the grace period")
    service = pc.Service(work.bin, work.env)
    control = service.hello(purpose=2)
    control.send("List", [])
    records = [json.loads(r) for r in control.receive().all("Record")]
    check(any(r["state"] == "client-lost" for r in records), "case 37: the application is client-lost")
    process.communicate(timeout=30)

    # Case 38: the client gone after a detached commit; the application goes on.
    fifo = work.root / "input.fifo"
    os.mkfifo(fifo)
    out_file = work.root / "detached.out"
    process = subprocess.Popen([str(work.neura), "run", "--runtime=required", "--detach", f"--stdout={out_file}",
                                f"--stderr={work.root / 'detached.err'}", f"--stdin={fifo}", echo],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    writer = os.open(fifo, os.O_WRONLY)
    out, err = process.communicate(timeout=30)
    application = out.decode().strip()
    check(process.returncode == 0 and application.startswith("dev.") and out.endswith(b"\n"),
          "--detach prints the application ID and exits 0")
    os.write(writer, b"after the client\n")
    os.close(writer)
    check(wait_until(lambda: out_file.read_bytes() == b"[after the client]\nend\n"),
          "case 38: the detached application continues after its client is gone")

    # Case 13: sixty-four applications, a sixty-fifth.
    connection = service.hello()
    echo_bytes = Path(echo).read_bytes()
    holders = []
    for _ in range(64):
        read_end, write_end = os.pipe()
        committed, _ = launch(service, connection, echo_bytes, echo, os.open(os.devnull, os.O_WRONLY),
                              stdin=read_end, wait=False)
        os.close(read_end)
        holders.append(write_end)
    status, out, err = neura_run(["--runtime=required", hello])
    check(status == 5 and b"(admission-applications)" in err, "case 13: the sixty-fifth under required is status 5")
    status, out, err = neura_run(["--runtime=auto", hello])
    check(status == 0 and out == b"Hello, World!\n" and b"running directly:" in err and b"admission-applications" in err,
          "case 13: the sixty-fifth under auto runs directly")
    for fd in holders:
        os.close(fd)
    connection.close()
    wait_until(lambda: not assigned_workers(work), 20)

    # Case 10: the endpoint directory with a wrong mode.
    os.chmod(service.directory, 0o755)
    for mode in ("auto", "required"):
        status, out, err = neura_run([f"--runtime={mode}", hello])
        check(status == 5 and out == b"" and b"nothing was run" in err, f"case 10: a wrong mode is identity under {mode}")
    os.chmod(service.directory, 0o700)

    # Case 30: a soft limit above the supervisor's hard limit.
    import resource
    kill_supervisors(work)
    wait_until(lambda: not supervisors(work))
    service = pc.Service(work.bin, work.env)
    service.start(preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_NOFILE, (256, 256)))
    for mode, expected_status in (("required", 5), ("auto", 0)):
        status, out, err = neura_run([f"--runtime={mode}", hello])
        check(status == expected_status and b"not-reproducible" in err, f"case 30: not-reproducible under {mode}")
    service.process.kill()
    service.process.wait()

    # Case 29: LD_LIBRARY_PATH takes a fresh worker; the program sees it.
    # The supervisor is started by a launch without the variable.
    environment = program(work, "environment")
    neura_run(["--runtime=required", hello])
    env = dict(work.env, LD_LIBRARY_PATH=str(work.root))
    status, out, _ = neura_run(["--runtime=required", f"--runtime-report={report}", "--allow-env", environment, "--",
                                "LD_LIBRARY_PATH"], env=env)
    check(status == 0 and out == f"LD_LIBRARY_PATH={work.root}\n".encode() and path_taken() == "service-fresh",
          f"case 29: a caller's LD_LIBRARY_PATH: the path is fresh ({status}, {out!r}, {path_taken()})")

    # Case 11: a supervisor of another build of the same generation.
    kill_supervisors(work)
    wait_until(lambda: not supervisors(work))
    service = pc.Service(work.bin, work.env)
    service.start(extra_env={"NEURA_TEST_SERVICE_TIMERS": "other-build=1"})
    for mode in ("auto", "required"):
        status, out, err = neura_run([f"--runtime={mode}", hello])
        check(status == 5 and out == b"" and b"another build" in err and b"nothing was run" in err,
              f"case 11: another build is identity under {mode}, not a fallback")
    control = service.hello(purpose=2)
    control.send("List", [])
    check(control.receive().kind == "Listing", "case 11: control still works with another build")
    service.process.kill()
    service.process.wait()

    # Cases 18 and 19: the connection lost after the commit.
    service = pc.Service(work.bin, work.env)
    service.start(extra_env={"NEURA_TEST_SERVICE_TIMERS": "drop-after-commit=1"})
    status, out, err = neura_run(["--runtime=required", exit_status, "--", "4"])
    check((status, out, err) == (4, b"argument: 4\n", b""),
          f"({status}, {out!r}, {err!r}) case 18: the connection dropped after the commit: the client asks and exits with the application's status")
    service.process.kill()
    service.process.wait()
    process = subprocess.Popen([str(work.neura), "run", "--runtime=required", echo], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    wait_until(lambda: assigned_workers(work))
    kill_supervisors(work, foreground_only=True)
    time.sleep(0.5)
    out, err = process.communicate(b"", timeout=30)
    check(process.returncode == 6 and b"is unknown; it was not run again" in err and out == b"",
          f"({process.returncode}, {out!r}, {err!r}) case 19: the supervisor gone after the commit: status 6, not run again")


# --------------------------------------------------------------------------
# Step 8: the `neura runtime` commands.

def fake_install(work: Work, root: Path, generations: list[str], current: str, unmigrated: bool = False) -> Path:
    """An archive install of this build: <root>/versions/<g>/bin, current, a schema 1 marker."""
    for generation in generations:
        bin_dir = root / "versions" / generation / "bin"
        bin_dir.mkdir(parents=True)
        for name in ("neura", "neura-run", "neurad"):
            shutil.copy2(work.bin / name, bin_dir / name)
        shutil.copytree(work.bin / "lib", bin_dir / "lib")
        if unmigrated:
            components = root / "versions" / generation / "share/neura/components"
            components.mkdir(parents=True)
            (components / "core.txt").write_text("neura-component 1\nname core\n")
    os.symlink(f"versions/{current}", root / "current")
    (root / "neura-install.txt").write_text("neura-install 1\nmethod archive\nlink " + str(root / "link/neura") +
                                            "\n" + "".join(f"version {g}\n" for g in generations))
    return root / "versions" / current / "bin" / "neura"


def runtime(work: Work, neura: Path, *arguments: str, env: dict | None = None, timeout: float = 120):
    result = subprocess.run([str(neura), "runtime", *arguments], capture_output=True, env=env or work.env,
                            cwd=work.root, timeout=timeout)
    return result.returncode, result.stdout, result.stderr


def control_suite(work: Work) -> None:
    hello = str(work.image("hello", path=HELLO))
    echo = program(work, "echo")
    exit_status = program(work, "exit-status")
    neura = work.neura

    def neura_run(arguments, neura_path=None, env=None, stdin=b""):
        result = subprocess.run([str(neura_path or neura), "run", *arguments], input=stdin, capture_output=True,
                                env=env or work.env, cwd=work.root, timeout=60)
        return result.returncode, result.stdout, result.stderr

    # Case 52: status, ps, inspect and doctor with nothing running.
    before = (work.files_under(work.run_dir), work.files_under(work.state), work.files_under(work.home))
    status, out, _ = runtime(work, neura, "status")
    check(status == 0 and b"generation dev: not running" in out, "case 52: status with nothing running")
    status, out, _ = runtime(work, neura, "status", "--json")
    data = json.loads(out)
    check(status == 0 and data["schema"] == 1 and data["installed"] and data["supervisors"][0]["running"] is False,
          "case 52: status --json with nothing running")
    check(runtime(work, neura, "ps")[0] == 0 and runtime(work, neura, "ps", "--all", "--json")[0] == 0,
          "case 52: ps with nothing running")
    status, _, err = runtime(work, neura, "inspect", "dev.0123abcd.1")
    check(status == 1 and b"is not an application of this installation" in err, "case 52: inspect with nothing running")
    subprocess.run([str(neura), "doctor"], capture_output=True, env=work.env, cwd=work.root)
    check((work.files_under(work.run_dir), work.files_under(work.state), work.files_under(work.home)) == before
          and not supervisors(work), "case 52: no process, socket or file appears")

    # Usage errors and IDs of another installation.
    status, _, err = runtime(work, neura, "bogus")
    check(status == 1 and b"unknown runtime command" in err, "an unknown runtime command is a usage error")
    for bad in ("not-an-id", "other.deadbeef.1", "dev.DEADBEEF.1"):
        status, _, err = runtime(work, neura, "inspect", bad)
        check(status == 1 and err == f"neura: runtime: {bad} is not an application of this installation\n".encode(),
              f"inspect {bad}: not an application of this installation")
        status, _, err = runtime(work, neura, "stop", bad)
        check(status == 1 and b"is not an application of this installation" in err, f"stop {bad}: status 1")

    # start keeps a supervisor that does not exit when idle.
    env = dict(work.env, NEURA_TEST_SERVICE_TIMERS="idle=300")
    status, out, _ = runtime(work, neura, "start", env=env)
    check(status == 0 and b"is running" in out, "neura runtime start starts the supervisor and waits for it")
    time.sleep(1.0)
    status, out, _ = runtime(work, neura, "status", "--json")
    data = json.loads(out)["supervisors"][0]
    check(data["running"] and data["state"] == "accepting" and data["readyWorkers"] == 1,
          "a supervisor started by neura runtime start stays when idle, with one ready worker")
    check(runtime(work, neura, "start")[0] == 0, "start of a running service is status 0")

    # Cases 46 and 47: stop of an application that ended, and of an expired ID.
    status, _, _ = neura_run(["--runtime=required", exit_status, "--", "3"])
    status, out, _ = runtime(work, neura, "ps", "--all", "--json")
    ended = [a for a in json.loads(out)["applications"] if a["state"] == "exited"]
    check(len(ended) == 1 and ended[0]["status"] == 3, "ps --all lists the terminal record")
    check(runtime(work, neura, "ps", "--json")[1] == b'{"applications":[],"schema":1}\n', "ps lists live applications only")
    application = ended[0]["id"]
    status, out, _ = runtime(work, neura, "stop", application)
    check((status, out) == (0, f"{application}: exited\n".encode()), "case 46: stop of an ended application prints its state")
    status, out, _ = runtime(work, neura, "inspect", application, "--json")
    record = json.loads(out)
    check(status == 0 and record["state"] == "exited" and record["status"] == 3 and record["arguments"] == 1,
          "inspect --json gives the record")
    status, out, _ = runtime(work, neura, "inspect", application)
    check(status == 0 and b"state: exited" in out and b"status: 3" in out, "inspect prints the record")
    # The record is removed: the number was issued, so the ID is expired.
    for path in work.state.rglob(f"records/{application}"):
        path.unlink()
    service = pc.Service(work.bin, work.env)
    kill_supervisors(work)
    wait_until(lambda: not supervisors(work))
    status, out, _ = runtime(work, neura, "stop", application)
    check((status, out) == (1, f"{application}: expired\n".encode()), "case 47: stop of an expired ID is status 1")
    status, out, _ = runtime(work, neura, "inspect", application)
    check(status == 1 and out == f"{application}: expired\n".encode() and not supervisors(work),
          "case 51: inspect of a gone supervisor's pruned record: expired, nothing started")

    # Case 40: the supervisor killed while three applications run.
    processes = [subprocess.Popen([str(neura), "run", "--runtime=required", echo], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
                 for _ in range(3)]
    check(wait_until(lambda: len(assigned_workers(work)) == 3), "case 40: three applications run")
    kill_supervisors(work, foreground_only=True)
    check(wait_until(lambda: not assigned_workers(work)), "case 40: all three workers end with their supervisor")
    results = [process.communicate(timeout=30) + (process.returncode,) for process in processes]
    check(all(r[2] == 6 and b"is unknown; it was not run again" in r[1] and r[0] == b"" for r in results),
          "case 40: their foreground clients exit 6 and nothing is replayed")
    status, out, _ = runtime(work, neura, "ps", "--all", "--json")
    states = sorted(a["state"] for a in json.loads(out)["applications"])
    check(states.count("unknown") == 3 and not supervisors(work),
          f"case 40: ps --all shows them unknown and starts nothing ({states})")

    # Case 48: shutdown with a detached application that never ends.
    fifo = work.root / "never.fifo"
    os.mkfifo(fifo)
    holder = os.open(fifo, os.O_RDWR)
    status, out, _ = neura_run(["--runtime=required", "--detach", f"--stdout={work.root / 'n.out'}",
                                f"--stderr={work.root / 'n.err'}", f"--stdin={fifo}", echo])
    detached = out.decode().strip()
    status, out, err = runtime(work, neura, "shutdown", "--timeout=1")
    check(status == 1 and detached.encode() in out and b"still running" in err,
          "case 48: shutdown times out with status 1 and lists the application")
    status, out, _ = runtime(work, neura, "ps", "--json")
    check(any(a["id"] == detached for a in json.loads(out)["applications"]), "case 48: the application runs on")
    status, out, err = neura_run(["--runtime=required", hello])
    check(status == 5 and b"(draining)" in err, "case 48: later launches get draining")
    status, out, err = neura_run(["--runtime=auto", hello])
    check(status == 0 and out == b"Hello, World!\n" and b"running directly:" in err and b"draining" in err,
          "case 49: the draining supervisor refuses; auto runs directly")
    status, out, _ = runtime(work, neura, "stop", detached, "--grace=1")
    check((status, out) == (0, f"{detached}: stopped\n".encode()), "stop ends a detached application: stopped")
    os.close(holder)
    check(wait_until(lambda: not supervisors(work)), "case 49: the draining supervisor exits when it is empty")
    status, out, err = neura_run(["--runtime=auto", f"--runtime-report={work.root / 'r.json'}", hello])
    check(status == 0 and err == b"" and json.loads((work.root / "r.json").read_text())["path"].startswith("service"),
          "case 49: after it exits, the next auto launch starts a new one")

    # shutdown --terminate stops what runs; cache clear.
    process = subprocess.Popen([str(neura), "run", "--runtime=required", echo], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    wait_until(lambda: assigned_workers(work))
    status, out, _ = runtime(work, neura, "cache", "clear")
    check(status == 0 and b"cache bytes in use" in out and not out.startswith(b"generation dev: 0 "),
          "cache clear keeps the image of a running application")
    status, out, _ = runtime(work, neura, "shutdown", "--terminate", "--timeout=20")
    process.wait(timeout=30)
    check(status == 0 and process.returncode == 128 + signal.SIGTERM and not supervisors(work),
          "shutdown --terminate stops the application and the supervisor exits")
    process.communicate()

    # The idle exit of a supervisor started on demand.
    env = dict(work.env, NEURA_TEST_SERVICE_TIMERS="idle=400")
    neura_run(["--runtime=required", hello], env=env)
    check(supervisors(work) and wait_until(lambda: not supervisors(work), 10),
          "a supervisor started on demand exits when idle")

    # Case 55: two installations; case 56: a root through a symbolic link,
    # with a space and a non-ASCII letter.
    other = copy_build(work, "second build")
    status, _, _ = neura_run(["--runtime=required", exit_status, "--", "1"], neura_path=other / "neura")
    status, _, _ = neura_run(["--runtime=required", exit_status, "--", "2"])
    first_ps = json.loads(runtime(work, neura, "ps", "--all", "--json")[1])["applications"]
    second_ps = json.loads(runtime(work, other / "neura", "ps", "--all", "--json")[1])["applications"]
    sockets = sorted(str(p.relative_to(work.run_dir)) for p in work.run_dir.rglob("s"))
    second_ids = {a["id"] for a in second_ps}
    check(len(sockets) == 2 and any(a.get("status") == 2 for a in first_ps)
          and not second_ids & {a["id"] for a in first_ps} and [a["status"] for a in second_ps] == [1],
          f"case 55: two installations, two sockets, separate ps ({sockets})")
    odd = work.root / "a b é"
    odd.mkdir()
    real = copy_build(work, "real build")
    target = odd / "build"
    os.rename(real, target)
    link = work.root / "link"
    os.symlink(target, link)
    for via in (link / "neura", target / "neura"):
        status, out, _ = neura_run(["--runtime=required", hello], neura_path=via)
        check(status == 0 and out == b"Hello, World!\n", f"case 56: launch through {via.parent.name}")
    ids = {p.parent.parent.name for p in work.state.rglob("records") if "dev" in str(p)}
    check(len(ids) == 3, f"case 56: one installation ID from the real path ({len(ids)} with the other two)")
    runtime(work, other / "neura", "shutdown", "--terminate", "--timeout=10")
    runtime(work, link / "neura", "shutdown", "--terminate", "--timeout=10")
    runtime(work, neura, "shutdown", "--terminate", "--timeout=10")

    # Case 50: an update activates another generation while an application runs.
    root = work.root / "install"
    old_neura = fake_install(work, root, ["0.3.0", "0.3.1"], "0.3.0")
    new_neura = root / "versions/0.3.1/bin/neura"
    process = subprocess.Popen([str(old_neura), "run", "--runtime=required", echo], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    wait_until(lambda: assigned_workers(work))
    os.unlink(root / "current")
    os.symlink("versions/0.3.1", root / "current")
    status, out, _ = runtime(work, old_neura, "status", "--json")
    # The old supervisor notices at its next Prepare; a launch of the old
    # generation retires it.
    neura_run(["--runtime=required", hello], neura_path=old_neura)
    old_status = [s for s in json.loads(runtime(work, new_neura, "status", "--json")[1])["supervisors"]
                  if s["generation"] == "0.3.0"]
    check(old_status and old_status[0]["running"] and old_status[0]["state"] == "retiring"
          and old_status[0]["readyWorkers"] == 0, "case 50: the old supervisor retires and drops its ready worker")
    listed = json.loads(runtime(work, new_neura, "ps", "--json")[1])["applications"]
    check(len(listed) == 1 and listed[0]["id"].startswith("0.3.0."), "case 50: ps from the new generation lists it")
    status, out, _ = runtime(work, new_neura, "stop", listed[0]["id"], "--grace=2")
    check(status == 0 and out.endswith(b": stopped\n"), "case 50: stop from the new generation reaches it")
    process.communicate(timeout=30)
    check(wait_until(lambda: not [p for p in supervisors(work) if b"0.3.0" in Path(f"/proc/{p}/cmdline").read_bytes()]),
          "case 50: the retired supervisor exits when it is empty")

    # Case 63: a release candidate's generation under a TMPDIR of 49 bytes.
    root = work.root / "rc"
    rc_neura = fake_install(work, root, ["0.3.0-rc.1"], "0.3.0-rc.1")
    tmp = work.root / ("t" * (49 - len(str(work.root)) - 1))
    tmp.mkdir()
    check(len(str(tmp)) == 49, "case 63: the TMPDIR is 49 bytes")
    env = dict(work.env, TMPDIR=str(tmp))
    env.pop("XDG_RUNTIME_DIR")
    status, out, _ = neura_run(["--runtime=required", hello], neura_path=rc_neura, env=env)
    sockets = [p for p in tmp.rglob("s")]
    check(status == 0 and out == b"Hello, World!\n" and len(sockets) == 1 and len(str(sockets[0])) < 104,
          f"case 63: the socket path is under 104 bytes ({len(str(sockets[0])) if sockets else 0}) and the service starts")
    runtime(work, rc_neura, "shutdown", "--terminate", "--timeout=10", env=env)


# --------------------------------------------------------------------------
# Step 9: login start, the capability report, and service modes refused
# where they do not apply.

def login_suite(work: Work) -> None:
    hello = str(work.image("hello", path=HELLO))
    neura = work.neura
    stand_in = work.root / "manager"
    stand_in.mkdir()
    calls = work.root / "manager.log"
    (stand_in / "systemctl").write_text(
        "#!/bin/sh\n"
        f'echo "$*" >> "{calls}"\n'
        'if [ "$2" = show-environment ]; then exit "${STAND_IN_ANSWERS:-0}"; fi\n'
        "exit 0\n")
    os.chmod(stand_in / "systemctl", 0o755)
    env = dict(work.env, PATH=f"{stand_in}:/usr/bin:/bin")
    service = pc.Service(work.bin, work.env)
    unit = work.home / ".config/systemd/user" / f"neurad-{service.installation}.service"

    # Case 53: no user service manager answers.
    for label, manager_env in (("no systemctl", dict(work.env, PATH="/nonexistent")),
                               ("systemctl without a user manager", dict(env, STAND_IN_ANSWERS="1"))):
        status, out, err = runtime(work, neura, "enable", env=manager_env)
        check(status == 1 and b"no user service manager answers here" in err and not unit.exists()
              and not (work.home / ".config").exists(), f"case 53: enable with {label}: status 1, nothing written")
    status, out, _ = subprocess.run([str(neura), "run", "--runtime=required", hello], capture_output=True,
                                    env=dict(work.env, PATH="/nonexistent"), cwd=work.root).returncode, b"", b""
    check(status == 0, "case 53: --runtime=required still works there")
    runtime(work, neura, "shutdown", "--terminate", "--timeout=10")

    # Case 54: enable twice, disable twice.
    for attempt in (1, 2):
        status, out, _ = runtime(work, neura, "enable", env=env)
        check(status == 0 and unit.exists(), f"case 54: enable {attempt} succeeds")
    units = list((work.home / ".config/systemd/user").iterdir())
    text = unit.read_text()
    check(len(units) == 1 and f'ExecStart="{work.bin}/neurad" --foreground --root "{work.bin}"' in text
          and "Restart=no" in text, "case 54: one unit, which starts this installation's supervisor and never restarts it")
    log = calls.read_text()
    check(f"--user enable neurad-{service.installation}.service" in log, "enable asks systemctl --user to enable the unit")
    status, out, _ = runtime(work, neura, "status", "--json")
    check(json.loads(out)["loginStart"] is True, "status reports login start registered")
    check(not supervisors(work), "enable starts nothing")
    for attempt in (1, 2):
        status, out, _ = runtime(work, neura, "disable", env=env)
        check(status == 0 and not unit.exists(), f"case 54: disable {attempt} succeeds")
    check(f"--user disable neurad-{service.installation}.service" in calls.read_text(), "disable asks systemctl to disable")

    # Cases 1 and 2 with a supervisor running and login start registered:
    # the defaults stay direct and make no connection.
    runtime(work, neura, "enable", env=env)
    service.start()
    control = service.hello(purpose=2)
    before = accepted_connections(control)
    result = subprocess.run([str(neura), "run", hello], capture_output=True, env=work.env, cwd=work.root)
    check((result.returncode, result.stdout, result.stderr) == (0, b"Hello, World!\n", b""),
          "case 1: plain neura run with a supervisor running and login start registered")
    tests = work.root / "suite.nra"
    tests.write_text("let test = import:Std.Test;\n\nlet Suite = module (\n"
                     "    export let test_adds = func() -> void {\n        test.assert(1 + 1 == 2);\n    };\n);\n")
    result = subprocess.run([str(neura), "test", str(tests)], capture_output=True, env=work.env, cwd=work.root)
    check(result.returncode == 0, "case 2: plain neura test with a supervisor running")
    ended, _ = debug_session(work, HELLO)
    check(ended, "case 2: a debug launch with a supervisor running")
    after = accepted_connections(control)
    control.send("List", [])
    listing = control.receive()
    check(after == before and listing.number("ApplicationCount") == 0 and not listing.all("Record"),
          f"case 1: the supervisor saw no connection and no application ({before} then {after})")
    control.close()
    service.process.kill()
    service.process.wait()
    runtime(work, neura, "disable", env=env)

    # Case 61: neura capabilities reads files only.
    def capabilities(neura_path: Path) -> dict:
        result = subprocess.run([str(neura_path), "capabilities", "--json"], capture_output=True, env=work.env)
        return json.loads(result.stdout)
    import socket as sockets
    service.directory.mkdir(parents=True, exist_ok=True)
    os.chmod(service.directory.parent, 0o700)
    os.chmod(service.directory, 0o700)
    listener = sockets.socket(sockets.AF_UNIX, sockets.SOCK_STREAM)
    service.socket.unlink(missing_ok=True)
    listener.bind(str(service.socket))
    listener.listen(4)
    listener.setblocking(False)
    with_listener = capabilities(neura)
    try:
        listener.accept()
        connected = True
    except BlockingIOError:
        connected = False
    listener.close()
    stale = capabilities(neura)
    service.start()
    running_answer = capabilities(neura)
    bare = copy_build(work, "bare", neurad=None)
    missing = capabilities(bare / "neura")
    check(not connected and with_listener["service"] == stale["service"] == running_answer["service"] ==
          {"installed": True, "protocol": "1.0", "state": "ready"},
          "case 61: the same answer with a stale socket and with a supervisor running, and no connection")
    check(missing["service"] == {"installed": False, "state": "missing"}, "case 61: a missing neurad is reported missing")
    check(with_listener["features"] == missing["features"] | {"coverage": True, "debugger": True, "documentation": True,
                                                                "formatter": True, "lsp": True, "testCoverage": True,
                                                                "testDiscovery": True, "testFilter": True,
                                                                "testReporter": True},
          "capabilities keeps its 0.2 features")
    service.process.kill()
    service.process.wait()

    # Case 60: an installation not yet migrated from a 0.2 update.
    root = work.root / "unmigrated"
    old = fake_install(work, root, ["0.3.0"], "0.3.0", unmigrated=True)
    instruction = b'finish the 0.3.0 install first: run "neura update --migrate-install"'
    for mode in ("required", "auto"):
        result = subprocess.run([str(old), "run", f"--runtime={mode}", hello], capture_output=True, env=work.env)
        check(result.returncode == 5 and instruction in result.stderr and result.stdout == b"",
              f"case 60: --runtime={mode} is refused with the instruction")
    for command in ("start", "enable"):
        status, out, err = runtime(work, old, command, env=env)
        check(status == 5 and instruction in err, f"case 60: neura runtime {command} is refused")
    result = subprocess.run([str(old), "run", "--runtime=off", hello], capture_output=True, env=work.env)
    check(result.returncode == 0 and result.stdout == b"Hello, World!\n", "case 60: --runtime=off runs")
    check(runtime(work, old, "status")[0] == 0 and runtime(work, old, "ps")[0] == 0, "case 60: status and ps answer")
    check(capabilities(old)["service"]["state"] == "unmigrated", "case 61: capabilities reports the state unmigrated")
    check(not [p for p in supervisors(work)] and not list(root.rglob("*.service")),
          "case 60: nothing was started or written")

    # Case 59: a debug launch that asks for a service mode.
    for value in ("required", "auto"):
        ended, messages = debug_session(work, HELLO, {"runtime": value})
        failed = [m for m in messages if m.get("type") == "response" and m.get("command") == "launch"]
        check(failed and not failed[0]["success"] and "runtime" in failed[0].get("message", "") and not ended,
              f"case 59: a debug launch with runtime {value} is refused before anything starts")
    ended, _ = debug_session(work, HELLO, {"runtime": "off"})
    check(ended, "a debug launch with runtime off runs")
    check(not supervisors(work), "case 59: no supervisor was started")


def parity_suite(work: Work) -> None:
    """The direct identity cases through --runtime=required (the note's parity gate)."""
    script = REPOSITORY / "scripts/runtime-identity.py"
    images = work.root / "images"
    captures = {}
    for name, extra in (("direct", []), ("service", ["--service"])):
        out = work.root / name
        result = subprocess.run([sys.executable, str(script), "capture", "--neura", str(work.neura), "--images",
                                 str(images), "--out", str(out), *extra], capture_output=True, env=work.env,
                                timeout=1800)
        captures[name] = result.stdout.decode()
        check(result.returncode == 0, f"the {name} capture ran\n{result.stderr.decode()[-2000:]}")
    runs = re.search(r"; (\d+) runs through the service", captures["service"])
    check(runs is not None and int(runs.group(1)) > 100,
          f"the service capture reached the service ({runs.group(1) if runs else 0} runs)")
    result = subprocess.run([sys.executable, str(script), "compare", str(work.root / "direct"),
                             str(work.root / "service")], capture_output=True, env=work.env)
    print(result.stdout.decode()[:2000])
    check(result.returncode == 0 and b"identical: " in result.stdout,
          "the identity cases through the service equal the direct run's: output, status, traps, steps, profile")


def isolation_suite(work: Work) -> None:
    """Cases 26 and 27 through one supervisor, and the isolation gate."""
    report = work.root / "report.json"
    image = program(work, "exit-status")

    def through(arguments, env):
        result = subprocess.run([str(work.neura), "run", "--runtime=required", f"--runtime-report={report}",
                                 *arguments], capture_output=True, env=env, cwd=work.root, timeout=60)
        return result.returncode, result.stdout, result.stderr, json.loads(report.read_text())

    # The supervisor's own environment says NEURA_JIT=off and NEURA_TIER3=off.
    service = pc.Service(work.bin, work.env)
    service.start(extra_env={"NEURA_JIT": "off", "NEURA_TIER3": "off"})
    control = service.hello(purpose=2)
    for variables in ({"NEURA_TIER3": "off"}, {"NEURA_TIER3": "native"}, {"NEURA_TIER3": "interp"},
                      {"NEURA_JIT": "on"}, {}):
        env = dict(work.env, **variables)
        wait_until(lambda: listing_value(control, "ReadyWorkers") == 1)
        status, out, err, data = through(["--tier-stats", image, "--", "0"], env)
        direct = subprocess.run([str(work.neura), "run", "--tier-stats", image, "--", "0"], capture_output=True,
                                env=env, cwd=work.root)
        def mode_lines(stderr: bytes) -> list[bytes]:
            # The lines that show the mode; lift counts may depend on timing.
            lines = tier_lines(stderr)
            jit = [line.split(b":")[1].strip() for line in lines if line.startswith(b"tier: jit")]
            return jit + [b"native programs" for line in lines if line.startswith(b"tier: native programs")]
        check(status == 0 and data["path"] == "service-warm" and mode_lines(err) == mode_lines(direct.stderr)
              and mode_lines(err), f"cases 26, 27: {variables or 'nothing set'} through one warm supervisor shows "
                                   f"the caller's mode ({data['path']})")

    # Simultaneous launches that write globals, statics and files.
    state = program(work, "state")
    processes = []
    for index in range(8):
        processes.append(subprocess.Popen([str(work.neura), "run", "--runtime=required", state, "--",
                                           f"launch{index}", f"file{index}.txt"], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env,
                                          cwd=work.root))
    check(wait_until(lambda: len(assigned_workers(work)) == 8 and all(
        (work.root / f"file{i}.txt").exists() for i in range(8))), "isolation: eight launches run at once")
    pids = set(assigned_workers(work))
    results = [process.communicate(b"\n", timeout=60) for process in processes]
    check(all(out == f"launch{i}: launches 1, count 1000\n".encode() for i, (out, _) in enumerate(results)),
          "isolation: each launch has its own globals and statics")
    check(all((work.root / f"file{i}.txt").read_text() == f"launch{i}" for i in range(8)),
          "isolation: each launch wrote its own file")
    seen = set(pids)
    for _ in range(4):
        process = subprocess.Popen([str(work.neura), "run", "--runtime=required", state, "--", "again", "again.txt"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=work.env, cwd=work.root)
        wait_until(lambda: assigned_workers(work))
        current = set(assigned_workers(work))
        out, _ = process.communicate(b"\n", timeout=60)
        check(not current & seen and out == b"again: launches 1, count 1000\n",
              "isolation: a worker's process ID is never seen again, and a new launch starts from zero")
        seen |= current
    startup = program(work, "startup-trap")
    first = subprocess.run([str(work.neura), "run", "--runtime=required", startup], capture_output=True, env=work.env)
    second = subprocess.run([str(work.neura), "run", "--runtime=required", f"--runtime-report={report}", startup],
                            capture_output=True, env=work.env)
    direct = subprocess.run([str(work.neura), "run", startup], capture_output=True, env=work.env)
    check((first.returncode, first.stderr) == (second.returncode, second.stderr) == (direct.returncode, direct.stderr)
          and json.loads(report.read_text())["state"] == "trapped",
          "case 42: a trap in startup; the next launch of the image runs startup again and traps the same way")
    after = subprocess.run([str(work.neura), "run", "--runtime=required", state, "--", "clean", "clean.txt"],
                           input=b"\n", capture_output=True, env=work.env, cwd=work.root)
    check(after.stdout == b"clean: launches 1, count 1000\n", "isolation: a launch after a trapped one starts clean")
    service.process.kill()
    service.process.wait()


def listing_value(connection, tag: str) -> int | None:
    connection.send("List", [])
    reply = connection.receive()
    return reply.number(tag) if reply is not None else None


def accepted_connections(connection) -> int:
    connection.send("List", [])
    reply = connection.receive()
    for value in reply.all("Limit"):
        if value[8:] == b"connections-accepted":
            return int.from_bytes(value[:8], "little")
    return -1


def socket_file(path: Path) -> bool:
    """Leaves a socket file with nothing listening, as a killed supervisor does."""
    import socket as sockets
    listener = sockets.socket(sockets.AF_UNIX, sockets.SOCK_STREAM)
    listener.bind(str(path))
    listener.close()
    return path.exists()


SUITES = {
    "cli-options": cli_options,
    "off-default": off_default,
    "worker": worker_suite,
    "protocol": protocol_suite,
    "run": run_suite,
    "parity": parity_suite,
    "control": control_suite,
    "login": login_suite,
    "isolation": isolation_suite,
}


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[1] not in SUITES:
        print(__doc__)
        return 2
    work = Work(Path(sys.argv[2]))
    try:
        SUITES[sys.argv[1]](work)
    finally:
        work.close()
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print(f"{sys.argv[1]} passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
