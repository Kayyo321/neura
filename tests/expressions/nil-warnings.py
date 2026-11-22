#!/usr/bin/env python3
"""Check nil warnings, fixes, warning controls, and imported summaries."""

import json
import pathlib
import subprocess
import sys
import tempfile


CODES = {"W0006", "W0007"}


def run(neura, *args, status=0):
    result = subprocess.run([neura, *args], capture_output=True, encoding="utf-8", timeout=120)
    if result.returncode != status:
        raise AssertionError(f"{args} exited {result.returncode}, expected {status}\n{result.stderr}")
    return result


def warnings(neura, source, *options, status=0):
    result = run(neura, "check", "--error-format=json", *options, source, status=status)
    return [value for value in (json.loads(line) for line in result.stderr.splitlines() if line.startswith("{"))
            if value["code"] in CODES]


def primary(value):
    return next(span for span in value["spans"] if span["primary"])


def key(value):
    span = primary(value)
    return value["code"], span["line"], span["column"], span["length"], value["args"]["cause"]


def expected_warnings(source):
    results = []
    for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), start=1):
        code, annotation, rest = line.partition("// W")
        if not annotation:
            continue
        warning, receiver, cause = ("W" + rest).split()
        offset = code.rfind(receiver)
        if offset == -1:
            raise AssertionError(f"{receiver!r} in fixture metadata is absent from line {number}")
        results.append((warning, number, offset + 1, len(receiver), cause))
    return sorted(results)


def check_suggestions(values):
    for value in values:
        code, line, _, _, cause = key(value)
        replacements = [edit["replacement"] for suggestion in value["suggestions"] for edit in suggestion["edits"]]
        use = value["args"]["use"]
        if code == "W0006" and use in {"field", "method"}:
            if "?." not in replacements:
                raise AssertionError(f"missing optional access suggestion on line {line}")
            if not any(replacement.startswith("if (") for replacement in replacements):
                raise AssertionError(f"missing narrowing suggestion on line {line}")
        if use == "field" and value["args"]["binding"] == "maybe":
            if not any("let guarded_value = holder.maybe;" in replacement for replacement in replacements):
                raise AssertionError("an unstable field must be read into a local in the narrowing fix")
        if code == "W0007" and not any("<non-nil" in replacement for replacement in replacements):
            raise AssertionError(f"missing non-nil argument or assignment suggestion on line {line}")
        if cause == "inferred-return" and not any("can return nil here" in (s["label"] or "") for s in value["spans"]):
            raise AssertionError(f"missing cause at the inferred nil return for line {line}")


def check_summaries(neura, directory):
    library = directory / "library.nra"
    library.write_text("""export let Library = module (
    export let Entry = class ( export let ready = true; );
    export let present = func() -> Entry => new;
    export let absent = func() -> Entry => nil;
);
""", encoding="utf-8")
    artifact = library.with_suffix(".nir")
    run(neura, "emit-ir", library, "-o", artifact)
    if b"NILS" not in artifact.read_bytes():
        raise AssertionError("the library has no persisted nil summaries")
    source_listing = run(neura, "emit-ir", "--no-optimize", library).stdout
    artifact_listing = run(neura, "emit-ir", "--no-optimize", artifact).stdout
    if source_listing != artifact_listing:
        raise AssertionError("NeuraIR round-trip changed the nullability summaries")
    consumer = directory / "consumer.nra"
    consumer.write_text("""let library = import:"library.nir".Library;
let known = func() -> bool => library.present().ready;
let maybe = func() -> bool => library.absent().ready;
""", encoding="utf-8")
    values = warnings(neura, consumer)
    if len(values) != 1 or key(values[0]) != ("W0006", 3, 31, 16, "inferred-return"):
        raise AssertionError(f"imported return summary vanished or had the wrong span: {[key(v) for v in values]}")
    if not any(s["line"] == 4 and s["file"].endswith("library.nra") and "return nil" in (s["label"] or "")
               for s in values[0]["spans"]):
        raise AssertionError("an imported summary lost the original return location")


def main():
    neura = pathlib.Path(sys.argv[1]).resolve()
    source = pathlib.Path(sys.argv[2]).resolve()
    values = warnings(neura, source)
    expected = expected_warnings(source)
    actual = sorted(map(key, values))
    if actual != expected:
        raise AssertionError(f"missing {sorted(set(expected) - set(actual))}, unexpected {sorted(set(actual) - set(expected))}")
    check_suggestions(values)
    for name, disabled in (("nil-access", "W0006"), ("nil-argument", "W0007")):
        remaining = sorted(map(key, warnings(neura, source, "-Wno-" + name)))
        if remaining != [item for item in expected if item[0] != disabled]:
            raise AssertionError(f"-Wno-{name} did not suppress only its warning")
    if {value["severity"] for value in warnings(neura, source, "-Werror", status=1)} != {"error"}:
        raise AssertionError("-Werror failed to promote the new warnings")
    with tempfile.TemporaryDirectory(prefix="neura-nil-warnings-") as directory:
        output = pathlib.Path(directory)
        check_summaries(neura, output)
        denied = output / "denied.nra"
        denied.write_text("""let Entity = class ( export let ready = true; );

.Deny("nil-access")
let read = func(item: Entity = nil) -> bool => item.ready;
""", encoding="utf-8")
        values = warnings(neura, denied, "-Wno-nil-access", status=1)
        if len(values) != 1 or values[0]["severity"] != "error":
            raise AssertionError("a declaration's Deny must override -Wno-nil-access")
    print(f"{len(expected)} use spans, warning controls, quick fixes, and imported summaries verified")


if __name__ == "__main__":
    main()
