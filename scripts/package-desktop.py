"""Assemble a reproducible desktop package of a Neura application: Linux x86-64 .tar.gz or .deb, or the Windows
x86-64 .zip container.

The input is a package directory as the window host loads it (desktop.json,
registry.json, the web bundle and the native child; docs/design/Desktop030.md),
the pinned Electron engine directory for the target system, and one 1,024-pixel
PNG icon. The application tree is

    <name>/<name>[.exe]               the engine executable, renamed
    <name>/resources/app/             package.json, main.js, preload.js and the package directory
    <name>/resources/icons/           the icon at 16 to 512 pixels (Linux), or <name>.ico (Windows)
    <name>/LICENSES/                  the engine's license and Chromium notices, Neura's license,
                                      PROVIDERS.txt, and the notices desktop.json lists
    <name>/...                        the rest of the engine, without default_app.asar

A .tar.gz holds that tree. A .deb installs it under /opt/<name>, with a
command in /usr/bin, a desktop entry and the hicolor icons. A .zip holds the
tree of a Windows engine (the directory must hold electron.exe) with the
native child from the Windows desktop build.

Entries are sorted, owned by 0:0 and dated at SOURCE_DATE_EPOCH (default 0;
a .zip records 1980-01-01, its earliest date), and gzip records no name or
time, so repeat runs with the same inputs give the same bytes. The tool
reports the Python, Pillow and zlib versions used. The package contains no
.nra source and no SDK tool. Nothing is signed. .dmg and .msix are
built on macOS and Windows and are refused here, as is signing.

--test-hooks adds a scripted dialog driver (a JSON list of answers) that the
window host honors only in such a package; a release package has none, and a
desktop.json that already names one is refused without --test-hooks.
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
import zipfile
import zlib

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "hosts" / "desktop"
NAME_CHARACTERS = set("abcdefghijklmnopqrstuvwxyz0123456789-")
EXCLUDED_ENGINE_FILES = {"resources/default_app.asar", "LICENSE", "LICENSES.chromium.html"}
LINUX_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
WINDOWS_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
ELECTRON_VERSION = "45.3.3"
# The Debian packages of the libraries the pinned Electron and its bundled libraries name as NEEDED, found with
# objdump -p and dpkg -S on Debian 13, plus xdg-utils, which opens external links. The second name of a pair is
# the package's name before Debian 13's 64-bit time transition; only Debian 13 has been checked.
DEB_DEPENDS = ("libasound2t64 | libasound2, libatk-bridge2.0-0t64 | libatk-bridge2.0-0, libatk1.0-0t64 | libatk1.0-0, "
               "libatspi2.0-0t64 | libatspi2.0-0, libc6, libcairo2, libcups2t64 | libcups2, libdbus-1-3, libdrm2, "
               "libexpat1, libgbm1, libgcc-s1, libglib2.0-0t64 | libglib2.0-0, libgtk-3-0t64 | libgtk-3-0, libnspr4, "
               "libnss3, libpango-1.0-0, libudev1, libx11-6, libxcb1, libxcomposite1, libxdamage1, libxext6, "
               "libxfixes3, libxkbcommon0, libxrandr2, xdg-utils")
PROVIDERS = """The native child of this application is a Neura native executable. Its runtime and its
host-service providers are part of Neura and are covered by LICENSE.neura. On Linux it links
only the system C and math libraries, which are not part of this package. The window host is
Electron (LICENSE.electron, LICENSES.chromium.html). Other notices in this directory are the
application's own, as its desktop.json lists them.
"""


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


def icons(source, sizes, windows):
    """The icon at each size as PNG bytes, and for Windows an .ico of them."""
    try:
        from PIL import Image
    except ImportError:
        fail("icons need Pillow (Pillow on PyPI or python3-pil on Debian)")
    with Image.open(source) as image:
        if image.format != "PNG" or image.size != (1024, 1024):
            fail(f"the icon must be a 1,024 by 1,024 PNG: {source}")
        image = image.convert("RGBA")
        scaled = {size: image.resize((size, size), Image.LANCZOS) for size in sizes}
    pngs = {}
    for size, picture in scaled.items():
        buffer = io.BytesIO()
        picture.save(buffer, format="PNG", optimize=False, compress_level=9)
        pngs[size] = buffer.getvalue()
    ico = None
    if windows:
        buffer = io.BytesIO()
        scaled[max(sizes)].save(buffer, format="ICO", sizes=[(size, size) for size in sizes])
        ico = buffer.getvalue()
    return pngs, ico


def collect(engine, package, name, icon, windows, test_hooks):
    """The application tree as a map of relative path to (bytes, executable), and desktop.json."""
    entries = {}

    def add(relative, data, executable=False):
        if relative in entries:
            fail(f"duplicate package entry: {relative}")
        entries[relative] = (data, executable)

    engine_executable = "electron.exe" if windows else "electron"
    target_executable = f"{name}.exe" if windows else name
    for relative, path in regular_files(engine):
        if relative in EXCLUDED_ENGINE_FILES:
            continue
        target = target_executable if relative == engine_executable else relative
        add(f"{name}/{target}", path.read_bytes(), windows or os.access(path, os.X_OK))
    if f"{name}/{target_executable}" not in entries:
        fail(f"the engine directory has no {engine_executable}")
    add(f"{name}/LICENSES/LICENSE.electron", (engine / "LICENSE").read_bytes())
    add(f"{name}/LICENSES/LICENSES.chromium.html", (engine / "LICENSES.chromium.html").read_bytes())
    add(f"{name}/LICENSES/LICENSE.neura", (ROOT / "LICENSE").read_bytes())
    add(f"{name}/LICENSES/PROVIDERS.txt", PROVIDERS.encode())

    manifest = json.loads((package / "desktop.json").read_text(encoding="utf-8"))
    if "testHooks" in manifest and test_hooks is None:
        fail("desktop.json names test hooks; build a test package with --test-hooks, or remove them for a release")
    for notice in manifest.get("notices", []):
        path = (package / notice).resolve()
        if not str(path).startswith(str(package.resolve()) + os.sep) or not path.is_file():
            fail(f"desktop.json lists a notice that is not a file in the package: {notice}")
        add(f"{name}/LICENSES/{path.name}", path.read_bytes())
    for relative, path in regular_files(package):
        if relative.endswith(".nra"):
            fail(f"the package directory holds Neura source: {relative}")
        if relative == "desktop.json" and test_hooks is not None:
            continue
        add(f"{name}/resources/app/{relative}", path.read_bytes(), os.access(path, os.X_OK))
    if test_hooks is not None:
        script = json.loads(test_hooks.read_text(encoding="utf-8"))
        if not isinstance(script, list):
            fail("the dialog script must be a JSON list of answers")
        add(f"{name}/resources/app/test-dialogs.json", (json.dumps(script, sort_keys=True) + "\n").encode())
        manifest["testHooks"] = {"dialogs": "test-dialogs.json"}
        add(f"{name}/resources/app/desktop.json", (json.dumps(manifest, sort_keys=True) + "\n").encode())
    for script in ("main.js", "preload.js"):
        add(f"{name}/resources/app/{script}", (HOST / script).read_bytes())
    metadata = {"name": manifest["id"], "version": manifest.get("version", "0.0.0"), "main": "main.js", "private": True}
    add(f"{name}/resources/app/package.json", (json.dumps(metadata, sort_keys=True) + "\n").encode())
    pngs, ico = icons(icon, WINDOWS_ICON_SIZES if windows else LINUX_ICON_SIZES, windows)
    if windows:
        add(f"{name}/resources/icons/{name}.ico", ico)
    else:
        for size, data in pngs.items():
            add(f"{name}/resources/icons/{size}x{size}.png", data)
    return entries, manifest, pngs


def directories_of(entries):
    found = set()
    for relative in entries:
        parts = relative.split("/")[:-1]
        for index in range(1, len(parts) + 1):
            found.add("/".join(parts[:index]))
    return found


def tar_bytes(entries, epoch, prefix="", links=(), modes=None):
    """A tar of `entries` (and of `links`, (path, target) pairs), every directory included, sorted."""
    modes = modes or {}
    links = dict(links)
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.GNU_FORMAT if prefix else tarfile.PAX_FORMAT) as archive:
        if prefix:
            info = tarfile.TarInfo(prefix.rstrip("/") or ".")
            info.type, info.mode, info.mtime = tarfile.DIRTYPE, 0o755, epoch
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            archive.addfile(info)
        for relative in sorted(directories_of({**entries, **links}) | set(entries) | set(links)):
            info = tarfile.TarInfo(prefix + relative)
            info.uid = info.gid = 0
            info.uname = info.gname = "root" if prefix else ""
            info.mtime = epoch
            if relative in links:
                info.type = tarfile.SYMTYPE
                info.linkname = links[relative]
                info.mode = 0o777
                archive.addfile(info)
            elif relative in entries:
                data, executable = entries[relative]
                info.size = len(data)
                info.mode = modes.get(relative, 0o755 if executable else 0o644)
                archive.addfile(info, io.BytesIO(data))
            else:
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                archive.addfile(info)
    return buffer.getvalue()


def gzip_bytes(data):
    return gzip.compress(data, compresslevel=9, mtime=0)


def ar_member(name, data, epoch):
    header = f"{name + '/':<16}{epoch:<12}{0:<6}{0:<6}{0o100644:<8o}{len(data):<10}`\n".encode()
    return header + data + (b"\n" if len(data) % 2 else b"")


def deb_bytes(entries, manifest, pngs, name, epoch):
    """A .deb: the tree under /opt/<name>, /usr/bin/<name>, a desktop entry, hicolor icons and a copyright file."""
    version = manifest.get("version", "0.0.0")
    if not all(character.isalnum() or character in ".+~-" for character in version) or not version[:1].isdigit():
        fail(f"desktop.json version is not a Debian version: {version}")
    files = {f"opt/{relative}": value for relative, value in entries.items()}
    title = manifest.get("title", name)
    desktop = (f"[Desktop Entry]\nType=Application\nName={title}\nExec=/opt/{name}/{name} %U\nIcon={name}\n"
               "Terminal=false\nCategories=Utility;\n")
    files[f"usr/share/applications/{name}.desktop"] = (desktop.encode(), False)
    for size, data in pngs.items():
        files[f"usr/share/icons/hicolor/{size}x{size}/apps/{name}.png"] = (data, False)
    files[f"usr/share/doc/{name}/copyright"] = (
        f"The licenses and notices of {name} are in /opt/{name}/LICENSES.\n".encode(), False)
    # Chromium falls back to its setuid sandbox helper where unprivileged user namespaces are off.
    modes = {f"opt/{name}/chrome-sandbox": 0o4755} if f"opt/{name}/chrome-sandbox" in files else {}
    data = gzip_bytes(tar_bytes(files, epoch, "./", links=[(f"usr/bin/{name}", f"/opt/{name}/{name}")], modes=modes))
    installed = sum(len(value) for value, _ in files.values()) // 1024 + 1
    description = manifest.get("description", f"{title}, a Neura desktop application").replace("\n", " ")
    control = (f"Package: {name}\nVersion: {version}\nArchitecture: amd64\n"
               f"Maintainer: {manifest.get('maintainer', 'Unknown <unknown@invalid>')}\nInstalled-Size: {installed}\n"
               f"Depends: {DEB_DEPENDS}\nSection: misc\nPriority: optional\nDescription: {description}\n")
    control_tar = gzip_bytes(tar_bytes({"control": (control.encode(), False)}, epoch, "./"))
    return (b"!<arch>\n" + ar_member("debian-binary", b"2.0\n", epoch) + ar_member("control.tar.gz", control_tar, epoch) +
            ar_member("data.tar.gz", data, epoch))


def zip_bytes(entries):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for relative in sorted(directories_of(entries) | set(entries)):
            if relative in entries:
                info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 0
                info.external_attr = 0
                archive.writestr(info, entries[relative][0], compresslevel=9)
            else:
                info = zipfile.ZipInfo(relative + "/", date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 0
                info.external_attr = 0x10
                archive.writestr(info, b"")
    return buffer.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", required=True, type=Path, help="the pinned Electron directory for the target system")
    parser.add_argument("--package", required=True, type=Path, help="the package directory with desktop.json")
    parser.add_argument("--name", required=True, help="the executable and top directory name: [a-z0-9-]+")
    parser.add_argument("--icon", required=True, type=Path, help="the 1,024 by 1,024 PNG the icons are made from")
    parser.add_argument("--format", default="tar.gz", choices=["tar.gz", "deb", "zip", "dmg", "msix"])
    parser.add_argument("--test-hooks", type=Path, metavar="DIALOG_SCRIPT",
                        help="build a test package whose host follows this scripted dialog driver")
    parser.add_argument("--sign", action="store_true", help="refused: signing runs on the target system")
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args()
    if args.format == "dmg":
        fail("cannot build a .dmg here: it is built on macOS (hdiutil), with the macOS engine and runner")
    if args.format == "msix":
        fail("cannot build a .msix here: it is built on Windows (MakeAppx), with the Windows engine and runner")
    if args.sign:
        fail("cannot sign here: signing runs on the target system (codesign and notarytool, or signtool); "
             "nothing was built")
    if not args.name or not set(args.name) <= NAME_CHARACTERS:
        fail("the name must be lowercase ASCII letters, digits and '-'")
    version = (args.engine / "version").read_text(encoding="utf-8").strip()
    if version != ELECTRON_VERSION:
        fail(f"the engine is Electron {version}, not the pinned {ELECTRON_VERSION}")
    windows = args.format == "zip"
    if windows and not (args.engine / "electron.exe").is_file():
        fail("cannot build a .zip here: it needs the Windows x86-64 engine (a directory with electron.exe)")
    if not windows and not (args.engine / "electron").is_file():
        fail(f"cannot build a .{args.format} here: it needs the Linux x86-64 engine (a directory with electron)")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    entries, manifest, pngs = collect(args.engine.resolve(), args.package.resolve(), args.name, args.icon, windows,
                                      args.test_hooks)
    if args.format == "tar.gz":
        archive = gzip_bytes(tar_bytes(entries, epoch))
    elif args.format == "deb":
        archive = deb_bytes(entries, manifest, pngs, args.name, epoch)
    else:
        archive = zip_bytes(entries)
    args.output.write_bytes(archive)
    tree = hashlib.sha256()
    for relative in sorted(entries):
        data, executable = entries[relative]
        tree.update(f"{relative}\0{int(executable)}\0{hashlib.sha256(data).hexdigest()}\n".encode())
    hooks = ", TEST HOOKS (not a release package)" if args.test_hooks else ""
    print(f"{args.output}: {len(archive)} bytes, sha256 {hashlib.sha256(archive).hexdigest()}, "
          f"tree {tree.hexdigest()}, {len(entries)} files, unsigned{hooks}")
    # Pillow was loaded by icons() above.
    from PIL import __version__ as pillow_version
    print(f"inputs: Python {sys.version.split()[0]}, Pillow {pillow_version}, zlib {zlib.ZLIB_RUNTIME_VERSION}")


if __name__ == "__main__":
    main()
