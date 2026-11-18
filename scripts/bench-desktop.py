"""Measure the desktop budgets B-D1 and B-D2 (docs/design/Apps030.md) on Linux under Xvfb.

B-D1 is the cold launch: from starting the window host to the first paint of the
application's first window, as the host reports it with --timing, over a number
of launches with a warm file cache (one launch before them is not counted).
B-D2 is memory: the resident size of the host's process tree 5 s after that
first paint (as PSS, which counts a page the processes share once, and as the
plain sum of RSS, which counts it in every process), and of the native child
alone (RSS). The launch time is reported twice: from starting xvfb-run (which
also starts an X server) to the first-paint line, and as the host reports it
(from the start of Node in Electron's main process); the budget is judged on
the first.

The application is the desktop fixture of tests/desktop, packaged as a
.tar.gz and extracted (its window shows a static page and its child is the
Neura fixture child), because the reference
application needs renderer-side Neura, which this branch does not have. The
numbers are therefore those of the window host, an empty renderer and the
child, not of a Neura UI.
"""

import argparse
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "desktop"))

from run_app_test import build_child  # noqa: E402
from run_package_test import make_icon, make_package, package as build_package  # noqa: E402
from run_shell_test import ELECTRON_X11, isolated_environment  # noqa: E402

BUDGET_LAUNCH_MEDIAN = 1500
BUDGET_LAUNCH_P95 = 2500
BUDGET_TREE_MIB = 350
BUDGET_CHILD_MIB = 40


def descendants(root):
    """`root` and every process below it."""
    parents = {}
    for entry in Path("/proc").iterdir():
        if entry.name.isdigit():
            try:
                fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
                parents.setdefault(int(fields[1]), []).append(int(entry.name))
            except (FileNotFoundError, ProcessLookupError, IndexError):
                pass
    found, pending = [], [root]
    while pending:
        pid = pending.pop()
        found.append(pid)
        pending.extend(parents.get(pid, []))
    return found


def resident_kib(pid, field="VmRSS:", source="status"):
    """A process's resident KiB: VmRSS, or with field "Pss:" and source "smaps_rollup" its proportional share."""
    try:
        for line in Path(f"/proc/{pid}/{source}").read_text().splitlines():
            if line.startswith(field):
                return int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
    return 0


def launch(executable, measure_memory, scratch):
    """One launch: the milliseconds to first paint and, if asked, the tree's and the child's resident KiB."""
    home = Path(tempfile.mkdtemp(prefix="home-", dir=scratch))
    env = isolated_environment(HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"),
                               XDG_DATA_HOME=str(home / ".local" / "share"))
    command = ["xvfb-run", "-a", executable, ELECTRON_X11, "--disable-background-networking", "--timing"]
    output = Path(scratch) / f"{home.name}.log"
    started = time.monotonic()
    with open(output, "w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True,
                                   env=env)
    try:
        line = None
        deadline = started + 60
        while line is None:
            line = next((text for text in output.read_text().splitlines() if text.startswith("neura-timing first-paint")),
                        None)
            if line is None and (process.poll() is not None or time.monotonic() > deadline):
                raise RuntimeError("no first paint within 60 s:\n" + output.read_text()[-4000:])
            if line is None:
                time.sleep(0.005)
        wall = (time.monotonic() - started) * 1000
        fields = dict(item.split("=") for item in line.split()[2:])
        result = {"host_ms": int(fields["ms"]), "wall_ms": round(wall)}
        if measure_memory:
            time.sleep(5)
            tree = descendants(int(fields["pid"]))
            result["tree_kib"] = sum(resident_kib(pid) for pid in tree)
            # Chromium's processes share most of their pages; PSS counts each shared page once over the tree.
            result["tree_pss_kib"] = sum(resident_kib(pid, "Pss:", "smaps_rollup") for pid in tree)
            result["child_kib"] = resident_kib(int(fields["child"]))
            result["processes"] = len(tree)
        return result
    finally:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, round(fraction * (len(ordered) - 1)))]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("electron", type=lambda value: str(Path(value).resolve()), help="the pinned Electron 45.3.3 executable")
    parser.add_argument("--neura", required=True, type=lambda value: str(Path(value).resolve()))
    parser.add_argument("--launches", type=int, default=10)
    parser.add_argument("--json", type=Path, help="also write the results here")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="neura-bench-desktop-") as scratch:
        # The installed .tar.gz package, as a user starts it.
        child = build_child(args.neura, scratch)
        directory = Path(scratch) / "package"
        make_package(directory, child)
        archive = Path(scratch) / "fixture.tar.gz"
        build_package(Path(args.electron).parent, directory, archive, make_icon(Path(scratch) / "icon.png"))
        with tarfile.open(archive) as opened:
            opened.extractall(Path(scratch) / "installed", filter="data")
        executable = str(Path(scratch) / "installed" / "fixture" / "fixture")
        launch(executable, False, scratch)
        runs = [launch(executable, index == 0, scratch) for index in range(args.launches)]
    host = [run["host_ms"] for run in runs]
    wall = [run["wall_ms"] for run in runs]
    memory = runs[0]
    report = {
        "workload": "tests/desktop fixture package (static page, Neura fixture child), not the reference application",
        "launches": args.launches,
        "B-D1": {"host_median_ms": statistics.median(host), "host_p95_ms": percentile(host, 0.95),
                 "wall_median_ms": statistics.median(wall), "wall_p95_ms": percentile(wall, 0.95),
                 "budget": f"{BUDGET_LAUNCH_MEDIAN} ms median, {BUDGET_LAUNCH_P95} ms 95th percentile"},
        "B-D2": {"tree_rss_sum_mib": round(memory["tree_kib"] / 1024, 1),
                 "tree_pss_mib": round(memory["tree_pss_kib"] / 1024, 1),
                 "child_mib": round(memory["child_kib"] / 1024, 1),
                 "processes": memory["processes"], "budget": f"{BUDGET_TREE_MIB} MiB tree, {BUDGET_CHILD_MIB} MiB child"},
    }
    # Judged on the wall time from starting xvfb-run, which includes starting the X server, and on PSS; the
    # host's own figure starts only when Node in Electron's main process starts, and summed RSS counts shared
    # pages once per process.
    report["B-D1"]["met"] = (report["B-D1"]["wall_median_ms"] <= BUDGET_LAUNCH_MEDIAN and
                             report["B-D1"]["wall_p95_ms"] <= BUDGET_LAUNCH_P95)
    report["B-D2"]["met"] = (report["B-D2"]["tree_pss_mib"] <= BUDGET_TREE_MIB and
                             report["B-D2"]["child_mib"] <= BUDGET_CHILD_MIB)
    text = json.dumps(report, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
