#!/usr/bin/env python3
"""Transcode every single-element ristretto255-SHA512 test vector in RFC 9497 A.1."""

import hashlib
from pathlib import Path
import re
import sys

from nra_vectors import write_module

SOURCE = Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9497.txt")


def fields(text):
    result = {}
    current = None
    for line in text.splitlines():
        entry = re.fullmatch(r"   (\w+) = ([a-f0-9,]+)", line)
        if entry:
            current, value = entry.groups()
            result[current] = value
        elif current is not None and re.fullmatch(r"   [a-f0-9,]+", line):
            result[current] += line.strip()
        else:
            current = None
    return result


def vectors(text):
    a1 = text.rsplit("\nA.1.  ristretto255-SHA512\n", 1)[1].split("\nA.2.  decaf448-SHAKE256\n", 1)[0]
    mode_sections = re.split(r"\nA\.1\.[123]\.  (OPRF|VOPRF|POPRF) Mode\n", a1)
    for mode, section in zip(mode_sections[1::2], mode_sections[2::2]):
        parts = re.split(r"\nA\.1\.[123]\.\d\.  Test Vector \d, Batch Size (\d)\n", section)
        key = fields(parts[0])
        for size, case in zip(parts[1::2], parts[2::2]):
            if size != "1":
                continue
            v = fields(case)
            proof = v.get("Proof", "")
            public = key.get("pkSm", "")
            if mode != "OPRF":
                assert len(bytes.fromhex(proof)) == 64 and len(bytes.fromhex(public)) == 32
            yield [mode, key["Seed"], key["KeyInfo"], key["skSm"], public, v["Input"], v.get("Info", ""),
                   v["Blind"], v["BlindedElement"], v["EvaluationElement"], proof, v.get("ProofRandomScalar", ""),
                   v["Output"]]


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    data = path.read_bytes()
    rows = list(vectors(data.decode("utf-8")))
    if len(rows) != 6:
        raise ValueError("expected two published single-element vectors for each of OPRF, VOPRF and POPRF")
    print(write_module("crypto-advanced", "oprf-vectors.nra", "OprfVectors", [
        "python3 scripts/crypto-vectors/gen-advanced-oprf.py",
        "RFC 9497 Appendix A.1, ristretto255-SHA512, batch size 1 for all three modes.",
        "RFC text SHA-256 " + hashlib.sha256(data).hexdigest(),
    ], [
        ("cases", ["mode", "seed", "key info", "server scalar", "server public key", "input", "info",
                   "blind", "blinded", "evaluated", "proof", "proof nonce", "output"], rows),
    ]))


if __name__ == "__main__":
    main()
