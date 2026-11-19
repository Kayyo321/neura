#!/usr/bin/env python3
"""Expected output of Std.Crypto.Random.ChaCha20Drbg from an independent Python reference.

The generator's construction is defined in stdlib/std/crypto/random/cha-cha20-drbg.nra (no standard fixes
it, so no published vectors exist). This script implements it from that description over hashlib's SHA-256
and the ChaCha20 of the `cryptography` package (version recorded in the file), and records a sequence of
calls: seed, fill(n) ..., reseed(material), fill(n) ....

    python3 scripts/crypto-vectors/gen-drbg.py
"""

import hashlib
import sys

import cryptography
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms

from nra_vectors import pattern, write_module


class Drbg:
    def __init__(self, seed: bytes):
        self.key = hashlib.sha256(b"Neura ChaCha20-DRBG seed" + seed).digest()

    def reseed(self, material: bytes):
        self.key = hashlib.sha256(b"Neura ChaCha20-DRBG reseed" + self.key + material).digest()

    def fill(self, count: int) -> bytes:
        stream = Cipher(algorithms.ChaCha20(self.key, bytes(16)), mode=None).encryptor().update(bytes(32 + count))
        self.key = stream[:32]
        return stream[32:]


def main() -> int:
    rows = []
    for seed_length, steps in [(32, ["fill:0", "fill:1", "fill:31", "fill:32", "fill:100"]),
                               (48, ["fill:64", "reseed:0", "fill:16", "reseed:40", "fill:200", "fill:7"]),
                               (100, ["reseed:3", "fill:33", "fill:96"])]:
        seed = pattern(seed_length, 13, seed_length)
        drbg = Drbg(seed)
        for step in steps:
            kind, number = step.split(":")
            n = int(number)
            if kind == "fill":
                rows.append([seed.hex(), step, drbg.fill(n).hex()])
            else:
                material = pattern(n, 5, 9)
                drbg.reseed(material)
                rows.append([seed.hex(), step, material.hex()])
    print(write_module(
        "crypto-random", "drbg-vectors.nra", "DrbgVectors",
        ["python3 scripts/crypto-vectors/gen-drbg.py: an independent Python reference of the construction",
         f"(hashlib SHA-256, cryptography {cryptography.__version__} ChaCha20); no standard vectors exist.",
         "Rows run in order on one generator per seed: fill:n gives the expected bytes, reseed:n the material."],
        [("steps", ["seed", "step", "output or reseed material"], rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
