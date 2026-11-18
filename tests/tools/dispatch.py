#!/usr/bin/env python3
"""The neura dispatcher and its sibling tools (docs/design/Products030.md).

    dispatch.py parity BIN_DIR
        Runs every command of the tool registry, with accepted and rejected
        arguments, as `neura <command> ...`, as `<tool> <command> ...` and,
        where the command is the tool's default, as `<tool> ...`, and compares
        the exit status, standard output, standard error and written files.
        Also checks that the registry in src/cli/tool_registry.cpp, tests/tools.cmake
        and the packaging scripts list the same tools.

    dispatch.py components BIN_DIR MIXED_NEURA
        Copies parts of BIN_DIR to new directories and checks the messages of
        a missing SDK, a missing core tool, a missing service component, and
        a dispatcher from another build (MIXED_NEURA, a neura built with
        another version line in its dispatch token).

    dispatch.py install BUILD_DIR CMAKE
        Installs BUILD_DIR into a prefix whose path holds a space and a
        non-ASCII letter, reaches it through a symbolic link on PATH as
        install.sh does, and checks that every parity case prints what it
        prints in the build tree. Then checks a relocated copy, and that a
        dispatcher uses its own installation's tools when another
        installation comes first on PATH.

Exit status 0 when every check passes.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
SUFFIX = ".exe" if os.name == "nt" else ""

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)
        print("FAIL", message)


# --------------------------------------------------------------------------
# The registry, read from its source.

def registry() -> tuple[dict[str, tuple[str, str, str]], dict[str, tuple[str, list[str]]]]:
    source = (REPOSITORY / "src/cli/tool_registry.cpp").read_text()
    words = {name: re.findall(r'"([^"]+)"', body)
             for name, body in re.findall(r'constexpr std::array<std::string_view, \d+> (\w+)\{([^}]*)\};', source)}
    tools = {name: (component, words[array])
             for name, component, array in re.findall(r'\{"([\w-]+)", Component::(\w+), (\w+)\}', source)}
    commands = {command: (tool, word, component)
                for command, tool, word, component in
                re.findall(r'\{"([\w-]+)", "([\w-]+)", "([\w-]+)", Component::(\w+)\}', source)}
    return commands, tools


def check_lists(tools: dict[str, tuple[str, list[str]]]) -> None:
    core = [name for name, (component, _) in tools.items() if component == "Core"]
    sdk = [name for name, (component, _) in tools.items() if component == "Sdk"]
    cmake = re.search(r"set\(NEURA_TOOL_NAMES(.*?)\)", (REPOSITORY / "tests/tools.cmake").read_text(), re.S)
    check(cmake is not None and cmake.group(1).split() == core + sdk, "tests/tools.cmake lists the registry's tools")
    for script in ("packaging/make-archive.sh", "packaging/qualify.sh"):
        text = (REPOSITORY / script).read_text()
        found_core = re.search(r'core_tools="([^"]*)"', text)
        found_sdk = re.search(r'sdk_tools="([^"]*)"', text)
        check(found_core is not None and found_core.group(1).split() == core, f"{script} lists the core tools")
        check(found_sdk is not None and found_sdk.group(1).split() == sdk, f"{script} lists the SDK tools")
    closure = (REPOSITORY / "scripts/check-core-closure.py").read_text()
    check(f"CORE_TOOLS = {['neura'] + core!r}".replace("'", '"') in closure,
          "scripts/check-core-closure.py lists the core tools")


# --------------------------------------------------------------------------
# Inputs and cases.

def lsp_session() -> bytes:
    def frame(message: dict) -> bytes:
        data = json.dumps(message).encode()
        return f"Content-Length: {len(data)}\r\n\r\n".encode() + data
    return (frame({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                   "params": {"rootUri": None, "capabilities": {}}})
            + frame({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
            + frame({"jsonrpc": "2.0", "method": "exit"}))


def dap_session() -> bytes:
    def frame(message: dict) -> bytes:
        data = json.dumps(message).encode()
        return f"Content-Length: {len(data)}\r\n\r\n".encode() + data
    return (frame({"seq": 1, "type": "request", "command": "initialize", "arguments": {"adapterID": "neura"}})
            + frame({"seq": 2, "type": "request", "command": "disconnect", "arguments": {}}))


def prepare(neura: Path, inputs: Path) -> dict[str, str]:
    """Writes the inputs, built once with `neura`, and returns their paths."""
    inputs.mkdir(parents=True, exist_ok=True)
    hello = REPOSITORY / "examples/hello-world/hello-world.nra"
    arguments = REPOSITORY / "tests/backend/each/arguments.nra"
    bad = inputs / "bad.nra"
    bad.write_text("let x = ;\n")
    header = inputs / "adder.h"
    header.write_text("int add(int a, int b);\n")
    lists = inputs / "lists.txt"
    lists.write_text("one two\nfailure\n\nthree\n")
    paths = {
        "HELLO": str(hello), "ARGS": str(arguments), "BAD": str(bad), "HEADER": str(header), "LISTS": str(lists),
        "MATH": str(REPOSITORY / "examples/testing/math-tests.nra"), "GUIDE": str(REPOSITORY / "examples/guide"),
        "HELLO_NVR": str(inputs / "hello.nvr"), "ARGS_NVR": str(inputs / "arguments.nvr"),
        "NVRA": str(inputs / "hello.nvra"), "NATIVE": str(inputs / "hello-native"),
    }
    for source, image in ((hello, paths["HELLO_NVR"]), (arguments, paths["ARGS_NVR"])):
        subprocess.run([str(neura), "build", str(source), "-o", image], check=True, capture_output=True)
    subprocess.run([str(neura), "disassemble", paths["HELLO_NVR"], "-o", paths["NVRA"]], check=True, capture_output=True)
    native = subprocess.run([str(neura), "build", "--native", str(hello), "-o", paths["NATIVE"]], capture_output=True)
    if native.returncode != 0:
        paths["NATIVE"] = paths["HELLO_NVR"]  # no launcher here: inspect reports a non-native file
    return paths


# Each case: the unified arguments, standard input, and the files it writes.
CASES: list[tuple[list[str], bytes | None, list[str]]] = [
    (["build", "{HELLO}", "-o", "out.nvr"], None, ["out.nvr"]),
    (["build", "--no-source-map", "{HELLO}", "-o", "out.nvr"], None, ["out.nvr"]),
    (["build", "{HELLO}"], None, []),
    (["build", "--no-such-option", "{HELLO}"], None, []),
    (["build", "{BAD}", "-o", "out.nvr"], None, []),
    (["check", "{HELLO}"], None, []),
    (["check", "{BAD}"], None, []),
    (["check", "--error-format=json", "{BAD}"], None, []),
    (["check", "--error-format=short", "{BAD}"], None, []),
    (["check", "--task-safety", "{ARGS}"], None, []),
    (["emit-ir", "{HELLO}"], None, []),
    (["emit-ir", "{HELLO}", "-o", "out.nir"], None, ["out.nir"]),
    (["emit-ir", "--no-optimize", "{BAD}"], None, []),
    (["run", "{HELLO_NVR}"], None, []),
    (["run", "{ARGS_NVR}", "--", "failure", "two words", "--"], None, []),
    (["run", "--stats", "--tier=0", "{ARGS_NVR}", "--", "x"], None, []),
    (["run", "--deny-all", "--jit=off", "{HELLO_NVR}"], None, []),
    (["run", "--each", "{LISTS}", "{ARGS_NVR}"], None, []),
    (["run"], None, []),
    (["run", "--help"], None, []),
    (["run", "--tier=7", "{HELLO_NVR}"], None, []),
    (["run", "--tier-stats", "{ARGS_NVR}", "--", "x"], None, []),
    (["run", "{HELLO}"], None, []),
    # `neura serve` cases that end before the host binds a port: usage, a
    # build failure, and a host option that the server host refuses.
    (["serve"], None, []),
    (["serve", "--help"], None, []),
    (["serve", "{BAD}"], None, []),
    (["serve", "{HELLO_NVR}", "--max-workers", "0"], None, []),
    (["test", "-I", "{GUIDE}", "{MATH}"], None, []),
    (["test", "-I", "{GUIDE}", "--list", "--format=json", "{MATH}"], None, []),
    (["test", "--list"], None, []),
    (["docs", "{HELLO}"], None, []),
    (["docs", "{HELLO}", "-o", "out.json"], None, ["out.json"]),
    (["inspect", "{HELLO_NVR}"], None, []),
    (["inspect", "{NATIVE}", "-o", "out.nvr"], None, []),
    (["inspect-image", "--json", "{HELLO_NVR}"], None, []),
    (["inspect-image"], None, []),
    (["disassemble", "{HELLO_NVR}"], None, []),
    (["disassemble", "{HELLO_NVR}", "-o", "out.nvra"], None, ["out.nvra"]),
    (["assemble", "{NVRA}", "-o", "out.nvr"], None, ["out.nvr"]),
    (["assemble", "{NVRA}"], None, []),
    (["decompile", "{HELLO_NVR}"], None, []),
    (["decompile", "--no-provenance", "{HELLO_NVR}", "-o", "out.nra"], None, ["out.nra"]),
    (["c-bindings", "{HEADER}"], None, []),
    (["c-bindings", "/nonexistent/header.h"], None, []),
    (["explain", "E0401"], None, []),
    (["explain", "--list"], None, []),
    (["explain"], None, []),
    (["explain", "E9999"], None, []),
    (["--explain", "E0401"], None, []),
    (["tags"], None, []),
    (["tags", "ThreadSafe"], None, []),
    (["tags", "Nonexistent"], None, []),
    (["doctor"], None, []),
    (["doctor", "extra"], None, []),
    (["update", "--check"], None, []),
    (["update", "--bogus"], None, []),
    (["update", "--nightly", "--check"], None, []),
    (["uninstall", "--dry-run"], None, []),
    (["fmt", "--check", "{HELLO}"], None, []),
    (["fmt", "--stdin", "--stdin-filepath", "x.nra"], b"let   x=1;\n", []),
    (["fmt"], None, []),
    (["lsp", "--stdio"], lsp_session(), []),
    (["lsp"], None, []),
    (["rename"], None, []),
    (["rename", "{HELLO}", "1:1"], None, []),
    (["debug", "--stdio"], dap_session(), []),
    (["debug"], None, []),
]


# `--tier-stats` reports wall times, which differ between any two runs.
TIMES = re.compile(rb"\d+\.\d+ ms|tier: time:[^\n]*\n")


def expand(arguments: list[str], paths: dict[str, str]) -> list[str]:
    return [argument.format(**paths) for argument in arguments]


def run(command: list[str], cwd: Path, stdin: bytes | None, env: dict[str, str] | None = None):
    result = subprocess.run(command, cwd=cwd, input=stdin if stdin is not None else b"",
                            capture_output=True, env=env, timeout=300)
    return result.returncode, result.stdout, result.stderr


def outcome(command: list[str], cwd: Path, stdin: bytes | None, files: list[str], env=None):
    if cwd.exists():
        shutil.rmtree(cwd)
    cwd.mkdir(parents=True)
    status, out, err = run(command, cwd, stdin, env)
    if "--tier-stats" in command:
        err = TIMES.sub(b"<time>", err)
    written = {name: (cwd / name).read_bytes() if (cwd / name).exists() else None for name in files}
    return status, out, err, written


def describe(a, b) -> str:
    for label, x, y in (("status", a[0], b[0]), ("stdout", a[1], b[1]), ("stderr", a[2], b[2]), ("files", a[3], b[3])):
        if x != y:
            text_x = x if not isinstance(x, bytes) else x.decode(errors="replace")
            text_y = y if not isinstance(y, bytes) else y.decode(errors="replace")
            return f"{label} differs:\n    {str(text_x)[:400]!r}\n    {str(text_y)[:400]!r}"
    return ""


def parity(bin_dir: Path) -> None:
    commands, tools = registry()
    check_lists(tools)
    covered = {case[0][0] for case in CASES}
    for command in commands:
        if command != "runtime":
            check(command in covered, f"a parity case runs `neura {command}`")
    neura = bin_dir / f"neura{SUFFIX}"
    with tempfile.TemporaryDirectory() as temporary:
        # The tools print canonical paths (/private/var/... on macOS).
        work = Path(temporary).resolve()
        paths = prepare(neura, work / "inputs")
        compared = 0
        for index, (arguments, stdin, files) in enumerate(CASES):
            unified = expand(arguments, paths)
            tool, word, _ = commands[unified[0]]
            reference = outcome([str(neura)] + unified, work / f"{index}-neura", stdin, files)
            forms = [[str(bin_dir / f"{tool}{SUFFIX}"), word] + unified[1:]]
            default = tools[tool][1][0]
            rest = unified[1:]
            if word == default and not (rest and (rest[0] in tools[tool][1] or rest[0] in ("--help", "--version"))):
                forms.append([str(bin_dir / f"{tool}{SUFFIX}")] + rest)
            for form in forms:
                direct = outcome(form, work / f"{index}-direct", stdin, files)
                difference = describe(reference, direct)
                check(not difference, f"`neura {' '.join(arguments)}` and `{Path(form[0]).name} "
                                      f"{' '.join(form[1:2])}...`: {difference}")
                compared += 1
        # The forms `neura` answers itself.
        version = run([str(neura), "--version"], work, None)
        for tool in tools:
            check(run([str(bin_dir / f"{tool}{SUFFIX}"), "--version"], work, None) == version,
                  f"{tool} --version prints neura --version")
            status, out, _ = run([str(bin_dir / f"{tool}{SUFFIX}"), "--help"], work, None)
            # A tool with no command words (the server host) serves no `neura`
            # command directly; its help names what starts it.
            heading = f"{tool}: the direct form of" if tools[tool][1] else f"{tool}: the host that neura serve starts"
            check(status == 0 and out.startswith(heading.encode()), f"{tool} --help")
        check(run([str(neura), "version"], work, None) == version, "neura version")
        help_text = run([str(neura), "help"], work, None)
        check(help_text[0] == 0 and help_text[1].startswith(b"usage: neura lsp --stdio\n"), "neura help")
        check(run([str(neura), "--help"], work, None) == help_text, "neura --help")
        check(run([str(neura), "help", "x"], work, None) == (1, b"", b"neura: error: help takes no files\n"),
              "neura help x")
        check(run([str(neura), "--version", "-x"], work, None) ==
              (1, b"", b"neura: error: unknown option for --version: -x\n"), "neura --version -x")
        status, _, err = run([str(neura)], work, None)
        check(status == 1 and err.startswith(b"usage: neura lsp --stdio\n"), "neura without arguments")
        check(run([str(neura), "frobnicate"], work, None)[2].startswith(b"neura: error: expected check, docs,"),
              "neura with an unknown command")
        capabilities = run([str(neura), "capabilities", "--json"], work, None)
        check(capabilities[0] == 0 and json.loads(capabilities[1])["features"] == {
            "lsp": True, "formatter": True, "debugger": True, "testDiscovery": True, "testFilter": True,
            "testReporter": True, "coverage": True, "testCoverage": True, "documentation": True,
            "imageInspection": True}, "neura capabilities --json")
        check(run([str(neura), "capabilities"], work, None) == (1, b"", b"neura: usage: neura capabilities --json\n"),
              "neura capabilities without --json")
        print(f"parity: {len(CASES)} cases, {compared} direct forms compared with neura")


# --------------------------------------------------------------------------
# Missing and mixed components.

def copy_tools(bin_dir: Path, destination: Path, names: list[str], neura: Path | None = None) -> Path:
    destination.mkdir(parents=True)
    shutil.copy2(neura or bin_dir / f"neura{SUFFIX}", destination / f"neura{SUFFIX}")
    for name in names:
        shutil.copy2(bin_dir / f"{name}{SUFFIX}", destination / f"{name}{SUFFIX}")
    # The shared libraries the tools find in lib/neura beside them; a copy
    # without the SDK tools gets no SDK library either.
    (destination / "lib/neura").mkdir(parents=True)
    sdk = any(tool in names for tool in ("neurac", "neura-test"))
    for library in (bin_dir / "lib/neura").glob("libneura-*"):
        if sdk or library.name.startswith("libneura-core"):
            shutil.copy2(library, destination / "lib/neura" / library.name)
    return destination / f"neura{SUFFIX}"


def components(bin_dir: Path, mixed: Path) -> None:
    commands, tools = registry()
    core = [name for name, (component, _) in tools.items() if component == "Core"]
    sdk = [name for name, (component, _) in tools.items() if component == "Sdk"]
    with tempfile.TemporaryDirectory() as temporary:
        # The tools print canonical paths (/private/var/... on macOS).
        work = Path(temporary).resolve()
        paths = prepare(bin_dir / f"neura{SUFFIX}", work / "inputs")

        # A runtime-only directory: the core runs images, the SDK is missing.
        neura = copy_tools(bin_dir, work / "core", core)
        where = str(neura.parent)
        for command, (tool, _, component) in commands.items():
            if component != "Sdk":
                continue
            expected = (f"neura: error: 'neura {command}' needs {tool}, which is not in {where}; it is part of the "
                        "Neura-sdk component. Install the SDK, or reinstall Neura\n")
            check(run([str(neura), command, "x"], work, None) == (1, b"", expected.encode()),
                  f"runtime only: neura {command} reports the missing SDK")
        check(run([str(neura), "run", paths["HELLO_NVR"]], work, None) == (0, b"Hello, World!\n", b""),
              "runtime only: neura run")
        check(run([str(neura.parent / "neura-run"), paths["HELLO_NVR"]], work, None) == (0, b"Hello, World!\n", b""),
              "runtime only: neura-run")
        status, out, _ = run([str(neura), "doctor"], work, None)
        check(status == 0 and b"ok  components: core (runtime only: the Neura-sdk component is not installed)\n" in out
              and b"ok  sdk: not installed; source commands need Neura-sdk\n" in out and b"stdlib" not in out,
              f"runtime only: neura doctor is healthy\n{out.decode()}")
        capabilities = json.loads(run([str(neura), "capabilities", "--json"], work, None)[1])["features"]
        check(capabilities == {"lsp": False, "formatter": False, "debugger": False, "testDiscovery": False,
                               "testFilter": False, "testReporter": False, "coverage": False, "testCoverage": False,
                               "documentation": False, "imageInspection": True},
              "runtime only: capabilities reports the missing tools")
        check(run([str(neura), "--version"], work, None) == run([str(bin_dir / "neura"), "--version"], work, None),
              "runtime only: neura --version")

        # A core tool missing.
        neura = copy_tools(bin_dir, work / "no-run", [name for name in core + sdk if name != "neura-run"])
        expected = (f"neura: error: 'neura run' needs neura-run, which is not in {neura.parent}; it is part of the "
                    "Neura core, so this installation is incomplete. Reinstall Neura\n")
        check(run([str(neura), "run", paths["HELLO_NVR"]], work, None) == (1, b"", expected.encode()),
              "missing neura-run")
        status, out, _ = run([str(neura), "doctor"], work, None)
        check(status == 1 and f"FAIL  components: the Neura core is incomplete: neura-run missing from {neura.parent}. "
              "Reinstall Neura\n".encode() in out, f"doctor reports the missing core tool\n{out.decode()}")

        # Part of the SDK missing.
        neura = copy_tools(bin_dir, work / "no-fmt", [name for name in core + sdk if name != "neura-fmt"])
        status, out, _ = run([str(neura), "doctor"], work, None)
        check(status == 1 and f"FAIL  components: the Neura-sdk component is incomplete: neura-fmt missing from "
              f"{neura.parent}. Reinstall Neura\n".encode() in out, f"doctor reports the incomplete SDK\n{out.decode()}")

        # The service component of `neura runtime`, in a copy without neurad.
        core_dir = work / "core"
        expected = (f"neura: error: 'neura runtime' needs the Neura-runtime service component (neurad), which is not "
                    f"in {core_dir}. 'neura run' runs images directly without it\n")
        check(run([str(core_dir / "neura"), "runtime", "status"], work, None) == (1, b"", expected.encode()),
              "neura runtime without neurad")

        # A dispatcher from another build: every tool refuses it.
        neura = copy_tools(bin_dir, work / "mixed", core + sdk, mixed)
        own = run([str(bin_dir / "neura"), "--version"], work, None)[1].decode().strip()
        for command, (tool, _, component) in commands.items():
            if component == "Service":
                continue
            expected = (f"neura: error: {neura.parent / tool} is from {own}, but the neura that started it is "
                        f"neura 0.0.0-mixed (test, commit test); every tool in {neura.parent} must come from one build. "
                        "Reinstall Neura\n")
            check(run([str(neura), command, paths["HELLO_NVR"]], work, None) == (1, b"", expected.encode()),
                  f"mixed: neura {command} is refused")
        print("components: runtime-only, missing core tool, incomplete SDK, service and mixed-build checks ran")


# --------------------------------------------------------------------------
# The build tree against an installed prefix.

def install(build: Path, cmake: str) -> None:
    commands, tools = registry()
    with tempfile.TemporaryDirectory() as temporary:
        # The tools print canonical paths (/private/var/... on macOS).
        work = Path(temporary).resolve()
        prefix = work / "pré fix"
        subprocess.run([cmake, "--install", str(build), "--prefix", str(prefix)], check=True, capture_output=True)
        links = work / "link bin"
        links.mkdir()
        (links / "neura").symlink_to(prefix / "bin/neura")
        env = dict(os.environ, PATH=f"{links}{os.pathsep}/usr/bin{os.pathsep}/bin")
        built = build / "neura"
        paths = prepare(built, work / "inputs")

        def normal(result, bin_dir: Path, library: Path):
            status, out, err, written = result
            def clean(data: bytes) -> bytes:
                return data.replace(str(bin_dir).encode(), b"<bin>").replace(str(library).encode(), b"<stdlib>")
            # A .nir file records the absolute paths of its standard library
            # sources, which differ by design; images do not.
            written = {name: data for name, data in written.items() if not name.endswith(".nir")}
            return status, clean(out), clean(err), written

        compared = 0
        for index, (arguments, stdin, files) in enumerate(CASES):
            unified = expand(arguments, paths)
            tree = normal(outcome([str(built)] + unified, work / f"{index}-tree", stdin, files, env),
                          build.resolve(), REPOSITORY / "stdlib")
            installed = normal(outcome(["neura"] + unified, work / f"{index}-installed", stdin, files, env),
                               prefix / "bin", prefix / "share/neura/stdlib")
            if unified[0] == "doctor":
                # The install's own lines name different files; compare the rest.
                keep = (b"ok  components", b"ok  install", b"FAIL", b"ok  version")
                tree = (tree[0], b"".join(l for l in tree[1].splitlines(True) if l.startswith(keep)), tree[2], tree[3])
                installed = (installed[0], b"".join(l for l in installed[1].splitlines(True) if l.startswith(keep)),
                             installed[2], installed[3])
            difference = describe(tree, installed)
            check(not difference, f"build tree and install differ for `neura {' '.join(arguments)}`: {difference}")
            compared += 1

        # Each direct tool is found by name when the install's bin/ is on PATH.
        direct_env = dict(os.environ, PATH=f"{prefix / 'bin'}{os.pathsep}/usr/bin{os.pathsep}/bin")
        for tool in tools:
            check(run([tool, "--version"], work, None, direct_env) == run([str(built), "--version"], work, None),
                  f"installed {tool} on PATH")
        out = outcome(["neurac", paths["HELLO"].replace("\\", "/"), "-o", "out.nvr"], work / "direct", None,
                      ["out.nvr"], direct_env)
        check(out[0] == 0 and out[3]["out.nvr"] == Path(paths["HELLO_NVR"]).read_bytes(),
              "installed neurac on PATH builds the same image")

        # A relocated copy resolves its tools and library from its new place.
        moved = work / "moved"
        shutil.copytree(prefix, moved, symlinks=True)
        shutil.rmtree(prefix)
        out = outcome([str(moved / "bin/neura"), "build", paths["HELLO"], "-o", "out.nvr"], work / "moved-run", None,
                      ["out.nvr"])
        check(out[0] == 0 and out[3]["out.nvr"] == Path(paths["HELLO_NVR"]).read_bytes(), "relocated install builds")

        # Two installations: the one first on PATH has a different compiler,
        # which `neura` from the other one must not run.
        other = work / "other"
        shutil.copytree(moved, other, symlinks=True)
        (other / "bin/neurac").unlink()
        (other / "bin/neurac").write_text("#!/bin/sh\necho 'the other installation'\n")
        (other / "bin/neurac").chmod(0o755)
        both = dict(os.environ, PATH=f"{other / 'bin'}{os.pathsep}{moved / 'bin'}{os.pathsep}/usr/bin{os.pathsep}/bin")
        out = outcome([str(moved / "bin/neura"), "build", paths["HELLO"], "-o", "out.nvr"], work / "pinned", None,
                      ["out.nvr"], both)
        check(out[0] == 0 and out[1] == b"" and out[3]["out.nvr"] is not None,
              "neura runs the compiler of its own installation, not the one first on PATH")
        out = run(["neura", "build", paths["HELLO"], "-o", "x.nvr"], work, None, both)
        check(out == (0, b"the other installation\n", b""), "the shell finds the other installation's neura by PATH")
        print(f"install: {compared} cases compared between the build tree and an install on PATH")


def main() -> int:
    if len(sys.argv) >= 3 and sys.argv[1] == "parity":
        parity(Path(sys.argv[2]))
    elif len(sys.argv) >= 4 and sys.argv[1] == "components":
        components(Path(sys.argv[2]), Path(sys.argv[3]))
    elif len(sys.argv) >= 4 and sys.argv[1] == "install":
        install(Path(sys.argv[2]), sys.argv[3])
    else:
        sys.exit(__doc__)
    print(f"{len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
