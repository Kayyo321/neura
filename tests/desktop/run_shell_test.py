"""Run the Electron shell fixture under Xvfb on Linux or on the Windows test desktop."""

import argparse
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


WINDOWS = os.name == "nt"
PIPED_TEXT = {"encoding": "utf-8", "errors": "replace"}
# The user's own display and session bus. xvfb-run replaces only DISPLAY, and Electron prefers Wayland when
# WAYLAND_DISPLAY is set, so a test that kept these would open its windows, dialogs and notifications on the
# user's desktop instead of the virtual X display.
SESSION_VARIABLES = ("WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS")
# Electron on X11 only: the virtual display that xvfb-run starts.
ELECTRON_X11 = "--ozone-platform=x11"


def isolated_environment(**extra):
    """Pass only the display and session bus created for this fixture on Linux."""
    environment = {key: value for key, value in os.environ.items() if key not in SESSION_VARIABLES}
    if WINDOWS:
        environment.update(extra)
        return environment
    environment["ELECTRON_OZONE_PLATFORM_HINT"] = "x11"
    environment["DBUS_SESSION_BUS_ADDRESS"] = "disabled:"
    environment.update(extra)
    if "WAYLAND_DISPLAY" in environment or "DISPLAY" in environment:
        raise RuntimeError("an Electron test would reach the user's display")
    return environment


def electron_command(engine, fixture):
    arguments = ["--disable-background-networking", str(fixture)]
    return [str(engine), *arguments] if WINDOWS else ["xvfb-run", "-a", str(engine), ELECTRON_X11, *arguments]


@contextmanager
def fixture_environment(prefix, **extra):
    with tempfile.TemporaryDirectory(prefix=prefix) as scratch:
        root = Path(scratch)
        (root / "profile").mkdir()
        if WINDOWS:
            for variable, directory in (("APPDATA", "Roaming"), ("LOCALAPPDATA", "Local app data 資")):
                path = root / directory
                path.mkdir()
                extra[variable] = str(path)
        else:
            extra["XDG_DATA_HOME"] = str(root / "data")
        yield root, isolated_environment(NEURA_DESKTOP_TEST_SCRATCH=scratch, **extra)


def use_subreaper():
    # Chromium may orphan zygotes and crash handlers; adopt them to check and reap them.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), "could not adopt fixture subprocesses")


def collect_children():
    while True:
        try:
            finished, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return []
        if finished == 0:
            own_children = Path(f"/proc/self/task/{os.getpid()}/children")
            return [int(pid) for pid in own_children.read_text().split()]


def stop_children():
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        if not collect_children():
            return
        time.sleep(0.02)
    while children := collect_children():
        for child in children:
            try:
                os.kill(child, signal.SIGKILL)
                os.waitpid(child, 0)
            except (ProcessLookupError, ChildProcessError):
                pass
    raise RuntimeError("the Electron fixture left running subprocesses")


@contextmanager
def fixture_process(command, **kwargs):
    if WINDOWS:
        from windows_desktop import fixture_job
        with fixture_job(command, **kwargs) as fixture:
            yield fixture
        return
    use_subreaper()
    with subprocess.Popen(command, start_new_session=True, **kwargs) as process:
        def kill():
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

        try:
            yield process, kill
        finally:
            if process.poll() is None:
                kill()
                process.wait(timeout=5)
            stop_children()


def captured_fixture(command, env, timeout, name):
    with fixture_process(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, **PIPED_TEXT) as (process, kill):
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            kill()
            stdout, stderr = process.communicate(timeout=5)
            print(stdout, end="")
            print(stderr, end="", file=sys.stderr)
            raise RuntimeError(f"the Electron fixture {name} timed out") from None
    return process.returncode, stdout, stderr


def node_executable(explicit):
    node = explicit or shutil.which("node")
    if node is None:
        raise RuntimeError("the native child fixture needs Node")
    return subprocess.check_output([node, "-p", "process.execPath"], timeout=5, **PIPED_TEXT).strip()


def run(engine, case, node):
    fixture = Path(__file__).with_name("shell_test.js").resolve()
    with fixture_environment("Neura shell 資-", NEURA_DESKTOP_TEST_CASE=case) as (_, env):
        if case != "shell":
            env["NEURA_DESKTOP_TEST_NODE"] = node_executable(node)
        status, stdout, stderr = captured_fixture(electron_command(engine, fixture), env, 25, case)
    expected = 1 if case == "child-crash" else 0
    marker = "Verified Electron bundle," if case == "shell" else f"Verified desktop {case}\n"
    verified = status == expected and marker in stdout
    if not verified:
        print(stderr, end="", file=sys.stderr)
    print(stdout, end="")
    return 0 if verified else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("electron", type=lambda value: str(Path(value).resolve()), help="path to the pinned Electron 45.3.3 executable")
    parser.add_argument("--node", type=Path, help="path to the Node executable")
    parser.add_argument("--case", action="append", choices=("shell", "child-close", "child-quit", "child-crash", "child-mismatch"))
    args = parser.parse_args()
    failed = 0
    for case in args.case or ["shell"]:
        failed |= run(args.electron, case, args.node)
    sys.exit(failed)
