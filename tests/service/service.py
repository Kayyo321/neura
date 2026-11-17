#!/usr/bin/env python3
"""The resident runtime service (docs/design/Service030.md), end to end.

    service.py SUITE BIN_DIR

BIN_DIR is the build directory that holds `neura`, its sibling tools and,
from the service milestone, `neurad`. Every suite runs in a fresh directory
with its own XDG_RUNTIME_DIR, XDG_STATE_HOME and HOME, so suites that run
side by side never meet each other's supervisor, and every supervisor a
suite started is shut down and killed when it ends. Each check names the
edge case of the note it covers.

Exit status 0 when every check passes.
"""

from __future__ import annotations

import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if condition:
        print("ok  ", message)
    else:
        failures.append(message)
        print("FAIL", message)


class Work:
    """A directory with its own run, state and home directories."""

    def __init__(self, bin_dir: Path):
        self.bin = bin_dir.resolve()
        self.neura = self.bin / "neura"
        self.temporary = tempfile.TemporaryDirectory(prefix="nsv-", dir="/tmp")
        self.root = Path(self.temporary.name).resolve()
        self.run_dir = self.root / "r"
        self.state = self.root / "s"
        self.home = self.root / "h"
        for directory in (self.run_dir, self.state, self.home):
            directory.mkdir(mode=0o700)
        self.env = dict(os.environ)
        self.env.update(XDG_RUNTIME_DIR=str(self.run_dir), XDG_STATE_HOME=str(self.state), HOME=str(self.home),
                        NEURA_GPU="off")
        self.env.pop("NEURA_JIT", None)
        self.images: dict[str, Path] = {}

    def image(self, name: str, source: str | None = None, path: Path | None = None) -> Path:
        """Builds a program, given as text or as a path, into an image."""
        if name in self.images:
            return self.images[name]
        if path is None:
            path = self.root / f"{name}.nra"
            path.write_text(source or "")
        image = self.root / f"{name}.nvr"
        status, _, err = self.run(["build", str(path), "-o", str(image)])
        if status != 0:
            raise SystemExit(f"{name}: build failed\n{err.decode(errors='replace')}")
        self.images[name] = image
        return image

    def run(self, arguments: list[str], stdin: bytes | None = None, env: dict | None = None,
            cwd: Path | None = None, timeout: float = 120) -> tuple[int, bytes, bytes]:
        result = subprocess.run([str(self.neura), *arguments], input=stdin if stdin is not None else b"",
                                capture_output=True, env=env or self.env, cwd=cwd or self.root, timeout=timeout)
        return result.returncode, result.stdout, result.stderr

    def files_under(self, directory: Path) -> list[str]:
        if not directory.exists():
            return []
        return sorted(str(p.relative_to(directory)) for p in directory.rglob("*"))

    def close(self) -> None:
        # Stop whatever supervisor this suite started, then remove the files.
        try:
            subprocess.run([str(self.neura), "runtime", "shutdown", "--terminate", "--timeout=5"], env=self.env,
                           capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            pass
        for pid in supervisors(self):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.temporary.cleanup()


def supervisors(work: Work) -> list[int]:
    """The processes whose environment names this suite's run directory."""
    found = []
    marker = f"XDG_RUNTIME_DIR={work.run_dir}".encode()
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if marker not in (entry / "environ").read_bytes().split(b"\0"):
                continue
            command = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        if b"neurad" in command:
            found.append(int(entry.name))
    return found


HELLO = REPOSITORY / "examples/hello-world/hello-world.nra"


# --------------------------------------------------------------------------
# Step 3: the options and the direct default.

def cli_options(work: Work) -> None:
    hello = str(work.image("hello", path=HELLO))
    usage = [
        (["--runtime=bogus"], "--runtime expects off, auto, or required", "case 5: --runtime=bogus"),
        (["--runtime"], "--runtime expects off, auto, or required", "case 5: --runtime without a value"),
        (["--detach"], "--detach needs --runtime=auto or --runtime=required", "case 6: --detach alone"),
        (["--detach", "--runtime=off"], "--detach needs --runtime=auto or --runtime=required",
         "case 6: --detach --runtime=off"),
        (["--detach", "--runtime=auto", "--stdout=out.txt"], "--detach needs --stdout and --stderr",
         "case 6: --detach --runtime=auto without --stderr"),
        (["--stdout=out.txt"], "--stdout, --stderr and --stdin apply only with --detach",
         "--stdout without --detach"),
    ]
    for options, message, label in usage:
        status, out, err = work.run(["run", *options, hello])
        check((status, out, err) == (1, b"", f"neura: runtime: {message}\n".encode()), label)
    for form in ([str(work.neura), "run"], [str(work.bin / "neura-run")], [str(work.bin / "neura-run"), "run"]):
        result = subprocess.run([*form, "--runtime=bogus", hello], capture_output=True, env=work.env, cwd=work.root)
        check(result.returncode == 1 and result.stderr == b"neura: runtime: --runtime expects off, auto, or required\n",
              f"case 5 through {Path(form[0]).name}")
    status, out, err = work.run(["run", "--runtime=off", hello])
    check((status, out, err) == (0, b"Hello, World!\n", b""), "--runtime=off is a direct run")
    status, out, err = work.run(["test", "--runtime=required", str(HELLO)])
    check((status, out) == (1, b"") and err == b"neura: runtime: neura test runs directly; --runtime applies only to run\n",
          "case 59: neura test --runtime=required is a usage error")
    status, _, err = work.run(["build", "--runtime=auto", str(HELLO), "-o", str(work.root / "x.nvr")])
    check(status == 1 and b"--runtime applies only to run" in err, "--runtime applies only to run")
    check(not (work.root / "out.txt").exists(), "no rejected launch opened its output file")


def debug_session(work: Work, program: Path, extra: dict | None = None) -> tuple[bool, list[dict]]:
    """A debug launch run to its end over the adapter protocol."""
    process = subprocess.Popen([str(work.neura), "debug", "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=work.env, cwd=work.root)
    sequence = 0
    messages: list[dict] = []

    def send(command: str, arguments: dict) -> None:
        nonlocal sequence
        sequence += 1
        data = json.dumps({"seq": sequence, "type": "request", "command": command, "arguments": arguments}).encode()
        process.stdin.write(b"Content-Length: " + str(len(data)).encode() + b"\r\n\r\n" + data)
        process.stdin.flush()

    def receive() -> dict | None:
        headers = {}
        while True:
            line = process.stdout.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            key, value = line.decode().split(":", 1)
            headers[key.lower()] = value.strip()
        return json.loads(process.stdout.read(int(headers["content-length"])))

    try:
        send("initialize", {"adapterID": "neura", "linesStartAt1": True, "columnsStartAt1": True})
        send("launch", {"program": str(program), "cwd": str(work.root), **(extra or {})})
        configured = False
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            message = receive()
            if message is None:
                break
            messages.append(message)
            if message.get("event") == "initialized" and not configured:
                configured = True
                send("configurationDone", {})
            if message.get("type") == "response" and message.get("command") == "launch" and not message["success"]:
                break
            if message.get("event") in ("terminated", "exited"):
                break
        send("disconnect", {})
    finally:
        try:
            process.stdin.close()
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    ended = any(m.get("event") in ("terminated", "exited") for m in messages)
    return ended, messages


def off_default(work: Work) -> None:
    """Cases 1 and 2: the defaults never look for the service."""
    hello = str(work.image("hello", path=HELLO))
    before_run = work.files_under(work.run_dir)
    status, out, err = work.run(["run", hello])
    check((status, out, err) == (0, b"Hello, World!\n", b""), "case 1: plain neura run")
    status, out, err = subprocess.run([str(work.bin / "neura-run"), hello], capture_output=True, env=work.env,
                                      cwd=work.root).returncode, b"", b""
    check(status == 0, "case 1: plain neura-run")
    tests = work.root / "suite.nra"
    tests.write_text("let test = import:Std.Test;\n\nlet Suite = module (\n"
                     "    export let test_adds = func() -> void {\n        test.assert(1 + 1 == 2);\n    };\n);\n")
    status, out, _ = work.run(["test", str(tests)])
    check(status == 0, "case 2: plain neura test")
    ended, _ = debug_session(work, HELLO)
    check(ended, "case 2: a debug launch runs to its end")
    check(work.files_under(work.run_dir) == before_run == [], "cases 1, 2: nothing appears in the run directory")
    check(work.files_under(work.state) == [], "cases 1, 2: nothing appears in the state directory")

    # The run and state directories unwritable, and no neurad next to neura.
    os.chmod(work.run_dir, 0o500)
    os.chmod(work.state, 0o500)
    try:
        copy = work.root / "copy"
        copy.mkdir()
        for entry in work.bin.iterdir():
            if entry.is_file() and os.access(entry, os.X_OK) and (entry.name == "neura" or entry.name.startswith("neura-")
                                                                  or entry.name == "neurac"):
                shutil.copy2(entry, copy / entry.name)
        shutil.copytree(work.bin / "lib", copy / "lib")
        if (work.bin / "stdlib").exists():
            os.symlink(work.bin / "stdlib", copy / "stdlib")
        check(not (copy / "neurad").exists(), "case 2: the copy has no neurad")
        result = subprocess.run([str(copy / "neura"), "run", hello], capture_output=True, env=work.env, cwd=work.root)
        check((result.returncode, result.stdout, result.stderr) == (0, b"Hello, World!\n", b""),
              "case 2: neura run without neurad and with an unwritable run directory")
        result = subprocess.run([str(copy / "neura"), "run", "--runtime=off", hello], capture_output=True,
                                env=work.env, cwd=work.root)
        check(result.returncode == 0, "case 2: --runtime=off without neurad")
    finally:
        os.chmod(work.run_dir, 0o700)
        os.chmod(work.state, 0o700)
    check(work.files_under(work.run_dir) == [], "case 2: still nothing in the run directory")


SUITES = {
    "cli-options": cli_options,
    "off-default": off_default,
}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in SUITES:
        print(__doc__)
        return 2
    work = Work(Path(sys.argv[2]))
    try:
        SUITES[sys.argv[1]](work)
    finally:
        work.close()
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print(f"{sys.argv[1]} passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
