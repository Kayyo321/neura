#!/usr/bin/env python3
"""The class hook operations of NVR 1.8 (docs/runtime/NVR.md, Class hooks).

The compiler emits HookEnter and HookLeave only for programs that declare a
class hook; these images are written by hand: operations.nra is built and
disassembled, and its function `touch` (function 3, a field read) is rewritten
with the operations. Each image runs in every execution mode of the runtime,
with the same output, status and trap in each; the loader rejects malformed
uses, and the assembler and decompiler read and write the operations.

Usage: operations.py <neura> <source-dir>
"""

import os
import pathlib
import re
import subprocess
import sys
import tempfile

NEURA = str(pathlib.Path(sys.argv[1]).resolve())
SOURCE = pathlib.Path(sys.argv[2]).resolve()

# Windows 0.3.0 runs tier 3 through the interpreter even for the shadow check.
NATIVE_JIT = os.name != "nt"
SHADOW_ARGS = ["--jit=on"] if NATIVE_JIT else ["--jit=off"]
SHADOW_ENV = {"NEURA_TIER_EAGER": "1", "NEURA_TIER_CHECK": "2"}
if not NATIVE_JIT:
    SHADOW_ENV["NEURA_TIER3"] = "interp"

MODES = [
    ("tier 0", ["--tier=0"], {}),
    ("tier 1", ["--tier=1"], {}),
    ("tier 2", ["--tier=2"], {}),
    ("jit off", ["--jit=off"], {}),
    *([("jit on", ["--jit=on"], {})] if NATIVE_JIT else []),
    ("tier 3 interp", [], {"NEURA_TIER3": "interp", "NEURA_TIER_EAGER": "1"}),
    ("eager check 2", SHADOW_ARGS, SHADOW_ENV),
    ("programs", [], {"NEURA_TIER_PROGRAMS": "1", "NEURA_TIER_EAGER": "1"}),
]

failures = []


def run(args, env=None, cwd=None):
    environment = dict(os.environ, NEURA_GPU="off")
    environment.update(env or {})
    return subprocess.run([NEURA, *args], capture_output=True, encoding="utf-8", env=environment, cwd=cwd, timeout=120)


def check(condition, message):
    if not condition:
        failures.append(message)


ENTER = "state hook-enter immediate {kind} arguments [ constant:{u32}:{policy} slot:{handle}:0 ] result - resume {next} assign [ ]"
LEAVE = "state hook-leave immediate 0 arguments [ ] result - resume {next} assign [ ]"
READ = "state read-field immediate 0 arguments [ slot:{handle}:0 ] result 1 resume {next} assign [ ]"
RETURN = "return arguments [ slot:{i32}:1 ] assign [ ]"
def function(blocks, policy, header, types):
    text = header + "\n"
    for index, exit in enumerate(blocks):
        line = exit.format(next=index + 1, kind=0, policy=policy, **types)
        text += f"  block {index} graph 0 selector 0 inputs [ ]\n    exit {line}\n"
    return text


CASES = {
    # name: (blocks, expected status, stderr pattern)
    "balanced": ([ENTER, READ, LEAVE, RETURN], 0, None),
    "nested": ([ENTER.replace("{kind}", "0"), ENTER.replace("{kind}", "1"), READ, LEAVE, LEAVE, RETURN], 0, None),
    "recursion": ([ENTER, ENTER, READ, LEAVE, LEAVE, RETURN], 134, r"trap: class hook recursion: get of Session"),
    "leave-twice": ([ENTER, READ, LEAVE, LEAVE, RETURN], 132,
                    r"trap: invalid class hook operation: no event of this frame is open"),
    "open-return": ([ENTER, READ, RETURN], 132,
                    r"trap: invalid class hook operation: a frame returns with a class hook event open"),
}


def main():
    with tempfile.TemporaryDirectory(prefix="neura-hook-ops-") as directory:
        work = pathlib.Path(directory)
        base = work / "operations.nvr"
        result = run(["build", str(SOURCE / "tests/backend/hooks/operations.nra"), "-o", str(base)])
        check(result.returncode == 0, "build failed: " + result.stderr)
        result = run(["disassemble", str(base), "-o", str(work / "operations.nvra")])
        check(result.returncode == 0, "disassemble failed: " + result.stderr)
        with (work / "operations.nvra").open(encoding="utf-8", newline=None) as stream:
            text = stream.read()
        text = re.sub(r"^nvr 1\.\d+ ", "nvr 1.8 ", text, count=1, flags=re.M)
        i32 = re.search(r"^type (\d+) signed 32$", text, re.M)[1]
        u32 = re.search(r"^type (\d+) unsigned 32$", text, re.M)[1]
        session = re.search(rf"^layout (\d+) parent \d+ fields \[ {i32} \]", text, re.M)[1]
        handle = re.search(rf"^type (\d+) object {session}$", text, re.M)[1]
        types = dict(handle=handle, i32=i32, u32=u32)
        touch = re.compile(rf"^(function (\d+) parameters 1 result {i32} frame \[ {handle} {i32} \])\n"
                           r"(?:  block .*\n    exit .*\n)+", re.M)
        match = touch.search(text)
        assert match and "state read-field" in match[0], "the raw touch function was not found"
        header, index = match.groups()
        text = "\n".join(line for line in text.split("\n") if not line.startswith(f"location {index} "))
        constants = [int(m) for m in re.findall(r"^constant (\d+) ", text, re.M)]
        policy = max(constants) + 1
        last = list(re.finditer(r"^constant \d+ .*$", text, re.M))[-1]
        word = int(session) + 1
        text = text[:last.end()] + f"\nconstant {policy} type {u32} bytes {word:02x}000000" + text[last.end():]
        expected_out = run(["run", str(base)]).stdout
        # 65 events with distinct policy words: the 65th passes the limit.
        first = max(int(m) for m in re.findall(r"^constant (\d+) ", text, re.M)) + 1
        last = list(re.finditer(r"^constant \d+ .*$", text, re.M))[-1]
        text = text[:last.end()] + "".join(f"\nconstant {first + i} type {u32} bytes {i + 100:02x}000000"
                                           for i in range(65)) + text[last.end():]
        deep = [ENTER.replace("{policy}", str(first + i)) for i in range(65)] + [READ] + [LEAVE] * 65 + [RETURN]
        CASES["limit"] = (deep, 139, r"trap: class hook nesting limit exceeded: 64 events are open")
        for name, (blocks, status, pattern) in CASES.items():
            source = work / f"{name}.nvra"
            source.write_text(touch.sub(function(blocks, policy, header, types), text, count=1),
                              encoding="utf-8", newline="\n")
            image = work / f"{name}.nvr"
            result = run(["assemble", str(source), "-o", str(image)])
            if result.returncode != 0:
                check(False, f"{name}: assemble failed: {result.stderr}")
                continue
            # The text form is lossless: disassembling and assembling again
            # gives the same bytes.
            again = work / f"{name}-again.nvra"
            run(["disassemble", str(image), "-o", str(again)])
            run(["assemble", str(again), "-o", str(work / f"{name}-again.nvr")])
            check((work / f"{name}-again.nvr").read_bytes() == image.read_bytes(),
                  f"{name}: the image does not round-trip through its text")
            outcomes = set()
            for mode, args, env in MODES:
                result = run(["run", *args, str(image)], env=env)
                stderr = result.stderr.replace(str(image), "IMAGE")
                stderr = "\n".join(l for l in stderr.split("\n") if l and not l.startswith("neura: note")
                                   and not l.startswith("tier"))
                outcomes.add((result.returncode, result.stdout, stderr))
                check(result.returncode == status, f"{name} {mode}: status {result.returncode}, expected {status}")
                if pattern:
                    check(re.search(pattern, result.stderr) is not None, f"{name} {mode}: stderr {result.stderr!r}")
                else:
                    check(result.stdout == expected_out, f"{name} {mode}: output {result.stdout!r}")
            check(len(outcomes) == 1, f"{name}: the modes disagree: {outcomes}")
        decompiled = run(["decompile", str(work / "balanced.nvr")])
        check("hook_enter(get, " in decompiled.stdout and "hook_leave()" in decompiled.stdout and
              "policy Session" in decompiled.stdout,
              "the decompiler names the operations: " + "\n".join(l for l in decompiled.stdout.split("\n") if "hook" in l))
        # Malformed uses: the loader rejects them before anything runs.
        with (work / "balanced.nvra").open(encoding="utf-8", newline=None) as stream:
            balanced = stream.read()
        malformed = {
            "an older version": (balanced.replace("nvr 1.8 ", "nvr 1.7 ", 1), "require NVR 1.8"),
            "a third kind": (balanced.replace("hook-enter immediate 0", "hook-enter immediate 2", 1),
                             "a get (0) or a set (1)"),
            "a missing argument": (balanced.replace(f"arguments [ constant:{u32}:{policy} slot:{handle}:0 ]", f"arguments [ slot:{handle}:0 ]", 1),
                                   "argument"),
            "a result": (balanced.replace("hook-leave immediate 0 arguments [ ] result -", "hook-leave immediate 0 arguments [ ] result 1", 1),
                         "result"),
        }
        for name, (source, fragment) in malformed.items():
            path = work / "malformed.nvra"
            path.write_text(source, encoding="utf-8", newline="\n")
            result = run(["assemble", str(path), "-o", str(work / "malformed.nvr")])
            message = result.stderr
            if result.returncode == 0:
                result = run(["run", str(work / "malformed.nvr")])
                message = result.stderr
            check(result.returncode != 0 and fragment in message, f"malformed ({name}): {message!r}")
    for failure in failures:
        print("FAIL", failure)
    print(f"class hook operations: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
