"""Run the Electron shell fixture on Linux under Xvfb."""

import argparse
import ctypes
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


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


def run(engine, case):
    # Chromium may orphan zygotes and crash handlers during shutdown. Adopt
    # them for this test so we can distinguish and reap exited children.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), "could not adopt fixture subprocesses")
    fixture = Path(__file__).with_name("shell_test.js").resolve()
    with tempfile.TemporaryDirectory(prefix="neura-desktop-") as scratch:
        (Path(scratch) / "profile").mkdir()
        command = ["xvfb-run", "-a", engine, "--disable-background-networking", str(fixture)]
        env = {**os.environ, "NEURA_DESKTOP_TEST_SCRATCH": scratch, "NEURA_DESKTOP_TEST_CASE": case}
        if case != "shell":
            node = shutil.which("node")
            if node is None:
                raise RuntimeError("the native child fixture needs Node")
            env["NEURA_DESKTOP_TEST_NODE"] = subprocess.check_output(
                [node, "-p", "process.execPath"], text=True, timeout=5
            ).strip()
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, start_new_session=True, env=env) as process:
            try:
                stdout, stderr = process.communicate(timeout=25)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate(timeout=5)
                print(stdout, end="")
                print(stderr, end="", file=sys.stderr)
                raise RuntimeError("the Electron shell fixture timed out")
            finally:
                stop_children()
        expected = 1 if case == "child-crash" else 0
        verified = case == "shell" or f"Verified desktop {case}\n" in stdout
        if process.returncode != expected or not verified:
            print(stderr, end="", file=sys.stderr)
        print(stdout, end="")
        return 0 if process.returncode == expected and verified else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("electron", type=lambda value: str(Path(value).resolve()), help="path to the pinned Electron 45.3.3 executable")
    parser.add_argument("--case", action="append", choices=("shell", "child-close", "child-quit", "child-crash", "child-mismatch"))
    args = parser.parse_args()
    sys.exit(any(run(args.electron, case) for case in args.case or ["shell"]))
