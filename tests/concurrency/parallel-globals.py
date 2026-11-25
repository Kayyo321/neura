#!/usr/bin/env python3
"""Globals read by a parallel worker must be copyable into the item's heap."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


NEURA = Path(sys.argv[1]).resolve()
PRELUDE = """
let program = import:Std.Program;
let console = import:Std.Io.Console;
let parallel = import:Std.Concurrent.Parallel;
"""
POINTER = """
let cell = import:Std.Core.Cell;
let pointer = import:Std.Core.Pointer;

let writable: pointer.Pointer<int64> = new(cell.of<int64>(3));
const view: pointer.ConstPointer<int64> = writable.as_const();
let read_pointer = func() -> int => view.read() as int;
"""
MONITOR = """
.Synchronized
let Counter = class (
    let count = 5;
    export let read = func() -> int => count;
);
const monitor = new Counter();
let read_monitor = func() -> int => monitor.read();
"""
FROZEN = """
let frozen = import:Std.Collections.Frozen;

const list = frozen.of<int>(5);
let read_frozen = func() -> int => list.get(0);
"""


def application(declarations, body):
    return PRELUDE + declarations + f"""
let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        {body}
        return program.ExitCode.Ok;
    }};
);
"""


def run(*args, status=0):
    result = subprocess.run([NEURA, *args], capture_output=True, text=True, timeout=30)
    assert result.returncode == status, result
    return result


with tempfile.TemporaryDirectory(prefix="neura-parallel-globals-") as scratch:
    root = Path(scratch)
    for name, declarations, binding, reason in (
        ("pointer", POINTER, "view", "is not transferable"),
        ("monitor", MONITOR, "monitor", "cannot be copied into a parallel item"),
        ("frozen", FROZEN, "list", "is not transferable"),
    ):
        source = root / f"{name}.nra"
        source.write_text(application(
            declarations + f"let work = func(n: int) -> int => n + read_{name}();\n",
            """let result = parallel.map<int, int>([1, 2], work, parallel.limits(100000, 1000));
        console.print_line(result[0].to_string());""",
        ), encoding="utf-8")
        for command in (("check",), ("build", "-o", root / f"{name}.nvr")):
            result = run(*command, "--error-format=json", source, status=1)
            diagnostic = json.loads(result.stderr.splitlines()[0])
            assert diagnostic["code"] == "E0601", diagnostic
            message = diagnostic["message"]
            assert f"uses the binding '{binding}'" in message and reason in message, diagnostic
            assert f"reaches 'read_{name}'" in message, diagnostic

    # A read view stays in its task: a spawned call cannot read it either.
    shared_view = root / "shared-view.nra"
    shared_view.write_text(application(POINTER, """scope {
            let pointer_result = spawn read_pointer();
            console.print_line(pointer_result.join().to_string());
        }"""), encoding="utf-8")
    result = run("check", "--error-format=json", shared_view, status=1)
    diagnostic = json.loads(result.stderr.splitlines()[0])
    assert diagnostic["code"] == "E0601", diagnostic
    assert "uses the binding 'view'" in diagnostic["message"] and "is not transferable" in diagnostic["message"], diagnostic

    good = root / "copy-or-share.nra"
    good.write_text(application(MONITOR + """
.Immutable
.Final
let Fixed = class ( export last value = 7; );
const fixed = new Fixed();
let work = func(n: int) -> int => n + fixed.value;
""", """let shared_total = 0;
        scope {
            let monitor_result = spawn read_monitor();
            shared_total = monitor_result.join();
        }
        console.print_line(shared_total.to_string());
        let copied = parallel.map<int, int>([1, 2], work, parallel.limits(100000, 1000));
        console.print_line(copied[0].to_string());"""), encoding="utf-8")
    for optimize in ((), ("--no-optimize",)):
        image = good.with_suffix(".nvr")
        run("build", *optimize, good, "-o", image)
        for workers in ("1", "4"):
            result = run("run", "--runtime=off", "--parallel-workers", workers, image)
            assert result.stdout == "5\n8\n", result

print("3 const global types rejected for parallel workers, a read view rejected for tasks; spawn sharing and "
      "immutable copies preserved")
