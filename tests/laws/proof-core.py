#!/usr/bin/env python3
"""Check reduction, induction, and proof diagnostics through the SDK."""

import json
import pathlib
import subprocess
import sys
import tempfile


NEURA = pathlib.Path(sys.argv[1]).resolve()
FIXTURE = pathlib.Path(__file__).with_name("core.nra").read_text(encoding="utf-8")


def invoke(*args):
    return subprocess.run([NEURA, *args], capture_output=True, text=True,
                          encoding="utf-8", timeout=30)


def checked(*args):
    result = invoke(*args)
    assert result.returncode == 0, result.stderr
    return result.stdout


def reject(path, before, after, code, expected_text):
    assert FIXTURE.count(before) == 1, before
    path.write_text(FIXTURE.replace(before, after), encoding="utf-8")
    result = invoke("prove", "--error-format=json", path)
    assert result.returncode == 1, (result.stdout, result.stderr)
    diagnostic = json.loads(result.stderr.splitlines()[0])
    assert diagnostic["code"] == code, diagnostic
    assert expected_text in diagnostic["message"], diagnostic
    return diagnostic


def main():
    with tempfile.TemporaryDirectory(prefix="neura-proof-core-") as temporary:
        root = pathlib.Path(temporary)
        source = root / "core.nra"
        source.write_text(FIXTURE, encoding="utf-8")
        verdict = json.loads(checked("prove", "--error-format=json", source))
        names = {law["name"] for law in verdict["laws"]}
        assert names == {f"Peano.{name}" for name in (
            "AddZeroLeft", "AddZeroRight", "ImmutableMethod", "NullableFlag",
            "GuardedMethod", "BooleanDecision", "ChoiceWitness", "Calculated", "ConstantValue",
            "ImmutableConstruction", "ImmutableDefault",
        )}, verdict
        assert verdict["proven"] == len(names), verdict

        nir = root / "core.nir"
        checked("emit-ir", source, "-o", nir)
        assert json.loads(checked("prove", "--error-format=json", nir)) == verdict

        reject(source, "AddZeroRight(p) in", "AddZeroRight(x) in",
               "E1413", "does not decrease")
        reject(source, "Nat.Succ(add(p, b))", "Nat.Succ(add(a, b))",
               "E1413", "does not decrease")
        reject(source, "for flag: Flag;", "for? flag: Flag;",
               "E1411", "possibly nil receiver")
        diagnostic = reject(source, "(right && left)", "(right || left)",
                            "E1401", "counterexample")
        assert any("argument" in note for note in diagnostic["notes"]), diagnostic
        reject(source, "const enabled = true;", "let enabled = true;",
               "E1410", "mutable or non-constant storage")
    print(f"{len(names)} proven laws, source and NeuraIR verdicts agree, 5 rejections")


if __name__ == "__main__":
    main()
