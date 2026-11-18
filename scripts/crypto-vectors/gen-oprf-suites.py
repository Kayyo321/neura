#!/usr/bin/env python3
"""Transcode the RFC 9497 A.1 through A.5 OPRF transcripts."""

import hashlib
from pathlib import Path
import re
import sys

from nra_vectors import write_module

SOURCE = Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9497.txt")
SOURCE_SHA256 = "95160517617374655fe23af2c6cec311eae326deb7689239f97641fd5a560432"
SUITES = {
    1: ("ristretto", "ristretto255-SHA512", 32, 32, 64),
    2: ("decaf", "decaf448-SHAKE256", 56, 56, 64),
    3: ("p256", "P256-SHA256", 32, 33, 32),
    4: ("p384", "P384-SHA384", 48, 49, 48),
    5: ("p521", "P521-SHA512", 66, 67, 64),
}
COLUMNS = [
    "mode", "seed", "key info", "server scalar", "server public key", "inputs", "info", "blinds",
    "blinded elements", "evaluated elements", "proof", "proof nonce", "outputs",
]


def fields(section):
    values = {}
    current = None
    for line in section.splitlines():
        entry = re.fullmatch(r"   (\w+) = ([a-f0-9,]+)", line)
        if entry:
            current, value = entry.groups()
            values[current] = value
        elif current is not None and re.fullmatch(r"   [a-f0-9,]+", line):
            values[current] += line.strip()
        else:
            current = None
    return values


def check_column(value, count, width=None):
    cells = value.split(",")
    if len(cells) != count:
        raise ValueError(f"expected {count} items in {value!r}")
    if width is not None and any(len(bytes.fromhex(cell)) != width for cell in cells):
        raise ValueError(f"expected items of {width} bytes in {value!r}")


def vectors(text, appendix, suite, scalar_width, point_width, digest_width):
    section = text.split(f"\nA.{appendix}.  {suite}\n", 1)[1]
    section = re.split(r"\n(?:A\.\d+\.  |Acknowledgments\n)", section, maxsplit=1)[0]
    modes = re.split(rf"\nA\.{appendix}\.[123]\.  (OPRF|VOPRF|POPRF) Mode\n", section)
    for mode, section in zip(modes[1::2], modes[2::2]):
        parts = re.split(rf"\nA\.{appendix}\.[123]\.\d+\.  Test Vector \d+, Batch Size (\d+)\n", section)
        key = fields(parts[0])
        check_column(key["Seed"], 1, 32)
        check_column(key["skSm"], 1, scalar_width)
        public = key.get("pkSm", "")
        if mode != "OPRF":
            check_column(public, 1, point_width)
        sizes = [int(size) for size in parts[1::2]]
        if sizes != ([1, 1] if mode == "OPRF" else [1, 1, 2]):
            raise ValueError(f"unexpected batch sizes for {suite} {mode}: {sizes}")
        for size, case in zip(sizes, parts[2::2]):
            v = fields(case)
            check_column(v["Input"], size)
            check_column(v["Blind"], size, scalar_width)
            check_column(v["BlindedElement"], size, point_width)
            check_column(v["EvaluationElement"], size, point_width)
            check_column(v["Output"], size, digest_width)
            proof = v.get("Proof", "")
            proof_nonce = v.get("ProofRandomScalar", "")
            if mode != "OPRF":
                check_column(proof, 1, 2 * scalar_width)
                check_column(proof_nonce, 1, scalar_width)
            yield [
                mode, key["Seed"], key["KeyInfo"], key["skSm"], public, v["Input"], v.get("Info", ""),
                v["Blind"], v["BlindedElement"], v["EvaluationElement"], proof, proof_nonce, v["Output"],
            ]


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError(f"unexpected RFC text SHA-256: {digest}")
    text = data.decode("utf-8")
    tables = []
    for appendix, (name, suite, scalar_width, point_width, digest_width) in SUITES.items():
        rows = list(vectors(text, appendix, suite, scalar_width, point_width, digest_width))
        if len(rows) != 8:
            raise ValueError(f"expected eight published transcripts for {suite}")
        tables.append((name, COLUMNS, rows))
    print(write_module("crypto-advanced", "oprf-suites-vectors.nra", "OprfSuitesVectors", [
        "python3 scripts/crypto-vectors/gen-oprf-suites.py",
        "RFC 9497 Appendix A.1 through A.5, all single-element and batched transcripts.",
        "RFC text SHA-256 " + digest,
    ], tables))


if __name__ == "__main__":
    main()
