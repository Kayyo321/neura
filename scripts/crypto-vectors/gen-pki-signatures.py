#!/usr/bin/env python3
"""Signature vectors for Std.Crypto.Pki.AlgorithmVerifier.

Reads the certificates that gen-pki-sig-material.py recorded with `openssl` 3.5.7 (one self-signed certificate per
signature algorithm: RSA PKCS #1 v1.5 with SHA-224/256/384/512 and SHA3-224/256/384/512, RSASSA-PSS with several
parameter sets and RSA-PSS keys, ECDSA over P-256/P-384/P-521/secp256k1 with SHA-2 and SHA-3, Ed25519, Ed448,
ML-DSA-44/65/87 and the twelve SLH-DSA sets) and writes, for each, its DER, the reference result of verifying its
own signature, and the result this library documents:

- reference: Python `cryptography` 43 for RSA, ECDSA, Ed25519 and Ed448; `openssl pkeyutl -verify -rawin` (3.5.7)
  for ML-DSA and SLH-DSA, whose keys `cryptography` 43 cannot load.
- expected here: the reference result, except for the algorithms the library does not verify (SHA-1), which are
  UnsupportedAlgorithm.

Outputs tests/stdlib/crypto-pki/support/signature-vectors.nra and slh-signature-vectors.nra.

    python3 scripts/crypto-vectors/gen-pki-signatures.py [material-dir]
"""

import importlib.util
import pathlib
import sys

import cryptography
from cryptography import x509

from nra_vectors import write_module

DEFAULT = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/pki2/material")
HERE = pathlib.Path(__file__).resolve().parent

# Certificates whose algorithm this library refuses as UnsupportedAlgorithm, with the reason.
UNSUPPORTED = {
    "rsa-sha1": "SHA-1 signatures are not verified",
    "pss-sha1-20": "SHA-1 signatures are not verified",
}


def paths_module():
    spec = importlib.util.spec_from_file_location("gen_pki_paths", HERE / "gen-pki-paths.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    material = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    reference = paths_module().verify
    rows, slh_rows = [], []
    for path in sorted((material / "certs").glob("*.der")):
        name = path.stem
        der = path.read_bytes()
        cert = x509.load_der_x509_certificate(der)
        ok = reference(cert, cert)
        if ok is None:
            raise SystemExit(f"{name}: no reference result")
        result = "valid" if ok else "invalid"
        expected = "unsupported" if name in UNSUPPORTED else result
        row = [name, der.hex(), result, expected, UNSUPPORTED.get(name, "")]
        (slh_rows if name.startswith("slh-") else rows).append(row)
    header = ["python3 scripts/crypto-vectors/gen-pki-signatures.py: self-signed certificates recorded with openssl 3.5.7",
              "by gen-pki-sig-material.py; reference results from Python cryptography " + cryptography.__version__,
              "(RSA, ECDSA, EdDSA) or `openssl pkeyutl -verify -rawin` (ML-DSA, SLH-DSA)."]
    columns = ["name", "DER", "reference result", "expected here", "reason when unsupported"]
    print(write_module("crypto-pki", "signature-vectors.nra", "SignatureVectors", header,
                       [("certificates", columns, rows)]))
    print(write_module("crypto-pki", "slh-signature-vectors.nra", "SlhSignatureVectors", header,
                       [("certificates", columns, slh_rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
