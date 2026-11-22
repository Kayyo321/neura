#!/usr/bin/env python3
"""Check lowered nil-narrowing use spans, persistence and image erasure."""

import pathlib
import re
import struct
import subprocess
import sys
import tempfile


def run(neura, *args, error=None):
    result = subprocess.run([neura, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            encoding="utf-8", timeout=120)
    if error is not None:
        if result.returncode == 0 or error not in result.stderr:
            raise AssertionError(f"expected {error!r} from {args}: {result.stderr}")
    elif result.returncode:
        raise AssertionError(f"{args} failed: {result.stderr}")
    return result.stdout


def source_facts(ir, filename):
    symbols = dict(re.findall(r'^  %(\d+) (?:parameter|local|global) "([^"]+)"', ir, re.MULTILINE))
    uses = set()
    for result in re.finditer(r'^  non_nil %(\d+) at "(.+)":(\d+):(\d+)\+(\d+)$', ir, re.MULTILINE):
        binding, path, line, column, length = result.groups()
        if pathlib.PurePosixPath(path.replace("\\", "/")).name == filename:
            uses.add((symbols[binding], int(line), int(column), int(length)))
    return uses


def compare_annotations(source, facts):
    proved = 0
    unproved = 0
    for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), start=1):
        code, sep, annotation = line.partition("//")
        if not sep:
            continue
        expectation, name = annotation.strip().split()
        columns = list(re.finditer(rf"\b{re.escape(name)}\b", code))
        if not columns:
            raise AssertionError(f"no use of {name!r} on line {number}")
        fact = name, number, columns[-1].start() + 1, len(name)
        if (fact in facts) != (expectation == "non-nil"):
            raise AssertionError(f"expected {expectation} at {source.name}:{number}:{fact[2]} for {name}")
        proved += expectation == "non-nil"
        unproved += expectation == "maybe-nil"
    print(f"{proved} proven and {unproved} unproven use spans matched")


def check_linkage(neura, directory):
    prelude = directory / "prelude.nra"
    prelude.write_text("export let Prelude = module ( export const Flag = true; );\n", encoding="utf-8")
    library = directory / "library.nra"
    library.write_text("""export let Library = module (
    export let Entry = class ( export last ready = true; );
    export let ready = func(entry: Entry) -> bool {
        if (entry) {
            return entry.ready;
        }
        return false;
    };
);
""", encoding="utf-8")
    for path in (prelude, library):
        run(neura, "emit-ir", path, "-o", path.with_suffix(".nir"))
    consumer = directory / "consumer.nra"
    consumer.write_text('let prelude = import:"prelude.nir".Prelude;\n'
                        'let library = import:"library.nir".Library;\n', encoding="utf-8")
    linked = run(neura, "emit-ir", "--no-optimize", consumer)
    if source_facts(linked, library.name) != {("entry", 5, 20, 5)}:
        raise AssertionError("prebuilt library's proof of a non-nil read was lost during symbol remapping")


def main():
    neura = pathlib.Path(sys.argv[1]).resolve()
    source = pathlib.Path(sys.argv[2]).resolve()
    ir = run(neura, "emit-ir", "--no-optimize", source)
    facts = source_facts(ir, source.name)
    compare_annotations(source, facts)
    with tempfile.TemporaryDirectory(prefix="neura-nil-narrowing-") as directory:
        output = pathlib.Path(directory)
        nir = output / "lowered.nir"
        stripped = output / "stripped.nir"
        old_minor = output / "old-minor.nir"
        malformed = output / "malformed.nir"
        run(neura, "emit-ir", source, "-o", nir)
        read = source_facts(run(neura, "emit-ir", "--no-optimize", nir), source.name)
        if read != facts:
            raise AssertionError("binary NeuraIR did not preserve the set of use spans")

        data = nir.read_bytes()
        if struct.unpack_from("<HH", data, 4) != (1, 16):
            raise AssertionError("facts must use NeuraIR 1.16")
        # NNIL is the last optional table. Each record is one 36-byte Span
        # followed by one 4-byte symbol reference.
        tag = data.rindex(b"NNIL")
        count, = struct.unpack_from("<I", data, tag + 4)
        if count < len(facts) or tag + 8 + 40 * count != len(data):
            raise AssertionError("invalid table size or record count")
        summaries = data.rfind(b"NILS", 0, tag)
        stripped.write_bytes(data[:summaries if summaries >= 0 else tag])
        if source_facts(run(neura, "emit-ir", "--no-optimize", stripped), source.name):
            raise AssertionError("stripping the metadata left some reported facts")

        old_minor.write_bytes(data[:6] + struct.pack("<H", 15) + data[8:])
        run(neura, "emit-ir", "--no-optimize", old_minor, error="require NeuraIR format 1.16")
        malformed.write_bytes(data[:-4] + b"\xff\xff\xff\xff")
        run(neura, "emit-ir", "--no-optimize", malformed, error="missing symbol reference")
        images = []
        for index, file in enumerate((source, nir, stripped)):
            image = output / f"image-{index}.nvr"
            run(neura, "build", file, "-o", image)
            images.append(image.read_bytes())
        if not images[0] == images[1] == images[2]:
            raise AssertionError("source, full NIR and stripped NIR built different images")
        check_linkage(neura, output)
    print("NeuraIR 1.16 round-trip, linking, invalid records, and byte-identical images verified")


if __name__ == "__main__":
    main()
