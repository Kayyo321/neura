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

import json
import os
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
    return [line for line in stderr.splitlines() if line.startswith(b"tier: ") and not line.startswith(b"tier: time:")]


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
    check(b"tier: jit auto: " in got and b"tier: jit off" not in got,
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
    deadline = time.monotonic() + 20
    while not pid_file.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    time.sleep(0.3)
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


SUITES = {
    "cli-options": cli_options,
    "off-default": off_default,
    "worker": worker_suite,
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
