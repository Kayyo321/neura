#!/usr/bin/env python3
"""Reproduce the Neura v1 envelope with independent AES-GCM/AES-SIV implementations.

Usage: python3 scripts/crypto-vectors/gen-envelope.py
Requires Python cryptography 43.0.0.
"""

from hashlib import sha256
import hmac
from struct import pack

import cryptography
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, AESSIV

from nra_vectors import write_module

LABEL = b"backup-primary"
KEK1 = bytes(range(64))
KEK2 = bytes(range(64, 128))
MATERIALS = [bytes(range(start, start + 40)) for start in (0, 64, 128)]


def envelope(kek: bytes, version: int, material: bytes, sequence: int, data: bytes, aad: bytes) -> bytes:
    dek, prefix = material[:32], material[32:]
    metadata = b"NENV\x01\x01" + bytes([len(LABEL)]) + pack(">I", version) + prefix + LABEL
    wrapped = AESSIV(kek).encrypt(dek, [b"neura-envelope/wrap/v1" + metadata])
    header = metadata + pack(">II", sequence, len(wrapped)) + wrapped
    auth = b"neura-envelope/payload/v1" + pack(">I", len(header)) + header + pack(">I", len(aad)) + aad
    nonce = prefix + pack(">I", sequence)
    sealed = AESGCM(dek).encrypt(nonce, data, auth)
    assert AESGCM(dek).decrypt(nonce, sealed, auth) == data
    assert AESSIV(kek).decrypt(wrapped, [b"neura-envelope/wrap/v1" + metadata]) == dek
    return header + sealed


def replaced(wire: bytes, offset: int, data: bytes) -> bytes:
    return wire[:offset] + data + wire[offset + len(data):]


def main() -> None:
    if cryptography.__version__ != "43.0.0":
        raise SystemExit("cryptography 43.0.0 is required to reproduce these vectors")
    cases = [
        ("first under version 1", KEK1, 1, MATERIALS[0], 1, b"first record", b"db/users/1"),
        ("same cached DEK", KEK1, 1, MATERIALS[0], 2, b"second record", b"db/users/2"),
        ("automatic DEK rotation", KEK1, 1, MATERIALS[1], 1, b"", b"db/users/3"),
        ("after KEK rotation", KEK2, 2, MATERIALS[2], 1, b"next year", b"db/users/4"),
    ]
    rows = [
        [name, LABEL.decode(), str(version), material.hex(), data.hex(), aad.hex(),
         envelope(kek, version, material, sequence, data, aad).hex()]
        for name, kek, version, material, sequence, data, aad in cases
    ]
    first = bytes.fromhex(rows[0][-1])
    ordinal_at = 19 + len(LABEL)
    neg = [
        ("modified magic", "crypto.envelope", replaced(first, 0, b"X")),
        ("new wire version", "crypto.envelope", replaced(first, 4, b"\x02")),
        ("unknown suite", "crypto.envelope", replaced(first, 5, b"\x02")),
        ("wrong label length", "crypto.envelope", replaced(first, 6, b"\x00")),
        ("invalid version zero", "crypto.envelope", replaced(first, 7, bytes(4))),
        ("known different key version", "crypto.authentication", replaced(first, 7, pack(">I", 2))),
        ("missing key version", "crypto.authentication", replaced(first, 7, pack(">I", 3))),
        ("modified nonce epoch", "crypto.authentication", replaced(first, 11, bytes([first[11] ^ 1]))),
        ("cross-label change", "crypto.envelope", replaced(first, 19, b"X")),
        ("sequence zero", "crypto.envelope", replaced(first, ordinal_at, bytes(4))),
        ("sequence over usage cap", "crypto.envelope", replaced(first, ordinal_at, pack(">I", 4097))),
        ("modified sequence", "crypto.authentication", replaced(first, ordinal_at, pack(">I", 2))),
        ("zero wrapped length", "crypto.envelope", replaced(first, ordinal_at + 4, bytes(4))),
        ("wrapped length too large", "crypto.envelope", replaced(first, ordinal_at + 4, bytes([255]) * 4)),
        ("modified wrapped data key", "crypto.authentication", replaced(first, ordinal_at + 8, bytes([first[ordinal_at + 8] ^ 1]))),
        ("truncated before tag", "crypto.envelope", first[:ordinal_at + 8 + 48 + 15]),
        ("modified payload tag", "crypto.authentication", first[:-1] + bytes([first[-1] ^ 1])),
        ("trailing data", "crypto.authentication", first + b"\0"),
    ]
    negatives = [[name, code, data.hex()] for name, code, data in neg]

    message, context = b"test message", b"test as associated data"
    signing_seed = bytes(range(192, 224))
    signing_key = Ed25519PrivateKey.from_private_bytes(signing_seed)
    hmac_key = bytes(range(224, 256))
    dek = MATERIALS[0][:32]
    store = [value.hex() for value in (
        KEK1, KEK2, dek, context, AESSIV(KEK1).encrypt(dek, [context]), signing_seed,
        message, signing_key.sign(message), hmac_key, hmac.digest(hmac_key, message, sha256),
    )]
    print(write_module("crypto-enclave", "envelope-vectors.nra", "EnvelopeVectors",
                       ["python3 scripts/crypto-vectors/gen-envelope.py",
                        "AES-GCM, AES-SIV and Ed25519 from Python cryptography 43.0.0; HMAC from Python hmac."],
                       [("envelopes", ["case", "label", "KEK version", "DEK material", "plaintext", "AAD", "wire"], rows),
                        ("reject", ["case", "error code", "wire"], negatives),
                        ("store", ["KEK 1", "KEK 2", "DEK", "wrap AAD", "wrapped DEK", "Ed25519 seed", "message",
                                   "signature", "HMAC key", "HMAC tag"], [store])]))


if __name__ == "__main__":
    main()
