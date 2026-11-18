#!/usr/bin/env python3
"""Known answers for SLH-DSA (FIPS 205) from NIST's ACVP test sets.

Signatures are 7.8 to 49.9 KB, so the cases are chosen to cover every
parameter set, interface and failure reason with few rows:

- keygen: the first KEYGEN cases of each parameter set of
  SLH-DSA-keyGen-FIPS205.
- verify: for each parameter set, from its pure (external) group of
  SLH-DSA-sigVer-FIPS205, one modified signature (SIGHT for the SHA-2 sets,
  SIGFORS for the SHAKE sets); and for the two 128s sets, from their pure
  group one case of every reason (valid, modified message, modified R,
  SIGFORS and SIGHT, and signatures that are too large or too small), and
  from their pre-hash and internal groups a valid case and a modified
  message.
- sign: the case with the shortest message of each of the six groups of
  SLH-DSA-sigGen-FIPS205 (deterministic or hedged; pure, pre-hash or
  internal) for the "f" sets, and of the deterministic pure and hedged
  pre-hash groups for the "s" sets; then, from the pre-hash groups of the
  128f sets, the shortest case of every pre-hash function not yet chosen, so
  that each of the 11 is signed. A row holds the signature's length and
  SHA-256 instead of the 7.8 to 49.9 KB signature; the test compares the
  SHA-256 of the signature it computes, then verifies it, which gives every
  set a valid-signature verification as well.

Pre-hash cases with SHA2-512/224, which Std.Crypto.Hash does not offer, are
skipped. See pqc_acvp.py for the pinned commit. Writes
tests/stdlib/crypto-pqc/support/slh-dsa-vectors.nra.

    python3 scripts/crypto-vectors/gen-pqc-slh-dsa.py [--json-dir DIR]
"""

import argparse
import hashlib
import pathlib
import tempfile

import pqc_acvp
from nra_vectors import write_module

KEYGEN = 2
SMALL_GROUPS = [(True, "pure"), (False, "prehash")]
SIGN_COLUMNS = ["set", "interface", "hash", "context", "message", "addrnd or -", "sk", "pk", "signature length",
                "signature SHA-256"]
HASHES = {"SHA2-224": "Sha224", "SHA2-256": "Sha256", "SHA2-384": "Sha384", "SHA2-512": "Sha512",
          "SHA2-512/256": "Sha512_256", "SHA3-224": "Sha3_224", "SHA3-256": "Sha3_256",
          "SHA3-384": "Sha3_384", "SHA3-512": "Sha3_512", "SHAKE-128": "Shake128", "SHAKE-256": "Shake256"}


def lower(rows: list[list[str]]) -> list[list[str]]:
    """ACVP writes hexadecimal in capitals; the vector files use lowercase."""
    return [[c.lower() if all(x in "0123456789ABCDEFabcdef" for x in c) else c for c in row] for row in rows]


def set_name(parameter_set: str) -> str:
    return parameter_set.removeprefix("SLH-DSA-")


def interface(group: dict) -> str:
    if group["signatureInterface"] == "internal":
        return "internal"
    return "prehash" if group["preHash"] == "preHash" else "pure"


def usable(group: dict, test: dict) -> bool:
    return interface(group) != "prehash" or test["hashAlg"] in HASHES


def hash_of(group: dict, test: dict) -> str:
    return HASHES[test["hashAlg"]] if interface(group) == "prehash" else "-"


def by_message(group: dict) -> list[dict]:
    return sorted((t for t in group["tests"] if usable(group, t)), key=lambda t: (len(t["message"]), t["tcId"]))


def sign_row(group: dict, test: dict) -> list[str]:
    addrnd = "-" if group["deterministic"] else test["additionalRandomness"]
    signature = bytes.fromhex(test["signature"])
    return [set_name(group["parameterSet"]), interface(group), hash_of(group, test), test.get("context", ""),
            test["message"], addrnd, test["sk"], test["pk"], str(len(signature)), hashlib.sha256(signature).hexdigest()]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json-dir", type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        json_dir = args.json_dir or pathlib.Path(temp)
        keygen = pqc_acvp.load("SLH-DSA-keyGen-FIPS205", json_dir)
        siggen = pqc_acvp.load("SLH-DSA-sigGen-FIPS205", json_dir)
        sigver = pqc_acvp.load("SLH-DSA-sigVer-FIPS205", json_dir)
    keygen_rows, verify_rows = [], []
    for group in keygen["testGroups"]:
        for test in group["tests"][:KEYGEN]:
            keygen_rows.append([set_name(group["parameterSet"]), test["skSeed"], test["skPrf"], test["pkSeed"],
                                test["sk"], test["pk"]])
    sign_tables = {"sign_fast": [], "sign_sha2_small": [], "sign_shake_small": []}
    for group in siggen["testGroups"]:
        name = group["parameterSet"]
        if name.endswith("s") and (group["deterministic"], interface(group)) not in SMALL_GROUPS:
            continue
        table = "sign_fast" if name.endswith("f") else "sign_shake_small" if "SHAKE" in name else "sign_sha2_small"
        sign_tables[table].append(sign_row(group, by_message(group)[0]))
    fast = sign_tables["sign_fast"]
    for hash_name in HASHES.values():
        if any(r[2] == hash_name for table in sign_tables.values() for r in table):
            continue
        cases = [(len(t["message"]), t["tcId"], g, t) for g in siggen["testGroups"]
                 if g["parameterSet"].endswith("128f") and interface(g) == "prehash"
                 for t in g["tests"] if usable(g, t) and hash_of(g, t) == hash_name]
        _, _, group, test = min(cases, key=lambda c: (c[0], c[1]))
        fast.append(sign_row(group, test))
    for group in sigver["testGroups"]:
        name = group["parameterSet"]
        kind = interface(group)
        if name.endswith("128s") and kind == "pure":
            wanted = sorted({t["reason"] for t in group["tests"]})
        elif name.endswith("128s"):
            wanted = ["modified message", "valid signature and message - signature should verify successfully"]
        elif kind == "pure":
            wanted = ["modified signature - SIGHT" if "SHA2" in name else "modified signature - SIGFORS"]
        else:
            continue
        for reason in wanted:
            test = next(t for t in by_message(group) if t["reason"] == reason)
            verify_rows.append([set_name(name), kind, hash_of(group, test), test.get("context", ""), test["message"],
                                test["pk"], test["signature"], "true" if test["testPassed"] else "false", reason])
    write_module("crypto-pqc", "slh-dsa-vectors.nra", "SlhDsaVectors",
                 ["python3 scripts/crypto-vectors/gen-pqc-slh-dsa.py from"]
                 + pqc_acvp.header(["SLH-DSA-keyGen-FIPS205", "SLH-DSA-sigGen-FIPS205", "SLH-DSA-sigVer-FIPS205"]),
                 [("keygen", ["set", "sk_seed", "sk_prf", "pk_seed", "sk", "pk"], lower(keygen_rows)),
                  ("sign_fast", SIGN_COLUMNS, lower(sign_tables["sign_fast"])),
                  ("sign_sha2_small", SIGN_COLUMNS, lower(sign_tables["sign_sha2_small"])),
                  ("sign_shake_small", SIGN_COLUMNS, lower(sign_tables["sign_shake_small"])),
                  ("verify", ["set", "interface", "hash", "context", "message", "pk", "signature", "valid", "reason"],
                   lower(verify_rows))])


if __name__ == "__main__":
    main()
