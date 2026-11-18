#!/usr/bin/env python3
"""Generate PKI HashML-DSA and composite signature fixtures.

HashML-DSA-44 uses the recorded synthetic OpenSSL key and a deterministic
OpenSSL 3.5.7 signature over FIPS 204 Algorithm 4's M'. HashML-DSA-65 and
HashML-DSA-87 use NIST ACVP's SHA-512, empty-context cases 237 and 266.
The composite public key, signatures and certificate come from the IETF
draft -19's published JSON vectors.

    python3 scripts/crypto-vectors/gen-pki-pqc-signatures.py [sources-dir]
"""

import base64
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

from nra_vectors import write_module

SOURCES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources")
ACVP_COMMIT = "975de31eb83d87039ec88934fdc47d8c312b892d"
COMPOSITE_COMMIT = "6df63fdcee0487b6b02271f928d0572c4720fb11"
SIGNATURE_ARC = "2.16.840.1.101.3.4.3."


def openssl(*args: str) -> bytes:
    return subprocess.run(["openssl", *args], check=True, capture_output=True).stdout


def openssl_hash_ml_dsa(key: pathlib.Path) -> list[str]:
    # FIPS 204 Algorithm 4: M' = 01 || len(ctx) || ctx || OID || PH(M).
    message = b"Neura Pki HashML-DSA test message"
    oid = openssl("asn1parse", "-genstr", "OID:2.16.840.1.101.3.4.2.3", "-noout", "-out", "/dev/stdout")
    encoded = b"\x01\x00" + oid + hashlib.sha512(message).digest()
    public_der = openssl("pkey", "-in", str(key), "-pubout", "-outform", "DER")
    # RFC 9881 SubjectPublicKeyInfo encodes this 1312-byte key directly in the final BIT STRING.
    public_key = public_der[-1312:]
    if public_der[-1317:-1312] != b"\x03\x82\x05\x21\x00":
        raise ValueError("unexpected ML-DSA-44 public key encoding")
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "encoded"
        path.write_bytes(encoded)
        signature = openssl("pkeyutl", "-sign", "-rawin", "-pkeyopt", "message-encoding:0",
                            "-pkeyopt", "deterministic:1", "-inkey", str(key), "-in", str(path))
    if len(signature) != 2420:
        raise ValueError("unexpected ML-DSA-44 signature size")
    return ["OpenSSL 3.5.7 HashML-DSA-44", SIGNATURE_ARC + "32", message.hex(), public_key.hex(), signature.hex()]


def hash_ml_dsa_rows(sources: pathlib.Path) -> list[list[str]]:
    key = sources / "pki2/material/keys/mldsa44.pem"
    acvp_json = sources / "pqc/ML-DSA-sigGen-FIPS204/internalProjection.json"
    acvp = json.loads(acvp_json.read_bytes())
    rows = [openssl_hash_ml_dsa(key)]
    for set_name, id_number, oid_suffix in [("ML-DSA-65", 237, "33"), ("ML-DSA-87", 266, "34")]:
        group, case = next((g, t) for g in acvp["testGroups"] for t in g["tests"] if t["tcId"] == id_number)
        if (group["parameterSet"] != set_name or group["preHash"] != "preHash"
                or case["hashAlg"] != "SHA2-512" or case["context"] != ""):
            raise ValueError(f"unexpected ACVP case {id_number}")
        rows.append([f"NIST ACVP tcId {id_number}", SIGNATURE_ARC + oid_suffix, case["message"].lower(),
                     case["pk"].lower(), case["signature"].lower()])
    return rows


def composite_row(sources: pathlib.Path) -> list[str]:
    source = json.loads((sources / "hybrid/composite-sigs-19-testvectors.json").read_bytes())
    row = next(case for case in source["tests"] if case["tcId"] == "id-MLDSA65-Ed25519-SHA512")
    encoded = [source["m"], row["pk"], row["s"], row["sWithContext"], row["x5c"]]
    return [base64.b64decode(item, validate=True).hex() for item in encoded]


def main() -> None:
    sources = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    print(write_module("crypto-pki", "pqc-signature-vectors.nra", "PqcSignatureVectors", [
        "python3 scripts/crypto-vectors/gen-pki-pqc-signatures.py",
        "HashML-DSA-44 from OpenSSL 3.5.7 and FIPS 204 Algorithm 4.",
        f"HashML-DSA-65 and -87 from NIST ACVP ML-DSA-sigGen-FIPS204 at {ACVP_COMMIT}.",
        f"Composite from draft-ietf-lamps-pq-composite-sigs-19 at {COMPOSITE_COMMIT}.",
    ], [
        ("hash_ml_dsa", ["name", "algorithm OID", "message", "public key", "signature"], hash_ml_dsa_rows(sources)),
        ("composite", ["message", "public key", "signature", "signature with context", "certificate"],
         [composite_row(sources)]),
    ]))


if __name__ == "__main__":
    main()
