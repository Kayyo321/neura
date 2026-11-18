#!/usr/bin/env python3
"""Known answers for ML-DSA (FIPS 204) from NIST's ACVP test sets and Wycheproof.

From ACVP (see pqc_acvp.py for the pinned commits):
- keygen: the first KEYGEN cases per parameter set of ML-DSA-keyGen-FIPS204.
- sign: from every test group of ML-DSA-sigGen-FIPS204 (deterministic and
  hedged signing; pure ML-DSA with a context, HashML-DSA, the internal
  interface over M' and over an external mu) the case with the shortest
  message, plus, from the HashML-DSA groups, the shortest case of every
  pre-hash function not yet chosen, so that each of the 11 is signed.
- verify: from every test group of ML-DSA-sigVer-FIPS204 a valid case and an
  invalid one, rotating through the four reasons (modified message, and
  signatures with a modified commitment c-tilde, z or hint) so that each set
  sees every reason, with the shortest messages; plus the shortest case of
  every pre-hash function not yet chosen.
Cases whose pre-hash is SHA2-512/224, which Std.Crypto.Hash does not offer,
are skipped.

From Wycheproof (mldsa_<set>_sign_seed_test.json and _verify_test.json):
- wycheproof_sign: every sign case flagged BoundaryCondition (z, r0 and hint
  limits just below and above, HighBits, LowBits and Decompose edge cases met
  in signing or in verifying, Power2Round remainders, rejection in matrix
  expansion, the most hints, and the rejection-loop cases of the ACVP draft's
  table 1), signed from the seed and then verified.
- wycheproof_verify: every case flagged InvalidHintsEncoding,
  IncorrectSignatureLength, IncorrectPublicKeyLength, InvalidContext or
  InvalidPrivateKey; the InfinityNormViolation cases at the boundary and the
  first four others; the first two ModifiedSignature cases; the first valid
  case; and the valid ZeroPublicKey cases with the first two invalid ones.

Writes tests/stdlib/crypto-pqc/support/ml-dsa-vectors.nra.

    python3 scripts/crypto-vectors/gen-pqc-ml-dsa.py [--json-dir DIR]
"""

import argparse
import pathlib
import tempfile

import pqc_acvp
from nra_vectors import write_module

KEYGEN = 5
INVALID = ["modified message", "modified signature - commitment", "modified signature - z",
           "modified signature - hint"]
VALID = "valid signature and message - signature should verify successfully"
VERIFY_FLAGS = {"InvalidHintsEncoding", "IncorrectSignatureLength", "IncorrectPublicKeyLength", "InvalidContext",
                "InvalidPrivateKey"}
HASHES = {"SHA2-224": "Sha224", "SHA2-256": "Sha256", "SHA2-384": "Sha384", "SHA2-512": "Sha512",
          "SHA2-512/256": "Sha512_256", "SHA3-224": "Sha3_224", "SHA3-256": "Sha3_256",
          "SHA3-384": "Sha3_384", "SHA3-512": "Sha3_512", "SHAKE-128": "Shake128", "SHAKE-256": "Shake256"}


def set_name(parameter_set: str) -> str:
    return parameter_set.removeprefix("ML-DSA-")


def interface(group: dict) -> str:
    if group["signatureInterface"] == "internal":
        return "mu" if group["externalMu"] else "internal"
    return "prehash" if group["preHash"] == "preHash" else "pure"


def usable(group: dict, test: dict) -> bool:
    return interface(group) != "prehash" or test["hashAlg"] in HASHES


def message_of(group: dict, test: dict) -> str:
    return test["mu"] if interface(group) == "mu" else test["message"]


def hash_of(group: dict, test: dict) -> str:
    return HASHES[test["hashAlg"]] if interface(group) == "prehash" else "-"


def lower(rows: list[list[str]]) -> list[list[str]]:
    """ACVP writes hexadecimal in capitals; the vector files use lowercase."""
    return [[c.lower() if all(x in "0123456789ABCDEFabcdef" for x in c) else c for c in row] for row in rows]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json-dir", type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        json_dir = args.json_dir or pathlib.Path(temp)
        keygen = pqc_acvp.load("ML-DSA-keyGen-FIPS204", json_dir)
        siggen = pqc_acvp.load("ML-DSA-sigGen-FIPS204", json_dir)
        sigver = pqc_acvp.load("ML-DSA-sigVer-FIPS204", json_dir)
        sign_seed = {level: pqc_acvp.load_wycheproof(f"mldsa_{level}_sign_seed_test", json_dir)
                     for level in ("44", "65", "87")}
        verify = {level: pqc_acvp.load_wycheproof(f"mldsa_{level}_verify_test", json_dir)
                  for level in ("44", "65", "87")}
    keygen_rows, sign_rows, verify_rows = [], [], []
    for group in keygen["testGroups"]:
        for test in group["tests"][:KEYGEN]:
            keygen_rows.append([set_name(group["parameterSet"]), test["seed"], test["pk"], test["sk"]])
    def shortest(group: dict, tests) -> list[dict]:
        return sorted((t for t in tests if usable(group, t)), key=lambda t: (len(message_of(group, t)), t["tcId"]))

    def add_hashes(groups: list[dict], rows: list, row_of) -> None:
        """Adds the shortest case of each pre-hash function that no row has yet."""
        for hash_name in HASHES.values():
            if any(r[2] == hash_name for r in rows):
                continue
            cases = [(len(message_of(g, t)), t["tcId"], g, t) for g in groups if interface(g) == "prehash"
                     for t in g["tests"] if usable(g, t) and hash_of(g, t) == hash_name]
            _, _, group, test = min(cases, key=lambda c: (c[0], c[1]))
            rows.append(row_of(group, test))

    def sign_row(group: dict, test: dict) -> list[str]:
        rnd = "0" * 64 if group["deterministic"] else test["rnd"]
        return [set_name(group["parameterSet"]), interface(group), hash_of(group, test), test.get("context", ""),
                message_of(group, test), rnd, test["sk"], test["signature"]]

    def verify_row(group: dict, test: dict) -> list[str]:
        return [set_name(group["parameterSet"]), interface(group), hash_of(group, test), test.get("context", ""),
                message_of(group, test), test["pk"], test["signature"], "true" if test["testPassed"] else "false",
                test["reason"]]

    for group in siggen["testGroups"]:
        if group["cornerCase"] != "none":
            raise SystemExit("unexpected corner case group")
        sign_rows.append(sign_row(group, shortest(group, group["tests"])[0]))
    add_hashes(siggen["testGroups"], sign_rows, sign_row)
    turn = 0
    for group in sigver["testGroups"]:
        wanted = [VALID, INVALID[turn % 4]]
        turn += 1
        for reason in wanted:
            verify_rows.append(verify_row(group, shortest(group, [t for t in group["tests"] if t["reason"] == reason])[0]))
    add_hashes(sigver["testGroups"], verify_rows, verify_row)

    wy_sign_rows, wy_verify_rows = [], []
    for level in ("44", "65", "87"):
        for group in sign_seed[level]["testGroups"]:
            for test in group["tests"]:
                if "BoundaryCondition" not in test["flags"]:
                    continue
                if test["result"] != "valid":
                    raise SystemExit(f"unexpected invalid boundary case {test['tcId']}")
                wy_sign_rows.append([level, str(test["tcId"]), group["privateSeed"], group["publicKey"],
                                     test.get("msg", "-"), test.get("ctx", ""), test.get("mu", ""),
                                     test.get("rnd", "0" * 64), test["sig"]])
        norm = modified = valid = zero = 0
        for group in verify[level]["testGroups"]:
            for test in group["tests"]:
                flags = set(test["flags"])
                take = bool(flags & VERIFY_FLAGS)
                if "InfinityNormViolation" in flags:
                    take = take or "BoundaryCondition" in flags or norm < 4
                    norm += "BoundaryCondition" not in flags
                if "ModifiedSignature" in flags:
                    take = take or modified < 2
                    modified += 1
                if "ZeroPublicKey" in flags:
                    take = take or test["result"] == "valid" or zero < 2
                    zero += test["result"] != "valid"
                if flags == {"ValidSignature"}:
                    take = take or valid < 1
                    valid += 1
                if take:
                    wy_verify_rows.append([level, str(test["tcId"]), group["publicKey"], test["msg"],
                                           test.get("ctx", ""), test["sig"], test["result"], " ".join(test["flags"])])
    write_module("crypto-pqc", "ml-dsa-vectors.nra", "MlDsaVectors",
                 ["python3 scripts/crypto-vectors/gen-pqc-ml-dsa.py from"]
                 + pqc_acvp.header(["ML-DSA-keyGen-FIPS204", "ML-DSA-sigGen-FIPS204", "ML-DSA-sigVer-FIPS204"])
                 + [f"and C2SP Wycheproof commit {pqc_acvp.WYCHEPROOF_COMMIT}, testvectors_v1/mldsa_<set>_sign_seed_test.json",
                    "and mldsa_<set>_verify_test.json."],
                 [("keygen", ["set", "xi", "pk", "sk"], lower(keygen_rows)),
                  ("sign", ["set", "interface", "hash", "context", "message or mu", "rnd", "sk", "signature"], lower(sign_rows)),
                  ("verify", ["set", "interface", "hash", "context", "message or mu", "pk", "signature", "valid",
                              "reason"], lower(verify_rows)),
                  ("wycheproof_sign", ["set", "tcId", "seed", "pk", "message or -", "context", "mu", "rnd", "signature"],
                   lower(wy_sign_rows)),
                  ("wycheproof_verify", ["set", "tcId", "pk", "message", "context", "signature", "result", "flags"],
                   lower(wy_verify_rows))])


if __name__ == "__main__":
    main()
