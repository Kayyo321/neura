"""Build the Neura fixture child and run the desktop application tests on Linux.

The Node bridge tests run first (tests/desktop/bridge_test.js); each Electron
case then runs in its own pinned Electron process under Xvfb, and every process
it started must be gone when it ends.
"""

import argparse
import ctypes
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

from run_shell_test import stop_children

CASES = {"app-close": 0, "app-quit": 7, "app-trap": 1}
HERE = Path(__file__).resolve().parent


def build_child(neura, scratch):
    child = Path(scratch) / "app-child"
    subprocess.run([neura, "build", "--native", "--deny-all", str(HERE / "fixtures" / "app-child.nra"), "-o", str(child)],
                   check=True, timeout=120)
    return child


def run_bridge(child):
    node = shutil.which("node")
    if node is None:
        raise RuntimeError("the bridge tests need Node")
    env = {**os.environ, "NEURA_DESKTOP_TEST_CHILD": str(child)}
    result = subprocess.run([node, "--test", "--test-timeout=30000", str(HERE / "bridge_test.js")], env=env, timeout=180,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end="")
    return result.returncode


def run_case(engine, child, case):
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), "could not adopt fixture subprocesses")
    with tempfile.TemporaryDirectory(prefix="neura-desktop-app-") as scratch:
        (Path(scratch) / "profile").mkdir()
        command = ["xvfb-run", "-a", engine, "--disable-background-networking", str(HERE / "app_test.js")]
        env = {**os.environ, "NEURA_DESKTOP_TEST_SCRATCH": scratch, "NEURA_DESKTOP_TEST_CASE": case,
               "NEURA_DESKTOP_TEST_CHILD": str(child), "XDG_DATA_HOME": str(Path(scratch) / "data")}
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                              start_new_session=True, env=env) as process:
            try:
                stdout, stderr = process.communicate(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate(timeout=5)
                print(stdout, end="")
                print(stderr, end="", file=sys.stderr)
                raise RuntimeError(f"the desktop application case {case} timed out")
            finally:
                stop_children()
        verified = f"Verified desktop {case}\n" in stdout
        if process.returncode != CASES[case] or not verified:
            print(stderr, end="", file=sys.stderr)
        print(stdout, end="")
        print(f"{case}: exit {process.returncode}, {'verified' if verified else 'NOT verified'}")
        return 0 if process.returncode == CASES[case] and verified else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("electron", type=lambda value: str(Path(value).resolve()), help="the pinned Electron 45.3.3 executable")
    parser.add_argument("--neura", required=True, type=lambda value: str(Path(value).resolve()), help="the neura tool")
    parser.add_argument("--case", action="append", choices=sorted(CASES))
    parser.add_argument("--skip-bridge", action="store_true", help="run only the Electron cases")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="neura-desktop-child-") as build:
        child = build_child(args.neura, build)
        failed = 0 if args.skip_bridge else run_bridge(child)
        for case in args.case or sorted(CASES):
            failed |= run_case(args.electron, child, case)
    sys.exit(1 if failed else 0)
