#!/usr/bin/env python3
"""Composite ML-DSA-65 + Ed25519 vectors from draft-ietf-lamps-pq-composite-sigs-19."""

import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from nra_vectors import write_module

SOURCE = Path("/home/dev/neura-gate/030/crypto2/sources/hybrid/composite-sigs-19-testvectors.json")
DIGEST = "a60f697f9fd94c3cd5e4501a40c4396d9a9ffc7cf0f8a1ddd87dedbb46a7abf0"
LABEL = b"COMPSIG-MLDSA65-Ed25519-SHA512"
PREFIX = b"CompositeAlgorithmSignatures2025"


def openssl(*args, message=None):
    return subprocess.run(["openssl", *args], input=message, capture_output=True, check=True).stdout


def formatted(message, context):
    return PREFIX + LABEL + bytes([len(context)]) + context + hashlib.sha512(message).digest()


def vectors(source):
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DIGEST:
        raise ValueError("unexpected composite draft test vector digest")
    fixtures = json.loads(raw)
    sample = next(row for row in fixtures["tests"] if row["tcId"] == "id-MLDSA65-Ed25519-SHA512")
    seed, public, official_empty, official_context = (
        base64.b64decode(sample[key], validate=True) for key in ["sk", "pk", "s", "sWithContext"]
    )
    message, context = (base64.b64decode(fixtures[key], validate=True) for key in ["m", "ctx"])
    if len(seed) != 64 or len(public) != 1984 or len(official_empty) != 3373 or len(official_context) != 3373:
        raise ValueError("unexpected component lengths")

    classical = Ed25519PrivateKey.from_private_bytes(seed[32:])
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        private_path = directory / "ml-dsa-seed.der"
        public_path = directory / "ml-dsa-public.der"
        message_path = directory / "message"
        signature_path = directory / "ml-dsa-signature"
        # RFC 9881 ML-DSA-65 OneAsymmetricKey with the seed-only private key choice.
        private_path.write_bytes(bytes.fromhex("3034020100300b060960864801650304031204228020") + seed[:32])
        public_info = openssl("pkey", "-inform", "DER", "-in", str(private_path), "-pubout", "-outform", "DER")
        public_path.write_bytes(public_info)
        assert public_info[-1952:] + classical.public_key().public_bytes_raw() == public
        rows = []
        for context_bytes, official in [(b"", official_empty), (context, official_context)]:
            data = formatted(message, context_bytes)
            message_path.write_bytes(data)
            signature_path.write_bytes(official[:3309])
            openssl("pkeyutl", "-verify", "-rawin", "-pubin", "-inkey", str(public_path), "-keyform", "DER",
                    "-pkeyopt", "hexcontext-string:" + LABEL.hex(), "-sigfile", str(signature_path),
                    "-in", str(message_path))
            classical.public_key().verify(official[3309:], data)
            pq_signature = openssl("pkeyutl", "-sign", "-rawin", "-inkey", str(private_path), "-keyform", "DER",
                                  "-pkeyopt", "hexcontext-string:" + LABEL.hex(), "-pkeyopt", "deterministic:1",
                                  "-in", str(message_path))
            deterministic = pq_signature + classical.sign(data)
            rows.append([context_bytes.hex(), official.hex(), deterministic.hex()])
    return [seed.hex(), public.hex(), message.hex()], rows


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    material, cases = vectors(source)
    print(write_module("crypto-hybrid", "hybrid-sign-vectors.nra", "HybridSignVectors", [
        "python3 scripts/crypto-vectors/gen-hybrid-sign.py",
        "Official composite -19 suite id-MLDSA65-Ed25519-SHA512 at 6df63fdcee0487b6b02271f928d0572c4720fb11.",
        "Component checks and deterministic signature outputs from OpenSSL 3.5.7 and Python cryptography 43.",
    ], [
        ("material", ["private seed", "public key", "message"], [material]),
        ("signatures", ["application context", "official signature", "deterministic signature"], cases),
    ]))


if __name__ == "__main__":
    main()
