"""Run the Electron shell fixture on Linux under Xvfb."""

import argparse
import ctypes
import os
from pathlib import Path
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


def run(engine):
    # Chromium may orphan zygotes and crash handlers during shutdown. Adopt
    # them for this test so we can distinguish and reap exited children.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), "could not adopt fixture subprocesses")
    fixture = Path(__file__).with_name("shell_test.js").resolve()
    with tempfile.TemporaryDirectory(prefix="neura-desktop-") as scratch:
        (Path(scratch) / "profile").mkdir()
        command = ["xvfb-run", "-a", engine, "--disable-background-networking", str(fixture)]
        env = {**os.environ, "NEURA_DESKTOP_TEST_SCRATCH": scratch}
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
        if process.returncode != 0:
            print(stderr, end="", file=sys.stderr)
        print(stdout, end="")
        return process.returncode


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("electron", type=lambda value: str(Path(value).resolve()), help="path to the pinned Electron 45.3.3 executable")
    sys.exit(run(parser.parse_args().electron))
