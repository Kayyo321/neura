#!/usr/bin/env python3
"""Pin nil summary provenance and the interpretation of legacy artifacts."""

from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


NEURA = Path(sys.argv[1]).resolve()


def run(*args):
    result = subprocess.run([NEURA, *args], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result
    return result.stdout


def summaries(listing):
    names = dict(re.findall(r'^  %(\d+) function "([^"]+)" ', listing, re.MULTILINE))
    return {names[ident]: state
            for ident, state in re.findall(r'^  result %(\d+) (\w+)', listing, re.MULTILINE)}


with tempfile.TemporaryDirectory(prefix="neura-nil-kinds-") as temporary:
    directory = Path(temporary)
    source = directory / "kinds.nra"
    source.write_text("""
let Entry = class ( export last ready = true; );
let Holder = class ( export let required: Entry = new; );

let identity = func(e: Entry) -> Entry => e;
let field = func(h: Holder) -> Entry => h.required;
let promised_call = func(e: Entry) -> Entry => identity(e);
let joined = func(e: Entry, b: bool) -> Entry => b ? e : new Entry();
let allocated = func() -> Entry => new Entry();
let proved_call = func() -> Entry => allocated();
let guarded = func(e: Entry = nil) -> Entry {
    if (e == nil) {
        return new Entry();
    }
    return e;
};
let absent = func() -> Entry => nil;
let from_list = func(items: Entry[]) -> Entry => items[0];
let recursive = func() -> Entry => recursive();
""", encoding="utf-8")
    expected = {
        "identity": "contract_non_nil", "field": "contract_non_nil",
        "promised_call": "contract_non_nil", "joined": "contract_non_nil",
        "allocated": "proved_non_nil", "proved_call": "proved_non_nil", "guarded": "proved_non_nil",
        "absent": "maybe_nil", "from_list": "unknown", "recursive": "unknown",
    }
    ir = run("emit-ir", "--no-optimize", source)
    assert summaries(ir) == expected, summaries(ir)
    facts = re.findall(r'^  non_nil %\d+ at ".+":(\d+):', ir, re.MULTILINE)
    guarded_return = source.read_text().splitlines().index("    return e;") + 1
    assert facts == [str(guarded_return)], facts

    module = source.with_suffix(".nir")
    run("emit-ir", source, "-o", module)
    assert run("emit-ir", "--no-optimize", module) == ir

    # Keep each summary and its original span layout, and simulate artifacts
    # with prerelease result tags that did not distinguish proof and promise.
    legacy = bytearray(module.read_bytes())
    tag = legacy.rindex(b"NILS")
    bindings = struct.unpack_from("<I", legacy, tag + 4)[0]
    result_vector = tag + 8 + 41 * bindings
    results = struct.unpack_from("<I", legacy, result_vector)[0]
    old_codes = {3: 0, 4: 0, 5: 2, 6: 1}
    assert results == len(expected), results
    for i in range(results):
        kind = result_vector + 8 + 41 * i
        legacy[kind] = old_codes[legacy[kind]]
    old_module = directory / "legacy.nir"
    old_module.write_bytes(legacy)
    old_ir = run("emit-ir", "--no-optimize", old_module)
    assert summaries(old_ir) == dict.fromkeys(expected, "unknown"), summaries(old_ir)
    assert old_ir.split("non_nil_uses {")[1] == ir.split("non_nil_uses {")[1]

print("10 result summaries classified and round-tripped, legacy result guarantees dropped, NNIL unchanged")
