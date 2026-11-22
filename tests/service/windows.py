"""The runtime service on Windows (docs/design/Windows030.md, W29), end to end:
the suites of service.py under the same names, against the Windows form of
the service. service.py runs them when sys.platform is win32.

Every suite runs with its own profile (USERPROFILE, LOCALAPPDATA, TEMP), so
its run and state directories, %LOCALAPPDATA%/Neura/run and .../state, are
its own, and every supervisor it started is shut down and ended when it
finishes. A case that cannot run on Windows prints NOT RUN with the reason.

NEURA_TEST_SERVICE_BUILDER, when set, names the neura.exe that builds the
test images instead of BIN_DIR's (images are byte-identical across builds of
one source, W13); a host whose application control blocks a newly linked
SDK library needs it.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import winreg
from ctypes import wintypes
from pathlib import Path

import protocol_client as pc
import service
from service import HELLO, PROGRAMS, check

CREATE_NEW_PROCESS_GROUP = subprocess.CREATE_NEW_PROCESS_GROUP
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"


def not_run(case: str, reason: str) -> None:
    print(f"NOT RUN {case}: {reason}")


class Work:
    """A directory with a profile of its own."""

    def __init__(self, bin_dir: Path):
        self.bin = bin_dir.resolve()
        self.neura = self.bin / "neura.exe"
        # The system's temporary directory keeps the socket path within the
        # 107 bytes of an AF_UNIX address.
        self.temporary = tempfile.TemporaryDirectory(prefix="nsv-")
        self.root = Path(self.temporary.name).resolve()
        self.home = self.root / "h"
        local = self.home / "AppData" / "Local"
        for directory in (local, self.home / "AppData" / "Roaming", self.home / "Temp"):
            directory.mkdir(parents=True)
        self.run_dir = local / "Neura" / "run"
        self.state = local / "Neura" / "state"
        self.env = dict(os.environ)
        self.env.update(USERPROFILE=str(self.home), LOCALAPPDATA=str(local), APPDATA=str(self.home / "AppData/Roaming"),
                        TEMP=str(self.home / "Temp"), TMP=str(self.home / "Temp"), NEURA_GPU="off")
        self.env.pop("NEURA_JIT", None)
        self.images: dict[str, Path] = {}
        builder = os.environ.get("NEURA_TEST_SERVICE_BUILDER")
        self.builder = Path(builder) if builder else self.neura

    def image(self, name: str, source: str | None = None, path: Path | None = None) -> Path:
        if name in self.images:
            return self.images[name]
        if path is None:
            path = self.root / f"{name}.nra"
            path.write_text(source or "", encoding="utf-8", newline="\n")
        image = self.root / f"{name}.nvr"
        result = subprocess.run([str(self.builder), "build", str(path), "-o", str(image)], capture_output=True,
                                env=self.env, cwd=self.root, timeout=300)
        if result.returncode != 0:
            raise SystemExit(f"{name}: build failed ({result.returncode})\n{result.stderr.decode(errors='replace')}")
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
        try:
            subprocess.run([str(self.neura), "runtime", "shutdown", "--terminate", "--timeout=5"], env=self.env,
                           capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            pass
        for pid in supervisors(self):
            end_process(pid)
        for _ in range(50):
            try:
                self.temporary.cleanup()
                return
            except OSError:
                time.sleep(0.1)


def end_process(pid: int) -> None:
    try:
        os.kill(pid, signal.SIGTERM)  # TerminateProcess
    except OSError:
        pass


def process_alive(pid: int) -> bool:
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    handle = kernel32.OpenProcess(0x00100000 | 0x1000, False, pid)  # SYNCHRONIZE | QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        return kernel32.WaitForSingleObject(handle, 0) == 0x102  # WAIT_TIMEOUT
    finally:
        kernel32.CloseHandle(handle)


def supervisors(work: Work) -> list[int]:
    """The supervisors of this suite's profile: each endpoint under its run
    directory whose supervisor answers, by the pid of its Welcome."""
    found = []
    if not work.run_dir.exists():
        return found
    for endpoint in work.run_dir.iterdir():
        try:
            connection = pc.Connection(endpoint / "s", timeout=2)
        except OSError:
            continue
        try:
            connection.send("Hello", [("ProtocolLow", pc.u16(1)), ("ProtocolHigh", pc.u16(1)), ("Build", b"probe"),
                                      ("Installation", b"probe"), ("Generation", b"dev"), ("Purpose", pc.u8(2))])
            reply = connection.receive()
            if reply is not None and reply.kind == "Welcome":
                found.append(reply.number("Pid"))
            elif reply is not None:
                found.extend(pid for pid in [pc.peer_pid(connection.socket)] if pid)
        except OSError:
            pass
        finally:
            connection.close()
    return found


def console_processes() -> int:
    """How many processes share this process's console (0 without one)."""
    processes = (wintypes.DWORD * 4)()
    return ctypes.windll.kernel32.GetConsoleProcessList(processes, 4)


def ensure_console() -> bool:
    """A console for this process, so that the children it starts share one
    and a console event can be sent to them: a hidden new one when the test
    runs without (as a CI runner may start it)."""
    kernel32 = ctypes.windll.kernel32
    if console_processes() > 0:
        return True
    if not kernel32.AllocConsole():
        return False
    window = kernel32.GetConsoleWindow()
    if window:
        ctypes.windll.user32.ShowWindow(window, 0)  # SW_HIDE
    return console_processes() > 0


def wait_until(condition, timeout: float = 10) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return condition()


def acl_entries(path: Path) -> list[str]:
    """The entries of a file's DACL as icacls prints them."""
    text = subprocess.run(["icacls", str(path)], capture_output=True).stdout.decode(errors="replace")
    lines = [line.strip() for line in text.splitlines() if line.strip()][:-1]
    if lines:
        lines[0] = lines[0][len(str(path)):].strip()
    return lines


def only_user(entries: list[str]) -> bool:
    """One entry, full control for this user, inherited by what is below."""
    return len(entries) == 1 and entries[0].endswith(os.environ.get("USERNAME", "?") + ":(OI)(CI)(F)")


def report_of(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def program(work: Work, name: str) -> str:
    return str(work.image(name, path=PROGRAMS / f"{name}.nra"))


# --------------------------------------------------------------------------
# The suites.

def cli_options(work: Work) -> None:
    # The options are read as on every host.
    service.cli_options(work)


def off_default(work: Work) -> None:
    """Cases 1 and 2: the defaults never look for the service."""
    hello = str(work.image("hello", path=HELLO))
    status, out, err = work.run(["run", hello])
    check((status, out, err) == (0, b"Hello, World!\n", b""), "case 1: plain neura run")
    result = subprocess.run([str(work.bin / "neura-run.exe"), hello], capture_output=True, env=work.env, cwd=work.root)
    check(result.returncode == 0 and result.stdout == b"Hello, World!\n", "case 1: plain neura-run")
    tests = work.root / "suite.nra"
    tests.write_text("let test = import:Std.Test;\n\nlet Suite = module (\n"
                     "    export let test_adds = func() -> void {\n        test.assert(1 + 1 == 2);\n    };\n);\n",
                     newline="\n")
    status, _, _ = work.run(["test", str(tests)])
    check(status == 0, "case 2: plain neura test")
    check(work.files_under(work.run_dir) == [] and work.files_under(work.state) == [],
          "cases 1, 2: nothing appears in the run or state directory")
    # A copy of the tools without neurad.
    copy = work.root / "copy"
    copy.mkdir()
    for entry in work.bin.iterdir():
        if entry.is_file() and (entry.suffix in (".exe", ".dll")) and entry.stem != "neurad" and (
                entry.stem in ("neura", "neurac") or entry.stem.startswith("neura-")):
            shutil.copy2(entry, copy / entry.name)
    if (work.bin / "stdlib").exists():
        shutil.copytree(work.bin / "stdlib", copy / "stdlib")
    check(not (copy / "neurad.exe").exists(), "case 2: the copy has no neurad")
    result = subprocess.run([str(copy / "neura.exe"), "run", hello], capture_output=True, env=work.env, cwd=work.root)
    if result.returncode == 0:
        check(result.stdout == b"Hello, World!\n" and result.stderr == b"", "case 2: neura run without neurad")
        result = subprocess.run([str(copy / "neura.exe"), "run", "--runtime=auto",
                                 f"--runtime-report={work.root / 'fallback.json'}", hello], capture_output=True,
                                env=work.env, cwd=work.root)
        report = report_of(work.root / "fallback.json")
        check(result.returncode == 0 and result.stdout == b"Hello, World!\n" and
              report.get("path") == "direct-fallback" and report.get("reason") == "not-installed",
              f"case 3: --runtime=auto without neurad runs directly ({report})")
        result = subprocess.run([str(copy / "neura.exe"), "run", "--runtime=required", hello], capture_output=True,
                                env=work.env, cwd=work.root)
        check(result.returncode == 5 and b"the service component is not installed" in result.stderr,
              "case 4: --runtime=required without neurad is status 5")
    else:
        not_run("case 2: the copy without neurad", f"the copied tools did not start ({result.returncode}): a host "
                "policy may block newly copied executables")
    check(work.files_under(work.run_dir) == [], "case 2: still nothing in the run directory")


def driver(work: Work, arguments: list[str], options: list[str] = (), stdin: bytes = b"",
           cwd: Path | None = None, env: dict | None = None) -> tuple[subprocess.CompletedProcess, dict]:
    report = work.root / "driver-report.json"
    report.unlink(missing_ok=True)
    result = subprocess.run([sys.argv[3], str(work.bin / "neurad.exe"), "--report", str(report), *options, "--",
                             *arguments], input=stdin, capture_output=True, env=env or work.env, cwd=cwd or work.root,
                            timeout=120)
    return result, report_of(report)


def worker_suite(work: Work) -> None:
    """The worker given one launch by a stand-in supervisor (Service030.md,
    "The worker"), with the Windows forms of its settings."""
    hello = str(work.image("hello", path=HELLO))
    result, ending = driver(work, [hello])
    check(result.stdout == b"Hello, World!\n" and ending.get("terminal") == "exited" and ending.get("status") == 0,
          f"a worker runs hello to its end ({ending}, {result.stderr!r})")
    check(ending.get("times") == ["worker-started", "loaded", "run-started", "exit"], "the worker reports its times")
    exit_status = program(work, "exit-status")
    result, ending = driver(work, [exit_status, "--", "7", "x"])
    check(result.returncode == 7 and ending.get("status") == 7 and result.stdout == b"argument: 7\nargument: x\n",
          "the exit status and the arguments of the launch")
    trap = program(work, "startup-trap")
    direct = subprocess.run([str(work.neura), "run", trap], capture_output=True, env=work.env, cwd=work.root)
    result, ending = driver(work, [trap])
    check(ending.get("terminal") == "trapped" and result.returncode == direct.returncode and
          result.stderr.replace(b"\r\n", b"\n") == direct.stderr, f"a trap as in a direct run ({ending})")
    echo = program(work, "echo")
    result, ending = driver(work, [echo], stdin=b"one\ntwo\n")
    check(result.stdout == b"[one]\n[two]\nend\n", f"the caller's input is the program's ({result.stdout!r})")
    environment = program(work, "environment")
    result, _ = driver(work, ["--allow-env", environment, "--", "PROBE_VARIABLE", "NEURA_TIER"],
                       options=["--supervisor-env", "PROBE_VARIABLE=supervisor"],
                       env=dict(work.env, PROBE_VARIABLE="caller"))
    check(result.stdout == b"PROBE_VARIABLE=caller\nNEURA_TIER unset\n",
          f"the caller's environment replaces the supervisor's ({result.stdout!r})")
    caller = work.root / "caller dir é"
    caller.mkdir()
    (caller / "note.txt").write_bytes(b"from the caller's directory\n")
    reader = program(work, "read-file")
    result, _ = driver(work, ["--allow-fs=.", reader, "--", "note.txt"], cwd=caller)
    check(result.stdout == b"from the caller's directory\n", f"the caller's working directory ({result.stdout!r})")
    gone = work.root / "gone"
    gone.mkdir()
    result, ending = driver(work, [hello], options=["--remove-cwd"], cwd=gone)
    # Before its streams are the caller's, the worker's refusal goes to NUL,
    # as it goes to /dev/null on POSIX.
    check(result.returncode == 1 and ending.get("terminal") == "worker-lost" and ending.get("times") == [] and
          not gone.exists(), f"a working directory that is gone is refused before the run ({ending})")
    result, ending = driver(work, [hello], options=["--second-assignment"])
    check(ending.get("second") == "refused", f"a worker takes one launch ({ending.get('second')})")
    pid_file = work.root / "worker.pid"
    result, _ = driver(work, [echo], options=["--exit-after-started", "--pid-file", str(pid_file)])
    pid = int(pid_file.read_text()) if pid_file.exists() else 0
    check(result.returncode == 99 and pid and wait_until(lambda: not process_alive(pid)),
          "a worker ends with its supervisor (its kill-on-close job)")
    for case in ("the umask", "the resource limits", "the nice value", "the ignored and blocked signals"):
        not_run(f"the worker takes {case}", "Windows has none to carry (W29); the priority class stands in for nice")


def launch(service_, connection, image: bytes, path: str, stdout: int, arguments=None, env=None):
    """Prepare, send the image when asked, commit with these streams, and
    wait for the outcome."""
    connection.send("Prepare", pc.envelope(image, path=path, arguments=arguments, environment=env or service_.env))
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
    # The application's input is a pipe at its end; the working directory
    # travels as a handle to the directory.
    read_end, write_end = os.pipe()
    os.close(write_end)
    directory = open_directory(Path.cwd())
    try:
        connection.send("Commit", [("Token", token), ("Descriptors", pc.u8(4))], [directory, read_end, stdout, 2])
    finally:
        os.close(directory)
        os.close(read_end)
    committed = connection.receive()
    if committed is None or committed.kind != "Committed":
        return committed, None
    return committed, connection.receive()


def open_directory(path: Path) -> int:
    """A C runtime descriptor that holds a handle to the directory `path`."""
    import msvcrt
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                     wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    handle = kernel32.CreateFileW(str(path), 0x80, 7, None, 3, 0x02000000, None)  # FILE_READ_ATTRIBUTES, BACKUP_SEMANTICS
    if handle in (None, wintypes.HANDLE(-1).value):
        raise OSError(ctypes.get_last_error(), f"cannot open {path}")
    return msvcrt.open_osfhandle(handle, os.O_RDONLY)


def protocol_suite(work: Work) -> None:
    """The protocol against a supervisor, read by a second client."""
    supervisor = pc.Service(work.bin, work.env)
    hello_path = str(work.image("hello", path=HELLO))
    hello_image = Path(hello_path).read_bytes()
    supervisor.start()
    connection = supervisor.hello()
    welcome = connection.welcome
    check(welcome is not None and welcome.kind == "Welcome" and welcome.number("Protocol") == 1 and
          welcome.text("Installation") == supervisor.installation and welcome.text("Generation") == "dev",
          "Welcome names protocol 1, this installation and generation")
    check(welcome.number("Pid") == supervisor.process.pid, "Welcome names the supervisor's pid")
    other = supervisor.hello(installation="0000000000000000")
    check(other.welcome is not None and other.welcome.kind == "Rejected" and other.welcome.text("Reason") == "identity",
          "case: another installation is refused (identity)")
    newer = supervisor.hello(low=2, high=3)
    check(newer.welcome is not None and newer.welcome.text("Reason") == "unsupported-protocol",
          "case: no common protocol major is refused")
    raw = pc.Connection(supervisor.socket)
    raw.send_raw(pc.frame(1, [(9999, b"x")], 1))
    reply = raw.receive()
    check(reply is not None and reply.kind == "Rejected" and reply.text("Reason") == "protocol" and raw.receive() is None,
          "a malformed message is answered with protocol and the connection closes")
    out = work.root / "out.txt"
    with open(out, "wb") as stream:
        committed, outcome = launch(supervisor, connection, hello_image, hello_path, stream.fileno())
    check(committed is not None and committed.kind == "Committed" and outcome is not None and
          outcome.kind == "Outcome" and pc.TERMINALS[outcome.number("Terminal")] == "exited",
          f"a launch with handles passed in-band ends exited ({committed}, {outcome})")
    check(out.read_bytes() == b"Hello, World!\n", "the application wrote to the handle the client passed")
    connection.send("Commit", [("Token", os.urandom(32)), ("Descriptors", pc.u8(4))],
                    [os.open(os.devnull, os.O_RDONLY) for _ in range(4)])
    reply = connection.receive()
    check(reply is not None and reply.kind == "Rejected" and reply.text("Reason") == "token-unknown",
          "a token this supervisor did not issue is token-unknown")
    connection.send("Prepare", pc.envelope(hello_image, path=hello_path, environment=supervisor.env))
    prepared = connection.receive()
    connection.send("Commit", [("Token", prepared.get("Token")), ("Descriptors", pc.u8(2))],
                    [os.open(os.devnull, os.O_RDONLY) for _ in range(2)])
    reply = connection.receive()
    check(reply is not None and reply.kind == "Rejected" and reply.text("Reason") == "protocol",
          "a Commit with the wrong number of handles is malformed")
    fresh = supervisor.hello()
    image_file = pc.memory_file(hello_image)
    # The launch names other bytes than the file holds.
    fresh.send("Prepare", pc.envelope(hello_image[:-1] + b"\0", path=hello_path, environment=supervisor.env))
    prepared = fresh.receive()
    fresh.send("Image", [("Token", prepared.get("Token")), ("Descriptors", pc.u8(1))], [image_file])
    os.close(image_file)
    reply = fresh.receive()
    check(reply is not None and reply.kind == "Rejected" and reply.text("Reason") == "image-mismatch",
          "bytes that do not match the digest are image-mismatch")
    for item in (connection, other, newer, raw, fresh):
        item.close()
    supervisor.process.terminate()
    supervisor.process.wait(timeout=10)
    not_run("descriptors beside a message (SCM_RIGHTS)", "Windows passes handles in-band (W29)")


def run_suite(work: Work) -> None:
    """`neura run --runtime=...` on demand, warm and fresh, detached, and the
    client's end."""
    hello = str(work.image("hello", path=HELLO))
    report = work.root / "report.json"
    status, out, err = work.run(["run", "--runtime=auto", f"--runtime-report={report}", hello])
    first = report_of(report)
    check((status, out, err) == (0, b"Hello, World!\n", b"") and first.get("path", "").startswith("service-") and
          first.get("state") == "exited", f"case 7: --runtime=auto starts neurad on demand ({first}, {err!r})")
    pids = supervisors(work)
    check(len(pids) == 1, f"one supervisor runs after the client ended ({pids})")
    status, out, _ = work.run(["run", "--runtime=required", f"--runtime-report={report}", hello])
    second = report_of(report)
    check(status == 0 and out == b"Hello, World!\n" and second.get("path") == "service-warm",
          f"the second launch uses a ready worker ({second})")
    exit_status = program(work, "exit-status")
    status, out, _ = work.run(["run", "--runtime=required", exit_status, "--", "3", "é"])
    check(status == 3 and out == "argument: 3\nargument: é\n".encode(), "the exit status and UTF-8 arguments")
    echo = program(work, "echo")
    status, out, _ = work.run(["run", "--runtime=required", echo], stdin=b"a\nb\n")
    check(status == 0 and out == b"[a]\n[b]\nend\n", "the client's input is the application's")
    trap = program(work, "startup-trap")
    direct = work.run(["run", trap])
    served = work.run(["run", "--runtime=required", trap])
    check(served == direct, "a trap gives the status and messages of a direct run")
    # Detached, with its streams in files that are appended to.
    out_file, err_file = work.root / "detached.out", work.root / "detached.err"
    out_file.write_bytes(b"before\n")
    status, out, err = work.run(["run", "--runtime=required", "--detach", f"--stdout={out_file}",
                                 f"--stderr={err_file}", hello])
    application = out.decode().strip()
    check(status == 0 and application.startswith("dev."), f"case: --detach prints the application ID ({out!r}, {err!r})")
    check(wait_until(lambda: out_file.read_bytes() == b"before\nHello, World!\n"), "the detached output is appended")
    # Ctrl+Break reaches the client, which forwards Term: the application's
    # job ends with 143, recorded as signal 15.
    if not ensure_console():
        not_run("Ctrl+Break is forwarded as Term", "this process has no console and cannot make one")
    client = subprocess.Popen([str(work.neura), "run", "--runtime=required", echo], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root,
                              creationflags=CREATE_NEW_PROCESS_GROUP)
    client.stdin.write(b"ready\n")
    client.stdin.flush()
    line = client.stdout.readline()
    try:
        os.kill(client.pid, signal.CTRL_BREAK_EVENT)
        forwarded = True
    except OSError as error:
        forwarded = False
        not_run("Ctrl+Break is forwarded as Term", f"no console to send the event in ({error})")
    if forwarded:
        # The input stays open: only the forwarded signal can end the run.
        try:
            client.wait(timeout=30)
        except subprocess.TimeoutExpired:
            client.kill()
        _, err = client.communicate()
        check(line == b"[ready]\n" and client.returncode == 143 and
              b"the application was ended by signal 15" in err,
              f"Ctrl+Break is forwarded as Term ({client.returncode}, {err!r}, {console_processes()} on the console)")
    else:
        client.kill()
        client.communicate()
    # The supervisor outlives the client that started it, in no job of the
    # client's: it ended with its client's console only if it had one.
    check(all(process_alive(pid) for pid in pids), "the supervisor outlives the clients")
    # Without its supervisor's endpoint directory being private, nothing runs.
    for directory in (work.run_dir, next(work.run_dir.iterdir())):
        entries = acl_entries(directory)
        check(only_user(entries), f"{directory.name} grants only this user ({entries})")
    # Last, as it leaves this process's console: an application's output
    # reaches the caller's console, which the worker attaches to.
    for label, options in (("fresh or warm", ["--runtime=required"]), ("direct", [])):
        shown = console_output(work, [str(work.neura), "run", *options, hello])
        if shown is None:
            not_run(f"the output reaches a console ({label})", "no console could be made and read here")
        else:
            check("Hello, World!" in shown and "status 0" in shown, f"the output reaches a console ({label}: {shown!r})")


def console_output(work: Work, command: list[str]) -> str | None:
    """What `command` shows in a console of its own, read from the console's
    screen buffer; nothing when no console can be made and read."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = wintypes.HANDLE
    holder = subprocess.Popen(["cmd.exe", "/d", "/c", subprocess.list2cmdline(command) +
                               " & echo status %errorlevel% & ping -n 6 127.0.0.1 >nul"], env=work.env, cwd=work.root,
                              creationflags=subprocess.CREATE_NEW_CONSOLE)
    try:
        time.sleep(3)
        kernel32.FreeConsole()
        if not kernel32.AttachConsole(holder.pid):
            return None
        try:
            output = kernel32.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None)
            text = ctypes.create_unicode_buffer(80 * 25)
            count = wintypes.DWORD()
            if not kernel32.ReadConsoleOutputCharacterW(output, text, 80 * 25, 0, ctypes.byref(count)):
                return None
            kernel32.CloseHandle(output)
            return " ".join(text.value[:count.value].split())
        finally:
            kernel32.FreeConsole()
    finally:
        holder.wait(timeout=30)


def parity_suite(work: Work) -> None:
    """The same image gives the same output and status directly and through
    the service."""
    cases = [
        ([str(work.image("hello", path=HELLO))], b""),
        ([program(work, "exit-status"), "--", "4", "two words"], b""),
        ([program(work, "echo")], b"x\ny\n"),
        ([program(work, "binary"), "--", "2"], b""),
        (["--allow-env", program(work, "environment"), "--", "USERPROFILE", "NO_SUCH_VARIABLE"], b""),
        ([program(work, "startup-trap")], b""),
        ([program(work, "long-startup")], b""),
    ]
    for arguments, stdin in cases:
        direct = work.run(["run", *arguments], stdin=stdin)
        served = work.run(["run", "--runtime=required", *arguments], stdin=stdin)
        check(served == direct, f"parity of {Path(arguments[0] if arguments[0] != '--allow-env' else arguments[1]).name}"
              f" ({direct[0]} vs {served[0]})")


def control_suite(work: Work) -> None:
    """The `neura runtime` commands."""
    status, out, _ = work.run(["runtime", "status"])
    check(status == 0 and b"service component: installed" in out and b"not running" in out,
          f"status before a start ({out!r})")
    status, out, err = work.run(["runtime", "start"])
    check(status == 0 and b"is running" in out, f"runtime start ({out!r}, {err!r})")
    status, out, _ = work.run(["runtime", "status", "--json"])
    state = json.loads(out) if status == 0 else {}
    check(state.get("installed") is True and state.get("supervisors", [{}])[0].get("running") is True,
          f"status --json shows the running supervisor ({state})")
    echo = program(work, "echo")
    out_file, err_file = work.root / "held.out", work.root / "held.err"
    held_input = work.root / "held.in"
    held_input.write_bytes(b"")
    pipe_read, pipe_write = os.pipe()
    client = subprocess.Popen([str(work.neura), "run", "--runtime=required", echo], stdin=pipe_read,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    os.close(pipe_read)
    wait_until(lambda: b"running" in work.run(["runtime", "ps"])[1], 20)
    status, out, _ = work.run(["runtime", "ps", "--json"])
    listed = json.loads(out).get("applications", []) if status == 0 else []
    check(len(listed) == 1 and listed[0].get("state") == "running", f"ps lists the running application ({listed})")
    application = listed[0]["id"] if listed else "dev.00000000.1"
    status, out, _ = work.run(["runtime", "inspect", application])
    check(status == 0 and f"id: {application}".encode() in out, f"inspect shows the application ({out!r})")
    status, out, _ = work.run(["runtime", "stop", application, "--grace=1"])
    check(status == 0 and out.strip().endswith(b"stopped"), f"stop ends it ({out!r})")
    os.close(pipe_write)
    _, err = client.communicate(timeout=30)
    check(client.returncode == 143 and b"the application was ended by signal 15" in err,
          f"the stopped application's client reports signal 15 ({client.returncode}, {err!r})")
    status, out, _ = work.run(["runtime", "ps", "--all", "--json"])
    records = json.loads(out).get("applications", []) if status == 0 else []
    stopped = [r for r in records if r.get("id") == application]
    check(stopped and stopped[0].get("state") == "stopped" and stopped[0].get("signal") == 15 and
          stopped[0].get("status") == 143, f"its record says stopped, signal 15, status 143 ({stopped})")
    status, out, _ = work.run(["runtime", "cache", "clear"])
    check(status == 0 and b"cache bytes in use" in out, f"cache clear ({out!r})")
    pids = supervisors(work)
    status, out, _ = work.run(["runtime", "shutdown"])
    check(status == 0 and b"the service has shut down" in out, f"shutdown ({out!r})")
    check(all(wait_until(lambda: not process_alive(pid)) for pid in pids), "the supervisor exited")
    status, out, _ = work.run(["runtime", "inspect", application])
    check(status == 0 and b"state: stopped" in out, f"inspect reads a gone supervisor's records ({out!r})")


def login_suite(work: Work) -> None:
    """`neura runtime enable` and `disable`: the user's Run key."""
    installation = hashlib.sha256(work.bin.as_posix().encode()).hexdigest()[:16]
    name = f"neurad-{installation}"
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            previous = winreg.QueryValueEx(key, name)
    except OSError:
        previous = None
    try:
        status, out, _ = work.run(["runtime", "disable"])
        if previous is None:
            check(status == 0 and out == b"login start is not registered\n", "disable when nothing is registered")
        status, out, err = work.run(["runtime", "enable"])
        location = f"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\{name}"
        check(status == 0 and out.decode().strip() == f"login start registered: {location}", f"enable ({out!r}, {err!r})")
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            value, kind = winreg.QueryValueEx(key, name)
        neurad = str(work.bin / "neurad.exe")
        check(kind == winreg.REG_SZ and value == subprocess.list2cmdline([neurad, "--foreground", "--root",
                                                                          str(work.bin)]),
              f"the value starts this build's neurad as a supervisor ({value!r})")
        status, out, _ = work.run(["runtime", "status", "--json"])
        check(status == 0 and json.loads(out).get("loginStart") is True, "status shows login start")
        status, out, _ = work.run(["runtime", "enable", "extra"])
        check(status == 1, "enable takes no argument")
        status, out, _ = work.run(["runtime", "disable"])
        check(status == 0 and out.decode().strip() == f"login start removed: {location}", f"disable ({out!r})")
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
                winreg.QueryValueEx(key, name)
            gone = False
        except OSError:
            gone = True
        check(gone, "disable removes the value")
        status, out, _ = work.run(["runtime", "status", "--json"])
        check(status == 0 and json.loads(out).get("loginStart") is False, "status shows no login start")
    finally:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            if previous is None:
                try:
                    winreg.DeleteValue(key, name)
                except OSError:
                    pass
            else:
                winreg.SetValueEx(key, name, 0, previous[1], previous[0])
    not_run("a logon starts the supervisor", "it needs a new logon session")


def isolation_suite(work: Work) -> None:
    """Applications do not share state, and the service is this user's."""
    state = program(work, "state")
    marker_a, marker_b = work.root / "a.txt", work.root / "b.txt"
    first = subprocess.Popen([str(work.neura), "run", "--runtime=required", "--allow-fs", state, "--", "one",
                              str(marker_a)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=work.env, cwd=work.root)
    check(wait_until(marker_a.exists, 30), "the first application runs")
    second = subprocess.Popen([str(work.neura), "run", "--runtime=required", "--allow-fs", state, "--", "two",
                               str(marker_b)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=work.env,
                              cwd=work.root)
    check(wait_until(marker_b.exists, 30), "the second application runs beside it")
    out_a, _ = first.communicate(b"\n", timeout=30)
    out_b, _ = second.communicate(b"\n", timeout=30)
    direct = work.run(["run", "--allow-fs", state, "--", "three", str(work.root / "c.txt")], stdin=b"\n")
    check(out_a.replace(b"one", b"three") == out_b.replace(b"two", b"three") == direct[1],
          f"each application starts from its own state ({out_a!r}, {out_b!r}, {direct[1]!r})")
    probe = pc.Service(work.bin, work.env)
    connection = probe.hello()
    check(connection.welcome is not None and connection.welcome.kind == "Welcome", "a process of this user connects")
    connection.close()
    for directory in (probe.directory.parent, probe.directory, probe.state.parent.parent):
        entries = acl_entries(directory)
        check(only_user(entries), f"{directory.name} grants only this user ({entries})")
    not_run("a process of another user cannot connect", "it needs a second account; the directory's DACL and the "
            "supervisor's token check refuse it")
    not_run("a process of a lower integrity level is refused", "it needs a low-integrity token")


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
    if service.failures:
        print(f"{len(service.failures)} check(s) failed")
        return 1
    print(f"{sys.argv[1]} passed")
    return 0
