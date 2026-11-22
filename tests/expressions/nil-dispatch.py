#!/usr/bin/env python3
"""Open virtual and interface calls have no persisted nil result guarantee."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


NEURA = Path(sys.argv[1]).resolve()


def run(*args, status=0):
    result = subprocess.run([NEURA, *args], capture_output=True, text=True, timeout=30)
    assert result.returncode == status, result
    return result


def state(listing, name):
    (function,) = re.findall(r'^  %(\d+) function "' + name + r'" ', listing, re.MULTILINE)
    (kind,) = re.findall(r'^  result %' + function + r' (\w+)', listing, re.MULTILINE)
    return kind


def warnings(source):
    result = run("check", "--error-format=json", source)
    return [item for line in result.stderr.splitlines() if line.startswith("{")
            if (item := json.loads(line))["code"] in {"W0006", "W0007"}]


with tempfile.TemporaryDirectory(prefix="neura-nil-dispatch-") as temporary:
    work = Path(temporary)
    source = work / "dispatch.nra"
    source.write_text("""
let Entry = class ( export last ready = true; );

let Provider = interface (
    let get = func() -> Entry => new Entry();
);
let NilProvider = class implements Provider (
    export let get = func() -> Entry => nil;
);

let Base = class (
    export let make = func() -> Entry => new Entry();
    .Final
    export let final_method = func() -> Entry => nil;
);
.Final
let Derived = class:Base (
    export let.override make = func() -> Entry => nil;
);

let wrap_iface = func(p: Provider) -> Entry => p.get();
let wrap_base = func(p: Base) -> Entry => p.make();
let wrap_final_class = func(p: Derived) -> Entry => p.make();
let wrap_final_method = func(p: Base) -> Entry => p.final_method();
let via_open_interface = func(p: Provider) -> bool => wrap_iface(p).ready;
let via_open_class = func(p: Base) -> bool => wrap_base(p).ready;
let via_final_class = func(p: Derived) -> bool => wrap_final_class(p).ready;
let via_final_method = func(p: Base) -> bool => wrap_final_method(p).ready;
""", encoding="utf-8")
    ir = run("emit-ir", "--no-optimize", source).stdout
    for name in ("wrap_iface", "wrap_base"):
        assert state(ir, name) == "unknown", (name, state(ir, name))
    for name in ("wrap_final_class", "wrap_final_method"):
        assert state(ir, name) == "maybe_nil", (name, state(ir, name))
    found = warnings(source)
    assert {item["args"]["function"] for item in found} == {"wrap_final_class", "wrap_final_method"}, found
    assert len(found) == 2, found

    library = work / "library.nra"
    library.write_text("""
export let Library = module (
    export let Entry = class ( export last ready = true; );
    export let Base = class (
        export let make = func() -> Entry => new Entry();
    );
    export let wrap = func(b: Base) -> Entry => b.make();
);
""", encoding="utf-8")
    module = library.with_suffix(".nir")
    run("emit-ir", library, "-o", module)
    assert state(run("emit-ir", "--no-optimize", module).stdout, "wrap") == "unknown"
    consumer = work / "consumer.nra"
    consumer.write_text("""
let program = import:Std.Program;
let library = import:"library.nir".Library;
let Derived = class:library.Base (
    export let.override make = func() -> library.Entry => nil;
);
let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let d: library.Base = new Derived();
        return library.wrap(d).ready ? program.ExitCode.Ok : program.ExitCode.Failure;
    };
);
""", encoding="utf-8")
    assert not warnings(consumer)
    image = work / "consumer.nvr"
    run("build", consumer, "-o", image)
    assert "nil class reference" in run("run", "--runtime=off", image, status=139).stderr

print("Open interface and class calls stay unknown, final targets retain nil diagnostics, imported dispatch traps")
