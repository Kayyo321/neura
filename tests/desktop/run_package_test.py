"""Build the Linux .tar.gz package of the fixture application twice, compare it, install it and launch it.

The installed package runs under Xvfb with no SDK, no neura tool and no Node on
its path. The page calls the bridge at load, and the native child renames the
window, so the X window title shows that the bundle, the bridge and the child
work from the package. The test then checks that no process of the application
listens on a socket, ends it with SIGTERM, and checks that it exits 0 and that
nothing it started survives.
"""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time

from run_app_test import build_child
from run_shell_test import ELECTRON_X11, isolated_environment, stop_children

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
APPLICATION_ID = "example.neura.fixture"
OPERATIONS = ["fixture.clipboard_read", "fixture.clipboard_write", "fixture.close", "fixture.data_dir", "fixture.echo", "fixture.fail", "fixture.focus", "fixture.menu", "fixture.menu_bad", "fixture.notify", "fixture.open", "fixture.open_many", "fixture.open_read", "fixture.quit", "fixture.save", "fixture.spin", "fixture.title", "fixture.trap", "fixture.unserved", "fixture.wait", "fixture.work"]
TITLE = "Packaged ✓"
CSP = "default-src 'none'; script-src 'self'; style-src 'self'; base-uri 'none'; form-action 'none'"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def make_package(directory, child):
    web = directory / "web"
    (directory / "bin").mkdir(parents=True)
    web.mkdir()
    files = []

    def add(relative, text, media_type):
        data = text.encode()
        (web / relative).write_bytes(data)
        files.append({"path": relative, "sha256": digest(data), "bytes": len(data), "media_type": media_type, "initial": True})
        return {"path": relative, "digest": digest(data)}

    image = add("app.fixture.nvr", "synthetic image", "application/octet-stream")
    add("page.fixture.js", "neuraDesktop.call('fixture.title', new TextEncoder().encode(%s));" % json.dumps(TITLE),
        "text/javascript")
    add("index.html", '<!doctype html><title>Page</title><script src="page.fixture.js"></script>', "text/html")
    manifest = json.dumps({"schema": 1, "kind": "web", "id": APPLICATION_ID, "name": "fixture", "version": "0.1.0",
                           "language": "0.3", "files": files, "image": image, "csp": CSP,
                           "requires": {"browserAdapter": 1}, "origins": {}, "limits": {}, "capabilities": ["bridge"],
                           "contracts": []}, separators=(",", ":")).encode()
    (web / "bundle.json").write_bytes(manifest)
    registry = json.dumps({"operations": OPERATIONS, "schema": 1}, separators=(",", ":")).encode()
    (directory / "registry.json").write_bytes(registry)
    shutil.copyfile(child, directory / "bin" / "app-child")
    (directory / "bin" / "app-child").chmod(0o755)
    (directory / "desktop.json").write_text(json.dumps({
        "schema": 1, "kind": "desktop", "id": APPLICATION_ID, "version": "0.1.0",
        "web": {"directory": "web", "manifestDigest": digest(manifest)},
        "registry": {"path": "registry.json", "sha256": digest(registry)},
        "child": {"executable": "bin/app-child", "args": []},
        "supportedRequirements": {"browserAdapter": 1}, "externalOrigins": [], "capabilities": [], "limits": {},
    }, sort_keys=True))


def package(engine, directory, output):
    result = subprocess.run([sys.executable, str(ROOT / "scripts" / "package-desktop.py"), "--engine", str(engine),
                             "--package", str(directory), "--name", "fixture", "-o", str(output)],
                            check=True, stdout=subprocess.PIPE, text=True, timeout=300)
    print(result.stdout, end="")
    return output.read_bytes()


def descendants(pid):
    found = []
    for task in Path(f"/proc/{pid}/task").glob("*"):
        try:
            children = (task / "children").read_text().split()
        except OSError:
            continue
        for child in map(int, children):
            found.append(child)
            found.extend(descendants(child))
    return found


def socket_inodes(pid):
    inodes = set()
    for fd in Path(f"/proc/{pid}/fd").glob("*"):
        try:
            target = os.readlink(fd)
        except OSError:
            continue
        if target.startswith("socket:["):
            inodes.add(target[8:-1])
    return inodes


def listening_inodes():
    inodes = set()
    for table in ("tcp", "tcp6", "udp", "udp6"):
        for line in Path(f"/proc/net/{table}").read_text().splitlines()[1:]:
            fields = line.split()
            # TCP state 0A is LISTEN; any bound UDP socket can receive.
            if table.startswith("udp") or fields[3] == "0A":
                inodes.add(fields[9])
    return inodes


def inner(executable):
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), "could not adopt package subprocesses")
    environment = {key: value for key, value in os.environ.items() if key in {"DISPLAY", "HOME", "XDG_DATA_HOME", "XAUTHORITY"}}
    environment["PATH"] = "/usr/bin:/bin"
    for tool in ("neura", "node"):
        if shutil.which(tool, path=environment["PATH"]):
            print(f"note: {tool} is on the system path; the package does not use it")
    environment["DBUS_SESSION_BUS_ADDRESS"] = "disabled:"
    process = subprocess.Popen([executable, ELECTRON_X11, "--disable-background-networking"], env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            found = subprocess.run(["xdotool", "search", "--name", TITLE], stdout=subprocess.PIPE, text=True)
            if found.stdout.strip():
                break
            if process.poll() is not None:
                raise RuntimeError(f"the package exited with {process.returncode} before its window showed")
            time.sleep(0.2)
        else:
            raise RuntimeError("no window titled by the native child appeared")
        tree = [process.pid] + descendants(process.pid)
        names = {Path(f"/proc/{pid}/exe").resolve().name for pid in tree if Path(f"/proc/{pid}").exists()}
        if "app-child" not in names:
            raise RuntimeError(f"the native child is not in the process tree: {sorted(names)}")
        listening = listening_inodes()
        for pid in tree:
            if socket_inodes(pid) & listening:
                raise RuntimeError(f"process {pid} of the package listens on a socket")
        process.send_signal(signal.SIGTERM)
        output, _ = process.communicate(timeout=15)
        if process.returncode != 0:
            print(output)
            raise RuntimeError(f"the package exited with {process.returncode}")
        stop_children()
        survivors = [pid for pid in tree[1:] if Path(f"/proc/{pid}").exists()]
        if survivors:
            raise RuntimeError(f"processes survived the package: {survivors}")
        print(f"Verified package launch: window, bridge, child, no listening socket, clean exit ({len(tree)} processes)")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("electron", nargs="?", type=Path, help="the pinned Electron 45.3.3 executable")
    parser.add_argument("--neura", type=Path)
    parser.add_argument("--inner", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.inner:
        inner(str(args.inner))
        return 0
    with tempfile.TemporaryDirectory(prefix="neura-desktop-package-") as scratch:
        scratch = Path(scratch)
        child = build_child(str(args.neura.resolve()), scratch)
        make_package(scratch / "package", child)
        engine = args.electron.resolve().parent
        first = package(engine, scratch / "package", scratch / "first.tar.gz")
        second = package(engine, scratch / "package", scratch / "second.tar.gz")
        if first != second:
            raise RuntimeError("two builds of the package differ")
        print(f"Verified reproducible package: {len(first)} bytes, sha256 {digest(first)}")
        install = scratch / "Install dir é"
        install.mkdir()
        with tarfile.open(scratch / "first.tar.gz") as archive:
            names = archive.getnames()
            if any(name.endswith(".nra") for name in names):
                raise RuntimeError("the package holds Neura source")
            archive.extractall(install, filter="data")
        executable = install / "fixture" / "fixture"
        home = scratch / "home é"
        home.mkdir()
        environment = isolated_environment(HOME=str(home), XDG_DATA_HOME=str(home / "data"))
        result = subprocess.run(["xvfb-run", "-a", sys.executable, str(Path(__file__).resolve()), "--inner", str(executable)],
                                env=environment, timeout=90)
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
