#!/usr/bin/env python3
"""Known answers for Std.Crypto.Hash SHA-2, SHA-3 and SHAKE from Python hashlib.

Writes tests/stdlib/crypto-hash/support/hash-vectors.nra. Messages are the
pattern of nra_vectors.pattern at lengths around every block and padding
boundary of every algorithm, plus "abc"; each digest is hashlib's.

    python3 scripts/crypto-vectors/gen-hash.py
"""

import hashlib
import ssl
import sys

from nra_vectors import pattern, write_module

DIGESTS = ["sha224", "sha256", "sha384", "sha512", "sha512_256",
           "sha3_224", "sha3_256", "sha3_384", "sha3_512"]
LENGTHS = [0, 1, 55, 56, 64, 71, 72, 73, 103, 104, 105, 111, 112, 128, 135, 136, 137, 143, 144, 145,
           167, 168, 169, 200, 300]


def main() -> int:
    digest_rows = []
    for name in DIGESTS:
        messages = [b"abc"] + [pattern(n) for n in LENGTHS]
        for message in messages:
            digest_rows.append([name, message.hex(), hashlib.new(name, message).hexdigest()])
    xof_rows = []
    for name in ["shake_128", "shake_256"]:
        for length, out in [(0, 32), (3, 64), (136, 200), (168, 400), (300, 17)]:
            message = b"abc" if length == 3 else pattern(length)
            xof_rows.append([name, message.hex(), str(out), hashlib.new(name, message).hexdigest(out)])
    path = write_module(
        "crypto-hash", "hash-vectors.nra", "HashVectors",
        [f"python3 scripts/crypto-vectors/gen-hash.py (Python {sys.version.split()[0]} hashlib,",
         f"hashlib over {ssl.OPENSSL_VERSION}). Messages: \"abc\" and pattern(n) for n in {LENGTHS}."],
        [("digests", ["hashlib name", "message", "digest"], digest_rows),
         ("xofs", ["hashlib name", "message", "output length", "output"], xof_rows)])
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
