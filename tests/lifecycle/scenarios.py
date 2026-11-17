#!/usr/bin/env python3
"""Scenario tests of the 0.3 install lifecycle (docs/design/Lifecycle030.md).

    scenarios.py SUITE BUILD_DIR WORK_DIR CMAKE

Each suite installs the build's components into prefixes, packs them with
packaging/make-archive.sh, and drives install.sh, neura update, neura
uninstall and neura doctor against roots below WORK_DIR, with an empty
environment: a new HOME, the system PATH and no terminal. Nothing needs the
network or root. The edge cases of the note that a suite covers are named
in its function's documentation and in the check messages ("case N").

Stand-in releases. A release of a newer version than the build is the
build's own components with bin/neura replaced by a shell script that
prints the newer version line, as the 0.2 tests did. Lifecycle commands are
then run with the tool of a real generation, by its path, since a tool finds
its installation from its own real path.

Exit status 0 when every check passes.
"""

from __future__ import annotations

import hashlib
import io
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
MAKE_ARCHIVE = REPOSITORY / "packaging/make-archive.sh"
MAKE_METADATA = REPOSITORY / "packaging/make-release-metadata.sh"

FAILURES: list[str] = []
SYSTEM_PATH = "/usr/bin:/bin"

CORE_TOOLS = ["neura", "neura-doctor", "neura-inspect", "neura-run", "neura-uninstall", "neura-update"]
SDK_TOOLS = ["neura-asm", "neura-cbind", "neura-debug", "neura-decompile", "neura-disasm", "neura-doc", "neura-explain",
             "neura-fmt", "neura-lsp", "neura-tags", "neura-test", "neurac"]


def check(condition: bool, message: str) -> bool:
    if not condition:
        FAILURES.append(message)
        print(f"FAIL: {message}", flush=True)
    return condition


def run(arguments: list, env: dict | None = None, cwd: Path | None = None, timeout: int = 300) -> subprocess.CompletedProcess:
    return subprocess.run([str(a) for a in arguments], env=env, cwd=cwd, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=timeout)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(root: Path) -> dict[str, str]:
    """Every path below root, with each file's digest and each link's target."""
    entries: dict[str, str] = {}
    if not root.exists() and not root.is_symlink():
        return entries
    for directory, names, files in os.walk(root):
        for name in names + files:
            path = Path(directory) / name
            relative = str(path.relative_to(root))
            if path.is_symlink():
                entries[relative] = "-> " + os.readlink(path)
            elif path.is_dir():
                entries[relative] = "dir"
            else:
                entries[relative] = sha256(path)
    return entries


class Kit:
    """The build's components, packed into archives on demand."""

    def __init__(self, build: Path, work: Path, cmake: str):
        self.build = build
        self.work = work
        self.cmake = cmake
        line = run([build / "neura", "--version"]).stdout.strip()
        match = re.fullmatch(r"neura (\S+) \((\S+), commit (\S+)\)", line)
        if not match:
            raise SystemExit(f"unexpected neura --version: {line!r}")
        self.line = line
        self.version, self.triple, self.commit = match.groups()
        self._prefixes: dict[str, Path] | None = None

    def prefixes(self) -> dict[str, Path]:
        if self._prefixes is None:
            base = self.work / "components"
            shutil.rmtree(base, ignore_errors=True)
            self._prefixes = {}
            for component in ("core", "sdk", "service"):
                prefix = base / component
                result = run([self.cmake, "--install", self.build, "--component", component, "--prefix", prefix])
                if result.returncode != 0:
                    raise SystemExit(f"cmake --install --component {component} failed:\n{result.stderr}")
                prefix.mkdir(parents=True, exist_ok=True)
                self._prefixes[component] = prefix
        return self._prefixes

    def variant(self, name: str, version: str | None = None, neurad: bool = False, change: str | None = None) -> dict[str, Path]:
        """A copy of the component prefixes. With `version`, bin/neura is a
        script that reports that version; with `neurad`, the service prefix
        holds a stand-in neurad; with `change`, that file of the sdk prefix
        gets a byte more, which makes another build of the same version."""
        base = self.work / "variants" / name
        shutil.rmtree(base, ignore_errors=True)
        result = {}
        for component, prefix in self.prefixes().items():
            shutil.copytree(prefix, base / component, symlinks=True)
            result[component] = base / component
        if version is not None:
            neura = result["core"] / "bin/neura"
            neura.unlink()
            neura.write_text(f"#!/bin/sh\ncase \"$1\" in\n--version | version) echo \"neura {version} ({self.triple}, commit test)\" ;;\nesac\nexit 0\n")
            neura.chmod(0o755)
        if neurad:
            daemon = result["service"] / "bin/neurad"
            daemon.parent.mkdir(parents=True, exist_ok=True)
            daemon.write_text("#!/bin/sh\n# A stand-in for neurad: the service component's tool.\nexit 0\n")
            daemon.chmod(0o755)
        if change is not None:
            with open(result["sdk"] / change, "ab") as file:
                file.write(b"\n")
        return result

    def pack(self, product: str, output: Path, prefixes: dict[str, Path] | None = None, version: str | None = None,
             build: str | None = None, epoch: str = "0") -> Path:
        prefixes = prefixes or self.prefixes()
        arguments = ["sh", MAKE_ARCHIVE, "--product", product, "--core", prefixes["core"]]
        if product in ("sdk", "legacy"):
            arguments += ["--sdk", prefixes["sdk"]]
        else:
            arguments += ["--service", prefixes["service"]]
        if build is not None:
            arguments += ["--build", build]
        elif version is not None:
            arguments += ["--build", "test"]
        arguments += [output, version or self.version, self.triple]
        result = run(arguments, env={**os.environ, "SOURCE_DATE_EPOCH": epoch})
        if result.returncode != 0:
            raise SystemExit(f"make-archive.sh {product} failed:\n{result.stderr}")
        return Path(result.stdout.strip())

    def release(self, directory: Path, prefixes: dict[str, Path] | None = None, version: str | None = None,
                build: str | None = None) -> Path:
        """A release directory with the three archives and the metadata."""
        shutil.rmtree(directory, ignore_errors=True)
        for product in ("sdk", "runtime", "legacy"):
            self.pack(product, directory, prefixes, version, build)
        result = run(["sh", MAKE_METADATA, directory, version or self.version])
        if result.returncode != 0:
            raise SystemExit(f"make-release-metadata.sh failed:\n{result.stderr}")
        return directory


def archive_members(path: Path) -> dict[str, tuple[str, int, bytes | None]]:
    """name -> (kind, mode, bytes of a file)."""
    members = {}
    with tarfile.open(path, "r:gz") as archive:
        for member in archive.getmembers():
            kind = "file" if member.isreg() else "dir" if member.isdir() else "other"
            data = archive.extractfile(member).read() if member.isreg() else None
            members[member.name] = (kind, member.mode, data)
    return members


def parse_manifest(text: str) -> dict:
    lines = text.split("\n")
    assert lines[-1] == ""
    manifest = {"requires": [], "tools": [], "files": {}, "protocol": None}
    for line in lines[:-1]:
        key, _, value = line.partition(" ")
        if key in ("name", "version", "build", "target"):
            manifest[key] = value
        elif key == "requires":
            manifest["requires"].append(value)
        elif key == "protocol":
            manifest["protocol"] = value
        elif key == "tool":
            manifest["tools"].append(value)
        elif key == "file":
            mode, digest, size, path = value.split(" ")
            manifest["files"][path] = (mode, digest, int(size))
    return manifest


# --------------------------------------------------------------------------
# Suites.

def suite_archives(kit: Kit) -> None:
    """The three archives of a target (step 3; cases 1 to 3 as archives):
    names, top directories, manifests, identical core, reproducible bytes,
    and the release metadata."""
    out = kit.work / "archives"
    shutil.rmtree(out, ignore_errors=True)
    with_daemon = kit.variant("with-neurad", neurad=True)
    sdk = kit.pack("sdk", out / "a")
    runtime = kit.pack("runtime", out / "a")
    legacy = kit.pack("legacy", out / "a")
    runtime_daemon = kit.pack("runtime", out / "daemon", with_daemon)
    v, t = kit.version, kit.triple
    check(sdk.name == f"neura-sdk-{v}-{t}.tar.gz" and runtime.name == f"neura-runtime-{v}-{t}.tar.gz"
          and legacy.name == f"neura-{v}-{t}.tar.gz", "archive names")
    trees = {}
    for path, top in ((sdk, f"neura-sdk-{v}-{t}"), (runtime, f"neura-runtime-{v}-{t}"), (legacy, f"neura-{v}-{t}"),
                      (runtime_daemon, f"neura-runtime-{v}-{t}")):
        members = archive_members(path)
        check(all(name == top or name.startswith(top + "/") for name in members), f"{path.name}: one top directory {top}")
        check(all(kind in ("file", "dir") for kind, _, _ in members.values()), f"{path.name}: regular files and directories only")
        check(all(re.fullmatch(r"[0-9A-Za-z._+/-]+", name) for name in members),
              f"{path.name}: every path uses letters, digits, '.', '_', '+', '-' and '/' only")
        check(all(mode in (0o755, 0o644) for _, mode, _ in members.values()), f"{path.name}: modes 755 and 644 only")
        check(members.get(f"{top}/VERSION", (None, 0, None))[2] == f"{v}\n".encode(), f"{path.name}: VERSION")
        check(f"{top}/install.sh" in members, f"{path.name}: install.sh")
        tree = {name[len(top) + 1:]: entry for name, entry in members.items() if name != top}
        trees[path] = tree
        components = sorted(name.removeprefix("share/neura/components/").removesuffix(".txt")
                            for name in tree if name.startswith("share/neura/components/"))
        expected = ["core", "service"] if "runtime" in path.name else ["core", "sdk"]
        check(components == expected, f"{path.name}: manifests {components}, expected {expected}")
        listed: set[str] = set()
        for component in components:
            text = tree[f"share/neura/components/{component}.txt"][2].decode()
            manifest = parse_manifest(text)
            check(manifest["name"] == component and manifest["version"] == v and manifest["target"] == t
                  and manifest["build"] == kit.commit, f"{path.name}: {component} manifest header")
            check(manifest["requires"] == ([] if component == "core" else ["core"]), f"{path.name}: {component} requires")
            check(manifest["protocol"] == ("service 1.0" if component == "service" else None), f"{path.name}: {component} protocol")
            files = manifest["files"]
            check(list(files) == sorted(files), f"{path.name}: {component} files sorted")
            check(manifest["tools"] == sorted(p[4:] for p in files if p.startswith("bin/")),
                  f"{path.name}: {component} declares every file of its bin/ as a tool")
            for file, (mode, digest, size) in files.items():
                entry = tree.get(file)
                if not check(entry is not None and entry[0] == "file", f"{path.name}: {component} lists {file}, which is there"):
                    continue
                check(hashlib.sha256(entry[2]).hexdigest() == digest and len(entry[2]) == size
                      and f"{entry[1]:o}" == mode, f"{path.name}: {file} has the size, digest and mode of its manifest")
                check(file not in listed, f"{path.name}: {file} belongs to one component")
                listed.add(file)
        unlisted = [name for name, (kind, _, _) in tree.items()
                    if kind == "file" and name not in listed and not name.startswith("share/neura/components/")
                    and name not in ("install.sh", "VERSION")]
        check(not unlisted, f"{path.name}: files no manifest lists: {unlisted[:3]}")
        tools = {name[4:] for name in tree if name.startswith("bin/")}
        if "runtime" in path.name:
            # Case 2 as an archive: no compiler, no standard library.
            check(not tools & set(SDK_TOOLS) and not any(n.startswith("share/neura/stdlib") for n in tree),
                  f"{path.name}: no SDK tool and no standard library")
            check(set(CORE_TOOLS) <= tools, f"{path.name}: every core tool")
            check(("neurad" in tools) == (path == runtime_daemon), f"{path.name}: neurad only when the service prefix has it")
        else:
            # Case 1 as an archive: every core and SDK tool, and no service.
            check(set(CORE_TOOLS) | set(SDK_TOOLS) == tools, f"{path.name}: the core and SDK tools, no neurad")
    service = parse_manifest(trees[runtime_daemon]["share/neura/components/service.txt"][2].decode())
    check(service["tools"] == ["neurad"] and list(service["files"]) == ["bin/neurad"], "a service prefix with neurad: its manifest")
    empty = parse_manifest(trees[runtime]["share/neura/components/service.txt"][2].decode())
    check(empty["tools"] == [] and empty["files"] == {}, "a service prefix without neurad: an empty service manifest")
    # Case 3 as archives: the core is byte-identical in the three archives.
    core = parse_manifest(trees[sdk]["share/neura/components/core.txt"][2].decode())
    for other in (runtime, legacy):
        check(trees[other]["share/neura/components/core.txt"] == trees[sdk]["share/neura/components/core.txt"],
              f"{other.name}: the same core manifest as the SDK archive")
        check(all(trees[other][file] == trees[sdk][file] for file in core["files"]), f"{other.name}: the core files are identical")
    check(trees[legacy] == trees[sdk], "the legacy archive holds exactly the tree of the SDK archive")
    # Reproducible bytes.
    for product, first in (("sdk", sdk), ("runtime", runtime), ("legacy", legacy)):
        again = kit.pack(product, out / "again")
        check(sha256(again) == sha256(first), f"{product}: packing again gives the same bytes")
    # Packing refuses mixed or incomplete prefixes.
    prefixes = kit.prefixes()
    bad = run(["sh", MAKE_ARCHIVE, "--product", "sdk", "--core", prefixes["sdk"], "--sdk", prefixes["sdk"], out / "bad", v, t])
    check(bad.returncode != 0 and "missing" in bad.stderr, "make-archive.sh refuses a core prefix without the core")
    bad = run(["sh", MAKE_ARCHIVE, "--product", "runtime", "--core", prefixes["core"], "--sdk", prefixes["sdk"], out / "bad", v, t])
    check(bad.returncode != 0, "make-archive.sh refuses an sdk component in the runtime product")
    odd = kit.variant("odd-name")
    (odd["sdk"] / "share/neura/stdlib/std/a b.nra").write_text("\n")
    bad = run(["sh", MAKE_ARCHIVE, "--product", "sdk", "--core", odd["core"], "--sdk", odd["sdk"], out / "bad", v, t])
    check(bad.returncode != 0 and "character" in bad.stderr, "make-archive.sh refuses a path with a space")

    # The release metadata of the three archives.
    metadata = run(["sh", MAKE_METADATA, out / "a", v])
    check(metadata.returncode == 0, f"make-release-metadata.sh: {metadata.stderr}")
    release = (out / "a/neura-release.txt").read_text()
    check(release == f"neura-release 1\nversion {v}\nasset {t} {legacy.name} {sha256(legacy)} {legacy.stat().st_size}\n",
          "neura-release.txt names the legacy archive only, in schema 1")
    components = (out / "a/neura-components.txt").read_text()
    check(components == f"neura-components 1\nversion {v}\nbuild {kit.commit}\n"
                        f"archive runtime {t} {runtime.name} {sha256(runtime)} {runtime.stat().st_size}\n"
                        f"archive sdk {t} {sdk.name} {sha256(sdk)} {sdk.stat().st_size}\n",
          f"neura-components.txt lists the SDK and runtime archives:\n{components}")
    sums = (out / "a/SHA256SUMS").read_text().splitlines()
    check(sorted(line.split("  ")[1] for line in sums) == sorted([sdk.name, runtime.name, legacy.name]), "SHA256SUMS lists every archive")
    (out / "a" / runtime.name).unlink()
    missing = run(["sh", MAKE_METADATA, out / "a", v])
    check(missing.returncode != 0 and "no neura-runtime" in missing.stderr, "make-release-metadata.sh requires all three archives")
    shutil.copy(runtime_daemon, out / "a" / runtime.name)
    other_build = kit.pack("runtime", out / "other-build", build="0ther")
    shutil.copy(other_build, out / "a" / runtime.name)
    mixed = run(["sh", MAKE_METADATA, out / "a", v])
    check(mixed.returncode != 0 and "build" in mixed.stderr, "make-release-metadata.sh refuses archives of two builds")
    (out / "a/neura-extra-x.tar.gz").write_bytes(b"x")
    stray = run(["sh", MAKE_METADATA, out / "a", v])
    check(stray.returncode != 0, "make-release-metadata.sh refuses an archive name it does not know")


SUITES = {
    "archives": suite_archives,
}


def main() -> int:
    if len(sys.argv) != 5 or sys.argv[1] not in SUITES:
        print(__doc__, file=sys.stderr)
        return 2
    suite, build, work, cmake = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4]
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    kit = Kit(build, work, cmake)
    SUITES[suite](kit)
    if FAILURES:
        print(f"{len(FAILURES)} check(s) failed in {suite}")
        return 1
    print(f"{suite} passed")
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
