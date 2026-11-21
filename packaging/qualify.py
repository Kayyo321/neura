#!/usr/bin/env python3
"""Runs the release qualification checks on the two archives of a Windows target.

    qualify.py --sdk-archive FILE --runtime-archive FILE --version VERSION
               --triple TRIPLE --examples DIR
               [--previous FILE] [--require-previous]
               [--size-reference-sdk FILE] [--size-reference-runtime FILE]
               [--server-components] [--openssl FILE] [--curl FILE]

  --sdk-archive             neura-sdk-<version>-<triple>.tar.gz
  --runtime-archive         neura-runtime-<version>-<triple>.tar.gz
  --version                 the full version the archives must report
  --triple                  the target triple they must report, such as
                            x86_64-windows-msvc
  --examples                the repository's examples directory
  --previous                a Windows SDK archive of the previous release, for
                            the upgrade check
  --require-previous        fail when --previous is not given
  --size-reference-sdk      the x86_64-linux-gnu SDK archive of the same build
  --size-reference-runtime  the x86_64-linux-gnu runtime archive of the same
                            build: each Windows archive may be at most 125% of
                            the Linux archive of its product (W35). Without
                            both references, check 14 is NOT RUN
  --server-components       the archives are built with the server components
                            (TLS, HTTP/2, PostgreSQL): their licence texts must
                            be in share/doc/neura/third-party, and the installed
                            server host must serve HTTPS with HTTP/2 and h2c and
                            have its PostgreSQL provider (check 15)
  --openssl                 the openssl.exe of the server-component build, which
                            makes check 15's certificate (default: PATH)
  --curl                    a curl with HTTP/2 for check 15, such as Git for
                            Windows' (default: PATH); System32's curl.exe may
                            have no HTTP/2

This is the Windows form of packaging/qualify.sh, which stays the POSIX
implementation (docs/design/Windows030.md, W35). The checks are numbered as
there. Check 9 needs a previous Windows release, which the first Windows
release (0.3.0) does not have, and check 13, the 0.2 bridge, never applies:
no 0.2 client runs on Windows. Both report NOT RUN then.

Every install goes into a new temporary profile: USERPROFILE, LOCALAPPDATA,
TEMP and NEURA_INSTALL_ROOT name directories below it, PATH holds only the
installation's <root>\\current\\bin and the System32 directories, and
install.ps1 runs with -NoPathUpdate, so neither a development checkout nor
this user's own installation and Path take part or change.

Each check prints one line: "PASS n name", "FAIL n name: reason", or
"NOT RUN n name: reason". The exit status is 0 only when no check failed
and, with --require-previous, check 9 ran. A release qualification passes
both size references, so that check 14 runs. It needs Python 3.12 or later.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

# The tools of each component (docs/design/Products030.md), as
# src/cli/tool_registry.cpp lists them.
CORE_TOOLS = ["neura-run", "neura-inspect", "neura-doctor", "neura-update", "neura-uninstall", "neura-server-host"]
SDK_TOOLS = ["neurac", "neura-test", "neura-doc", "neura-cbind", "neura-decompile", "neura-asm", "neura-disasm", "neura-explain",
             "neura-tags", "neura-debug", "neura-lsp", "neura-fmt", "neura-new", "neura-package", "neura-dev"]
# W35: a Windows archive is at most 125% of the same build's Linux archive.
GROWTH_PERCENT = 125
FILE_ATTRIBUTE_REPARSE_POINT = 0x400
IO_REPARSE_TAG_MOUNT_POINT = 0xA0000003
TIMEOUT = 900


class Qualification:
    def __init__(self, arguments: argparse.Namespace, work: Path):
        self.args = arguments
        self.work = work
        self.failed = 0
        self.not_run = 0
        self.system_root = os.environ.get("SystemRoot", r"C:\Windows")

    # Reporting -----------------------------------------------------------

    def report(self, number, name: str, reason: str | None) -> None:
        if reason:
            print(f"FAIL {number} {name}: {' '.join(reason.split())}", flush=True)
            self.failed += 1
        else:
            print(f"PASS {number} {name}", flush=True)

    def skip(self, number, name: str, reason: str) -> None:
        print(f"NOT RUN {number} {name}: {reason}", flush=True)
        self.not_run += 1

    # Profiles ------------------------------------------------------------

    def profile(self, name: str) -> Path:
        """A new user profile with an install root of its own."""
        home = self.work / name
        for directory in ("AppData/Local", "AppData/Roaming", "Temp"):
            (home / directory).mkdir(parents=True, exist_ok=True)
        return home

    @staticmethod
    def root(home: Path) -> Path:
        return home / "AppData" / "Local" / "Neura"

    def env(self, home: Path, extra: dict | None = None) -> dict:
        """The environment of a clean machine for `home`."""
        env = {name: os.environ[name] for name in ("SystemDrive", "ProgramData", "ProgramFiles", "ProgramFiles(x86)",
                                                    "CommonProgramFiles", "PROCESSOR_ARCHITECTURE", "NUMBER_OF_PROCESSORS",
                                                    "OS", "COMPUTERNAME", "USERNAME", "USERDOMAIN") if name in os.environ}
        env.update({
            "SystemRoot": self.system_root,
            "windir": self.system_root,
            "USERPROFILE": str(home),
            "HOME": str(home),
            "LOCALAPPDATA": str(home / "AppData" / "Local"),
            "APPDATA": str(home / "AppData" / "Roaming"),
            "TEMP": str(home / "Temp"),
            "TMP": str(home / "Temp"),
            "NEURA_INSTALL_ROOT": str(self.root(home)),
            "PATH": f"{self.root(home) / 'current' / 'bin'};{self.system_root}\\System32;{self.system_root}",
            "PATHEXT": ".COM;.EXE",
        })
        env.update(extra or {})
        return env

    def run(self, home: Path, arguments: list, cwd: Path | None = None, extra: dict | None = None,
            timeout: int = TIMEOUT) -> subprocess.CompletedProcess:
        """Runs a command of the installation in the clean environment. A
        bare tool name is found in <root>/current/bin, as PATH finds it."""
        program = str(arguments[0])
        if os.sep not in program and "/" not in program:
            program = str(self.root(home) / "current" / "bin" / (program + ".exe"))
        try:
            return subprocess.run([program, *[str(a) for a in arguments[1:]]], env=self.env(home, extra), cwd=cwd or home,
                                  stdin=subprocess.DEVNULL, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                  timeout=timeout)
        except (OSError, subprocess.TimeoutExpired) as error:
            return subprocess.CompletedProcess(arguments, 1, "", f"{program}: {error}")

    def powershell(self) -> str:
        return str(Path(self.system_root) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe")

    def install(self, home: Path, archive: Path, add: Path | None = None, front: str = "install.ps1") -> tuple[bool, str]:
        """Unpacks the archives below `home` and runs the first one's
        installer, with -Add for the second and -NoPathUpdate."""
        unpack = home / "unpack"
        try:
            top = extract(archive, unpack)
            arguments = ["-NoPathUpdate"]
            if add is not None:
                arguments = ["-Add", str(extract(add, unpack)), *arguments]
        except subprocess.CalledProcessError as error:
            return False, f"tar.exe could not unpack the archive: {tail((error.stderr or b'').decode('utf-8', errors='replace'))}"
        except (OSError, subprocess.SubprocessError) as error:
            return False, f"tar.exe could not unpack the archive: {error}"
        if front == "install.cmd":
            command = [str(Path(self.system_root) / "System32" / "cmd.exe"), "/d", "/c", str(top / "install.cmd"), *arguments]
        else:
            command = [self.powershell(), "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                       str(top / "install.ps1"), *arguments]
        try:
            result = subprocess.run(command, env=self.env(home), cwd=top, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=TIMEOUT)
        except (OSError, subprocess.SubprocessError) as error:
            return False, f"{front} did not finish: {error}"
        log = result.stdout + result.stderr
        (home / "install.log").write_text(log, encoding="utf-8")
        return result.returncode == 0, tail(log)

    def marker(self, home: Path) -> str:
        try:
            return (self.root(home) / "neura-install.txt").read_text(encoding="utf-8")
        except OSError:
            return ""

    def layout_problems(self, home: Path, tools: list[str], neurad: bool) -> list[str]:
        """The Windows layout (W22): a junction current, the marker naming
        <root>/current/bin with no links, and the tools in that directory."""
        problems = []
        root = self.root(home)
        current = root / "current"
        try:
            info = os.lstat(current)
            if not info.st_file_attributes & FILE_ATTRIBUTE_REPARSE_POINT or info.st_reparse_tag != IO_REPARSE_TAG_MOUNT_POINT:
                problems.append("current is not a junction")
            else:
                # readlink gives the junction's substitute name, \??\C:\...,
                # as \\?\C:\..., which resolve() keeps.
                target = Path(os.readlink(current).removeprefix("\\\\?\\"))
                if target.parent.resolve() != (root / "versions").resolve() or not target.is_dir():
                    problems.append(f"current names {target}, not a generation in versions")
        except OSError as error:
            problems.append(f"current: {error}")
        marker = self.marker(home)
        if not marker.startswith("neura-install 2\n"):
            problems.append("no schema 2 marker")
        expected = f"bin {root.resolve().as_posix()}/current/bin"
        if expected not in marker.splitlines():
            problems.append(f"the marker does not name {expected}")
        if any(line.startswith("link ") for line in marker.splitlines()):
            problems.append("the marker lists links")
        for tool in tools:
            if not (current / "bin" / f"{tool}.exe").is_file():
                problems.append(f"no {tool}.exe in current/bin")
        if (current / "bin" / "neurad.exe").exists() != neurad:
            problems.append("neurad.exe is in current/bin" if not neurad else "no neurad.exe in current/bin")
        return problems

    def wait_removed(self, path: Path, seconds: float = 60) -> bool:
        """The removal helper of uninstall finishes after the command ends."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if not path.exists():
                return True
            time.sleep(0.25)
        return not path.exists()

    # The checks -------------------------------------------------------------

    def check_archives(self) -> None:
        version, triple = self.args.version, self.args.triple
        names = {"sdk": f"neura-sdk-{version}-{triple}", "runtime": f"neura-runtime-{version}-{triple}"}
        trees = {}
        reasons = []
        for product, archive in (("sdk", self.args.sdk_archive), ("runtime", self.args.runtime_archive)):
            listing = self.work / f"list-{product}"
            try:
                with tarfile.open(archive, "r:gz") as tar:
                    members = tar.getmembers()
                    if any(not (m.isreg() or m.isdir()) for m in members):
                        reasons.append(f"the {product} archive holds a member that is not a regular file or a directory;")
                    tops = {m.name.split("/")[0] for m in members}
                    if tops != {names[product]}:
                        reasons.append(f"expected one directory {names[product]} in the {product} archive;")
                        continue
                    for member in members:
                        mode = member.mode & 0o777
                        wanted = 0o755 if member.isdir() or member.name.endswith((".exe", ".dll")) else 0o644
                        if mode != wanted:
                            reasons.append(f"{member.name} has mode {mode:o}, not {wanted:o};")
                            break
                    tar.extractall(listing, filter="data")
                trees[product] = listing / names[product]
            except (OSError, tarfile.TarError) as error:
                reasons.append(f"the {product} archive does not unpack: {error};")
        if len(trees) == 2:
            top = trees["sdk"]
            for path in ("bin/neura.exe", "install.ps1", "install.cmd", "share/doc/neura/LICENSE", "share/neura/stdlib/std",
                         "share/neura/components/core.txt", "share/neura/components/sdk.txt", f"bin/neura-core-{version}.dll",
                         f"bin/neura-sdk-{version}.dll"):
                if not (top / path).exists():
                    reasons.append(f"{path} is missing;")
            if self.args.server_components:
                for path in ("openssl-LICENSE.txt", "nghttp2-COPYING", "postgresql-COPYRIGHT"):
                    if not (top / "share/doc/neura/third-party" / path).is_file():
                        reasons.append(f"share/doc/neura/third-party/{path} is missing;")
            if read(top / "VERSION") != f"{version}\n":
                reasons.append(f"VERSION is not {version};")
            for tool in CORE_TOOLS + SDK_TOOLS:
                if not (top / "bin" / f"{tool}.exe").is_file():
                    reasons.append(f"bin/{tool}.exe is missing;")
            core = read(top / "share/neura/components/core.txt")
            for line in core.splitlines():
                if line.startswith("file "):
                    path = line.split(" ", 4)[4]
                    if read_bytes(top / path) != read_bytes(trees["runtime"] / path):
                        reasons.append(f"{path} differs between the SDK and runtime archives;")
            if core != read(trees["runtime"] / "share/neura/components/core.txt"):
                reasons.append("the core manifests differ;")
            # W25: the manifests record 755 for .exe and .dll files and 644
            # for the others; tools are the bin/*.exe files.
            for tree in trees.values():
                for manifest in (tree / "share/neura/components").glob("*.txt"):
                    for line in read(manifest).splitlines():
                        if line.startswith("file "):
                            _, mode, _, _, path = line.split(" ", 4)
                            wanted = "755" if path.endswith((".exe", ".dll")) else "644"
                            if mode != wanted:
                                reasons.append(f"{manifest.name} gives {path} mode {mode}, not {wanted};")
                        elif line.startswith("tool ") and not (tree / "bin" / (line[5:] + ".exe")).is_file():
                            reasons.append(f"{manifest.name} names the tool {line[5:]}, which has no bin/{line[5:]}.exe;")
        self.trees = trees
        self.report(1, "archive contents", " ".join(reasons))

    def check_install(self) -> None:
        self.home = self.profile("home")
        ok, log = self.install(self.home, self.args.sdk_archive)
        reasons = self.layout_problems(self.home, ["neura", *CORE_TOOLS, *SDK_TOOLS], neurad=False) if ok else [log]
        self.report(2, "install", "; ".join(reasons))

    def check_version(self) -> None:
        result = self.run(self.home, ["neura", "--version"])
        line = (result.stdout + result.stderr).strip()
        reason = None if line.startswith(f"neura {self.args.version} ({self.args.triple}, commit ") else f"printed '{line}'"
        self.report(3, "version report", reason)

    def check_doctor(self) -> None:
        result = self.run(self.home, ["neura", "doctor"])
        failures = " ".join(line for line in result.stdout.splitlines() if line.startswith("FAIL"))
        self.report(4, "doctor", None if result.returncode == 0 else failures or tail(result.stdout + result.stderr))

    def check_build_and_run(self) -> None:
        self.hello = self.work / "hello"
        self.hello.mkdir(exist_ok=True)
        source = self.args.examples / "hello-world" / "hello-world.nra"
        steps = [
            (["neura", "build", source, "-o", "hello.nvr"], None),
            (["neura", "run", "hello.nvr"], "Hello, World!\n"),
            (["neurac", source, "-o", "direct.nvr"], None),
            (["neura-run", "direct.nvr"], "Hello, World!\n"),
        ]
        reason = None
        for arguments, expected in steps:
            result = self.run(self.home, arguments, cwd=self.hello)
            if result.returncode != 0 or (expected is not None and result.stdout != expected):
                reason = f"{' '.join(map(str, arguments[:2]))}: {tail(result.stdout + result.stderr)}"
                break
        self.report(5, "build and run", reason)

    def check_test_runner(self) -> None:
        examples = self.args.examples
        result = self.run(self.home, ["neura", "test", "-I", examples / "guide", examples / "testing" / "math-tests.nra"])
        self.report(6, "test runner", None if result.returncode == 0 else tail(result.stdout + result.stderr))

    def check_native(self) -> None:
        # The default launcher depends only on Windows system libraries
        # (W30), so the executable runs in an environment without Neura.
        source = self.args.examples / "hello-world" / "hello-world.nra"
        result = self.run(self.home, ["neura", "build", "--native", source, "-o", "hello.exe"], cwd=self.hello)
        reason = None
        if result.returncode != 0:
            reason = tail(result.stdout + result.stderr)
        else:
            env = {"SystemRoot": self.system_root, "PATH": f"{self.system_root}\\System32"}
            run = subprocess.run([str(self.hello / "hello.exe")], env=env, cwd=self.hello, stdin=subprocess.DEVNULL, capture_output=True,
                                 text=True, timeout=TIMEOUT)
            if run.returncode != 0 or run.stdout != "Hello, World!\n":
                reason = f"hello.exe printed {run.stdout!r} and exited {run.returncode}: {tail(run.stderr)}"
        self.report(7, "native executable", reason)

    def check_update_check(self) -> None:
        # The release directory: the two archives and the metadata that
        # make-release-metadata.sh writes, whose neura-release.txt (read by
        # 0.2 clients) names no Windows archive.
        self.release = self.work / "release"
        self.release.mkdir()
        for archive in (self.args.sdk_archive, self.args.runtime_archive):
            shutil.copy2(archive, self.release / archive.name)
        build = next(line[6:] for line in read(self.trees["sdk"] / "share/neura/components/core.txt").splitlines()
                     if line.startswith("build "))
        (self.release / "neura-release.txt").write_bytes(f"neura-release 1\nversion {self.args.version}\n".encode())
        lines = [f"neura-components 1\nversion {self.args.version}\nbuild {build}\n"]
        for product, archive in (("runtime", self.args.runtime_archive), ("sdk", self.args.sdk_archive)):
            lines.append(f"archive {product} {self.args.triple} {archive.name} {digest(archive)} {archive.stat().st_size}\n")
        (self.release / "neura-components.txt").write_bytes("".join(lines).encode())
        result = self.run(self.home, ["neura", "update", "--check", "--source", self.release])
        self.report(8, "update check", None if result.returncode == 0 else tail(result.stdout + result.stderr))

    def check_upgrade(self) -> None:
        if self.args.previous is None:
            if self.args.require_previous:
                self.report(9, "upgrade", "no previous archive was given and one is required")
            else:
                self.skip(9, "upgrade", "no previous Windows archive was given; 0.3.0 is the first Windows release")
            return
        old = self.profile("old")
        ok, log = self.install(old, self.args.previous)
        if not ok:
            self.report(9, "upgrade", f"the previous archive did not install: {log}")
            return
        before = self.run(old, ["neura", "--version"]).stdout.strip()
        steps = [
            ["neura", "update", "--source", self.release],
            ["neura", "doctor"],
            ["neura", "build", self.args.examples / "hello-world" / "hello-world.nra", "-o", old / "upgraded.nvr"],
            ["neura", "run", old / "upgraded.nvr"],
        ]
        reason = None
        for arguments in steps:
            result = self.run(old, arguments)
            if result.returncode != 0:
                reason = f"{' '.join(map(str, arguments[:2]))}: {tail(result.stdout + result.stderr)}"
                break
        if reason is None:
            after = self.run(old, ["neura", "--version"]).stdout.strip()
            if after == before or not after.startswith(f"neura {self.args.version} ("):
                reason = f"the update did not activate {self.args.version}: {after}"
        if reason is None:
            if self.run(old, ["neura", "update", "--rollback"]).returncode != 0 or self.run(old, ["neura", "--version"]).stdout.strip() != before:
                reason = "the rollback did not return to the previous release"
            elif self.run(old, ["neura", "update", "--rollback"]).returncode != 0:
                reason = "the second rollback failed"
            elif self.run(old, ["neura", "uninstall", "--yes"]).returncode != 0 or not self.wait_removed(self.root(old)):
                reason = "the uninstall did not remove the installation"
        self.report(9, f"upgrade from {before.split(' ')[1] if ' ' in before else before}, rollback, and uninstall", reason)

    def check_uninstall(self) -> None:
        root = self.root(self.home)
        result = self.run(self.home, ["neura", "uninstall", "--yes"])
        reason = None
        if result.returncode != 0:
            reason = tail(result.stdout + result.stderr)
        elif not self.wait_removed(root):
            reason = f"{root} remains after the command ended: {sorted(p.name for p in root.iterdir())}"
        else:
            shutil.rmtree(self.home / "unpack", ignore_errors=True)
            # Install again through the other front.
            ok, log = self.install(self.home, self.args.sdk_archive, front="install.cmd")
            if not ok or self.run(self.home, ["neura", "--version"]).returncode != 0:
                reason = f"installing again with install.cmd failed: {log}"
        self.report(10, "uninstall and reinstall", reason)

    def service_runs(self, home: Path) -> str | None:
        result = self.run(home, ["neura", "run", "--runtime=required", self.hello / "hello.nvr"], extra={"NEURA_GPU": "off"})
        self.run(home, ["neura", "runtime", "shutdown"])
        return None if result.stdout == "Hello, World!\n" else f"the service did not run the image: {tail(result.stdout + result.stderr)}"

    def check_runtime(self) -> None:
        top = self.trees.get("runtime")
        reasons = []
        if top is None:
            self.report(11, "runtime archive", "the runtime archive did not unpack")
            return
        for tool in SDK_TOOLS:
            if (top / "bin" / f"{tool}.exe").exists():
                reasons.append(f"bin/{tool}.exe is an SDK tool;")
        if (top / "share/neura/stdlib").exists():
            reasons.append("it holds the standard library;")
        if list((top / "bin").glob("neura-sdk-*.dll")):
            reasons.append("it holds the SDK library;")
        if not (top / "bin/neurad.exe").is_file():
            reasons.append("it does not hold the service, bin/neurad.exe;")
        if not reasons:
            home = self.profile("runtime-home")
            ok, log = self.install(home, self.args.runtime_archive)
            if not ok:
                reasons.append(f"the runtime archive did not install: {log}")
            else:
                reasons += self.layout_problems(home, ["neura", *CORE_TOOLS], neurad=True)
                if (self.root(home) / "current/bin/neurac.exe").exists():
                    reasons.append("a neurac.exe")
                for command in (["neura", "run"], ["neura-run"]):
                    result = self.run(home, [*command, self.hello / "hello.nvr"])
                    if result.stdout != "Hello, World!\n":
                        reasons.append(f"{' '.join(command)} did not run the image: {tail(result.stdout + result.stderr)}")
                build = self.run(home, ["neura", "build", self.args.examples / "hello-world/hello-world.nra", "-o",
                                        self.work / "runtime-x.nvr"])
                if build.returncode == 0 or "needs Neura-sdk, which is not installed here" not in build.stderr:
                    reasons.append(f"neura build did not name Neura-sdk: {tail(build.stdout + build.stderr)}")
                doctor = self.run(home, ["neura", "doctor"])
                if doctor.returncode != 0:
                    reasons.append("doctor failed: " + " ".join(l for l in doctor.stdout.splitlines() if l.startswith("FAIL")))
                if not reasons:
                    problem = self.service_runs(home)
                    if problem:
                        reasons.append(problem)
        self.report(11, "runtime archive", " ".join(reasons))

    def check_both(self) -> None:
        home = self.profile("both-home")
        ok, log = self.install(home, self.args.sdk_archive, self.args.runtime_archive)
        reasons = [] if ok else [log]
        if ok:
            versions = self.root(home) / "versions"
            if len(list(versions.iterdir())) != 1:
                reasons.append("more than one generation")
            if "components core sdk service" not in self.marker(home).splitlines():
                reasons.append("the marker does not list core sdk service")
            reasons += self.layout_problems(home, ["neura", *CORE_TOOLS, *SDK_TOOLS], neurad=True)
            doctor = self.run(home, ["neura", "doctor"])
            if doctor.returncode != 0:
                reasons.append("doctor failed: " + " ".join(l for l in doctor.stdout.splitlines() if l.startswith("FAIL")))
        self.report(12, "SDK and runtime together", "; ".join(reasons))

    def check_sizes(self) -> None:
        sdk = self.args.sdk_archive.stat().st_size
        runtime = self.args.runtime_archive.stat().st_size
        print(f"size: {self.args.sdk_archive.name} {sdk} bytes; {self.args.runtime_archive.name} {runtime} bytes", flush=True)
        reasons = []
        if runtime >= sdk:
            reasons.append("the runtime archive is not smaller than the SDK archive;")
        missing = [option for option, reference in (("--size-reference-sdk", self.args.size_reference_sdk),
                                                    ("--size-reference-runtime", self.args.size_reference_runtime)) if reference is None]
        if missing and not reasons:
            # Without the Linux archives the W35 budget is not checked, and
            # the check does not pass.
            self.skip(14, "archive sizes", f"{' and '.join(missing)} not given, so the {GROWTH_PERCENT}% budget of W35 was not checked")
            return
        for product, size, reference in (("SDK", sdk, self.args.size_reference_sdk), ("runtime", runtime, self.args.size_reference_runtime)):
            if reference is None:
                reasons.append(f"the {product} archive's budget was not checked: no {product.lower()} size reference;")
                continue
            limit = reference.stat().st_size * GROWTH_PERCENT // 100
            print(f"size: the reference {reference.name} is {reference.stat().st_size} bytes; the {product} archive may be "
                  f"{GROWTH_PERCENT}% of it, {limit} bytes", flush=True)
            if size > limit:
                reasons.append(f"the {product} archive is more than {GROWTH_PERCENT}% of {reference.name};")
        self.report(14, "archive sizes", " ".join(reasons))

    def check_server(self) -> None:
        if not self.args.server_components:
            self.skip(15, "server host", "the archives are not declared to hold the server components (--server-components)")
            return
        openssl = self.args.openssl or shutil.which("openssl")
        curl = self.args.curl or shutil.which("curl")
        if not openssl or not curl:
            self.report(15, "server host", "openssl and a curl with HTTP/2 are needed (--openssl, --curl)")
            return
        server = self.work / "server"
        server.mkdir()
        reasons = []
        build = self.run(self.home, ["neura", "build", self.args.examples / "server/hello-server.nra", "-o", "hello-server.nvr"], cwd=server)
        certificate = subprocess.run([str(openssl), "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(server / "key.pem"),
                                      "-out", str(server / "cert.pem"), "-days", "2", "-subj", "/CN=localhost", "-addext",
                                      "subjectAltName=IP:127.0.0.1,DNS:localhost"], capture_output=True, text=True,
                                     stdin=subprocess.DEVNULL, timeout=TIMEOUT)
        if build.returncode != 0:
            reasons.append(f"building the server example failed: {tail(build.stdout + build.stderr)}")
        elif certificate.returncode != 0:
            reasons.append(f"openssl could not make a certificate: {tail(certificate.stderr)}")
        else:
            host = subprocess.Popen([str(self.root(self.home) / "current/bin/neura-server-host.exe"), "--listen", "127.0.0.1:0",
                                     "--tls-listen", "127.0.0.1:0", "--cert", str(server / "cert.pem"), "--key", str(server / "key.pem"),
                                     "--h2c", "--config", "greeting=hello", str(server / "hello-server.nvr")],
                                    env=self.env(self.home), cwd=server, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:
                ports = {}
                deadline = time.monotonic() + 30
                while len(ports) < 2 and time.monotonic() < deadline:
                    line = host.stdout.readline()
                    if not line:
                        break
                    match = re.fullmatch(r"neura-server-host: listening on (https?)://127\.0\.0\.1:(\d+)\n", line)
                    if match:
                        ports[match.group(1)] = match.group(2)
                if len(ports) < 2:
                    reasons.append("the host did not listen")
                else:
                    https = subprocess.run([str(curl), "-sS", "--http2", "--cacert", str(server / "cert.pem"), "-w", "%{http_version}",
                                            "-o", str(server / "https.body"), f"https://127.0.0.1:{ports['https']}/hello?name=qualify"],
                                           capture_output=True, text=True, timeout=60)
                    if https.stdout != "2" or read(server / "https.body") != "hello, qualify":
                        reasons.append(f"HTTPS with --http2 answered HTTP version '{https.stdout}' and '{read(server / 'https.body')}' "
                                       f"{tail(https.stderr)};")
                    h2c = subprocess.run([str(curl), "-sS", "--http2-prior-knowledge", "-w", "%{http_version}", "-o",
                                          str(server / "h2c.body"), f"http://127.0.0.1:{ports['http']}/hello"],
                                         capture_output=True, text=True, timeout=60)
                    if h2c.stdout != "2" or read(server / "h2c.body") != "hello, world":
                        reasons.append(f"h2c answered HTTP version '{h2c.stdout}' and '{read(server / 'h2c.body')}' {tail(h2c.stderr)};")
            finally:
                host.kill()
                host.communicate()
            postgres = self.run(self.home, ["neura-server-host", "migrate", "--sql-pool", "main=postgresql://neura@127.0.0.1:1/neura",
                                            server / "hello-server.nvr"], cwd=server)
            if postgres.returncode == 0:
                reasons.append("a pool on a closed port was accepted;")
            elif "no PostgreSQL provider" in postgres.stdout + postgres.stderr:
                reasons.append("the server host has no PostgreSQL provider;")
        self.report(15, "server host: HTTPS with HTTP/2, h2c, PostgreSQL provider" if not reasons else "server host", " ".join(reasons))

    def guarded(self, number: int, name: str, check) -> None:
        """Runs a check; an error that it does not handle itself, such as a
        program that does not start, fails that check alone."""
        try:
            check()
        except (OSError, subprocess.SubprocessError, tarfile.TarError, AttributeError, ValueError, StopIteration) as error:
            detail = ""
            if isinstance(error, subprocess.CalledProcessError):
                detail = " " + tail((error.stderr or b"").decode("utf-8", errors="replace") if isinstance(error.stderr, bytes)
                                    else error.stderr or "")
            self.report(number, name, f"{type(error).__name__}: {error}{detail}")

    def run_all(self) -> int:
        self.trees = {}
        self.home = self.profile("home")
        self.hello = self.work / "hello"
        self.release = self.work / "release"
        for number, name, check in ((1, "archive contents", self.check_archives), (2, "install", self.check_install),
                                    (3, "version report", self.check_version), (4, "doctor", self.check_doctor),
                                    (5, "build and run", self.check_build_and_run), (6, "test runner", self.check_test_runner),
                                    (7, "native executable", self.check_native), (8, "update check", self.check_update_check),
                                    (9, "upgrade", self.check_upgrade), (10, "uninstall and reinstall", self.check_uninstall),
                                    (11, "runtime archive", self.check_runtime), (12, "SDK and runtime together", self.check_both)):
            self.guarded(number, name, check)
        self.skip(13, "bridge", "no 0.2 client runs on Windows, so there is no 0.2 installation to bridge from")
        self.guarded(14, "archive sizes", self.check_sizes)
        self.guarded(15, "server host", self.check_server)
        print(f"{self.failed} failed, {self.not_run} not run.", flush=True)
        if self.failed:
            return 1
        return 0


def extract(archive: Path, destination: Path) -> Path:
    """Unpacks an archive as a user would, with System32's tar.exe, and
    returns its top directory."""
    destination.mkdir(parents=True, exist_ok=True)
    tar = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "tar.exe"
    subprocess.run([str(tar), "-xzf", str(archive), "-C", str(destination)], check=True, capture_output=True,
                   stdin=subprocess.DEVNULL, timeout=TIMEOUT)
    return destination / archive.name.removesuffix(".tar.gz")


def read(path: Path) -> str:
    try:
        return path.read_bytes().decode("utf-8", errors="replace")
    except OSError:
        return ""


def read_bytes(path: Path) -> bytes | None:
    try:
        return path.read_bytes()
    except OSError:
        return None


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tail(text: str, lines: int = 3) -> str:
    return " ".join(text.strip().splitlines()[-lines:])


def main() -> int:
    parser = argparse.ArgumentParser(description="Qualifies the Windows release archives (see the module documentation).")
    parser.add_argument("--sdk-archive", type=Path, required=True)
    parser.add_argument("--runtime-archive", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--triple", required=True)
    parser.add_argument("--examples", type=Path, required=True)
    parser.add_argument("--previous", type=Path)
    parser.add_argument("--require-previous", action="store_true")
    parser.add_argument("--size-reference-sdk", type=Path)
    parser.add_argument("--size-reference-runtime", type=Path)
    parser.add_argument("--server-components", action="store_true")
    parser.add_argument("--openssl", type=Path)
    parser.add_argument("--curl", type=Path)
    arguments = parser.parse_args()
    if sys.platform != "win32":
        print("qualify.py: this qualifies Windows archives on Windows; use qualify.sh elsewhere", file=sys.stderr)
        return 2
    if sys.version_info < (3, 12):
        # tarfile's extraction filter and the str methods it uses need 3.12.
        print(f"qualify.py: this needs Python 3.12 or later, not {sys.version.split()[0]}", file=sys.stderr)
        return 2
    if not arguments.triple.endswith("-windows-msvc"):
        print(f"qualify.py: {arguments.triple} is not a Windows target; use qualify.sh", file=sys.stderr)
        return 2
    for name in ("sdk_archive", "runtime_archive", "examples", "previous", "size_reference_sdk", "size_reference_runtime"):
        value = getattr(arguments, name)
        if value is not None:
            if not value.exists():
                print(f"qualify.py: {value} does not exist", file=sys.stderr)
                return 2
            setattr(arguments, name, value.resolve())
    work = Path(tempfile.mkdtemp(prefix="neura-qualify-"))
    try:
        return Qualification(arguments, work).run_all()
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
