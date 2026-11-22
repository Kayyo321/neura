#!/usr/bin/env python3
"""A law's clause order determines which values a witness can use."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


NEURA = Path(sys.argv[1]).resolve()
FLAG = """
.Immutable
.Final
let Flag = class (
    export last enabled: bool = true;
);
"""


def law(clauses, steps):
    return f"""
let M = module (
    export let Ordered = law (
        {clauses}
    );
        | proof: {{
            {steps}
        }};
);
"""


def run(*arguments, status=0):
    result = subprocess.run([NEURA, *arguments], text=True, capture_output=True, timeout=30)
    assert result.returncode == status, result
    return result


def verdict(path):
    result = run("prove", "--error-format=json", path)
    summary = json.loads(result.stdout)
    assert summary["proven"] == 1, summary
    return summary


def round_trip(source, root):
    module = root / "ordered.nir"
    original = verdict(source)
    run("emit-ir", source, "-o", module)
    assert verdict(module) == original
    return module.read_bytes()


with tempfile.TemporaryDirectory(prefix="neura-law-order-") as scratch:
    root = Path(scratch)
    source = root / "ordered.nra"
    for domain, universal, declarations in (("bool", "for", ""), ("Flag", "for?", FLAG)):
        source.write_text(declarations + law(
            f"exists y: {domain}; {universal} x: {domain}; claim: {{ y == x : {domain} }};",
            "witness x; {==}",
        ), encoding="utf-8")
        failure = run("prove", "--error-format=json", source, status=1)
        diagnostic = json.loads(failure.stderr.splitlines()[0])
        assert diagnostic["code"] == "E1409", diagnostic
        assert "unbound value in law" in diagnostic["message"], diagnostic

    source.write_text(law(
        "for x: bool; exists y: bool; claim: { y == x : bool };",
        "witness x; {==}",
    ), encoding="utf-8")
    data = round_trip(source, root)
    assert data.count(b"LAW2") == 1
    legacy = root / "unordered-format.nir"
    legacy.write_bytes(data.replace(b"LAW2", b"LAWS", 1))
    failure = run("prove", legacy, status=1)
    assert "obsolete NeuraIR law table; rebuild the artifact from source" in failure.stderr, failure

    source.write_text(law(
        "exists y: bool; for x: bool; given eq: { y == x : bool }; "
        "last alias = x; claim: { y == alias : bool };",
        "witness true; intro x; intro eq; have result: { y == alias : bool } = eq; result",
    ), encoding="utf-8")
    round_trip(source, root)

print("2 out-of-scope witnesses rejected, 2 ordered laws round-tripped, legacy tag rejected")
