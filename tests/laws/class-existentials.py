#!/usr/bin/env python3
"""Existential class witnesses must denote live immutable instances."""

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

let M = module (
    .Pure
    let new_flag = func() -> Flag => new Flag();
"""


def run(*arguments, status=0):
    result = subprocess.run([NEURA, *arguments], text=True, capture_output=True, timeout=30)
    assert result.returncode == status, result
    return result


with tempfile.TemporaryDirectory(prefix="neura-law-exists-") as scratch:
    root = Path(scratch)
    source = root / "exists.nra"
    for clause, claim, witness in (
        ("", "holds: f == nil;", "nil"),
        ("for? x: Flag;", "claim: { f == x : Flag };", "x"),
    ):
        source.write_text(FLAG + f"""
    export let Live = law (
        {clause}
        exists f: Flag;
        {claim}
    );
        | proof: {{
            witness {witness};
            {{==}}
        }};
);
""", encoding="utf-8")
        failure = run("prove", "--error-format=json", source, status=1)
        diagnostic = json.loads(failure.stderr.splitlines()[0])
        assert diagnostic["code"] == "E1409", diagnostic
        assert "requires a non-nil immutable instance" in diagnostic["message"], diagnostic

    source.write_text(FLAG + """
    export let Constructed = law (
        exists f: Flag;
        holds: f.enabled;
    );
        | proof: {
            witness new_flag();
            {==}
        };

    export let Guarded = law (
        for? x: Flag;
        exists f: Flag;
        holds: f != nil;
    );
        | proof: {
            if (empty: x == nil) {
                witness new Flag();
                {==}
            } else {
                witness x;
                {==}
            }
        };
);
""", encoding="utf-8")
    original = json.loads(run("prove", "--error-format=json", source).stdout)
    assert original["proven"] == 2, original
    module = root / "exists.nir"
    run("emit-ir", source, "-o", module)
    restored = json.loads(run("prove", "--error-format=json", module).stdout)
    assert restored == original, (original, restored)

print("2 possibly nil class witnesses rejected, 2 live instance witnesses round-tripped")
