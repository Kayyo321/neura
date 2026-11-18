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
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
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

    def has_neurad(self) -> bool:
        """Whether the build's service component holds the real neurad."""
        return (self.prefixes()["service"] / "bin/neurad").is_file()

    def variant(self, name: str, version: str | None = None, neurad: bool = False, change: str | None = None,
                without_neurad: bool = False) -> dict[str, Path]:
        """A copy of the component prefixes. With `version`, bin/neura is a
        script that reports that version; with `neurad`, the service prefix
        holds a stand-in neurad in place of the build's; with
        `without_neurad`, it holds none, as in a build without the service;
        with `change`, that file of the sdk prefix gets a byte more, which
        makes another build of the same version."""
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
        if without_neurad:
            (result["service"] / "bin/neurad").unlink(missing_ok=True)
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


def unpack(archive: Path, destination: Path) -> Path:
    """Unpacks an archive into a new directory and returns its top directory."""
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r:gz") as tar:
        tar.extractall(destination, filter="tar") if sys.version_info >= (3, 12) else tar.extractall(destination)
    return destination / archive.name.removesuffix(".tar.gz")


class Home:
    """A new home directory with an install root and a bin directory."""

    def __init__(self, kit: Kit, name: str, root: Path | None = None, bin_dir: Path | None = None):
        self.kit = kit
        self.path = kit.work / "homes" / name
        shutil.rmtree(self.path, ignore_errors=True)
        self.path.mkdir(parents=True)
        self.root = root or self.path / "root"
        self.bin = bin_dir or self.path / "bin"

    def env(self, extra: dict | None = None) -> dict:
        env = {"HOME": str(self.path), "PATH": f"{self.bin}:{SYSTEM_PATH}", "LC_ALL": "C",
               "NEURA_INSTALL_ROOT": str(self.root), "NEURA_BIN_DIR": str(self.bin)}
        env.update(extra or {})
        return env

    def install(self, top: Path, *arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        return run(["sh", top / "install.sh", *arguments], env=self.env(env), cwd=top)

    def neura(self, *arguments, env: dict | None = None, cwd: Path | None = None) -> subprocess.CompletedProcess:
        return run([self.bin / "neura", *arguments], env=self.env(env), cwd=cwd or self.path)

    def tool(self, generation: str, name: str, *arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        """A tool of a generation by its path, not through the links."""
        return run([self.real_root() / "versions" / generation / "bin" / name, *arguments], env=self.env(env), cwd=self.path)

    def real_root(self) -> Path:
        return self.root.resolve()

    def marker(self) -> str:
        return (self.root / "neura-install.txt").read_text()

    def current(self) -> str:
        return os.readlink(self.root / "current").removeprefix("versions/")

    def generations(self) -> list[str]:
        versions = self.root / "versions"
        return sorted(p.name for p in versions.iterdir()) if versions.is_dir() else []

    def links(self) -> dict[str, str]:
        return {p.name: os.readlink(p) for p in self.bin.iterdir() if p.is_symlink()} if self.bin.is_dir() else {}

    def leftovers(self) -> list[str]:
        if not self.root.is_dir():
            return []
        return sorted(p.name for p in self.root.iterdir()
                      if p.name.startswith((".staging-", ".removing-", ".meta-", ".marker-", ".current-", ".install-", ".journal",
                                            ".update-lock", ".neura-install.txt-")))


def expect_links(home: Home, tools: set[str], what: str) -> None:
    root = home.real_root()
    links = home.links()
    check(set(links) == tools, f"{what}: links {sorted(set(links) ^ tools)} differ from the tools")
    check(all(target == f"{root}/current/bin/{name}" for name, target in links.items()), f"{what}: every link points through current")
    marker_links = [line[5:] for line in home.marker().splitlines() if line.startswith("link ")]
    check(marker_links == sorted(tools), f"{what}: the marker lists the links")


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
    without_daemon = kit.variant("without-neurad", without_neurad=True)
    sdk = kit.pack("sdk", out / "a")
    runtime = kit.pack("runtime", out / "a")
    legacy = kit.pack("legacy", out / "a")
    runtime_daemon = kit.pack("runtime", out / "daemon", with_daemon)
    runtime_bare = kit.pack("runtime", out / "bare", without_daemon)
    v, t = kit.version, kit.triple
    check(sdk.name == f"neura-sdk-{v}-{t}.tar.gz" and runtime.name == f"neura-runtime-{v}-{t}.tar.gz"
          and legacy.name == f"neura-{v}-{t}.tar.gz", "archive names")
    trees = {}
    for path, top in ((sdk, f"neura-sdk-{v}-{t}"), (runtime, f"neura-runtime-{v}-{t}"), (legacy, f"neura-{v}-{t}"),
                      (runtime_daemon, f"neura-runtime-{v}-{t}"), (runtime_bare, f"neura-runtime-{v}-{t}")):
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
            check(("neurad" in tools) == (path == runtime_daemon or (path == runtime and kit.has_neurad())),
                  f"{path.parent.name}/{path.name}: neurad only when the service prefix has it")
        else:
            # Case 1 as an archive: every core and SDK tool, and no service.
            check(set(CORE_TOOLS) | set(SDK_TOOLS) == tools, f"{path.name}: the core and SDK tools, no neurad")
    service = parse_manifest(trees[runtime_daemon]["share/neura/components/service.txt"][2].decode())
    check(service["tools"] == ["neurad"] and list(service["files"]) == ["bin/neurad"], "a service prefix with neurad: its manifest")
    empty = parse_manifest(trees[runtime_bare]["share/neura/components/service.txt"][2].decode())
    check(empty["tools"] == [] and empty["files"] == {} and empty["protocol"] == "service 1.0",
          "a service prefix without neurad: an empty service manifest")
    # The service component of this build: the real neurad (Service030.md).
    if kit.has_neurad():
        real = parse_manifest(trees[runtime]["share/neura/components/service.txt"][2].decode())
        check(real["tools"] == ["neurad"] and list(real["files"]) == ["bin/neurad"] and real["requires"] == ["core"],
              "the build's service component: neurad, requiring core")
        check(trees[runtime]["bin/neurad"][2] == (kit.prefixes()["service"] / "bin/neurad").read_bytes(),
              "the runtime archive's neurad is the build's")
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


def hello(kit: Kit, home: Home, name: str = "hello.nvr") -> Path:
    """Builds the hello-world example with the installed neura."""
    output = home.path / name
    result = home.neura("build", REPOSITORY / "examples/hello-world/hello-world.nra", "-o", output)
    check(result.returncode == 0, f"neura build through the link: {result.stderr}")
    return output


def suite_install(kit: Kit) -> None:
    """install.sh and the change procedure (step 4): cases 1 to 9, 11, 13 and
    22 for unpacked archives, and the rules of the 0.2 tests that still hold."""
    release = kit.release(kit.work / "release", kit.variant("service", neurad=True))
    v, t = kit.version, kit.triple
    tops = {p: unpack(release / (f"neura-{p}-{v}-{t}.tar.gz" if p != "legacy" else f"neura-{v}-{t}.tar.gz"), kit.work / "unpacked" / p)
            for p in ("sdk", "runtime", "legacy")}
    core_tools = set(CORE_TOOLS)
    sdk_tools = core_tools | set(SDK_TOOLS)

    # Case 1: a clean SDK install.
    sdk = Home(kit, "sdk")
    result = sdk.install(tops["sdk"])
    check(result.returncode == 0, f"case 1: install.sh of the SDK archive: {result.stderr}")
    generation = sdk.current()
    check(re.fullmatch(re.escape(v) + r"\+[0-9a-f]{12}", generation) is not None, f"case 1: generation name {generation}")
    check(sdk.generations() == [generation], "case 1: one generation")
    expect_links(sdk, sdk_tools, "case 1")
    check(sdk.marker().startswith(f"neura-install 2\nmethod archive\nbin {sdk.bin.resolve()}\ncomponents core sdk\ngeneration {generation}\n"),
          f"case 1: the marker\n{sdk.marker()}")
    check((sdk.root / "leases" / generation).is_file(), "case 1: the generation's lease file")
    check(not (sdk.root / "versions" / generation / "bin/neurad").exists() and "neurad" not in sdk.links(), "case 1: no neurad")
    check(not (sdk.path / ".config").exists() and not (sdk.path / ".local/state").exists(), "case 1: no unit and no service state")
    check(not sdk.leftovers(), f"case 1: leftovers {sdk.leftovers()}")
    check(sdk.neura("--version").stdout == kit.line + "\n", "case 1: neura --version through the link")
    for tool in sorted(sdk_tools - {"neura"}):
        out = run([sdk.bin / tool, "--version"], env=sdk.env())
        check(out.returncode == 0 and out.stdout == kit.line + "\n", f"case 1: {tool} --version from the bin directory")
    image = hello(kit, sdk)
    check(sdk.neura("run", image).stdout == "Hello, World!\n", "case 1: neura run through the link")
    doctor = sdk.neura("doctor")
    check(doctor.returncode == 0, f"case 1: doctor exits 0\n{doctor.stdout}")
    check(not (sdk.root / "update-check.txt").exists(), "no update state without terminals")
    check(result.stdout.startswith(f"Installed neura {v} ({generation})\n"), f"the installer says what it installed\n{result.stdout}")
    check("is not on your PATH" not in result.stdout, "no PATH advice when the bin directory is on PATH")
    for name in ("install.sh", "VERSION"):
        check(not (sdk.root / "versions" / generation / name).exists(), f"the generation holds no {name}")
    tree = sdk.root / "versions" / generation
    check(all(stat.S_IMODE((tree / "bin" / name).stat().st_mode) == 0o755 for name in sdk_tools), "tools are mode 755")

    # Case 2: a clean runtime install.
    runtime = Home(kit, "runtime")
    result = runtime.install(tops["runtime"])
    check(result.returncode == 0, f"case 2: install.sh of the runtime archive: {result.stderr}")
    expect_links(runtime, core_tools | {"neurad"}, "case 2")
    rt = runtime.root / "versions" / runtime.current()
    check(not (rt / "bin/neurac").exists() and not (rt / "share/neura/stdlib").exists(), "case 2: no compiler and no standard library")
    check(runtime.neura("run", image).stdout == "Hello, World!\n", "case 2: neura run of an image the SDK built")
    build = runtime.neura("build", REPOSITORY / "examples/hello-world/hello-world.nra", "-o", runtime.path / "x.nvr")
    check(build.returncode == 1 and build.stderr == 'neura: "build" needs Neura-sdk, which is not installed here\n',
          f"case 2: neura build names Neura-sdk: {build.stderr!r}")
    check(runtime.neura("doctor").returncode == 0, f"case 2: doctor exits 0\n{runtime.neura('doctor').stdout}")
    check("components core service" in runtime.marker(), "case 2: the marker's components")

    # Case 3: a clean combined install from two archives.
    both = Home(kit, "both")
    result = both.install(tops["sdk"], "--add", tops["runtime"])
    check(result.returncode == 0, f"case 3: install.sh --add: {result.stderr}")
    check(len(both.generations()) == 1, "case 3: one generation")
    expect_links(both, sdk_tools | {"neurad"}, "case 3")
    combined = both.root / "versions" / both.current()
    check(sorted(p.name for p in (combined / "share/neura/components").iterdir()) == ["core.txt", "sdk.txt", "service.txt"],
          "case 3: one copy of core and the three manifests")
    check("components core sdk service" in both.marker(), "case 3: the marker's components")
    check(both.neura("doctor").returncode == 0, "case 3: doctor exits 0")
    before = snapshot(both.root)
    result = both.install(tops["sdk"], "--force")
    check(result.returncode == 1 and 'neura uninstall --component service' in result.stderr and snapshot(both.root) == before,
          f"the installer does not drop a component the installation has: {result.stderr}")

    # Case 4: two archives of different builds.
    other = kit.pack("runtime", kit.work / "other-build", kit.variant("other", neurad=True), build="0ther")
    other_top = unpack(other, kit.work / "unpacked/other")
    four = Home(kit, "four")
    result = four.install(tops["sdk"], "--add", other_top)
    check(result.returncode == 1 and "build" in result.stderr and "one build" in result.stderr, f"case 4: {result.stderr}")
    check(not four.root.exists() or not any(four.root.iterdir()), "case 4: nothing installed")
    check(not four.links(), "case 4: no link")

    # Case 5: a regular file named neurac in the bin directory.
    five = Home(kit, "five")
    five.bin.mkdir()
    (five.bin / "neurac").write_text("mine\n")
    (five.bin / "neura-fmt").write_text("mine too\n")
    result = five.install(tops["sdk"])
    check(result.returncode == 1, "case 5: refused")
    check(f"{five.bin.resolve()}/neurac exists and is not a link made by neura" in result.stderr
          and f"{five.bin.resolve()}/neura-fmt exists and is not a link made by neura" in result.stderr
          and result.stderr.rstrip().endswith("nothing was changed"), f"case 5: every conflict is listed\n{result.stderr}")
    check(not five.links() and not (five.bin / "neura").exists(), "case 5: no neura link either")
    check(not (five.root / "versions").exists() or not any((five.root / "versions").iterdir()), "case 5: nothing installed")
    check((five.bin / "neurac").read_text() == "mine\n", "case 5: the file is left")

    # Case 6: a link into another installation's root.
    six = Home(kit, "six")
    six.bin.mkdir()
    os.symlink(f"{both.real_root()}/current/bin/neura-fmt", six.bin / "neura-fmt")
    result = six.install(tops["sdk"])
    check(result.returncode == 1 and "neura-fmt exists and is not a link made by neura" in result.stderr, f"case 6: {result.stderr}")
    check(not (six.bin / "neura").exists(), "case 6: nothing installed")

    # Case 7: a dangling link into this root is ours and is repointed.
    seven = Home(kit, "seven")
    seven.bin.mkdir()
    seven.root.mkdir()
    os.symlink(f"{seven.root.resolve()}/versions/gone/bin/neurac", seven.bin / "neurac")
    result = seven.install(tops["sdk"])
    check(result.returncode == 0, f"case 7: {result.stderr}")
    expect_links(seven, sdk_tools, "case 7")

    # Case 8: a bin directory with a space and a non-ASCII letter, and a
    # root reached through a symbolic link.
    eight = Home(kit, "eight")
    real_root = eight.path / "real root"
    real_root.mkdir()
    os.symlink(real_root, eight.path / "via")
    eight.root = eight.path / "via"
    eight.bin = eight.path / "my bïn"
    result = eight.install(tops["sdk"])
    check(result.returncode == 0, f"case 8: {result.stderr}")
    check(eight.real_root() == real_root.resolve(), "case 8: the root resolves")
    expect_links(eight, sdk_tools, "case 8")
    check(all(target.startswith(str(real_root.resolve())) for target in eight.links().values()), "case 8: links name the real root")
    out = run([eight.bin / "neura", "run", image], env=eight.env())
    check(out.stdout == "Hello, World!\n", f"case 8: the tools resolve the real root: {out.stderr}")
    check(run([eight.bin / "neura", "doctor"], env=eight.env()).returncode == 0, "case 8: doctor exits 0")

    # Case 9: the same build again without --force.
    before = snapshot(sdk.root)
    links_before = sdk.links()
    result = sdk.install(tops["sdk"])
    check(result.returncode == 1 and "is already installed" in result.stderr and "--force" in result.stderr, f"case 9: {result.stderr}")
    check(snapshot(sdk.root) == before and sdk.links() == links_before, "case 9: nothing changed")
    # The legacy archive is the SDK archive: the same generation.
    result = sdk.install(tops["legacy"])
    check(result.returncode == 1 and "is already installed" in result.stderr, "case 9: the legacy archive is the same generation")

    # --force on a whole generation: nothing copied, links repaired.
    (sdk.bin / "neura-fmt").unlink()
    result = sdk.install(tops["sdk"], "--force")
    check(result.returncode == 0 and "is whole" in result.stdout, f"--force on a whole generation: {result.stdout}{result.stderr}")
    expect_links(sdk, sdk_tools, "--force repairs links")
    # Case 10: --force on a whole generation while a tool of it runs.
    session = Session(sdk, "neura-lsp")
    tree_before = snapshot(sdk.root / "versions")
    result = sdk.install(tops["sdk"], "--force")
    check(result.returncode == 0 and "is whole" in result.stdout and snapshot(sdk.root / "versions") == tree_before and session.alive(),
          f"case 10: nothing is overwritten while a tool runs: {result.stderr}")
    # Case 12: --force on a damaged generation that a running process holds.
    held = sdk.root / "versions" / generation / "share/neura/stdlib/std/core/version.nra"
    original = held.read_bytes()
    held.write_bytes(original[:-1])
    before = snapshot(sdk.root)
    result = sdk.install(tops["sdk"], "--force")
    check(result.returncode == 1 and "in use by a running neura process" in result.stderr and snapshot(sdk.root) == before,
          f"case 12: refused while the damaged generation is in use: {result.stderr}")
    session.end()
    held.write_bytes(original)

    # Case 11: --force on a generation with a truncated file and no lease.
    damaged = sdk.root / "versions" / generation / "bin/neura-fmt"
    damaged.write_bytes(damaged.read_bytes()[:100])
    result = sdk.install(tops["sdk"], "--force")
    check(result.returncode == 0 and "is damaged" in result.stdout, f"case 11: {result.stdout}{result.stderr}")
    check(damaged.stat().st_size == (tops["sdk"] / "bin/neura-fmt").stat().st_size, "case 11: the file is replaced")
    check(sdk.generations() == [generation] and not sdk.leftovers(), f"case 11: one generation, no leftovers {sdk.leftovers()}")
    check(sdk.neura("doctor").returncode == 0, "case 11: doctor exits 0")

    # Case 13: a rebuilt release of the same version is another generation.
    rebuilt = kit.release(kit.work / "rebuilt", kit.variant("rebuilt", change="share/neura/stdlib/std/core/version.nra"), build="rebuilt")
    rebuilt_top = unpack(rebuilt / f"neura-sdk-{v}-{t}.tar.gz", kit.work / "unpacked/rebuilt")
    result = sdk.install(rebuilt_top)
    check(result.returncode == 0, f"case 13: {result.stderr}")
    check(len(sdk.generations()) == 2 and generation in sdk.generations() and sdk.current() != generation,
          f"case 13: both generations on disk {sdk.generations()}")
    check(sdk.marker().count("\ngeneration ") == 2, "case 13: the marker lists both")

    # Case 22: an unpacked archive that does not match its manifests.
    for name, damage, text in (
            ("unlisted", lambda top: (top / "bin/extra").write_text("x"), "not in any manifest"),
            ("missing", lambda top: (top / "bin/neura-fmt").unlink(), "missing"),
            ("digest", lambda top: (top / "share/neura/stdlib/std/core/version.nra").write_text("changed\n"), "expected"),
            ("mode", lambda top: (top / "bin/neura-fmt").chmod(0o644), "expected mode 755"),
            ("manifest", lambda top: (top / "share/neura/components/sdk.txt").write_text(
                (top / "share/neura/components/sdk.txt").read_text() + "note x\n"), "unknown line"),
    ):
        top = unpack(release / f"neura-sdk-{v}-{t}.tar.gz", kit.work / "unpacked" / f"bad-{name}")
        damage(top)
        home = Home(kit, f"bad-{name}")
        result = home.install(top)
        check(result.returncode == 1 and text in result.stderr, f"case 22 ({name}): {result.stderr}")
        check(not home.links() and not (home.root / "versions").exists(), f"case 22 ({name}): nothing installed")

    # The rules of 0.2 that still hold.
    managed = Home(kit, "managed")
    managed.root.mkdir()
    (managed.root / "neura-install.txt").write_text("neura-install 1\nmethod deb\nlink /usr/bin/neura\nversion 0.1.0\n")
    result = managed.install(tops["sdk"])
    check(result.returncode == 3 and "owned by deb" in result.stderr, f"a package manager's root: {result.stderr}")
    relative = Home(kit, "relative")
    result = relative.install(tops["sdk"], env={"NEURA_INSTALL_ROOT": "relative/root"})
    check(result.returncode == 1 and "absolute" in result.stderr, f"a relative root: {result.stderr}")
    result = relative.install(tops["sdk"], "--frobnicate")
    check(result.returncode == 1 and "unknown argument" in result.stderr, "an unknown argument")
    check(relative.install(tops["sdk"], "--help").returncode == 0, "--help")
    # A default root and bin directory below HOME.
    plain = Home(kit, "plain")
    env = plain.env()
    del env["NEURA_INSTALL_ROOT"], env["NEURA_BIN_DIR"]
    result = run(["sh", tops["sdk"] / "install.sh"], env=env, cwd=tops["sdk"])
    check(result.returncode == 0 and (plain.path / ".local/share/neura/current").is_symlink() and (plain.path / ".local/bin/neurac").is_symlink(),
          f"the default root and bin directory: {result.stderr}")
    check(f'export PATH="{plain.path}/.local/bin:$PATH"' in result.stdout, f"the installer prints the PATH line\n{result.stdout}")


def suite_bootstrap(kit: Kit) -> None:
    """packaging/bootstrap.sh from a local release directory (step 4):
    --components, the 0.2 metadata fallback, and the refusals of the 0.2
    tests. A download over HTTPS and the provenance check need the network;
    the release workflow runs the script against each published candidate."""
    release = kit.release(kit.work / "release", kit.variant("service", neurad=True))
    v, t = kit.version, kit.triple
    script = REPOSITORY / "packaging/bootstrap.sh"

    def bootstrap(home: Home, *arguments) -> subprocess.CompletedProcess:
        return run(["sh", script, *arguments], env=home.env())

    sets = {"sdk": set(CORE_TOOLS) | set(SDK_TOOLS), "runtime": set(CORE_TOOLS) | {"neurad"},
            "sdk,service": set(CORE_TOOLS) | set(SDK_TOOLS) | {"neurad"}}
    for components, tools in [(None, sets["sdk"])] + list(sets.items()):
        home = Home(kit, f"boot-{components or 'default'}".replace(",", "-"))
        arguments = ["--source", release] + (["--components", components] if components else [])
        result = bootstrap(home, *arguments)
        if not check(result.returncode == 0, f"bootstrap --components {components}: {result.stderr}"):
            continue
        expect_links(home, tools, f"bootstrap --components {components}")
        check("Not verified: build provenance" in result.stdout, "bootstrap says that a local source is not attested")
        check(home.neura("--version").stdout == kit.line + "\n", f"bootstrap --components {components}: neura --version")
        check(home.neura("doctor").returncode == 0, f"bootstrap --components {components}: doctor")
        expected = "neura-sdk" if components != "runtime" else "neura-runtime"
        check(f"Verified: size and SHA-256 of {expected}-{v}-{t}.tar.gz" in result.stdout, f"bootstrap verifies {expected}")

    home = Home(kit, "boot-named")
    check(bootstrap(home, "--source", release, "--version", v).returncode == 0, "bootstrap --version of the release")
    again = bootstrap(home, "--source", release)
    check(again.returncode == 1 and "already installed" in again.stderr, "a second bootstrap of the same build needs --force")
    check(bootstrap(home, "--source", release, "--force").returncode == 0, "bootstrap --force")
    home = Home(kit, "boot-other-version")
    other = bootstrap(home, "--source", release, "--version", "9.9.9")
    check(other.returncode == 1 and "asked for version 9.9.9" in other.stderr and not home.root.exists(), "bootstrap of another version")
    home = Home(kit, "boot-bad-set")
    bad = bootstrap(home, "--source", release, "--components", "service")
    check(bad.returncode == 1 and "--components takes" in bad.stderr, "bootstrap --components service alone is refused")

    # A 0.2 release: neura-release.txt and its archive only.
    legacy = kit.work / "legacy-release"
    legacy.mkdir()
    for name in (f"neura-{v}-{t}.tar.gz", "neura-release.txt"):
        shutil.copy(release / name, legacy / name)
    home = Home(kit, "boot-legacy")
    result = bootstrap(home, "--source", legacy)
    check(result.returncode == 0 and f"Verified: size and SHA-256 of neura-{v}-{t}.tar.gz" in result.stdout,
          f"bootstrap of a release without component metadata: {result.stderr}")
    expect_links(home, sets["sdk"], "bootstrap of a release without component metadata")
    home = Home(kit, "boot-legacy-runtime")
    result = bootstrap(home, "--source", legacy, "--components", "runtime")
    check(result.returncode == 1 and "no component metadata" in result.stderr, "a runtime from a release without component metadata")

    # Releases that must be refused, with nothing installed.
    def broken(name: str, change, text: str) -> None:
        directory = kit.work / "broken" / name
        shutil.copytree(release, directory)
        change(directory)
        home = Home(kit, f"broken-{name}")
        result = bootstrap(home, "--source", directory)
        check(result.returncode == 1 and text in result.stderr, f"bootstrap of a release with {name}: {result.stderr}")
        check(not home.root.exists() and not home.bin.exists(), f"bootstrap of a release with {name}: nothing installed")

    sdk_name = f"neura-sdk-{v}-{t}.tar.gz"
    broken("an archive of another size", lambda d: (d / sdk_name).open("ab").write(b"x"), "the release metadata says")
    broken("an archive of other bytes", lambda d: (d / sdk_name).write_bytes(b"x" * (d / sdk_name).stat().st_size), "SHA-256 mismatch")
    broken("no archive for this machine", lambda d: (d / "neura-components.txt").write_text(f"neura-components 1\nversion {v}\nbuild x\n"),
           f"has no sdk archive for {t}")
    broken("metadata that is something else", lambda d: (d / "neura-components.txt").write_text("<html>Not Found</html>\n"),
           "is not component metadata")

    def escaping(directory: Path) -> None:
        pack = directory / "pack"
        (pack / f"neura-sdk-{v}-{t}").mkdir(parents=True)
        (pack / f"neura-sdk-{v}-{t}/install.sh").write_text("exit 0\n")
        (pack / "outside").mkdir()
        (pack / "outside/file").write_text("x\n")
        with tarfile.open(directory / sdk_name, "w:gz") as tar:
            tar.add(pack / f"neura-sdk-{v}-{t}", f"neura-sdk-{v}-{t}")
            tar.add(pack / "outside", "outside")
        digest, size = sha256(directory / sdk_name), (directory / sdk_name).stat().st_size
        (directory / "neura-components.txt").write_text(
            f"neura-components 1\nversion {v}\nbuild x\narchive sdk {t} {sdk_name} {digest} {size}\n")
    broken("a member outside its directory", escaping, "holds a member outside")
    home = Home(kit, "boot-http")
    http = bootstrap(home, "--source", "http://example.invalid/release")
    check(http.returncode == 1 and "an http:// source is not accepted" in http.stderr, "an http source")
    unknown = bootstrap(home, "--frobnicate")
    check(unknown.returncode == 1 and "unknown option" in unknown.stderr, "an unknown option")


FAULT_STEPS_BEFORE = ["download", "checksum", "extract", "verify", "compose", "journal", "place"]
FAULT_STEPS_AFTER = ["links-remove", "activate-link", "links-add", "activate-marker"]


def components_metadata(directory: Path, version: str, build: str, archives: list[tuple[str, str, Path]]) -> None:
    """Writes neura-components.txt for (product, triple, file) entries."""
    lines = [f"neura-components 1\nversion {version}\nbuild {build}\n"]
    for product, triple, file in sorted(archives):
        lines.append(f"archive {product} {triple} {file.name} {sha256(file)} {file.stat().st_size}\n")
    (directory / "neura-components.txt").write_text("".join(lines))


def hand_archive(kit: Kit, name: str, version: str, build, product: str = "sdk") -> Path:
    """A release directory with one hand-made archive; `build(top)` fills the
    top directory and may add members through the returned tar hook."""
    directory = kit.work / "hand" / name
    shutil.rmtree(directory, ignore_errors=True)
    tree = kit.work / "hand" / f"{name}-tree"
    shutil.rmtree(tree, ignore_errors=True)
    top_name = f"neura-{product}-{version}-{kit.triple}"
    top = tree / top_name
    (top / "bin").mkdir(parents=True)
    (top / "bin/neura").write_text(f"#!/bin/sh\necho 'neura {version} (x, commit test)'\n")
    (top / "bin/neura").chmod(0o755)
    directory.mkdir(parents=True)
    archive = directory / f"{top_name}.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(top, top_name)
        build(tar, tree)
    components_metadata(directory, version, "test", [(product, kit.triple, archive)])
    return directory


class Session:
    """A long-running tool with its standard input open, such as an editor
    session: `neura-lsp --stdio` or `neura-debug --stdio`."""

    def __init__(self, home: Home, tool: str):
        self.process = subprocess.Popen([str(home.bin / tool), "--stdio"], env=home.env(), stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        # A tool takes its lease as it starts, which takes a while on a loaded
        # machine; the checks that follow need it held.
        if (home.root / "leases" / home.current()).exists():
            check(wait_for(lambda: lease_held(home, home.current())), f"{tool} holds its generation's lease")

    def request(self, message: str) -> bytes:
        data = message.encode()
        self.process.stdin.write(f"Content-Length: {len(data)}\r\n\r\n".encode() + data)
        self.process.stdin.flush()
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            byte = self.process.stdout.read(1)
            if not byte:
                return b""
            header += byte
        length = int(re.search(rb"Content-Length: (\d+)", header).group(1))
        return self.process.stdout.read(length)

    def alive(self) -> bool:
        return self.process.poll() is None

    def end(self) -> None:
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()


def lease_held(home: Home, generation: str) -> bool:
    import fcntl
    with open(home.root / "leases" / generation) as file:
        try:
            fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return True
        fcntl.flock(file, fcntl.LOCK_UN)
        return False


def suite_update(kit: Kit) -> None:
    """neura update, --rollback, --repair, leases and pruning (step 5):
    cases 14, 16 to 21, 25 to 31, 34 to 37 and 61, and the rules of the 0.2
    update tests that still hold."""
    v, t = kit.version, kit.triple
    base_release = kit.release(kit.work / "base-release", kit.variant("service", neurad=True))
    tops = {p: unpack(base_release / f"neura-{p}-{v}-{t}.tar.gz", kit.work / "unpacked" / p) for p in ("sdk", "runtime")}
    newer = kit.release(kit.work / "newer", kit.variant("newer", version="9.9.9", neurad=True), version="9.9.9")
    newest = kit.release(kit.work / "newest", kit.variant("newest", version="9.9.10", neurad=True), version="9.9.10")

    home = Home(kit, "update")
    check(home.install(tops["sdk"]).returncode == 0, "the SDK install for the update tests")
    base = home.current()
    image = hello(kit, home)

    def update(*arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        return run([home.bin / "neura-update", "update", *arguments], env=home.env(env), cwd=home.path)

    def expect_untouched(what: str, before: dict, links: dict) -> None:
        check(snapshot(home.root) == before, f"{what}: the root is unchanged")
        check(home.links() == links, f"{what}: the links are unchanged")
        check(not home.leftovers(), f"{what}: no leftovers {home.leftovers()}")

    # Case 25 and the same and older versions: up to date.
    legacy_only = kit.work / "legacy-only"
    legacy_only.mkdir()
    shutil.copy(base_release / "neura-release.txt", legacy_only)
    before, links = snapshot(home.root), home.links()
    for source, what in ((base_release, "the installed version"), (legacy_only, "case 25: only neura-release.txt")):
        for flags in ([], ["--check"]):
            result = update(*flags, "--source", source)
            check(result.returncode == 0 and result.stdout == f"neura {v} is up to date\n", f"{what} {flags}: {result.stdout}{result.stderr}")
    older = kit.work / "older"
    older.mkdir()
    (older / "neura-release.txt").write_text("neura-release 1\nversion 0.0.1\n")
    result = update("--source", older)
    check(result.returncode == 0 and "is up to date" in result.stdout, "an older version is up to date")
    expect_untouched("up to date", before, links)
    # Case 26: a newer version without component metadata.
    newer_legacy = kit.work / "newer-legacy"
    newer_legacy.mkdir()
    shutil.copy(newer / "neura-release.txt", newer_legacy)
    for flags in ([], ["--check"]):
        result = update(*flags, "--source", newer_legacy)
        check(result.returncode == 1 and "release 9.9.9 has no component metadata" in result.stderr, f"case 26 {flags}: {result.stderr}")
    expect_untouched("case 26", before, links)
    result = update("--check", "--source", newer)
    check(result.returncode == 4 and f"neura {v} is installed; neura 9.9.9 is available" in result.stdout, f"--check: {result.stdout}")
    expect_untouched("--check", before, links)

    # Failures that leave the installation untouched.
    def expect_failure(what: str, source: Path, text: str, env: dict | None = None, arguments=()) -> None:
        result = update("--source", source, *arguments, env=env)
        check(result.returncode == 1 and text in result.stderr, f"{what}: expected '{text}', got {result.returncode}: {result.stderr}")
        expect_untouched(what, before, links)
        check(home.neura("run", image).stdout == "Hello, World!\n", f"{what}: the installation works")

    sdk_name = f"neura-sdk-9.9.9-{t}.tar.gz"

    def variant_release(name: str, change) -> Path:
        directory = kit.work / "variants-release" / name
        shutil.rmtree(directory, ignore_errors=True)
        shutil.copytree(newer, directory)
        change(directory)
        return directory

    expect_failure("a wrong SHA-256", variant_release("sha", lambda d: (d / "neura-components.txt").write_text(
        (d / "neura-components.txt").read_text().replace(sha256(d / sdk_name), "0" * 64))), "SHA-256 mismatch")
    expect_failure("a wrong size", variant_release("size", lambda d: (d / "neura-components.txt").write_text(
        (d / "neura-components.txt").read_text().replace(f" {(d / sdk_name).stat().st_size}\n", f" {(d / sdk_name).stat().st_size + 1}\n"))),
        "the release metadata says")
    # Case 21: members that are refused before anything is unpacked.
    def member(kind):
        def add(tar: tarfile.TarFile, tree: Path) -> None:
            info = tarfile.TarInfo({"dotdot": "../evil", "absolute": "/tmp/neura-absolute-member",
                                    "symlink": f"neura-sdk-9.9.9-{t}/link", "hardlink": f"neura-sdk-9.9.9-{t}/bin/neura2",
                                    "outside": "outside/file"}[kind])
            if kind == "symlink":
                info.type, info.linkname = tarfile.SYMTYPE, "/etc/passwd"
                tar.addfile(info)
            elif kind == "hardlink":
                info.type, info.linkname = tarfile.LNKTYPE, f"neura-sdk-9.9.9-{t}/bin/neura"
                tar.addfile(info)
            else:
                data = b"outside\n"
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
        return add
    for kind, text in (("dotdot", "is not inside"), ("absolute", "is not inside"), ("outside", "is not inside"),
                       ("symlink", "is not a regular file or a directory"), ("hardlink", "is not a regular file or a directory")):
        expect_failure(f"case 21: a {kind} member", hand_archive(kit, kind, "9.9.9", member(kind)), text)
        check(not (kit.work / "hand" / "evil").exists() and not Path("/tmp/neura-absolute-member").exists(),
              f"case 21: the {kind} member was not unpacked")
    # Case 22 through an update: an archive that does not match its manifest.
    def repack(name: str, change) -> Path:
        directory = kit.work / "variants-release" / name
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True)
        top = unpack(newer / sdk_name, kit.work / "variants-release" / f"{name}-tree")
        change(top)
        with tarfile.open(directory / sdk_name, "w:gz") as tar:
            tar.add(top, top.name)
        components_metadata(directory, "9.9.9", "test", [("sdk", t, directory / sdk_name)])
        return directory
    expect_failure("case 22: an unlisted file", repack("unlisted", lambda top: (top / "bin/extra").write_text("x")), "not in any manifest")
    expect_failure("case 22: a listed file missing", repack("missing", lambda top: (top / "bin/neura-fmt").unlink()), "missing")
    expect_failure("case 22: a wrong digest", repack("digest", lambda top: (top / "share/neura/stdlib/std/core/version.nra").write_text("x\n")),
                   "expected")
    expect_failure("case 22: a wrong mode", repack("mode", lambda top: (top / "bin/neura-fmt").chmod(0o644)), "expected mode 755")
    expect_failure("case 23 through an update: a manifest with an unknown line", repack("manifest-line", lambda top: (
        top / "share/neura/components/sdk.txt").write_text((top / "share/neura/components/sdk.txt").read_text() + "note x\n")),
        "unknown line")
    expect_failure("an unknown component", repack("unknown-component", lambda top: (
        top / "share/neura/components/web.txt").write_text("neura-component 1\nname web\nversion 9.9.9\nbuild test\ntarget x\nrequires core\n")),
        "is not one this neura knows")
    lying = kit.release(kit.work / "lying", kit.variant("lying", version="9.9.9"), version="9.9.9")
    script = (kit.work / "variants/lying/core/bin/neura")
    expect_failure("a staged binary that reports another version",
                   repack("lying-binary", lambda top: (top / "bin/neura").write_text(script.read_text().replace("9.9.9", "1.2.3"))),
                   "expected")
    other_triple = variant_release("other-triple", lambda d: (d / "neura-components.txt").write_text(
        (d / "neura-components.txt").read_text().replace(t, "riscv64-plan9-gnu")))
    expect_failure("a release without a build for this platform", other_triple, f"has no sdk archive for {t}")
    expect_failure("malformed component metadata", variant_release("malformed", lambda d: (d / "neura-components.txt").write_text("nonsense\n")),
                   "the component metadata is not valid")
    missing = kit.work / "missing-metadata"
    missing.mkdir()
    expect_failure("a source without metadata", missing, "cannot read release metadata")
    expect_failure("case 24 through an update: a size of 0", variant_release("zero", lambda d: (d / "neura-components.txt").write_text(
        re.sub(r"(archive .*) \d+\n", r"\1 0\n", (d / "neura-components.txt").read_text()))), "the size is not between")
    # Cases 17 and 18: a fault at every step.
    for step in FAULT_STEPS_BEFORE:
        expect_failure(f"case 17: a fault at {step}", newer, f"injected fault at step {step}", env={"NEURA_TEST_UPDATE_FAULT": step})
    for step in FAULT_STEPS_AFTER:
        expect_failure(f"case 18: a fault at {step}", newer, f"update failed and was rolled back; {base} is still active",
                       env={"NEURA_TEST_UPDATE_FAULT": step})
    # --require-attestation needs gh, signed in.
    tools = kit.work / "no-gh"
    tools.mkdir()
    for name in ("tar", "gzip", "sha256sum"):
        found = shutil.which(name, path=SYSTEM_PATH)
        if found:
            os.symlink(found, tools / name)
    expect_failure("--require-attestation without gh", newer, "gh (the GitHub CLI), which is not on PATH",
                   env={"PATH": str(tools)}, arguments=["--require-attestation"])
    signed_out = kit.work / "signed-out-gh"
    shutil.copytree(tools, signed_out, symlinks=True)
    (signed_out / "gh").write_text("#!/bin/sh\nexit 1\n")
    (signed_out / "gh").chmod(0o755)
    expect_failure("--require-attestation with gh signed out", newer, "needs gh (the GitHub CLI) to be signed in",
                   env={"PATH": str(signed_out)}, arguments=["--require-attestation"])
    # Sources.
    for flags in ([], ["--check"]):
        for source, text in (("http://127.0.0.1:9/releases", "https://"), ("file:///tmp", "https://"),
                             (str(kit.work / "does-not-exist"), "is not a directory")):
            result = update(*flags, "--source", source)
            check(result.returncode == 1 and text in result.stderr, f"a source {source} {flags}: {result.stderr}")
    check(update("--bogus").returncode == 1, "an unknown option")
    check(update("--add", "x").returncode == 1 and update("--terminate").returncode == 1, "options of other modes")
    check(update("--rollback", "--repair").returncode == 1, "two modes at once")
    expect_untouched("rejected sources", before, links)

    # Case 14: a second command while one holds the lock.
    import fcntl
    with open(home.root / ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        result = update("--source", newer)
        check(result.returncode == 1 and "another install, update or uninstall is running (lock " in result.stderr, f"case 14: {result.stderr}")
        result = home.install(tops["sdk"], "--force")
        check(result.returncode == 1 and "another install, update or uninstall is running" in result.stderr, "case 14: install.sh too")
    expect_untouched("case 14", before, links)
    # Case 16: a 0.2 lock file on a schema 2 root is left over and removed.
    (home.root / ".update-lock").write_text("1234\n")
    result = update("--repair")
    check(result.returncode == 0 and not (home.root / ".update-lock").exists(), f"case 16: {result.stderr}")

    # Case 19: a crash at each of steps 7 to 11, then recovery by the rule
    # of step 2. Case 20: the tools work before and after it.
    for step in ["journal", "place", "links-remove", "activate-link", "links-add", "activate-marker"]:
        before_crash = home.current()
        result = update("--source", newer, env={"NEURA_TEST_UPDATE_CRASH": step})
        check(result.returncode == 70, f"case 19: a crash at {step}: {result.returncode} {result.stderr}")
        completed = step in ("links-add", "activate-marker")
        if not completed:
            check(home.current() == before_crash, f"case 19: current is unchanged after a crash at {step}")
        check((home.root / ".journal").exists() == (step != "journal"), f"case 19: a journal after a crash at {step}")
        check(run([home.bin / "neura-run", image], env=home.env()).stdout == "Hello, World!\n", f"case 20: a run after a crash at {step}")
        built = run([home.bin / "neurac", REPOSITORY / "examples/hello-world/hello-world.nra", "-o", home.path / "crash.nvr"], env=home.env())
        check(built.returncode == 0, f"case 20: a build after a crash at {step}: {built.stderr}")
        result = update("--repair")
        check(result.returncode == 0, f"case 19: recovery after a crash at {step}: {result.stderr}")
        if step != "journal":
            check(("completed an interrupted update" if completed else "undid an interrupted update") in result.stdout,
                  f"case 19: recovery after {step} says what it did: {result.stdout}")
        check(not (home.root / ".journal").exists() and not home.leftovers(), f"case 19: no journal or leftovers after {step}")
        expected = home.current()
        check(expected.startswith("9.9.9+") if completed else expected == base, f"case 19: the active generation after {step}: {expected}")
        tools_now = set(CORE_TOOLS) | set(SDK_TOOLS)
        expect_links(home, tools_now, f"case 19: links after recovery from {step}")
        check(run([home.bin / "neura-run", image], env=home.env()).stdout == "Hello, World!\n", f"case 20: a run after recovery from {step}")
        built = run([home.bin / "neurac", REPOSITORY / "examples/hello-world/hello-world.nra", "-o", home.path / "crash.nvr"], env=home.env())
        check(built.returncode == 0, f"case 20: a build after recovery from {step}: {built.stderr}")
        if completed:
            back = update("--rollback")
            check(back.returncode == 0 and home.current() == base, f"case 19: back to the base after {step}: {back.stderr}")
            # The new generation is the previous one now; remove it for the next round.
            shutil.rmtree(home.root / "versions" / expected)
            (home.root / "leases" / expected).unlink()
            check(update("--repair").returncode == 0, "repair after removing a generation by hand")

    # Case 27: an SDK installation updates; the runtime archive is not read.
    no_runtime = variant_release("no-runtime", lambda d: (d / f"neura-runtime-9.9.9-{t}.tar.gz").unlink())
    sessions = [Session(home, "neura-lsp"), Session(home, "neura-debug")]
    init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}'
    check(b'"capabilities"' in sessions[0].request(init), "case 30: the editor session answers before the update")
    result = update("--source", no_runtime)
    check(result.returncode == 0 and f"updated neura {v} to 9.9.9" in result.stdout, f"case 27: {result.stdout}{result.stderr}")
    first = home.current()
    check(first.startswith("9.9.9+") and "components core sdk\n" in home.marker(), "case 27: the set stays core sdk")
    check("verified: size and SHA-256" in result.stdout and re.search(r"(NOT verified|not checked): build provenance", result.stdout),
          "the update says what it verified")
    check(home.neura("--version").stdout == f"neura 9.9.9 ({t}, commit test)\n", "the links reach the new generation")
    check(sorted(home.generations()) == sorted([base, first]), "the previous generation is kept")
    # Case 30: the sessions keep working on their generation, which stays.
    check(all(session.alive() for session in sessions) and lease_held(home, base), "case 30: both sessions run and hold the old generation")
    check(b'"result"' in sessions[0].request('{"jsonrpc":"2.0","id":2,"method":"shutdown"}'), "case 30: the editor session still answers")
    # Case 31: two updates later the session's generation is still there.
    result = update("--source", newest)
    check(result.returncode == 0, f"case 31: the second update: {result.stderr}")
    second = home.current()
    check(base in home.generations() and "kept generation " + base + ": it is in use" in result.stdout,
          f"case 31: the session's generation is kept: {home.generations()} {result.stdout}")
    check(f"generation {base}\n" in home.marker(), "case 31: the marker lists it")
    for session in sessions:
        session.end()
    third = kit.release(kit.work / "third", kit.variant("third", version="9.9.11", neurad=True), version="9.9.11")
    result = update("--source", third)
    check(result.returncode == 0 and base not in home.generations() and first not in home.generations(),
          f"case 31: removed by the first update after the session ends: {home.generations()}")
    check(sorted(home.generations()) == sorted([second, home.current()]), "the active and previous generations remain")

    # Cases 34 and 35: rollback, and rollback twice.
    latest = home.current()
    result = update("--rollback")
    check(result.returncode == 0 and home.current() == second, f"case 34: {result.stdout}{result.stderr}")
    expect_links(home, set(CORE_TOOLS) | set(SDK_TOOLS), "case 34: the links follow")
    check(sorted(home.generations()) == sorted([second, latest]), "case 34: the newer one is the previous")
    result = update("--rollback")
    check(result.returncode == 0 and home.current() == latest, f"case 35: {result.stderr}")
    # Case 37: a rollback to a generation whose file was changed.
    changed = home.root / "versions" / second / "share/neura/stdlib/std/core/version.nra"
    original = changed.read_bytes()
    changed.write_bytes(original + b"\n")
    before, links = snapshot(home.root), home.links()
    result = update("--rollback")
    check(result.returncode == 1 and "expected" in result.stderr and home.current() == latest, f"case 37: {result.stderr}")
    expect_untouched("case 37", before, links)
    changed.write_bytes(original)
    # Case 19 for a rollback: a crash before step 9 leaves both generations.
    result = update("--rollback", env={"NEURA_TEST_UPDATE_CRASH": "links-remove"})
    check(result.returncode == 70, "case 19: a crash during --rollback")
    check(update("--repair").returncode == 0 and home.current() == latest and sorted(home.generations()) == sorted([second, latest]),
          f"case 19: both generations after recovering a rollback {home.generations()}")
    # Case 36: no previous generation.
    for name in home.generations():
        if name != latest:
            shutil.rmtree(home.root / "versions" / name)
            (home.root / "leases" / name).unlink()
    result = update("--rollback")
    check(result.returncode == 1 and "no previous 0.3 generation is installed" in result.stderr, f"case 36: {result.stderr}")

    # Case 28 and 29: a combined installation.
    both = Home(kit, "combined")
    check(both.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "the combined install")
    combined_base = both.current()
    bad_runtime = variant_release("bad-runtime", lambda d: (d / f"neura-runtime-9.9.9-{t}.tar.gz").open("r+b").write(b"\0"))
    before_combined = snapshot(both.root)
    result = run([both.bin / "neura-update", "update", "--source", bad_runtime], env=both.env())
    check(result.returncode == 1 and "SHA-256 mismatch" in result.stderr and snapshot(both.root) == before_combined,
          f"case 29: {result.stderr}")
    result = run([both.bin / "neura-update", "update", "--source", newer], env=both.env())
    check(result.returncode == 0 and "neura-sdk-9.9.9" in result.stdout and "neura-runtime-9.9.9" in result.stdout,
          f"case 28: both archives are fetched: {result.stdout}{result.stderr}")
    check(len(both.generations()) == 2 and both.current() != combined_base and "components core sdk service" in both.marker(),
          "case 28: one new generation with every component")
    expect_links(both, set(CORE_TOOLS) | set(SDK_TOOLS) | {"neurad"}, "case 28")

    # The 0.2 rules for installs neura does not manage.
    for arguments in (["update"], ["update", "--check"], ["uninstall"], ["uninstall", "--dry-run"]):
        result = run([kit.build / "neura", *arguments], env=home.env())
        check(result.returncode == 3 and "not an archive install" in result.stderr, f"{arguments} on the build tree's neura")
    doctor = run([kit.build / "neura", "doctor"], env=home.env())
    check(doctor.returncode == 0 and "ok  install: unmanaged" in doctor.stdout, "doctor on the build tree's neura")

    # Opt-in prereleases, through the real transport with a stand-in curl.
    nightly = kit.release(kit.work / "nightly", kit.variant("nightly", version="9.9.9-rc.10"), version="9.9.9-rc.10")
    fixture = kit.work / "nightly-fixture"
    fixture.mkdir()
    mock_tools = kit.work / "nightly-tools"
    mock_tools.mkdir()
    for name in ("tar", "gzip", "cp"):
        os.symlink(shutil.which(name, path=SYSTEM_PATH), mock_tools / name)
    (mock_tools / "curl").write_text(f"""#!/bin/sh
output=
url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) shift; output=$1 ;;
        --url) shift; url=$1 ;;
    esac
    shift
done
printf '%s\\n' "$url" >> "{fixture}/requests"
case "$NEURA_NIGHTLY_MODE" in
    failure) echo "fixture GitHub API failure" >&2; exit 22 ;;
esac
case "$url" in
    'https://api.github.com/repos/Kayyo321/neura/releases?per_page=20&page=1') source="{fixture}/page1.json" ;;
    'https://api.github.com/repos/Kayyo321/neura/releases?per_page=20&page=2') source="{fixture}/page2.json" ;;
    'https://github.com/Kayyo321/neura/releases/latest/download/'*) source="{third}/${{url##*/}}" ;;
    'https://github.com/Kayyo321/neura/releases/download/v9.9.9-rc.10/neura-components.txt')
        if [ "$NEURA_NIGHTLY_MODE" = mismatch ]; then source="{third}/neura-components.txt"; else source="{nightly}/neura-components.txt"; fi ;;
    'https://github.com/Kayyo321/neura/releases/download/v9.9.9-rc.10/'*) source="{nightly}/${{url##*/}}" ;;
    *) echo "unexpected fixture URL: $url" >&2; exit 22 ;;
esac
[ -f "$source" ] || exit 22
cp "$source" "$output"
""")
    (mock_tools / "curl").chmod(0o755)
    page_one = "[" + ",".join(['{"tag_name":"v9.9.8","draft":false,"prerelease":false,"published_at":"2026-11-12T00:00:00Z"}'] * 19) + \
               ',{"tag_name":"v99.0.0","draft":true,"prerelease":false,"published_at":null}]'
    (fixture / "page1.json").write_text(page_one)
    (fixture / "page2.json").write_text('[{"tag_name":"v9.9.9-rc.2","draft":false,"prerelease":true,"published_at":"2026-11-12T00:00:00Z"},'
                                        '{"tag_name":"v9.9.9-rc.10","draft":false,"prerelease":true,"published_at":"2026-11-11T00:00:00Z"},'
                                        '{"tag_name":"v98.0.0-rc.1","draft":false,"prerelease":true,"published_at":null}]')
    night = Home(kit, "nightly")
    check(night.install(tops["sdk"]).returncode == 0, "the install for the nightly tests")
    mock_env = {"PATH": str(mock_tools)}

    def night_update(*arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        return run([night.bin / "neura-update", "update", *arguments], env=night.env({**mock_env, **(env or {})}))

    result = night_update("--check")
    check(result.returncode == 4 and "neura 9.9.11 is available" in result.stdout, f"the default channel stays stable: {result.stdout}{result.stderr}")
    check((fixture / "requests").read_text() == "https://github.com/Kayyo321/neura/releases/latest/download/neura-components.txt\n",
          "the stable channel never asks for prereleases")
    (fixture / "requests").write_text("")
    result = night_update("--check", "--nightly")
    requests = (fixture / "requests").read_text()
    check(result.returncode == 4 and "neura 9.9.9-rc.10 is available" in result.stdout and "neura update --nightly" in result.stdout,
          f"--nightly --check: {result.stdout}{result.stderr}")
    check("per_page=20&page=2" in requests and "/download/v9.9.9-rc.10/neura-components.txt" in requests, "--nightly pages and pins its tag")
    result = night_update("--nightly", "--check", "--source", nightly)
    check(result.returncode == 4 and "neura 9.9.9-rc.10 is available" in result.stdout, "--nightly with a local source")
    night_before = snapshot(night.root)
    for mode, text in (("failure", "cannot check GitHub releases"), ("mismatch", "does not match selected GitHub tag")):
        result = night_update("--nightly", env={"NEURA_NIGHTLY_MODE": mode})
        check(result.returncode == 1 and text in result.stderr and snapshot(night.root) == night_before, f"--nightly {mode}: {result.stderr}")
    for page in ("{}", "[]", "not JSON"):
        (fixture / "page1.json").write_text(page)
        result = night_update("--check", "--nightly")
        check(result.returncode == 1 and snapshot(night.root) == night_before, f"--nightly with the release list {page!r}")
    (fixture / "page1.json").write_text(page_one)
    result = night_update("--nightly")
    check(result.returncode == 0 and f"updated neura {v} to 9.9.9-rc.10" in result.stdout and night.current().startswith("9.9.9-rc.10+"),
          f"--nightly installs the candidate: {result.stdout}{result.stderr}")
    check(not night.leftovers(), "--nightly leaves nothing behind")

    # Installs that a package manager owns, and a marker that is not valid.
    homebrew = kit.work / "homebrew"
    tree = homebrew / "versions" / base / "bin"
    tree.parent.mkdir(parents=True)
    shutil.copytree(kit.prefixes()["core"] / "bin", tree)
    shutil.copytree(kit.prefixes()["core"] / "lib", tree.parent / "lib")
    (homebrew / "neura-install.txt").write_text(f"neura-install 1\nmethod homebrew\nlink /opt/homebrew/bin/neura\nversion {v}\n")
    for arguments in (["neura-update", "update"], ["neura-update", "update", "--check"], ["neura-update", "update", "--migrate-install"],
                      ["neura-update", "update", "--rollback"], ["neura-uninstall", "uninstall"], ["neura-uninstall", "uninstall", "--yes"]):
        result = run([tree / arguments[0], *arguments[1:]], env=home.env())
        check(result.returncode == 3 and "homebrew" in result.stderr, f"case 62: {' '.join(arguments[1:])} on a homebrew install: {result.stderr}")
    check((homebrew / "neura-install.txt").exists() and (homebrew / "versions" / base).exists(), "case 62: nothing changed")
    (homebrew / "neura-install.txt").write_text("neura-install 1\nmethod homebrew\n")
    result = run([tree / "neura-update", "update"], env=home.env())
    check(result.returncode == 1 and "marker is not valid" in result.stderr, "update with a malformed marker")
    result = run([tree / "neura-doctor"], env=home.env())
    check(result.returncode == 1 and "FAIL  install" in result.stdout, "doctor with a malformed marker")

    # Case 61: a file system that cannot lock. A preloaded flock that fails
    # with ENOLCK stands in for it.
    compiler = shutil.which("cc") or shutil.which("gcc")
    if not sys.platform.startswith("linux") or compiler is None:
        print("NOT RUN: case 61 needs Linux (LD_PRELOAD) and a C compiler for the flock stand-in")
        return
    shim = kit.work / "noflock.c"
    shim.write_text("#include <errno.h>\nint flock(int fd, int operation) { (void)fd; (void)operation; errno = ENOLCK; return -1; }\n")
    library = kit.work / "libnoflock.so"
    built = run([compiler, "-shared", "-fPIC", "-o", library, shim])
    if built.returncode != 0:
        print(f"NOT RUN: case 61: the flock stand-in did not build: {built.stderr}")
        return
    nolock = Home(kit, "nolock")
    preload = {"LD_PRELOAD": str(library)}
    result = nolock.install(tops["sdk"], env=preload)
    check(result.returncode == 0, f"case 61: install without flock: {result.stderr}")
    out = run([nolock.bin / "neura-run", image], env=nolock.env(preload))
    check(out.stdout == "Hello, World!\n", f"case 61: tools run without a lease: {out.stderr}")
    nolock_base = nolock.current()
    for release in (newer, newest, third):
        result = run([nolock.bin / "neura-update", "update", "--source", release], env=nolock.env(preload))
        check(result.returncode == 0, f"case 61: update without flock: {result.stderr}")
    check(nolock_base in nolock.generations() and len(nolock.generations()) == 4, f"case 61: nothing is pruned: {nolock.generations()}")


def legacy_state(kit: Kit, home: Home, top: Path, version: str) -> None:
    """The state a 0.2 updater leaves (docs/design/Lifecycle030.md, "The 0.2
    bridge"): the whole legacy archive tree at versions/<version>, current,
    a schema 1 marker, and one link, neura. Built by hand, as the 0.2 code
    does it; the release workflow runs the real 0.2 clients."""
    root = home.root
    (root / "versions").mkdir(parents=True)
    shutil.copytree(top, root / "versions" / version)
    os.symlink(f"versions/{version}", root / "current")
    home.bin.mkdir(parents=True, exist_ok=True)
    real = root.resolve()
    os.symlink(f"{real}/current/bin/neura", home.bin / "neura")
    (root / "neura-install.txt").write_text(f"neura-install 1\nmethod archive\nlink {home.bin.resolve()}/neura\nversion {version}\n")


def suite_migrate(kit: Kit) -> None:
    """The legacy archive, the unmigrated state and --migrate-install (step 6):
    cases 15, 16 (schema 1), 49 to 58."""
    v, t = kit.version, kit.triple
    release = kit.release(kit.work / "release")
    legacy_top = unpack(release / f"neura-{v}-{t}.tar.gz", kit.work / "unpacked/legacy")
    sdk_top = unpack(release / f"neura-sdk-{v}-{t}.tar.gz", kit.work / "unpacked/sdk")
    every = set(CORE_TOOLS) | set(SDK_TOOLS)

    def update(home: Home, *arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        # Through the dispatcher, the one link a 0.2 client made.
        return home.neura("update", *arguments, env=env)

    # Case 49: after the 0.2 update.
    home = Home(kit, "bridge")
    legacy_state(kit, home, legacy_top, v)
    check(home.neura("--version").stdout == kit.line + "\n", "case 49: neura --version reports the new version")
    image = hello(kit, home)
    check(home.neura("run", image).stdout == "Hello, World!\n", "case 49: neura run")
    test = home.neura("test", "-I", REPOSITORY / "examples/guide", REPOSITORY / "examples/testing/math-tests.nra")
    check(test.returncode == 0, f"case 49: neura test: {test.stdout}{test.stderr}")
    check(not (home.root / "leases").exists(), "case 49: no leases/ directory")
    check(shutil.which("neurac", path=str(home.bin)) is None, "case 49: neurac is not on PATH")
    direct = run([home.root / "current/bin/neurac", "--version"], env=home.env())
    check(direct.returncode == 0, "case 49: the tools run directly")
    # Case 55: updates, component changes and rollback refuse.
    instruction = 'this installation was updated by a 0.2 client; run "neura update --migrate-install" first'
    before = snapshot(home.root)
    for arguments in (["--source", release], ["--check", "--source", release], ["--rollback"], ["--repair"], ["--components", "runtime"]):
        result = update(home, *arguments)
        check(result.returncode == 1 and instruction in result.stderr, f"case 55: update {arguments}: {result.stderr}")
    result = home.neura("uninstall", "--component", "sdk")
    check(result.returncode == 1 and instruction in result.stderr, f"case 55: uninstall --component: {result.stderr}")
    check(snapshot(home.root) == before, "case 55: nothing changed")
    # Case 57, and the service note's case 60: service launches and service
    # start are refused with the instruction, although the legacy archive
    # holds no neurad; a plain run works and nothing is started.
    service_instruction = 'finish the 0.3.0 install first: run "neura update --migrate-install"'
    run_dir = Path(tempfile.mkdtemp(prefix="neura-run-", dir="/tmp"))
    service_env = {"XDG_RUNTIME_DIR": str(run_dir), "NEURA_GPU": "off"}
    for arguments in (["run", "--runtime=required", image], ["run", "--runtime=auto", image], ["runtime", "start"],
                      ["runtime", "enable"]):
        result = home.neura(*arguments, env=service_env)
        check(result.returncode == 5 and service_instruction in result.stderr and result.stdout == "",
              f"case 57: {arguments} is refused with status 5: {result.returncode} {result.stdout}{result.stderr}")
    check(home.neura("run", "--runtime=off", image, env=service_env).stdout == "Hello, World!\n", "case 57: --runtime=off runs")
    check(not any(run_dir.iterdir()) and snapshot(home.root) == before, "case 57: nothing was started or changed")
    shutil.rmtree(run_dir, ignore_errors=True)
    # Case 54: a file of the generation was altered.
    altered = home.root / "versions" / v / "share/neura/stdlib/std/core/version.nra"
    original = altered.read_bytes()
    altered.write_bytes(original + b"\n")
    result = update(home, "--migrate-install")
    check(result.returncode == 1 and "expected" in result.stderr and snapshot(home.root) != before, f"case 54: {result.stderr}")
    check(home.marker().startswith("neura-install 1\n"), "case 54: still unmigrated")
    altered.write_bytes(original)
    # Case 52: a conflicting file at one tool's path.
    (home.bin / "neura-fmt").write_text("mine\n")
    result = update(home, "--migrate-install")
    check(result.returncode == 1 and "neura-fmt exists and is not a link made by neura" in result.stderr, f"case 52: {result.stderr}")
    check(home.marker().startswith("neura-install 1\n") and set(home.links()) == {"neura"}, "case 52: nothing changed")
    (home.bin / "neura-fmt").unlink()
    # Case 15: a 0.2 update runs, so the migration refuses on its lock file.
    (home.root / ".update-lock").write_text("4242\n")
    result = update(home, "--migrate-install")
    check(result.returncode == 1 and "a 0.2 update is running" in result.stderr, f"case 15: {result.stderr}")
    old = home.root / ".update-lock"
    os.utime(old, (0, 0))
    result = update(home, "--migrate-install")
    check(result.returncode == 1 and "more than an hour old" in result.stderr and old.exists(), f"case 16: a stale 0.2 lock: {result.stderr}")
    old.unlink()
    # Case 53: interrupted after some links; the next run completes it.
    result = update(home, "--migrate-install", env={"NEURA_TEST_UPDATE_CRASH": "links-add-3"})
    check(result.returncode == 70 and 1 < len(home.links()) < len(every), f"case 53: a crash after some links: {home.links()}")
    # Case 15: the 0.3 command holds the 0.2 lock file, which a 0.2 updater
    # creates with O_EXCL, while it runs; the crash left it behind.
    check((home.root / ".update-lock").read_text().startswith("neura-lifecycle "), "case 15: the 0.2 lock file is held during the change")
    check(home.neura("run", image).stdout == "Hello, World!\n", "case 53: the installation works as left")
    # Case 50: the migration.
    result = update(home, "--migrate-install")
    check(result.returncode == 0 and "completed an interrupted migrate" in result.stdout, f"case 53/50: {result.stdout}{result.stderr}")
    check(home.marker().startswith(f"neura-install 2\nmethod archive\nbin {home.bin.resolve()}\ncomponents core sdk\ngeneration {v}\n"),
          f"case 50: schema 2\n{home.marker()}")
    expect_links(home, every, "case 50")
    check((home.root / "leases" / v).is_file() and not (home.root / ".update-lock").exists(), "case 50: the lease, and no 0.2 lock")
    out = run(["neurac", "--version"], env=home.env())
    check(out.returncode == 0 and out.stdout == kit.line + "\n", "case 50: neurac --version from PATH")
    check(home.neura("doctor").returncode == 0, f"case 50: doctor exits 0\n{home.neura('doctor').stdout}")
    # Case 51: again, and on a fresh 0.3 installation.
    result = update(home, "--migrate-install")
    check(result.returncode == 0 and result.stdout.startswith("already migrated"), f"case 51: {result.stdout}")
    (home.bin / "neura-tags").unlink()
    result = update(home, "--migrate-install")
    check(result.returncode == 0 and "restored 1 links" in result.stdout and (home.bin / "neura-tags").is_symlink(), "case 51: restores a link")
    fresh = Home(kit, "fresh")
    check(fresh.install(sdk_top).returncode == 0, "a fresh 0.3 install")
    result = fresh.neura("update", "--migrate-install")
    check(result.returncode == 0 and result.stdout.startswith("already migrated"), "case 51: on a fresh 0.3 installation")
    # The legacy generation is named by its version; a later update keeps
    # the name, and rollback to it is allowed once it has manifests.
    result = run([home.bin / "neura-update", "update", "--source",
                  kit.release(kit.work / "newer", kit.variant("newer", version="9.9.9"), version="9.9.9")], env=home.env())
    check(result.returncode == 0 and v in home.generations(), f"an update after the migration keeps {v}: {result.stderr}")
    result = run([home.bin / "neura-update", "update", "--rollback"], env=home.env())
    check(result.returncode == 0 and home.current() == v, f"--rollback returns to the generation a 0.2 updater placed: {result.stderr}")

    # Case 56: uninstall of an unmigrated installation, by the schema 1 rules.
    gone = Home(kit, "uninstall-unmigrated")
    legacy_state(kit, gone, legacy_top, v)
    result = gone.neura("uninstall", "--yes")
    check(result.returncode == 0 and not gone.root.exists() and not (gone.bin / "neura").is_symlink(), f"case 56: {result.stderr}")

    # Case 58: the 0.3 installer on a 0.2 root migrates and installs; a 0.2
    # generation (no manifests) becomes the previous one, and --rollback
    # refuses to return to it (case 36).
    old_root = Home(kit, "old-root")
    legacy_state(kit, old_root, legacy_top, "0.2.0")
    shutil.rmtree(old_root.root / "versions/0.2.0/share/neura/components")
    (old_root.root / "versions/0.2.0/VERSION").write_text("0.2.0\n")
    (old_root.root / "neura-install.txt").write_text(old_root.marker().replace(f"version 0.2.0\n", "version 0.2.0\n"))
    result = old_root.install(sdk_top)
    check(result.returncode == 0, f"case 58: the 0.3 installer on a 0.2 root: {result.stderr}")
    check(old_root.marker().startswith("neura-install 2\n") and sorted(old_root.generations()) == sorted(["0.2.0", old_root.current()]),
          f"case 58: migrated, with the 0.2 generation as the previous one: {old_root.generations()}")
    expect_links(old_root, every, "case 58")
    result = run([old_root.bin / "neura-update", "update", "--rollback"], env=old_root.env())
    check(result.returncode == 1 and "no previous 0.3 generation is installed" in result.stderr, f"case 36: a 0.2 previous generation: {result.stderr}")
    # A 0.2 install.sh on a schema 2 root stops at the marker's first line.
    old_installer = run(["git", "-C", REPOSITORY, "show", "v0.2.0:packaging/install.sh"])
    if old_installer.returncode != 0:
        print("NOT RUN: case 58 (a 0.2 install.sh on a schema 2 root): the tag v0.2.0 is not in this checkout")
    else:
        old_top = kit.work / "unpacked/old-installer"
        shutil.copytree(legacy_top, old_top)
        (old_top / "install.sh").write_text(old_installer.stdout)
        before = snapshot(old_root.root)
        result = run(["sh", old_top / "install.sh"], env=old_root.env(), cwd=old_top)
        check(result.returncode != 0 and "is not a neura install marker" in result.stderr and snapshot(old_root.root) == before,
              f"case 58: a 0.2 install.sh on a schema 2 root: {result.stderr}")


def strict_snapshot(root: Path) -> dict[str, tuple]:
    """Every path below root with its digest or link target and its times."""
    entries = {}
    if not root.exists():
        return entries
    for directory, names, files in os.walk(root):
        for name in names + files:
            path = Path(directory) / name
            info = path.lstat()
            content = os.readlink(path) if path.is_symlink() else ("dir" if path.is_dir() else sha256(path))
            entries[str(path.relative_to(root))] = (content, info.st_mtime_ns, info.st_mode)
    return entries


def suite_doctor(kit: Kit) -> None:
    """neura doctor (step 8) on the states of the other cases, and cases 60,
    63 and 64."""
    v, t = kit.version, kit.triple
    release = kit.release(kit.work / "release", kit.variant("service", neurad=True))
    tops = {p: unpack(release / (f"neura-{p}-{v}-{t}.tar.gz" if p != "legacy" else f"neura-{v}-{t}.tar.gz"), kit.work / "unpacked" / p)
            for p in ("sdk", "runtime", "legacy")}
    newer = kit.release(kit.work / "newer", kit.variant("newer", version="9.9.9"), version="9.9.9")

    def doctor(home: Home) -> subprocess.CompletedProcess:
        return run([home.root / "current/bin/neura-doctor"], env=home.env())

    def lines(result: subprocess.CompletedProcess, name: str) -> list[str]:
        return [line for line in result.stdout.splitlines() if re.match(rf"(ok|warn|FAIL)  {re.escape(name)}: ", line)]

    states: dict[str, Home] = {}
    # Complete: SDK, runtime, combined.
    sdk = Home(kit, "sdk")
    check(sdk.install(tops["sdk"]).returncode == 0, "the SDK install")
    states["sdk"] = sdk
    result = doctor(sdk)
    check(result.returncode == 0, f"case 1: doctor exits 0\n{result.stdout}")
    check(lines(result, "components") == [f"ok  components: core, sdk (version {v}, build {kit.commit})"], "the components line")
    check(lines(result, "service") == ["ok  service: not installed (optional)"], "case 1: the service is not installed")
    check(lines(result, "integrity")[0].startswith("ok  ") and lines(result, "transaction") == ["ok  transaction: none interrupted"]
          and lines(result, "migration") == ["ok  migration: not needed"], "the state lines")
    check(lines(result, "generations") == [f"ok  generations: active {sdk.current()}, no previous"], "the generations line")
    check(f"state complete" in lines(result, "install")[0], "the state is named")
    check(lines(result, "links")[0].startswith(f"ok  links: {len(CORE_TOOLS) + len(SDK_TOOLS)} in "), "the links line")
    check(lines(result, "stdlib") and lines(result, "launchers"), "the SDK checks of 0.2")
    runtime = Home(kit, "runtime")
    check(runtime.install(tops["runtime"]).returncode == 0, "the runtime install")
    states["runtime"] = runtime
    result = doctor(runtime)
    check(result.returncode == 0 and lines(result, "sdk") == ["ok  sdk: not installed; source commands need Neura-sdk"]
          and not lines(result, "libclang") and not lines(result, "c-compiler") and not lines(result, "gpu"),
          f"case 2: doctor of a runtime installation\n{result.stdout}")
    check(lines(result, "service") == ["ok  service: installed, protocol 1.0; no supervisor is running; login start is not registered"],
          f"the service line: {lines(result, 'service')}")
    both = Home(kit, "both")
    check(both.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "the combined install")
    states["combined"] = both
    check(doctor(both).returncode == 0, "case 3: doctor exits 0")

    # Unmigrated (case 49).
    bridge = Home(kit, "unmigrated")
    legacy_state(kit, bridge, tops["legacy"], v)
    states["unmigrated"] = bridge
    result = run([bridge.bin / "neura", "doctor"], env=bridge.env())
    check(result.returncode == 1 and 'FAIL  migration: run "neura update --migrate-install" to finish the install' in result.stdout
          and "state unmigrated" in result.stdout, f"case 49: doctor fails on migration\n{result.stdout}")

    # Interrupted: a crash after the journal of a change.
    crashed = Home(kit, "interrupted")
    check(crashed.install(tops["sdk"]).returncode == 0, "the install for the interrupted state")
    result = run([crashed.bin / "neura-update", "update", "--source", newer], env=crashed.env({"NEURA_TEST_UPDATE_CRASH": "links-remove"}))
    check(result.returncode == 70, "a crash during an update")
    states["interrupted"] = crashed
    result = doctor(crashed)
    check(result.returncode == 1 and "FAIL  transaction: an interrupted update left" in result.stdout and "neura update --repair" in result.stdout
          and "state interrupted" in result.stdout, f"case 19: doctor reports the interrupted change\n{result.stdout}")

    # Damaged (case 60): neurac replaced by a file of another build.
    damaged = Home(kit, "damaged")
    check(damaged.install(tops["sdk"]).returncode == 0, "the install for the damaged state")
    neurac = damaged.root / "versions" / damaged.current() / "bin/neurac"
    neurac.write_bytes(neurac.read_bytes()[:-1] + bytes([neurac.read_bytes()[-1] ^ 1]))
    states["damaged"] = damaged
    result = doctor(damaged)
    check(result.returncode == 1 and lines(result, "integrity")[0].startswith("FAIL  integrity: ") and f"{neurac}: expected sha256" in result.stdout
          and "state damaged" in result.stdout, f"case 60: doctor fails on integrity\n{result.stdout}")

    # A generation kept in use beside the active and previous ones.
    kept = Home(kit, "kept")
    check(kept.install(tops["sdk"]).returncode == 0, "the install for the kept state")
    first = kept.current()
    session = Session(kept, "neura-lsp")
    for directory in (newer, kit.release(kit.work / "newest", kit.variant("newest", version="9.9.10"), version="9.9.10")):
        check(run([kept.bin / "neura-update", "update", "--source", directory], env=kept.env()).returncode == 0, "updates for the kept state")
    states["kept"] = kept
    result = doctor(kept)
    check(f"warn  generations: {first} is on disk and in use by a running neura process" in result.stdout, f"case 30: doctor shows the generation in use\n{result.stdout}")
    session.end()
    result = doctor(kept)
    check(f"warn  generations: {first} is on disk and not in use" in result.stdout, "doctor says a generation can be removed")

    # Case 61: a file system that cannot lock.
    compiler = shutil.which("cc") or shutil.which("gcc")
    if compiler and sys.platform.startswith("linux"):
        shim = kit.work / "noflock.c"
        shim.write_text("#include <errno.h>\nint flock(int fd, int operation) { (void)fd; (void)operation; errno = ENOLCK; return -1; }\n")
        library = kit.work / "libnoflock.so"
        if run([compiler, "-shared", "-fPIC", "-o", library, shim]).returncode == 0:
            result = run([sdk.bin / "neura-doctor"], env=sdk.env({"LD_PRELOAD": str(library)}))
            check("warn  leases: the file system of" in result.stdout and result.returncode == 0, f"case 61: doctor warns\n{result.stdout}")
    else:
        print("NOT RUN: case 61 in doctor: it needs Linux (LD_PRELOAD) and a C compiler for the flock stand-in")

    # Cases 63 and 64: the read-only commands change nothing under the root,
    # and without terminals print no notice and write no state.
    for name, home in states.items():
        before = strict_snapshot(home.root)
        for arguments in (["neura", "doctor"], ["neura", "--version"], ["neura", "capabilities", "--json"],
                          ["neura-doctor"], ["neura-update", "update", "--check", "--source", newer]):
            program = home.bin / arguments[0]
            if not program.exists():
                program = home.root / "current/bin" / arguments[0]
            result = run([program, *arguments[1:]], env=home.env())
            check("is available; run" not in result.stderr and "--migrate-install\" first" not in result.stderr or arguments[0] == "neura-update",
                  f"case 64: {name}: no notice from {arguments}")
        check(strict_snapshot(home.root) == before, f"case 63: {name}: the read-only commands changed files under the root")
        check(not (home.root / "update-check.txt").exists(), f"case 64: {name}: no update state")


class ServiceStandIn:
    """What the service leaves on a machine, made by hand at the paths of
    docs/design/Service030.md: a supervisor (a process that holds the
    supervisor's lock), the login unit, the state directory, and a stand-in
    systemctl and control command that log their calls."""

    def __init__(self, kit: Kit, home: Home):
        self.home = home
        self.run_dir = home.path / "run"
        self.run_dir.mkdir(mode=0o700, exist_ok=True)
        self.tools = home.path / "service-tools"
        self.tools.mkdir(exist_ok=True)
        self.log = home.path / "service.log"
        self.holders: list[subprocess.Popen] = []
        (self.tools / "systemctl").write_text(f"#!/bin/sh\necho \"systemctl $*\" >> '{self.log}'\nexit 0\n")
        (self.tools / "systemctl").chmod(0o755)
        # The control command that --terminate runs (NEURA_TEST_SERVICE_CONTROL):
        # it ends the supervisors, as `neura runtime shutdown --terminate` would.
        (self.tools / "control").write_text(f"#!/bin/sh\necho \"control $*\" >> '{self.log}'\n"
                                            f"for pid in $(cat '{home.path}/supervisors' 2> /dev/null); do kill \"$pid\" 2> /dev/null; done\n"
                                            "sleep 0.2\nexit 0\n")
        (self.tools / "control").chmod(0o755)

    def env(self) -> dict:
        return {"XDG_RUNTIME_DIR": str(self.run_dir), "PATH": f"{self.home.bin}:{self.tools}:{SYSTEM_PATH}",
                "NEURA_TEST_SERVICE_CONTROL": str(self.tools / "control")}

    def id(self) -> str:
        return hashlib.sha256(str(self.home.real_root()).encode()).hexdigest()[:16]

    def unit(self) -> Path:
        return self.home.path / ".config/systemd/user" / f"neurad-{self.id()}.service"

    def state(self) -> Path:
        return self.home.path / ".local/state/neura" / self.id()

    def register(self) -> None:
        self.unit().parent.mkdir(parents=True, exist_ok=True)
        self.unit().write_text("[Service]\n")
        (self.state() / self.home.current() / "records").mkdir(parents=True, exist_ok=True)
        (self.state() / self.home.current() / "log").write_text("")

    def supervise(self, generation: str) -> None:
        key = str(self.home.real_root()).encode() + b"\0" + generation.encode()
        directory = self.run_dir / "neura" / hashlib.sha256(key).hexdigest()[:16]
        directory.mkdir(parents=True, exist_ok=True)
        holder = subprocess.Popen([sys.executable, "-c", "import fcntl, sys, time\n"
                                   "f = open(sys.argv[1], 'w')\nfcntl.flock(f, fcntl.LOCK_EX)\nprint('held', flush=True)\ntime.sleep(600)\n",
                                   str(directory / "lock")], stdout=subprocess.PIPE, text=True)
        holder.stdout.readline()
        self.holders.append(holder)
        with open(self.home.path / "supervisors", "a") as file:
            file.write(f"{holder.pid}\n")

    def end(self) -> None:
        for holder in self.holders:
            holder.kill()
            holder.wait()


def suite_components(kit: Kit) -> None:
    """--components and uninstall --component (step 7): cases 38 to 43 and
    59. The service is a stand-in (ServiceStandIn); a real neurad belongs to
    the service milestone."""
    v, t = kit.version, kit.triple
    release = kit.release(kit.work / "release", kit.variant("service", neurad=True))
    tops = {p: unpack(release / f"neura-{p}-{v}-{t}.tar.gz", kit.work / "unpacked" / p) for p in ("sdk", "runtime")}
    every = set(CORE_TOOLS) | set(SDK_TOOLS)

    # Case 38: add the service to an SDK installation.
    home = Home(kit, "add")
    check(home.install(tops["sdk"]).returncode == 0, "the SDK install")
    service = ServiceStandIn(kit, home)
    sdk_generation = home.current()

    def update(target: Home, *arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        return run([target.bin / "neura-update", "update", *arguments], env=target.env(env))

    result = update(home, "--components", "sdk,service", "--source", release, env=service.env())
    check(result.returncode == 0 and f"neura-runtime-{v}-{t}.tar.gz" in result.stdout, f"case 38: {result.stdout}{result.stderr}")
    check(home.current() != sdk_generation and "components core sdk service" in home.marker(), "case 38: a new generation with the service")
    expect_links(home, every | {"neurad"}, "case 38: the neurad link appears")
    check(not service.unit().exists() and not service.log.exists() and not (service.run_dir / "neura").exists(),
          "case 38: nothing is started or registered")
    again = update(home, "--components", "sdk,service", "--source", release)
    check(again.returncode == 0 and "nothing to change" in again.stdout, "the same set again changes nothing")

    # Case 39: the archive comes from the installed version's own release.
    mock_tools = kit.work / "mock-tools"
    mock_tools.mkdir()
    for name in ("tar", "gzip", "cp"):
        os.symlink(shutil.which(name, path=SYSTEM_PATH), mock_tools / name)
    requests = kit.work / "requests"
    (mock_tools / "curl").write_text(f"""#!/bin/sh
while [ "$#" -gt 0 ]; do
    case "$1" in --output) shift; output=$1 ;; --url) shift; url=$1 ;; esac
    shift
done
printf '%s\\n' "$url" >> '{requests}'
case "$url" in
    'https://github.com/Kayyo321/neura/releases/download/v{v}/'*) source='{release}'/"${{url##*/}}" ;;
    *) exit 22 ;;
esac
cp "$source" "$output"
""")
    (mock_tools / "curl").chmod(0o755)
    candidate = Home(kit, "candidate")
    check(candidate.install(tops["sdk"]).returncode == 0, "the install for case 39")
    result = update(candidate, "--components", "sdk,service", env={"PATH": f"{mock_tools}:{SYSTEM_PATH}"})
    check(result.returncode == 0 and "neurad" in candidate.links(), f"case 39: from the installed version's release: {result.stderr}")
    check(requests.read_text().splitlines()[0] == f"https://github.com/Kayyo321/neura/releases/download/v{v}/neura-components.txt",
          f"case 39: the URL of the installed version: {requests.read_text()}")
    newer = kit.release(kit.work / "newer", kit.variant("newer", version="9.9.9", neurad=True), version="9.9.9")
    rebuilt = kit.release(kit.work / "rebuilt", kit.variant("rebuilt", neurad=True, change="share/neura/stdlib/std/core/version.nra"),
                          build="rebuilt")
    other = Home(kit, "other-build")
    check(other.install(tops["sdk"]).returncode == 0, "the install for case 39's refusals")
    before = snapshot(other.root)
    for source in (newer, rebuilt):
        result = update(other, "--components", "sdk,service", "--source", source)
        check(result.returncode == 1 and "Update first, then add the component" in result.stderr and snapshot(other.root) == before,
              f"case 39: a release of another build: {result.stderr}")
    check(update(other, "--components", "everything").returncode == 1, "an unknown set")

    # Case 40: remove the service with no application running.
    service.register()
    (service.log).unlink(missing_ok=True)
    result = update(home, "--components", "sdk", env=service.env())
    check(result.returncode == 0 and "neurad" not in home.links() and "components core sdk\n" in home.marker(), f"case 40: {result.stderr}")
    check(not service.unit().exists() and not service.state().exists(), "case 40: the unit and the state are removed")
    check("systemctl --user disable" in service.log.read_text(), "case 40: login start is disabled")
    image = hello(kit, home)
    check(home.neura("run", image).stdout == "Hello, World!\n", "case 40: neura run still works")

    # Case 41: the same with an application running.
    check(update(home, "--components", "sdk,service", "--source", release).returncode == 0, "the service again")
    service.supervise(home.current())
    before, links = snapshot(home.root), home.links()
    result = update(home, "--components", "sdk", env=service.env())
    check(result.returncode == 1 and "pass --terminate" in result.stderr and snapshot(home.root) == before and home.links() == links,
          f"case 41: refused while the service runs: {result.stderr}")
    result = update(home, "--components", "sdk", "--terminate", env=service.env())
    check(result.returncode == 0 and "control shutdown --terminate" in service.log.read_text() and "neurad" not in home.links(),
          f"case 41: --terminate stops the applications first: {result.stdout}{result.stderr}")
    service.end()

    # Case 42: removing the only product component.
    runtime = Home(kit, "runtime")
    check(runtime.install(tops["runtime"]).returncode == 0, "the runtime install")
    result = run([runtime.bin / "neura-uninstall", "uninstall", "--component", "service"], env=runtime.env())
    check(result.returncode == 1 and 'removing service would leave no product; use "neura uninstall"' in result.stderr, f"case 42: {result.stderr}")
    sdk_only = Home(kit, "sdk-only")
    check(sdk_only.install(tops["sdk"]).returncode == 0, "the SDK install for case 42")
    result = run([sdk_only.bin / "neura-uninstall", "uninstall", "--component", "sdk"], env=sdk_only.env())
    check(result.returncode == 1 and 'removing sdk would leave no product' in result.stderr, f"case 42: {result.stderr}")
    result = update(sdk_only, "--components", "runtime", "--source", release)
    check(result.returncode == 0 and "components core service" in sdk_only.marker(), "an SDK installation becomes a runtime one")
    check(run([sdk_only.bin / "neura-uninstall", "uninstall", "--component", "bogus"], env=sdk_only.env()).returncode == 1, "an unknown component")

    # Case 43: remove the SDK while a long command of it runs.
    both = Home(kit, "both")
    check(both.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "the combined install")
    first = both.current()
    busy = Session(both, "neura-lsp")
    result = run([both.bin / "neura-uninstall", "uninstall", "--component", "sdk"], env=both.env())
    check(result.returncode == 0 and "neurac" not in both.links() and "components core service" in both.marker(), f"case 43: {result.stderr}")
    check(busy.alive() and lease_held(both, first) and first in both.generations(), "case 43: the running tool keeps its generation")
    build = both.neura("build", REPOSITORY / "examples/hello-world/hello-world.nra", "-o", both.path / "x.nvr")
    check(build.returncode == 1 and "needs Neura-sdk" in build.stderr, f"case 43: new commands find no neurac: {build.stderr}")
    busy.end()
    check(both.neura("run", image).stdout == "Hello, World!\n", "case 43: the runtime still runs images")

    # Case 59: a tool copied out of its generation runs alone.
    copy = kit.work / "copied"
    (copy / "bin").mkdir(parents=True)
    shutil.copytree(home.root / "versions" / home.current() / "lib", copy / "lib")
    for name in ("neura", "neura-update"):
        shutil.copy(home.root / "versions" / home.current() / "bin" / name, copy / "bin" / name)
    result = run([copy / "bin/neura", "update", "--source", release], env=home.env())
    check(result.returncode == 3 and "not an archive install" in result.stderr, f"case 59: unmanaged: {result.stderr}")
    result = run([copy / "bin/neura", "build", "x.nra"], env=home.env())
    check(result.returncode == 1 and f"needs neurac, which is not in {copy / 'bin'}" in result.stderr, f"case 59: no sibling: {result.stderr}")


def suite_uninstall(kit: Kit) -> None:
    """neura uninstall (step 7): cases 44 to 48, and the rules of the 0.2
    uninstall tests that still hold."""
    v, t = kit.version, kit.triple
    release = kit.release(kit.work / "release", kit.variant("service", neurad=True))
    tops = {p: unpack(release / f"neura-{p}-{v}-{t}.tar.gz", kit.work / "unpacked" / p) for p in ("sdk", "runtime")}

    def uninstall(home: Home, *arguments, env: dict | None = None, program: Path | None = None) -> subprocess.CompletedProcess:
        return run([program or home.bin / "neura-uninstall", "uninstall", *arguments], env=home.env(env))

    # Cases 44 to 46 and 48 on a combined installation with login start.
    home = Home(kit, "combined")
    check(home.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "the combined install")
    service = ServiceStandIn(kit, home)
    service.register()
    second = Home(kit, "second")
    check(second.install(tops["runtime"]).returncode == 0, "a second installation")
    second_service = ServiceStandIn(kit, second)
    second_service.register()
    second_service.supervise(second.current())
    (home.root / "keep.txt").write_text("not neura's\n")
    (home.bin / "other-tool").write_text("not neura's\n")
    (home.root / "update-check.txt").write_text("neura-update-check 1\nchecked 1\nlatest 0.1.0\n")
    (home.root / ".staging-1-old").mkdir()
    service.supervise(home.current())
    before, bin_before = snapshot(home.root), snapshot(home.bin)
    # Case 44: --dry-run.
    result = uninstall(home, "--dry-run", env=service.env())
    out = result.stdout
    check(result.returncode == 0 and f"would remove {home.real_root()}/versions/{home.current()}" in out
          and f"would remove {home.bin.resolve()}/neurac" in out and f"would remove {service.unit()} (the service's)" in out
          and f"would remove {service.state()} (the service's)" in out and "a supervisor of this installation is running" in out
          and "keep.txt" in out and "nothing was removed (--dry-run)" in out, f"case 44: --dry-run\n{out}")
    check(snapshot(home.root) == before and snapshot(home.bin) == bin_before and service.unit().exists(), "case 44: nothing removed")
    # Without --yes and without a terminal, nothing is removed.
    result = uninstall(home, env=service.env())
    check(result.returncode == 1 and snapshot(home.root) == before, "a running service refuses before the prompt")
    result = uninstall(home, "--yes", env=service.env())
    check(result.returncode == 1 and "pass --terminate" in result.stderr and snapshot(home.root) == before, f"refused while the service runs: {result.stderr}")
    service.end()
    result = uninstall(home, env=service.env())
    check(result.returncode == 1 and "pass --yes" in result.stderr and snapshot(home.root) == before, "uninstall without --yes")
    check(uninstall(home, "--everything").returncode == 1, "an unknown option")
    # Case 45: an editor session holds the generation.
    session = Session(home, "neura-lsp")
    result = uninstall(home, "--yes", env=service.env())
    check(result.returncode == 1 and f"generation {home.current()} is in use by a running neura process" in result.stderr
          and snapshot(home.root) == before, f"case 45: {result.stderr}")
    session.end()
    # Case 46: the full uninstall.
    result = uninstall(home, "--yes", env=service.env())
    check(result.returncode == 0, f"case 46: {result.stderr}")
    check(not home.links() and (home.bin / "other-tool").read_text() == "not neura's\n", "case 46: every link, and only the links")
    check(not service.unit().exists() and not service.state().exists(), "case 46: the unit and the state directory")
    check(sorted(p.name for p in home.root.iterdir()) == ["keep.txt"] and f"left {home.real_root()}, which still holds" in result.stdout
          and "keep.txt" in result.stdout, f"case 46: the user's files are left and listed: {list(home.root.iterdir())}")
    # Case 48: the other installation is untouched.
    check(second_service.unit().exists() and second_service.state().exists() and second.neura("--version").returncode == 0,
          "case 48: another installation's unit, state and supervisor")
    check(run([sys.executable, "-c", "import fcntl,sys\nf=open(sys.argv[1])\ntry:\n fcntl.flock(f, fcntl.LOCK_EX|fcntl.LOCK_NB)\nexcept OSError:\n sys.exit(0)\nsys.exit(1)",
               str(next((second_service.run_dir / "neura").iterdir()) / "lock")]).returncode == 0, "case 48: its supervisor still holds its lock")
    second_service.end()

    # Case 47: one owned link replaced by a regular file.
    replaced = Home(kit, "replaced")
    check(replaced.install(tops["sdk"]).returncode == 0, "the install for case 47")
    (replaced.bin / "neura-fmt").unlink()
    (replaced.bin / "neura-fmt").write_text("a program of the user\n")
    result = uninstall(replaced, "--yes")
    check(result.returncode == 0 and f"left {replaced.bin.resolve()}/neura-fmt: it is not a symbolic link" in result.stdout,
          f"case 47: {result.stdout}{result.stderr}")
    check((replaced.bin / "neura-fmt").read_text() == "a program of the user\n" and not replaced.links() and not replaced.root.exists(),
          "case 47: the file is left; the rest is removed")

    # A link elsewhere, a dangling current, and a link inside the root that
    # points outside it.
    elsewhere = Home(kit, "elsewhere")
    check(elsewhere.install(tops["sdk"]).returncode == 0, "the install for a link elsewhere")
    target = elsewhere.path / "elsewhere.txt"
    target.write_text("elsewhere\n")
    (elsewhere.bin / "neura").unlink()
    os.symlink(target, elsewhere.bin / "neura")
    result = uninstall(elsewhere, "--yes")
    check(result.returncode == 0 and os.readlink(elsewhere.bin / "neura") == str(target) and target.exists(), "a link elsewhere is left")
    dangling = Home(kit, "dangling")
    check(dangling.install(tops["sdk"]).returncode == 0, "the install for a dangling current")
    generation = dangling.current()
    (dangling.root / "current").unlink()
    result = uninstall(dangling, "--yes", program=dangling.root / "versions" / generation / "bin/neura-uninstall")
    check(result.returncode == 0 and not dangling.links() and not dangling.root.exists(), f"a dangling current: {result.stderr}")
    escape = Home(kit, "escape")
    check(escape.install(tops["sdk"]).returncode == 0, "the install for an escaping link")
    outside = escape.path / "outside"
    outside.mkdir()
    (outside / "precious").write_text("do not delete\n")
    os.symlink(outside, escape.root / "versions" / escape.current() / "share/outside-link")
    result = uninstall(escape, "--yes")
    check(result.returncode == 0 and (outside / "precious").read_text() == "do not delete\n", "a link inside the root is not followed")


def suite_service(kit: Kit) -> None:
    """The service across a change (step 9), with the stand-in service:
    cases 32 and 33, retire on update, and an application across a rollback.
    An application is a process that holds its generation's lease, as a
    worker does (Service030.md); its supervisor holds the supervisor's lock.
    Cases 30 and 31 (editor sessions) are in lifecycle-update, 40 and 41 in
    lifecycle-components. What needs the real neurad (ps and stop from the
    new generation, the old supervisor's exit) belongs to the service
    milestone's tests."""
    v, t = kit.version, kit.triple
    release = kit.release(kit.work / "release", kit.variant("service", neurad=True))
    tops = {p: unpack(release / f"neura-{p}-{v}-{t}.tar.gz", kit.work / "unpacked" / p) for p in ("sdk", "runtime")}
    newer = kit.release(kit.work / "newer", kit.variant("newer", version="9.9.9", neurad=True), version="9.9.9")
    newest = kit.release(kit.work / "newest", kit.variant("newest", version="9.9.10", neurad=True), version="9.9.10")

    home = Home(kit, "service")
    check(home.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "the combined install")
    service = ServiceStandIn(kit, home)
    first = home.current()
    service.supervise(first)
    application = subprocess.Popen([sys.executable, "-c", "import fcntl, sys, time\nf = open(sys.argv[1])\n"
                                    "fcntl.flock(f, fcntl.LOCK_SH)\nprint('running', flush=True)\ntime.sleep(600)\n",
                                    str(home.root / "leases" / first)], stdout=subprocess.PIPE, text=True)
    application.stdout.readline()

    def update(*arguments, env: dict | None = None) -> subprocess.CompletedProcess:
        return run([home.bin / "neura-update", "update", *arguments], env=home.env({**service.env(), **(env or {})}))

    # Case 33: an update rolled back at step 10 leaves the application alone.
    result = update("--source", newer, env={"NEURA_TEST_UPDATE_FAULT": "links-add"})
    check(result.returncode == 1 and "rolled back" in result.stderr and home.current() == first, f"case 33: {result.stderr}")
    check(application.poll() is None and lease_held(home, first), "case 33: the application runs on")
    check(not service.log.exists() or "retire" not in service.log.read_text(), "case 33: nothing is retired")
    # Case 32: an update while an application runs.
    result = update("--source", newer)
    check(result.returncode == 0 and home.current() != first, f"case 32: {result.stderr}")
    check(application.poll() is None and lease_held(home, first) and first in home.generations(), "case 32: the application continues on its generation")
    check(f"retire --root {home.real_root()} --generation {first}" in service.log.read_text(), f"the old supervisor is told it is retired: {service.log.read_text()}")
    expect_links(home, set(CORE_TOOLS) | set(SDK_TOOLS) | {"neurad"}, "case 32: the links reach the new generation")
    # Two updates later the application's generation is still kept, and
    # the first change after it ends removes it.
    check(update("--source", newest).returncode == 0 and first in home.generations(), "the application's generation is kept while it runs")
    result = run([home.bin / "neura-doctor"], env=home.env(service.env()))
    check(f"warn  generations: {first} is on disk and in use" in result.stdout, "doctor shows it in use")
    application.kill()
    application.wait()
    service.end()
    result = update("--repair")
    check(result.returncode == 0 and first not in home.generations(), f"removed after the application ends: {home.generations()}")

    # An application across a rollback.
    worker_generation = home.current()
    application = subprocess.Popen([sys.executable, "-c", "import fcntl, sys, time\nf = open(sys.argv[1])\n"
                                    "fcntl.flock(f, fcntl.LOCK_SH)\nprint('running', flush=True)\ntime.sleep(600)\n",
                                    str(home.root / "leases" / worker_generation)], stdout=subprocess.PIPE, text=True)
    application.stdout.readline()
    result = update("--rollback")
    check(result.returncode == 0 and home.current() != worker_generation and application.poll() is None and lease_held(home, worker_generation),
          f"a rollback leaves the application alone: {result.stderr}")
    application.kill()
    application.wait()


class RealService:
    """The real neurad of an installation: its run directory (short, so
    that the socket path fits), its state, and the processes that hold a
    generation's lease, read from /proc/locks."""

    created: list[Path] = []

    def __init__(self, home: Home):
        self.home = home
        self.run_dir = Path(tempfile.mkdtemp(prefix="neura-run-", dir="/tmp"))
        RealService.created.append(self.run_dir)

    def env(self) -> dict:
        # The service refuses a launch that may use a graph device.
        return {"XDG_RUNTIME_DIR": str(self.run_dir), "NEURA_GPU": "off"}

    def id(self) -> str:
        return hashlib.sha256(str(self.home.real_root()).encode()).hexdigest()[:16]

    def log(self, generation: str) -> str:
        path = self.home.path / ".local/state/neura" / self.id() / generation / "log"
        return path.read_text() if path.exists() else ""

    def holders(self, generation: str) -> dict[int, list[str]]:
        """The processes that hold a lock on the generation's lease, with
        their command lines."""
        inode = os.stat(self.home.root / "leases" / generation).st_ino
        holders = {}
        for line in Path("/proc/locks").read_text().splitlines():
            fields = line.split()
            if len(fields) < 6 or fields[1] != "FLOCK":
                continue
            if int(fields[5].split(":")[2]) != inode:
                continue
            pid = int(fields[4])
            try:
                holders[pid] = Path(f"/proc/{pid}/cmdline").read_bytes().decode(errors="replace").split("\0")
            except OSError:
                pass
        return holders

    def supervisor_running(self, generation: str) -> bool:
        return any("--foreground" in command for command in self.holders(generation).values())

    def end(self) -> None:
        shutil.rmtree(self.run_dir, ignore_errors=True)


def wait_for(condition, seconds: float = 20) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return condition()


def end_installed_processes(kit: Kit) -> None:
    """Kills every process that runs an executable below the work
    directory, such as a supervisor a failed check left behind, and removes
    the run directories of RealService."""
    work = str(kit.work.resolve()) + "/"
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if os.path.realpath(entry / "exe").startswith(work):
                os.kill(int(entry.name), 9)
        except OSError:
            pass
    for directory in RealService.created:
        shutil.rmtree(directory, ignore_errors=True)


def suite_neurad(kit: Kit) -> None:
    try:
        neurad_checks(kit)
    finally:
        if sys.platform.startswith("linux"):
            end_installed_processes(kit)


def neurad_checks(kit: Kit) -> None:
    """The real neurad across a change: a supervisor and an application of
    the installed generation while `neura update` moves to a stand-in
    release. The update sends Retire through the service's control client;
    the application keeps its generation, whose lease its worker and
    supervisor hold, and the retired supervisor exits once the application
    ends, after which the generation is removable. Then uninstall against
    the real service: refused while an application runs, an idle
    supervisor stopped, --terminate."""
    if not sys.platform.startswith("linux") or not kit.has_neurad():
        print("NOT RUN: the real neurad across a change: it needs Linux and a build with neurad")
        return
    v, t = kit.version, kit.triple
    release = kit.release(kit.work / "release")
    tops = {p: unpack(release / f"neura-{p}-{v}-{t}.tar.gz", kit.work / "unpacked" / p) for p in ("sdk", "runtime")}
    newer = kit.release(kit.work / "newer", kit.variant("newer", version="9.9.9"), version="9.9.9")
    newest = kit.release(kit.work / "newest", kit.variant("newest", version="9.9.10"), version="9.9.10")
    echo = REPOSITORY / "tests/service/programs/echo.nra"

    home = Home(kit, "neurad")
    check(home.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "the combined install with the real neurad")
    first = home.current()
    check(re.fullmatch(re.escape(v) + r"\+[0-9a-f]{12}", first) is not None, f"the generation is named <version>+<id>: {first}")
    service = RealService(home)
    env = home.env(service.env())
    image = home.path / "echo.nvr"
    result = home.neura("build", echo, "-o", image)
    check(result.returncode == 0, f"the echo image builds: {result.stderr}")

    def start_application(neura: Path) -> subprocess.Popen:
        application = subprocess.Popen([neura, "run", "--runtime=required", image], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd=home.path)
        application.stdin.write(b"one\n")
        application.stdin.flush()
        check(application.stdout.readline() == b"[one]\n", "the application runs through the service")
        return application

    def ps(generation: str) -> list[dict]:
        result = home.tool(generation, "neura", "runtime", "ps", "--json", env=service.env())
        check(result.returncode == 0, f"runtime ps: {result.stderr}")
        return json.loads(result.stdout)["applications"] if result.returncode == 0 else []

    application = start_application(home.bin / "neura")
    holders = service.holders(first)
    check(any("--foreground" in c for c in holders.values()) and any("--worker" in c for c in holders.values()),
          f"the supervisor and the application's worker hold the generation's lease: {holders}")
    workers = [pid for pid, command in holders.items() if "--worker" in command]
    check(all(os.path.realpath(f"/proc/{pid}/exe").startswith(str(home.real_root() / "versions" / first)) for pid in workers),
          "the worker runs the generation's neurad")
    running = ps(first)
    check(len(running) == 1 and running[0]["id"].startswith(first + "."), f"ps lists the application of {first}: {running}")

    # The control client answers for the service: uninstall is refused while
    # the application runs, and the count is the service's.
    result = run([home.bin / "neura-uninstall", "uninstall", "--yes"], env=env)
    check(result.returncode == 1 and "1 applications of this installation are running" in result.stderr
          and home.current() == first, f"uninstall refused while an application runs: {result.stderr}")

    # The update: Retire, and the application keeps its generation.
    result = run([home.bin / "neura-update", "update", "--source", newer], env=env)
    second = home.current()
    check(result.returncode == 0 and second != first, f"the update while the application runs: {result.stderr}")
    check(wait_for(lambda: "retired by an update" in service.log(first), 5), f"the old supervisor got Retire: {service.log(first)}")
    application.stdin.write(b"two\n")
    application.stdin.flush()
    check(application.stdout.readline() == b"[two]\n" and application.poll() is None, "the application runs on")
    check(first in home.generations() and service.supervisor_running(first), "its generation and supervisor stay")
    check([a["id"] for a in ps(first)] == [r["id"] for r in running], "ps from the old generation still lists it")
    status = home.tool(first, "neura", "runtime", "status", "--json", env=service.env())
    supervisors = {s["generation"]: s for s in json.loads(status.stdout)["supervisors"]} if status.returncode == 0 else {}
    check(supervisors.get(first, {}).get("state") == "retiring", f"status says the supervisor is retiring: {status.stdout}")
    # From the new generation (its neura-run is the build's; only its neura
    # is a stand-in): ps lists the old application, and a new launch goes to
    # a supervisor of the new generation.
    new_run = home.real_root() / "versions" / second / "bin/neura-run"
    result = run([new_run, "runtime", "ps", "--json"], env=env)
    listed = json.loads(result.stdout)["applications"] if result.returncode == 0 else []
    check([a["id"] for a in listed] == [r["id"] for r in running], f"ps from the new generation lists the old application: {result.stdout}{result.stderr}")
    launch = subprocess.run([new_run, "run", "--runtime=required", image], input=b"new\n", capture_output=True, env=env,
                            cwd=home.path, timeout=60)
    check(launch.returncode == 0 and launch.stdout == b"[new]\nend\n", f"a launch from the new generation: {launch.stderr}")
    result = run([new_run, "runtime", "ps", "--all", "--json"], env=env)
    ids = [a["id"] for a in json.loads(result.stdout)["applications"]] if result.returncode == 0 else []
    check(any(i.startswith(second + ".") for i in ids), f"it ran on a supervisor of the new generation: {ids}")
    # A second update keeps the application's generation, which is in use.
    result = run([home.bin / "neura-update", "update", "--source", newest], env=env)
    check(result.returncode == 0 and first in home.generations(), f"the generation in use is kept: {result.stderr}")

    # The application ends; the retired supervisor exits with it, and the
    # generation is then removed.
    application.stdin.close()
    check(application.stdout.read() == b"end\n" and application.wait(30) == 0, "the application ends normally")
    check(wait_for(lambda: not lease_held(home, first)), f"the retired supervisor exits: {service.holders(first)}")
    check("exiting" in service.log(first), "its log says it exited")
    check(wait_for(lambda: not service.supervisor_running(second)), "the second generation's supervisor, retired too, exits")
    result = run([home.bin / "neura-update", "update", "--repair"], env=env)
    check(result.returncode == 0 and first not in home.generations(), f"the generation is removed afterwards: {result.stderr}")
    service.end()

    # Uninstall against the real service.
    other = Home(kit, "neurad-uninstall")
    check(other.install(tops["runtime"]).returncode == 0, "a runtime install")
    service = RealService(other)
    env = other.env(service.env())
    generation = other.current()
    # An idle supervisor is stopped, not refused.
    result = run([other.bin / "neura", "runtime", "start"], env=env)
    check(result.returncode == 0 and service.supervisor_running(generation), f"runtime start: {result.stderr}")
    result = run([other.bin / "neura-uninstall", "uninstall", "--dry-run"], env=env)
    check(result.returncode == 0 and "would stop the service's supervisor, which runs no application" in result.stdout
          and service.supervisor_running(generation), f"--dry-run names the idle supervisor and leaves it: {result.stdout}")
    result = run([other.bin / "neura-uninstall", "uninstall"], env=env)
    check(result.returncode == 1 and "pass --yes" in result.stderr and service.supervisor_running(generation) and other.root.exists(),
          f"an uninstall that is not confirmed leaves the idle supervisor running: {result.stdout}{result.stderr}")
    reinstall = Home(kit, "neurad-terminate")
    check(reinstall.install(tops["sdk"], "--add", tops["runtime"]).returncode == 0, "a combined install for --terminate")
    result = run([other.bin / "neura-uninstall", "uninstall", "--yes"], env=env)
    check(result.returncode == 0 and "stopping the service's supervisor, which runs no application" in result.stdout
          and not other.root.exists() and not other.links(), f"uninstall stops an idle supervisor: {result.stdout}{result.stderr}")
    service.end()
    # --terminate stops a running application.
    home, service = reinstall, RealService(reinstall)
    env = home.env(service.env())
    image = home.path / "echo.nvr"
    check(home.neura("build", echo, "-o", image).returncode == 0, "the echo image builds")
    application = start_application(home.bin / "neura")
    stopped = start_application(home.bin / "neura")
    identifier = next(a["id"] for a in ps(home.current()) if a["state"] == "running" and a["id"].endswith(".2"))
    result = run([home.bin / "neura", "runtime", "stop", identifier], env=env)
    check(result.returncode == 0 and "stopped" in result.stdout and stopped.wait(30) != 0, f"runtime stop reaches an application: {result.stdout}{result.stderr}")
    result = run([home.bin / "neura-uninstall", "uninstall", "--yes", "--terminate"], env=env)
    check(result.returncode == 0 and not home.root.exists(), f"uninstall --terminate: {result.stdout}{result.stderr}")
    check(application.wait(30) != 0, "the application was terminated")
    service.end()


SUITES = {
    "archives": suite_archives,
    "install": suite_install,
    "bootstrap": suite_bootstrap,
    "update": suite_update,
    "migrate": suite_migrate,
    "doctor": suite_doctor,
    "components": suite_components,
    "uninstall": suite_uninstall,
    "service": suite_service,
    "neurad": suite_neurad,
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
