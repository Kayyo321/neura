"""Assemble a reproducible Linux x86-64 desktop package (.tar.gz) of a Neura application.

The input is a package directory as the window host loads it (desktop.json,
registry.json, the web bundle and the native child; docs/design/Desktop030.md)
and the pinned Electron engine directory. The output tree is

    <name>/<name>                    the engine executable, renamed
    <name>/resources/app/            package.json, main.js, preload.js and the package directory
    <name>/LICENSES/                 the engine's license and Chromium notices
    <name>/...                       the rest of the engine, without default_app.asar

Entries are sorted, owned by 0:0, dated at SOURCE_DATE_EPOCH (default 0), and
gzip carries no name or time, so the same inputs give the same bytes. The
package contains no .nra source and no SDK tool. Nothing is signed.
"""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import sys
import tarfile

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "hosts" / "desktop"
NAME_CHARACTERS = set("abcdefghijklmnopqrstuvwxyz0123456789-")
EXCLUDED_ENGINE_FILES = {"resources/default_app.asar", "LICENSE", "LICENSES.chromium.html"}


def fail(message):
    print(f"package-desktop: {message}", file=sys.stderr)
    sys.exit(1)


def regular_files(directory):
    """Every file under `directory` as (relative posix path, path), refusing links and other file types."""
    found = []
    for current, directories, files in os.walk(directory):
        directories.sort()
        for name in directories + files:
            path = Path(current) / name
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
                fail(f"not a regular file or directory: {path}")
        for name in sorted(files):
            path = Path(current) / name
            found.append((path.relative_to(directory).as_posix(), path))
    return found


def collect(engine, package, name):
    """The package tree as a map of relative path to (bytes, executable)."""
    entries = {}

    def add(relative, data, executable=False):
        if relative in entries:
            fail(f"duplicate package entry: {relative}")
        entries[relative] = (data, executable)

    for relative, path in regular_files(engine):
        if relative in EXCLUDED_ENGINE_FILES:
            continue
        target = name if relative == "electron" else relative
        add(f"{name}/{target}", path.read_bytes(), os.access(path, os.X_OK))
    if f"{name}/{name}" not in entries:
        fail("the engine directory has no electron executable")
    add(f"{name}/LICENSES/LICENSE.electron", (engine / "LICENSE").read_bytes())
    add(f"{name}/LICENSES/LICENSES.chromium.html", (engine / "LICENSES.chromium.html").read_bytes())
    add(f"{name}/LICENSES/LICENSE.neura", (ROOT / "LICENSE").read_bytes())

    manifest = json.loads((package / "desktop.json").read_text(encoding="utf-8"))
    for relative, path in regular_files(package):
        if relative.endswith(".nra"):
            fail(f"the package directory holds Neura source: {relative}")
        add(f"{name}/resources/app/{relative}", path.read_bytes(), os.access(path, os.X_OK))
    for script in ("main.js", "preload.js"):
        add(f"{name}/resources/app/{script}", (HOST / script).read_bytes())
    metadata = {"name": manifest["id"], "version": manifest.get("version", "0.0.0"), "main": "main.js", "private": True}
    add(f"{name}/resources/app/package.json", (json.dumps(metadata, sort_keys=True) + "\n").encode())
    return entries


def directories_of(entries):
    found = set()
    for relative in entries:
        parts = relative.split("/")[:-1]
        for index in range(1, len(parts) + 1):
            found.add("/".join(parts[:index]))
    return found


def tar_bytes(entries, epoch):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for relative in sorted(directories_of(entries) | set(entries)):
            info = tarfile.TarInfo(relative)
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = epoch
            if relative in entries:
                data, executable = entries[relative]
                info.size = len(data)
                info.mode = 0o755 if executable else 0o644
                archive.addfile(info, io.BytesIO(data))
            else:
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                archive.addfile(info)
    return buffer.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", required=True, type=Path, help="the pinned Electron linux-x64 directory")
    parser.add_argument("--package", required=True, type=Path, help="the package directory with desktop.json")
    parser.add_argument("--name", required=True, help="the executable and top directory name: [a-z0-9-]+")
    parser.add_argument("--format", default="tar.gz", choices=["tar.gz", "deb", "zip", "dmg", "msix"])
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args()
    if args.format != "tar.gz":
        fail(f"a .{args.format} package cannot be built by this tool; only .tar.gz for Linux x86-64 is implemented")
    if not args.name or not set(args.name) <= NAME_CHARACTERS:
        fail("the name must be lowercase ASCII letters, digits and '-'")
    version = (args.engine / "version").read_text(encoding="utf-8").strip()
    if version != "45.3.3":
        fail(f"the engine is Electron {version}, not the pinned 45.3.3")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    entries = collect(args.engine.resolve(), args.package.resolve(), args.name)
    archive = gzip.compress(tar_bytes(entries, epoch), compresslevel=9, mtime=0)
    args.output.write_bytes(archive)
    tree = hashlib.sha256()
    for relative in sorted(entries):
        data, executable = entries[relative]
        tree.update(f"{relative}\0{int(executable)}\0{hashlib.sha256(data).hexdigest()}\n".encode())
    print(f"{args.output}: {len(archive)} bytes, sha256 {hashlib.sha256(archive).hexdigest()}, "
          f"tree {tree.hexdigest()}, {len(entries)} files, unsigned")


if __name__ == "__main__":
    main()
