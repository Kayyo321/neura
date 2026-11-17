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
    if compiler is None:
        print("NOT RUN: case 61 needs a C compiler for the flock stand-in")
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


SUITES = {
    "archives": suite_archives,
    "install": suite_install,
    "bootstrap": suite_bootstrap,
    "update": suite_update,
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
