#!/usr/bin/env python3
"""Known answers for Std.Crypto.Mac HMAC, Poly1305 and AES-CMAC.

HMAC tags come from Python's hmac module over hashlib (every SHA-2 and SHA-3
digest that Std.Crypto.Hash implements), with keys shorter than, equal to and
longer than the block. Poly1305 and AES-CMAC tags come from the Python
`cryptography` package (versions recorded in the file). Keys and messages are
nra_vectors.pattern bytes.

    python3 scripts/crypto-vectors/gen-mac.py
"""

import hashlib
import hmac
import sys

import cryptography
from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.ciphers import algorithms
from cryptography.hazmat.primitives.poly1305 import Poly1305

from nra_vectors import pattern, write_module

DIGESTS = ["sha224", "sha256", "sha384", "sha512", "sha512_256",
           "sha3_224", "sha3_256", "sha3_384", "sha3_512"]


def main() -> int:
    hmac_rows = []
    for name in DIGESTS:
        block = hashlib.new(name).block_size
        for key_length, message_length in [(0, 0), (20, 50), (block, 130), (block + 1, 0), (200, 300)]:
            key = pattern(key_length, 11, 3)
            message = pattern(message_length)
            hmac_rows.append([name, key.hex(), message.hex(), hmac.new(key, message, name).hexdigest()])
    poly_rows = []
    for message_length in [0, 1, 15, 16, 17, 64, 100, 257]:
        key = pattern(32, 29, message_length)
        message = pattern(message_length, 7, 1)
        poly_rows.append([key.hex(), message.hex(), Poly1305.generate_tag(key, message).hex()])
    # All-ones key parts and messages exercise the final carry and reduction.
    for key, message in [(bytes([255] * 32), bytes([255] * 48)), (bytes([0] * 16 + [255] * 16), bytes([255] * 16)),
                         (bytes([2] + [0] * 15 + [0] * 16), bytes([255] * 16 + [251] + [255] * 15 + [254] + [255] * 15))]:
        poly_rows.append([key.hex(), message.hex(), Poly1305.generate_tag(key, message).hex()])
    cmac_rows = []
    for key_length in (16, 24, 32):
        for message_length in [0, 1, 16, 17, 32, 40, 64, 100]:
            key = pattern(key_length, 17, key_length)
            message = pattern(message_length)
            c = cmac.CMAC(algorithms.AES(key))
            c.update(message)
            cmac_rows.append([key.hex(), message.hex(), c.finalize().hex()])
    print(write_module(
        "crypto-mac", "mac-vectors.nra", "MacVectors",
        ["python3 scripts/crypto-vectors/gen-mac.py: HMAC from Python hmac/hashlib,",
         f"Poly1305 and AES-CMAC from cryptography {cryptography.__version__}."],
        [("hmac", ["hashlib name", "key", "message", "tag"], hmac_rows),
         ("poly1305", ["one-time key", "message", "tag"], poly_rows),
         ("aes_cmac", ["key", "message", "tag"], cmac_rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
