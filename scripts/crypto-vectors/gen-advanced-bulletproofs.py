#!/usr/bin/env python3
"""Transcode independent Merlin 3.0.0/Bulletproofs 5.0.0 reference answers.

The pinned Rust driver in bulletproofs-reference/ writes merlin.tsv and
range.tsv; see the recorded source manifest for the exact commands.

    python3 scripts/crypto-vectors/gen-advanced-bulletproofs.py [sources-dir]
"""

import pathlib
import sys

from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/advanced/bulletproofs")


def lines(path: pathlib.Path, width: int) -> list[list[str]]:
    rows = [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines()]
    if any(len(row) != width for row in rows):
        raise ValueError(f"unexpected field count in {path}")
    return rows


def main() -> None:
    directory = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    write_module("crypto-advanced", "merlin-vectors.nra", "MerlinVectors", [
        "python3 scripts/crypto-vectors/gen-advanced-bulletproofs.py",
        "Rust merlin 3.0.0, git a94892f457202399661573480bb7250b4fe7c0af.",
    ], [
        ("operations", ["scenario", "operation", "label", "argument", "challenge"], lines(directory / "merlin.tsv", 5)),
    ])
    write_module("crypto-advanced", "bulletproofs-vectors.nra", "BulletproofsVectors", [
        "python3 scripts/crypto-vectors/gen-advanced-bulletproofs.py",
        "Rust bulletproofs 5.0.0, git 86eadbeeb4a96d8da41427137b45ead810d03b41.",
    ], [
        ("range_cases", ["bits", "value", "blinding", "Merlin application label", "test random bytes", "commitment",
                         "proof", "unrelated commitment", "proof with altered t_x", "proof with altered final scalar"],
         lines(directory / "range.tsv", 10)),
    ])


if __name__ == "__main__":
    main()
