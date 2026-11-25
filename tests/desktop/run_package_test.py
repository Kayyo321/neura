"""Build the packages of the fixture application twice, compare them, install them and launch them.

The Linux .tar.gz and .deb are each built twice and compared byte for byte.
The .tar.gz is extracted under a path with spaces and non-ASCII characters;
the .deb is checked with dpkg-deb (-I, -c) and extracted with dpkg-deb -x as an
installation root, never installed into the system. Each installed tree runs
under Xvfb with no SDK, no neura tool and no Node on its path. The page calls
the bridge at load, and the native child renames the window, so the X window
title shows that the bundle, the bridge and the child work from the package.
The test then checks that no process of the application listens on a socket,
ends it with SIGTERM, and checks that it exits 0 and that nothing it started
survives.

The .zip container fixture uses a stand-in Windows engine. It compares the
output bytes and verifies the archive entries. The test also checks the .dmg,
.msix and --sign refusals and that test hooks reach only a package built with
--test-hooks.
"""

import argparse
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
import zipfile

from run_app_test import build_child
from run_shell_test import ELECTRON_X11, PIPED_TEXT, WINDOWS, isolated_environment, stop_children, use_subreaper

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
APPLICATION_ID = "example.neura.fixture"
OPERATIONS = ["fixture.clipboard_read", "fixture.clipboard_write", "fixture.close", "fixture.data_dir",
              "fixture.echo", "fixture.fail", "fixture.focus", "fixture.menu", "fixture.menu_bad", "fixture.notify",
              "fixture.open", "fixture.open_many", "fixture.open_read", "fixture.quit", "fixture.save",
              "fixture.spin", "fixture.title", "fixture.trap", "fixture.unserved", "fixture.wait", "fixture.work"]
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
    child_path = f"bin/{child.name}"
    shutil.copyfile(child, directory / child_path)
    if not WINDOWS:
        (directory / child_path).chmod(0o755)
    (directory / "desktop.json").write_text(json.dumps({
        "schema": 1, "kind": "desktop", "id": APPLICATION_ID, "version": "0.1.0",
        "web": {"directory": "web", "manifestDigest": digest(manifest)},
        "registry": {"path": "registry.json", "sha256": digest(registry)},
        "child": {"executable": child_path, "args": []},
        "supportedRequirements": {"browserAdapter": 1}, "externalOrigins": [], "capabilities": [], "limits": {},
    }, sort_keys=True))


def make_icon(path):
    from PIL import Image, ImageDraw
    image = Image.new("RGBA", (1024, 1024), (32, 64, 160, 255))
    ImageDraw.Draw(image).ellipse((192, 192, 832, 832), fill=(250, 200, 40, 255))
    image.save(path, format="PNG")
    return path


def packager(engine, directory, output, icon, *extra, check=True):
    return subprocess.run([sys.executable, "-X", "utf8", str(ROOT / "scripts" / "package-desktop.py"), "--engine", str(engine),
                           "--package", str(directory), "--name", "fixture", "--icon", str(icon), "-o", str(output),
                           *extra], check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300,
                          **PIPED_TEXT)


def package(engine, directory, output, icon, *extra):
    result = packager(engine, directory, output, icon, *extra)
    print(result.stdout, end="")
    return output.read_bytes()


def twice(engine, directory, scratch, icon, suffix, *extra):
    first = package(engine, directory, scratch / f"first.{suffix}", icon, *extra)
    second = package(engine, directory, scratch / f"second.{suffix}", icon, *extra)
    if first != second:
        raise RuntimeError(f"two builds of the .{suffix} package differ")
    print(f"Verified reproducible .{suffix}: {len(first)} bytes, sha256 {digest(first)}")
    return scratch / f"first.{suffix}"


def check_deb(deb):
    info = subprocess.run(["dpkg-deb", "-I", str(deb)], check=True, stdout=subprocess.PIPE, text=True).stdout
    for field in ("Package: fixture", "Version: 0.1.0", "Architecture: amd64", "Depends: ", "Installed-Size: "):
        if field not in info:
            raise RuntimeError(f"dpkg-deb -I lacks {field!r}:\n{info}")
    listing = subprocess.run(["dpkg-deb", "-c", str(deb)], check=True, stdout=subprocess.PIPE, text=True).stdout
    lines = listing.splitlines()
    wanted = ["./opt/fixture/fixture", "./opt/fixture/resources/app/desktop.json", "./opt/fixture/LICENSES/PROVIDERS.txt",
              "./usr/share/applications/fixture.desktop", "./usr/share/icons/hicolor/256x256/apps/fixture.png",
              "./usr/bin/fixture -> /opt/fixture/fixture"]
    for path in wanted:
        if not any(line.endswith(" " + path) for line in lines):
            raise RuntimeError(f"dpkg-deb -c lacks {path}")
    if any(line.endswith(".nra") for line in lines):
        raise RuntimeError("the .deb holds Neura source")
    if not_root := [line for line in lines if "root/root" not in line]:
        raise RuntimeError(f"the .deb has entries not owned by root: {not_root[:3]}")
    sandbox = next(line for line in lines if line.endswith(" ./opt/fixture/chrome-sandbox"))
    if not sandbox.startswith("-rwsr-xr-x"):
        raise RuntimeError(f"chrome-sandbox is not setuid root: {sandbox}")
    print(f"Verified .deb metadata and listing ({len(lines)} entries)")


def check_refusals(engine, directory, scratch, icon, suffix):
    refusals = [(["--format", "dmg"], "built on macOS"), (["--format", "msix"], "built on Windows"),
                (["--sign"], "signing runs on the target system")]
    if suffix == "tar.gz":
        refusals.append((["--format", "zip"], "needs the Windows x86-64 engine"))
    for extra, message in refusals:
        result = packager(engine, directory, scratch / "refused", icon, *extra, check=False)
        if result.returncode == 0 or message not in result.stderr or (scratch / "refused").exists():
            raise RuntimeError(f"{extra} was not refused as expected: {result.stderr}")
    hooked = scratch / "hooked"
    shutil.copytree(directory, hooked)
    manifest = json.loads((hooked / "desktop.json").read_text())
    manifest["testHooks"] = {"dialogs": "test-dialogs.json"}
    (hooked / "desktop.json").write_text(json.dumps(manifest))
    result = packager(engine, hooked, scratch / "refused", icon, "--format", suffix, check=False)
    if result.returncode == 0 or "--test-hooks" not in result.stderr:
        raise RuntimeError("a release build of a desktop.json with test hooks was not refused")
    print("Verified package format, signing and release test-hook refusals")


def read_dialogs(path, suffix):
    script = "fixture/resources/app/test-dialogs.json"
    manifest = "fixture/resources/app/desktop.json"
    if suffix == "zip":
        with zipfile.ZipFile(path) as archive:
            return script in archive.namelist(), json.loads(archive.read(manifest))
    with tarfile.open(path) as archive:
        return script in archive.getnames(), json.load(archive.extractfile(manifest))


def check_test_hooks(engine, directory, scratch, icon, suffix):
    script = scratch / "dialogs.json"
    script.write_text('[{"canceled": true}]')
    release_path = scratch / f"release.{suffix}"
    test_path = scratch / f"test.{suffix}"
    release = package(engine, directory, release_path, icon, "--format", suffix)
    test = package(engine, directory, test_path, icon, "--format", suffix, "--test-hooks", str(script))
    scripted, manifest = read_dialogs(release_path, suffix)
    if scripted or "testHooks" in manifest:
        raise RuntimeError("a release package carries test hooks")
    scripted, manifest = read_dialogs(test_path, suffix)
    if not scripted or manifest.get("testHooks") != {"dialogs": "test-dialogs.json"}:
        raise RuntimeError("a --test-hooks package lacks its dialog script or manifest entry")
    if release == test:
        raise RuntimeError("the test package equals the release package")
    print("Verified test hooks: only a --test-hooks package carries the dialog driver")


def make_windows_engine(scratch):
    # A stand-in for the Windows engine directory: the file names the packager handles, with placeholder bytes.
    engine = scratch / "windows-engine"
    (engine / "resources").mkdir(parents=True)
    (engine / "version").write_text("45.3.3")
    for name, data in (("electron.exe", b"MZ stand-in"), ("ffmpeg.dll", b"stand-in"), ("LICENSE", b"license"),
                       ("LICENSES.chromium.html", b"notices"), ("resources/default_app.asar", b"excluded")):
        (engine / name).write_bytes(data)
    return engine


def check_zip(engine, directory, scratch, icon):
    archive = twice(engine, directory, scratch, icon, "zip", "--format", "zip")
    with zipfile.ZipFile(archive) as opened:
        names = opened.namelist()
        if opened.testzip() is not None:
            raise RuntimeError("the .zip is corrupt")
        manifest = json.loads(opened.read("fixture/resources/app/desktop.json"))
        child = opened.read(f"fixture/resources/app/{manifest['child']['executable']}")
        if WINDOWS and not child.startswith(b"MZ"):
            raise RuntimeError("the .zip does not contain the Windows PE child")
    for wanted in ("fixture/fixture.exe", "fixture/resources/icons/fixture.ico", "fixture/LICENSES/LICENSE.electron",
                   "fixture/resources/app/main.js"):
        if wanted not in names:
            raise RuntimeError(f"the .zip lacks {wanted}")
    if "fixture/resources/default_app.asar" in names:
        raise RuntimeError("the .zip holds default_app.asar")
    print(f"Verified the .zip container from a stand-in Windows engine ({len(names)} entries)")


def launch(executable, scratch, label):
    home = scratch / f"home {label} é"
    home.mkdir()
    environment = isolated_environment(HOME=str(home), XDG_DATA_HOME=str(home / "data"))
    result = subprocess.run(["xvfb-run", "-a", sys.executable, str(Path(__file__).resolve()), "--inner", str(executable)],
                            env=environment, timeout=90)
    return result.returncode


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
            # TCP state 0A is LISTEN. A UDP socket can receive from anyone unless it is connected to one peer
            # (state 01, a remote address set), as the network service's DNS queries to the local resolver are.
            if fields[3] == "0A" if table.startswith("tcp") else fields[3] != "01":
                inodes.add(fields[9])
    return inodes


def inner(executable):
    use_subreaper()
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
            if found := socket_inodes(pid) & listening:
                exe = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ")[:200].decode(errors="replace")
                rows = [line for table in ("tcp", "tcp6", "udp", "udp6")
                        for line in Path(f"/proc/net/{table}").read_text().splitlines()[1:]
                        if line.split()[9] in found]
                raise RuntimeError(f"process {pid} of the package listens on a socket: {exe}; {rows}")
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
    with tempfile.TemporaryDirectory(prefix="Neura package 資-") as scratch:
        scratch = Path(scratch)
        child = build_child(str(args.neura.resolve()), scratch)
        directory = scratch / "package"
        make_package(directory, child)
        icon = make_icon(scratch / "icon.png")
        windows_engine = make_windows_engine(scratch)
        if WINDOWS:
            check_zip(windows_engine, directory, scratch, icon)
            check_refusals(windows_engine, directory, scratch, icon, "zip")
            check_test_hooks(windows_engine, directory, scratch, icon, "zip")
            return 0

        engine = args.electron.resolve().parent
        tarball = twice(engine, directory, scratch, icon, "tar.gz")
        install = scratch / "Install dir é"
        install.mkdir()
        with tarfile.open(tarball) as archive:
            names = archive.getnames()
            if any(name.endswith(".nra") for name in names):
                raise RuntimeError("the package holds Neura source")
            if "fixture/resources/icons/512x512.png" not in names:
                raise RuntimeError("the package has no icons")
            archive.extractall(install, filter="data")
        failed = launch(install / "fixture" / "fixture", scratch, "tar")
        deb = twice(engine, directory, scratch, icon, "deb", "--format", "deb")
        check_deb(deb)
        root = scratch / "Deb root é"
        subprocess.run(["dpkg-deb", "-x", str(deb), str(root)], check=True)
        failed |= launch(root / "opt" / "fixture" / "fixture", scratch, "deb")
        check_zip(windows_engine, directory, scratch, icon)
        check_refusals(engine, directory, scratch, icon, "tar.gz")
        check_test_hooks(engine, directory, scratch, icon, "tar.gz")
        return failed


if __name__ == "__main__":
    sys.exit(main())
