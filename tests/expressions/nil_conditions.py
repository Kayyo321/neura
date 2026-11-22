#!/usr/bin/env python3
"""Check nil-test lowering, runtime behavior, && type diagnostics."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

NEURA = Path(sys.argv[1]).resolve()
FIXTURES = Path(__file__).resolve().with_name("nil-conditions")

with tempfile.TemporaryDirectory(prefix="nil-conditions-", dir=Path.cwd()) as directory:
    work = Path(directory)
    source = work / "condition.nra"

    def neura(*args, success=True):
        result = subprocess.run([NEURA, *args], cwd=work, capture_output=True, timeout=120)
        assert (result.returncode == 0) == success, (args, result.stderr)
        return result

    for flags in ((), ("--no-optimize",)):
        neura("build", *flags, FIXTURES / "conditions.nra", "-o", "conditions.nvr")
        for _ in range(2):
            result = neura("run", "--runtime=off", "--tier=0", "conditions.nvr")
            assert result.stdout.splitlines() == [b"50", b"3"], result.stdout

    template = """let Item = class ( export let value: int = 7; );
let read = func(val: Item) -> int {
    if (CONDITION) {
        return val.value;
    }
    return 0;
};
"""
    for flags in ((), ("--no-optimize",)):
        source.write_text(template.replace("CONDITION", "val"), encoding="utf-8")
        implicit = neura("emit-ir", *flags, source.name).stdout
        source.write_text(template.replace("CONDITION", "val != nil"), encoding="utf-8")
        explicit = neura("emit-ir", *flags, source.name).stdout
        assert implicit == explicit, flags

    for marked in (
        "if ([7]) {}",
        "while ([text]) {}",
        "if ([State.On]) {}",
        "if ([Error.Failed(1)]) {}",
        "if ([dynamic]) {}",
        "let _ = ![7];",
        "let _ = not [text];",
        "let _ = not [State.On];",
        "if ([item] && true) {}",
        "if (false || [item]) {}",
        "return true unless [item];",
    ):
        line = "    " + marked
        start = line.index("[")
        end = line.index("]")
        source.write_text(
            """let State = enum (Off, On);
let Error = enum (Failed(int));
let Item = class ( export let value: int = 7; );
let condition = func(item: Item, dynamic: any, text: string) -> bool {
"""
            + line.replace("[", "").replace("]", "") + "\n    return false;\n};\n",
            encoding="utf-8",
        )
        short = neura("check", "--error-format=short", source.name, success=False)
        assert f"condition.nra:5:{start + 1}: error[E0403]:".encode() in short.stderr.splitlines()[0], (
            marked, short.stderr,
        )
        detail = neura("check", "--error-format=json", source.name, success=False)
        diagnostic = json.loads(detail.stderr.splitlines()[0])
        primary = next(span for span in diagnostic["spans"] if span["primary"])
        assert (primary["line"], primary["column"], primary["length"]) == (5, start + 1, end - start - 1), (
            marked, primary,
        )

    # The ordinary read after the test may see a new value. A class getter
    # can change the observed value even when its binding is a last.
    program = """let console = import:Std.Io.Console;
let program = import:Std.Program;

let Item = class ( export let value: int = 7; );

let Holder = class (
    export let item: Item = new Item();

    export let clear = func() {
        item = nil;
    };
);

let State = module (
    export let item: Item = new Item();

    export let clear = func() {
        item = nil;
    };
);

let Hooked = class (
    | get: {
        reads += 1;
        if (reads == 1) {
            return instance;
        }
        return nil;
    };

    static let reads: int = 0;
    export let value: int = 7;
);

let Entry = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
BODY
        return program.ExitCode.Ok;
    };
);
"""
    for name, body in (
        ("local", """        let item = new Item();
        if (item) {
            item = nil;
            console.print_line(item.value.to_string());
        }"""),
        ("field", """        let holder = new Holder();
        if (holder.item) {
            holder.clear();
            console.print_line(holder.item.value.to_string());
        }"""),
        ("module", """        if (State.item) {
            State.clear();
            console.print_line(State.item.value.to_string());
        }"""),
        ("getter", """        last item: Hooked = new Hooked();
        if (item) {
            console.print_line(item.value.to_string());
        }"""),
    ):
        source.write_text(program.replace("BODY", body), encoding="utf-8")
        for flags in ((), ("--no-optimize",)):
            neura("build", *flags, source.name, "-o", "conditions.nvr")
            result = neura("run", "--runtime=off", "--tier=0", "conditions.nvr", success=False)
            assert b"nil class reference" in result.stderr, (name, flags, result.stderr)
