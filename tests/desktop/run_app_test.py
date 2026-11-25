"""Build the Neura fixture child and run the desktop application tests.

The Node bridge tests run first (tests/desktop/bridge_test.js); each Electron
case then runs under Xvfb or on the Windows test desktop and checks for
surviving subprocesses.
"""

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

from run_shell_test import (PIPED_TEXT, WINDOWS, captured_fixture, electron_command,
                            fixture_environment, fixture_process, isolated_environment, node_executable)

CASES = {"app-close": 0, "app-quit": 7, "app-trap": 1, "app-services": 0, "app-denied": 0, "app-windows": 0, "app-dialogs": 0, "app-trap-dialog": 1,
         "app-killed": None}
HERE = Path(__file__).resolve().parent
# A desktop child runs as long as the application, so its policy records no practical step limit: the default
# of `neura run` (100,000,000 steps, summed over the run) would end a long-lived application.
UNLIMITED_STEPS = "18446744073709551615"


def build_child(neura, scratch, source=HERE / "fixtures" / "app-child.nra", allow=("--allow-clock",)):
    child = Path(scratch) / Path(source).stem
    if WINDOWS:
        child = child.with_suffix(".exe")
    subprocess.run([neura, "build", "--native", "--deny-all", *allow, "--max-steps", UNLIMITED_STEPS, str(source),
                    "-o", str(child)], check=True, timeout=120)
    return child


def check_rejection(neura):
    """A bridge operation that captures the Context is rejected (E0602) at the program's own call of `run`."""
    source = HERE / "fixtures" / "capture-context.nra"
    result = subprocess.run([neura, "check", str(source)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120, **PIPED_TEXT)
    expected = ["error[E0602]: a value of type func(Call) -> Result<uint8[list]> is not transferable",
                f"--> {source}:15:16", "captures a value of type Context here"]
    missing = [text for text in expected if text not in result.stdout]
    if result.returncode == 0 or missing:
        print(result.stdout, end="")
        print(f"capture-context: NOT rejected as expected (missing {missing})")
        return 1
    print("capture-context: rejected with E0602 at the program's call of run")
    return 0


def run_bridge(child, node):
    env = {**os.environ, "NEURA_DESKTOP_TEST_CHILD": str(child)}
    result = subprocess.run([node_executable(node), "--test", "--test-timeout=60000", str(HERE / "bridge_test.js")],
                            env=env, timeout=600, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **PIPED_TEXT)
    print(result.stdout, end="")
    return result.returncode


def alive(pid):
    """Whether `pid` is a process that has not exited; reaps it when it is a zombie this runner adopted."""
    try:
        finished, _ = os.waitpid(pid, os.WNOHANG)
        if finished == pid:
            return False
    except ChildProcessError:
        pass
    try:
        state = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
    except (FileNotFoundError, ProcessLookupError):
        return False
    return state not in ("Z", "X")


def survivors(processes, running):
    """Give the child and renderer time to end after their connection to the host closes."""
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        left = [pid for pid, process in processes.items() if running(process)]
        if not left:
            return left
        time.sleep(0.05)
    return [pid for pid, process in processes.items() if running(process)]


def kill_host(main, child, renderer):
    if WINDOWS:
        from windows_desktop import PROCESS_TERMINATE, SYNCHRONIZE, kill_process, process_handle, process_running
        # Hold the original handles across the kill, even if Windows recycles a process ID.
        with (process_handle(main, SYNCHRONIZE | PROCESS_TERMINATE) as host,
              process_handle(child) as native, process_handle(renderer) as render):
            if not all(process_running(handle) for handle in (host, native, render)):
                raise RuntimeError("the host, child and renderer must be running before the kill")
            kill_process(host)
            return survivors({child: native, renderer: render}, process_running)
    os.kill(main, signal.SIGKILL)
    return survivors({child: child, renderer: renderer}, alive)


def read_log(path):
    return path.read_text(**PIPED_TEXT)


def run_killed(command, env, scratch):
    """Case 34: kill only the window host and check that its child and renderer end on their own."""
    log = scratch / "killed.out"
    with (log.open("wb") as output,
          fixture_process(command, stdout=output, stderr=subprocess.STDOUT, env=env) as (process, kill)):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and process.poll() is None:
            match = re.search(r"^PIDS main=(\d+) child=(\d+) renderer=(\d+)\n", read_log(log), re.MULTILINE)
            if match:
                main, child, renderer = map(int, match.groups())
                left = kill_host(main, child, renderer)
                print(f"app-killed: host {main} killed; child {child} and renderer {renderer} "
                      f"{'still running: ' + str(left) if left else 'ended by themselves'}")
                if left:
                    kill()
                    return 1
                process.wait(timeout=10)
                if WINDOWS and process.returncode != 137:
                    raise RuntimeError(f"the killed host returned {process.returncode}, expected 137")
                return 0
            time.sleep(0.05)
    print(read_log(log), end="", file=sys.stderr)
    print("app-killed: the case did not report its processes")
    return 1


def dismiss_error_dialog(text):
    if WINDOWS:
        from windows_desktop import close_window, error_dialog
        owner = re.search(r"^DIALOG_PID=(\d+)\n", text, re.MULTILINE)
        if owner and (found := error_dialog(int(owner[1]), "Neura desktop application ended")):
            print(f"app-trap-dialog: the native error window {found} belongs to host {owner[1]}")
            close_window(found)
            return found
        return None

    display = next((line[8:] for line in text.splitlines() if line.startswith("DISPLAY=")), None)
    authority = next((line[11:] for line in text.splitlines() if line.startswith("XAUTHORITY=")), None)
    if not display or authority is None:
        return None
    x = {**isolated_environment(), "DISPLAY": display, "XAUTHORITY": authority}
    # GTK titles the error dialog's window "Error"; its text is the host's title and message.
    search = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Error$"],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=x, **PIPED_TEXT)
    if not search.stdout.strip():
        return None
    found = search.stdout.split()[0]
    name = subprocess.run(["xdotool", "getwindowname", found], stdout=subprocess.PIPE, env=x, **PIPED_TEXT)
    print(f"app-trap-dialog: the window {found} named {name.stdout.strip()!r} is mapped")
    if os.environ.get("NEURA_DESKTOP_EVIDENCE"):
        subprocess.run(["import", "-window", found, os.environ["NEURA_DESKTOP_EVIDENCE"]], env=x, timeout=20)
    subprocess.run(["xdotool", "windowfocus", "--sync", found], env=x, timeout=10)
    subprocess.run(["xdotool", "key", "--window", found, "Return"], env=x, timeout=10)
    return found


def run_trap_dialog(command, env, scratch):
    """Case 32 with the default failure report: find the native error dialog, dismiss it, and expect status 1."""
    log = scratch / "dialog.out"
    found = None
    with (log.open("wb") as output,
          fixture_process(command, stdout=output, stderr=subprocess.STDOUT, env=env) as (process, _)):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and process.poll() is None:
            found = dismiss_error_dialog(read_log(log))
            if found is not None:
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    pass
                break
            time.sleep(0.1)
    text = read_log(log)
    verified = found is not None and "Verified desktop app-trap-dialog\n" in text and process.returncode == 1
    if not verified:
        print(text, end="", file=sys.stderr)
    print(f"app-trap-dialog: dialog {'found' if found else 'NOT found'}, exit {process.returncode}, "
          f"{'verified' if verified else 'NOT verified'}")
    return 0 if verified else 1


def run_case(engine, child, case):
    with fixture_environment("Neura app 資-", NEURA_DESKTOP_TEST_CASE=case,
                             NEURA_DESKTOP_TEST_CHILD=str(child)) as (scratch, env):
        command = electron_command(engine, HERE / "app_test.js")
        if case == "app-killed":
            return run_killed(command, env, scratch)
        if case == "app-trap-dialog":
            return run_trap_dialog(command, env, scratch)
        status, stdout, stderr = captured_fixture(command, env, 60, case)
    verified = f"Verified desktop {case}\n" in stdout
    if status != CASES[case] or not verified:
        print(stderr, end="", file=sys.stderr)
    print(stdout, end="")
    print(f"{case}: exit {status}, {'verified' if verified else 'NOT verified'}")
    return 0 if status == CASES[case] and verified else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("electron", type=lambda value: str(Path(value).resolve()), help="the pinned Electron 45.3.3 executable")
    parser.add_argument("--neura", required=True, type=lambda value: str(Path(value).resolve()), help="the neura tool")
    parser.add_argument("--node", type=Path, help="path to the Node executable")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("--skip-bridge", action="store_true", help="run only the Electron cases")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="Neura child 資-") as build:
        child = build_child(args.neura, build)
        failed = 0 if args.skip_bridge else run_bridge(child, args.node) | check_rejection(args.neura)
        for case in args.case or sorted(CASES):
            failed |= run_case(args.electron, child, case)
    sys.exit(1 if failed else 0)
