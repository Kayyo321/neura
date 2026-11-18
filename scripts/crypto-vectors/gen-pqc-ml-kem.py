#!/usr/bin/env python3
"""Known answers for ML-KEM (FIPS 203) from NIST's ACVP test sets.

Takes, per parameter set, the first KEYGEN of the 25 keyGen cases, the
first ENCAPS of the 25 encapsulation cases, every decapsulation case (valid and modified
ciphertexts, the latter answered by implicit rejection) and every
encapsulation-key and decapsulation-key check case (modulus check and hash
check, passing and failing) of ML-KEM-keyGen-FIPS203 and
ML-KEM-encapDecap-FIPS203 (see pqc_acvp.py for the pinned commit). Writes
tests/stdlib/crypto-pqc/support/ml-kem-vectors.nra.

    python3 scripts/crypto-vectors/gen-pqc-ml-kem.py [--json-dir DIR]
"""

import argparse
import pathlib
import tempfile

import pqc_acvp
from nra_vectors import write_module

KEYGEN = 10
ENCAPS = 10
DECAPS = 10
SETS = {"ML-KEM-512": "512", "ML-KEM-768": "768", "ML-KEM-1024": "1024"}


def lower(rows: list[list[str]]) -> list[list[str]]:
    """ACVP writes hexadecimal in capitals; the vector files use lowercase."""
    return [[c.lower() if all(x in "0123456789ABCDEFabcdef" for x in c) else c for c in row] for row in rows]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json-dir", type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        json_dir = args.json_dir or pathlib.Path(temp)
        keygen = pqc_acvp.load("ML-KEM-keyGen-FIPS203", json_dir)
        encdec = pqc_acvp.load("ML-KEM-encapDecap-FIPS203", json_dir)
    keygen_rows, encaps_rows, decaps_rows, ek_rows, dk_rows = [], [], [], [], []
    for group in keygen["testGroups"]:
        for test in group["tests"][:KEYGEN]:
            keygen_rows.append([SETS[group["parameterSet"]], test["d"], test["z"], test["ek"], test["dk"]])
    for group in encdec["testGroups"]:
        name = SETS[group["parameterSet"]]
        function = group["function"]
        if function == "encapsulation":
            for test in group["tests"][:ENCAPS]:
                encaps_rows.append([name, test["ek"], test["m"], test["c"], test["k"]])
        elif function == "decapsulation":
            for test in group["tests"][:DECAPS]:
                decaps_rows.append([name, test["dk"], test["c"], test["k"], test["reason"]])
        elif function == "encapsulationKeyCheck":
            for test in group["tests"]:
                ek_rows.append([name, test["ek"], "true" if test["testPassed"] else "false", test["reason"]])
        elif function == "decapsulationKeyCheck":
            for test in group["tests"]:
                dk_rows.append([name, test["dk"], "true" if test["testPassed"] else "false", test["reason"]])
        else:
            raise SystemExit(f"unknown function {function}")
    write_module("crypto-pqc", "ml-kem-vectors.nra", "MlKemVectors",
                 ["python3 scripts/crypto-vectors/gen-pqc-ml-kem.py from"]
                 + pqc_acvp.header(["ML-KEM-keyGen-FIPS203", "ML-KEM-encapDecap-FIPS203"]),
                 [("keygen", ["set", "d", "z", "ek", "dk"], lower(keygen_rows)),
                  ("encaps", ["set", "ek", "m", "c", "k"], lower(encaps_rows)),
                  ("decaps", ["set", "dk", "c", "k", "reason"], lower(decaps_rows)),
                  ("ek_check", ["set", "ek", "passes", "reason"], lower(ek_rows)),
                  ("dk_check", ["set", "dk", "passes", "reason"], lower(dk_rows))])


if __name__ == "__main__":
    main()
