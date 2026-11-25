#!/usr/bin/env python3
"""Exercise installed Win64 launcher packaging and standalone execution."""

import argparse
import os
import re
import runpy
import shutil
import struct
import subprocess
import zlib
from pathlib import Path


HELLO = b"Hello, World!\n"
TRIPLE = b"x86_64-windows-msvc"
FOOTER = struct.Struct("<QQII8s")
CRC_OFFSET = struct.calcsize("<QQ")
DEFAULT_POLICY_SIZE = 80  # Unscoped version 3, including the image length word.


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def footer(data):
    end = len(data) - FOOTER.size
    offset, size, checksum, version, magic = FOOTER.unpack_from(data, end)
    require(magic == b"NEURAEXE" and 0 < offset < end and size == end - offset,
            "invalid footer or payload bounds in the unsigned executable")
    return offset, end, checksum, version


class LauncherTests:
    def __init__(self, neura, launcher, source, scratch):
        self.neura = neura.resolve(strict=True)
        self.launcher = launcher.resolve(strict=True)
        self.source = source.resolve(strict=True)
        self.scratch = scratch.resolve()
        self.hello = self.source / "examples/hello-world/hello-world.nra"
        if self.scratch.exists():
            shutil.rmtree(self.scratch)
        self.scratch.mkdir(parents=True)
        self.build_env = dict(os.environ)
        self.build_env.pop("NEURA_RUN_OPTIONS", None)

        # Keep the OS environment required by CreateProcessW and the Windows
        # loader, with an empty PATH and a test-owned profile and temp directory.
        profile = self.scratch / "profile"
        empty_path = self.scratch / "empty-path"
        for directory in (profile / "Local", profile / "Roaming", profile / "Temp", empty_path):
            directory.mkdir(parents=True)
        self.clean_env = {
            "SystemRoot": os.environ["SystemRoot"],
            "WINDIR": os.environ["SystemRoot"],
            "PATH": str(empty_path),
            "USERPROFILE": str(profile),
            "HOME": str(profile),
            "LOCALAPPDATA": str(profile / "Local"),
            "APPDATA": str(profile / "Roaming"),
            "TEMP": str(profile / "Temp"),
            "TMP": str(profile / "Temp"),
        }

    def run(self, *args, cwd=None, clean=False, options=None, stdin=b"", status=0, stdout=None, error=None):
        env = self.clean_env if clean else self.build_env
        if options is not None:
            env = dict(env, NEURA_RUN_OPTIONS=options)
        result = subprocess.run(args, cwd=cwd or self.scratch, env=env, input=stdin, capture_output=True)
        details = f"{args!r}: status {result.returncode}\nstdout: {result.stdout!r}\nstderr: {result.stderr!r}"
        require(result.returncode == status, details)
        if stdout is not None:
            require(result.stdout == stdout, f"expected stdout {stdout!r}\n{details}")
        if error is not None:
            require(error in result.stderr, f"expected diagnostic {error!r}\n{details}")
        else:
            require(not result.stderr, details)
        return result.stdout

    def package(self, source, output, *flags, clean=False):
        self.run(self.neura, "build", "--native", *flags, source, "-o", output, clean=clean)
        return output

    def deploy_copy(self, original):
        deployed = self.scratch / "deployed"
        cwd = self.scratch / "run"
        deployed.mkdir()
        cwd.mkdir()
        copied = deployed / "relocated.exe"
        shutil.copyfile(original, copied)
        original.unlink()
        require(list(deployed.iterdir()) == [copied], "deployment must contain only the copied executable")
        self.run(copied, cwd=cwd, clean=True, stdout=HELLO)

    def deploy(self):
        executable = self.package(self.hello, self.scratch / "hello.exe")
        self.deploy_copy(executable)

    def unicode(self):
        directory = self.scratch / "étape 編譯"
        directory.mkdir()
        source = directory / "arguments reçus 引数.nra"
        shutil.copyfile(self.source / "tests/native/print-arguments.nra", source)
        executable = self.package(source, directory / "paramètres 猫.exe")
        extracted = directory / "image récupérée 像.nvr"
        inspected = self.run(self.neura, "inspect", executable, "-o", extracted, cwd=directory)
        require(f"native executable: {executable.name}\n".encode() in inspected,
                f"inspect did not print the Unicode executable name: {inspected!r}")
        arguments = ("", "deux mots", "é 猫 🧪", "--help")
        expected = "".join(f"<{argument}>\n" for argument in arguments).encode()
        self.run(executable, *arguments, cwd=directory, clean=True, stdout=expected)
        self.run(self.neura, "run", "--no-gpu", extracted, "--", *arguments, cwd=directory, stdout=expected)

    def inspect_replay(self):
        # Reuse the line-reading fixture for CRLF, LF, non-UTF-8 and EOF.
        fixture = self.source / "tests/stdlib/console/programs"
        cwd = self.scratch / "traces enregistrés 記録"
        cwd.mkdir()
        executable = self.package(fixture / "read-lines.nra", cwd / "read-lines.exe")
        image = cwd / "extracted.nvr"
        inspected = self.run(self.neura, "inspect", executable, "-o", image)
        require(re.search(rb"^launcher: neura .+, " + TRIPLE + rb", default, runs NVR up to 1\.\d+$",
                          inspected, re.MULTILINE) and b"run options: accepted\n" in inspected,
                f"inspect omitted the PE launcher descriptor or policy: {inspected!r}")
        stdin = b"one\r\ntwo\n\nbad\xff\napr\xc3\xa8s\nlast"
        expected = (fixture / "read-lines.out").read_text(encoding="utf-8").encode()
        self.run(self.neura, "run", "--no-gpu", "--record", "cpu.trace", image,
                 cwd=cwd, stdin=stdin, stdout=expected)
        self.run(executable, cwd=cwd, clean=True, options="--record native.trace", stdin=stdin, stdout=expected)
        cpu_trace = (cwd / "cpu.trace").read_bytes()
        require(b'request neura.console.read ' in cpu_trace and b'one\\r\\ntwo' in cpu_trace,
                "the trace should capture the original CRLF bytes from redirected stdin")
        require(cpu_trace == (cwd / "native.trace").read_bytes(), "the runner and launcher traces differ")
        self.run(executable, cwd=cwd, clean=True, options="--replay cpu.trace", stdout=expected)
        self.run(self.neura, "run", "--no-gpu", "--replay", "native.trace", executable, cwd=cwd, stdout=expected)

    def policy(self):
        unsealed = self.package(self.hello, self.scratch / "open.exe", "--deny-all")
        sealed = self.package(self.hello, self.scratch / "sealed.exe", "--deny-all", "--sealed")
        inspected = self.run(self.neura, "inspect", sealed)
        require(b"capabilities: console, process exit\n" in inspected and b"run options: sealed\n" in inspected,
                f"inspect did not report the sealed capability policy: {inspected!r}")
        self.run(unsealed, clean=True, stdout=HELLO)
        self.run(unsealed, clean=True, options="--max-steps 1", status=152, stdout=b"",
                 error=b"trap: step limit exceeded")
        self.run(unsealed, clean=True, options="--allow-fs", status=1, stdout=b"",
                 error=b"NEURA_RUN_OPTIONS: --allow-fs is not accepted by a native executable")
        self.run(sealed, clean=True, options="--max-steps 1 --record sealed.trace --allow-fs", stdout=HELLO)
        require(not (self.scratch / "sealed.trace").exists(), "a sealed launcher accepted --record")
        limited = self.package(self.hello, self.scratch / "limited.exe", "--sealed", "--max-steps", "1")
        self.run(limited, clean=True, options="--max-steps 100000000", status=152, stdout=b"",
                 error=b"trap: step limit exceeded")

    def corrupt(self):
        image_path = self.scratch / "hello.nvr"
        self.run(self.neura, "build", self.hello, "-o", image_path)
        executable = self.package(image_path, self.scratch / "hello.exe")
        data = bytearray(executable.read_bytes())
        offset, end, _, _ = footer(data)
        image_start = end - image_path.stat().st_size
        require(offset < image_start and data[image_start:image_start + 4] == b"NEUR", "missing NVR header")
        data[image_start] ^= 1
        wrong_crc = self.scratch / "bad-checksum.exe"
        wrong_crc.write_bytes(data)

        # Repair the outer checksum to reach the NVR validator with the same
        # damaged image. Both failures must happen before the program prints.
        struct.pack_into("<I", data, end + CRC_OFFSET, zlib.crc32(data[offset:end]))
        wrong_image = self.scratch / "bad-image.exe"
        wrong_image.write_bytes(data)
        extracted = self.scratch / "bad-image.nvr"
        cases = ((wrong_crc, b"payload checksum mismatch"), (wrong_image, b"header: not an NVR image"))
        for corrupt, error in cases:
            self.run(corrupt, clean=True, status=1, stdout=b"", error=error)
            self.run(self.neura, "inspect", corrupt, "-o", extracted, status=1, stdout=b"", error=error)
            self.run(self.neura, "run", "--no-gpu", corrupt, status=1, stdout=b"", error=error)
            require(not extracted.exists(), "inspect extracted an invalid image")

    def dependencies(self, dumpbin):
        checker = runpy.run_path(str(self.source / "scripts/check-windows-binaries.py"))
        executable = self.package(self.hello, self.scratch / "hello.exe")
        for binary in (self.launcher, executable):
            output = self.run(dumpbin, "/nologo", "/dependents", binary).decode("utf-8", errors="replace")
            libraries = checker["imports"](output)
            require(libraries and libraries <= checker["SYSTEM_DLLS"],
                    f"{binary.name} should import only Windows system DLLs, found {sorted(libraries)}\n{output}")
            print(f"{binary.name}: {binary.stat().st_size} bytes, imports {', '.join(sorted(libraries))}")

    def identity(self):
        image_path = self.scratch / "hello.nvr"
        self.run(self.neura, "build", self.hello, "-o", image_path)
        from_source = self.package(self.hello, self.scratch / "from-source.exe")
        from_image = self.package(image_path, self.scratch / "from-image.exe")
        data = from_source.read_bytes()
        require(from_image.read_bytes() == data, "building from source versus its NVR produced different executables")
        offset, end, checksum, version = footer(data)
        launcher = self.launcher.read_bytes()
        image = image_path.read_bytes()
        require(data[:offset] == launcher, "packaging changed the installed PE launcher")
        require(version == 3 and end - offset == DEFAULT_POLICY_SIZE + len(image),
                "the default executable must grow by only its policy, image and footer")
        require(data[offset + DEFAULT_POLICY_SIZE:end] == image, "packaging changed the NVR image")
        require(checksum == zlib.crc32(data[offset:end]), "footer CRC-32 disagrees with the packaged payload")
        extracted = self.scratch / "extracted.nvr"
        self.run(self.neura, "inspect", from_source, "-o", extracted)
        require(extracted.read_bytes() == image, "inspect -o changed the NVR image")

    def no_toolchain(self):
        # The installed SDK is invoked explicitly to build. Its child process
        # has neither a compiler nor linker on PATH or a VS/Windows SDK environment.
        source = self.scratch / "hello.nra"
        shutil.copyfile(self.hello, source)
        executable = self.package(source, self.scratch / "hello.exe", clean=True)
        source.unlink()
        inspected = self.run(self.neura, "inspect", executable)
        require(b"native library absent\n" in inspected, f"expected Neura-only output: {inspected!r}")
        self.deploy_copy(executable)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    suites = {
        "deploy": LauncherTests.deploy,
        "unicode": LauncherTests.unicode,
        "inspect-replay": LauncherTests.inspect_replay,
        "policy": LauncherTests.policy,
        "corrupt": LauncherTests.corrupt,
        "dependencies": LauncherTests.dependencies,
        "identity": LauncherTests.identity,
        "no-toolchain": LauncherTests.no_toolchain,
    }
    parser.add_argument("suite", choices=suites)
    parser.add_argument("neura", type=Path)
    parser.add_argument("launcher", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--dumpbin", type=Path)
    args = parser.parse_args()
    if args.suite == "dependencies" and args.dumpbin is None:
        parser.error("dependencies requires --dumpbin")
    test = LauncherTests(args.neura, args.launcher, args.source, args.scratch)
    if args.suite == "dependencies":
        test.dependencies(args.dumpbin.resolve(strict=True))
    else:
        suites[args.suite](test)


if __name__ == "__main__":
    main()
