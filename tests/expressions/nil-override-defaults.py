#!/usr/bin/env python3
"""Exercise inherited literal nil defaults through source and NeuraIR."""

import json
import pathlib
import subprocess
import sys
import tempfile


compiler = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(__file__).with_suffix(".nra")
reject = source.parents[1] / "ui" / "calls" / "default-override-dropped.nra"


def invoke(*args, status=0):
    result = subprocess.run([compiler, *args], capture_output=True, encoding="utf-8", timeout=30)
    assert result.returncode == status, (args, result.stdout, result.stderr)
    return result


def diagnostics(result):
    return [json.loads(line) for line in result.stderr.splitlines() if line.startswith("{")]


def main():
    with tempfile.TemporaryDirectory(prefix="neura-nil-override-") as temporary:
        root = pathlib.Path(temporary)
        nir = root / "inherited.nir"
        assert not diagnostics(invoke("emit-ir", "--error-format=json", source, "-o", nir))

        for input_file in (source, nir):
            image = root / f"{input_file.suffix[1:]}.nvr"
            assert not diagnostics(invoke("build", "--error-format=json", input_file, "-o", image))
            result = invoke("run", "--tier=0", "--jit=off", image)
            assert result.stdout == "inherited nil\n", result

        unguarded = root / "equals.nra"
        unguarded.write_text("""let Item = class (
    export last id = 1;
    export let.override equals = func(other: class) -> bool {
        let item = other as Item;
        return item.id == id;
    };
);
let probe = func(value: Item) -> bool => value.equals(nil);
""", encoding="utf-8")
        warnings = diagnostics(invoke("check", "--error-format=json", unguarded))
        assert [item["code"] for item in warnings] == ["W0006"], warnings
        assert warnings[0]["args"]["cause"] == "nil-default", warnings
        primary = next(span for span in warnings[0]["spans"] if span["primary"])
        assert primary["line"] == 5, warnings

        errors = diagnostics(invoke("check", "--error-format=json", reject, status=1))
        assert [item["code"] for item in errors] == ["E0561"], errors

    print("inherited defaults run from source and NeuraIR, equals and E0561 checked")


if __name__ == "__main__":
    main()
