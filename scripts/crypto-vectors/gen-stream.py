#!/usr/bin/env python3
"""Expected values for Std.Crypto.Symmetric.OnlineCipher (STREAM) from an independent Python reference.

STREAM (Hoang, Reyhanitabar, Rogaway and Vizar, "Online Authenticated-Encryption
and its Nonce-Reuse Misuse-Resistance", CRYPTO 2015) has no standard test
vectors, so this script implements the nonce-based variant that Std.Crypto
specifies and compares against: segment i (from 0) of a stream is sealed by the
underlying AEAD with the nonce

    prefix || big-endian uint32(i) || (0x01 for the last segment, else 0x00)

and the stream's associated data, where the prefix fills the AEAD's nonce
length less 5 bytes (Tink's nonce-prefix segmented AEAD lays its nonces out the
same way). The AEADs are those of the Python `cryptography` package; for
XChaCha20-Poly1305 the HChaCha20 subkey is derived from `cryptography`'s
ChaCha20 as in gen-symmetric.py.

    python3 scripts/crypto-vectors/gen-stream.py
"""

import struct
import sys

import cryptography
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, AESGCMSIV, ChaCha20Poly1305

from nra_vectors import pattern, write_module


def hchacha20(key: bytes, nonce16: bytes) -> bytes:
    block = Cipher(algorithms.ChaCha20(key, nonce16), mode=None).encryptor().update(bytes(64))
    words = struct.unpack("<16I", block)
    initial = struct.unpack("<4I", b"expand 32-byte k") + struct.unpack("<8I", key) + struct.unpack("<4I", nonce16)
    raw = [(w - i) % 2**32 for w, i in zip(words, initial)]
    return struct.pack("<8I", *(raw[0:4] + raw[12:16]))


class XChaCha20Poly1305:
    def __init__(self, key):
        self.key = key

    def encrypt(self, nonce, data, aad):
        return ChaCha20Poly1305(hchacha20(self.key, nonce[:16])).encrypt(bytes(4) + nonce[16:], data, aad)


def seal_stream(aead, prefix: bytes, aad: bytes, segments: list[bytes]) -> list[bytes]:
    out = []
    for i, segment in enumerate(segments):
        nonce = prefix + struct.pack(">I", i) + (b"\x01" if i == len(segments) - 1 else b"\x00")
        out.append(aead.encrypt(nonce, segment, aad))
    return out


def main() -> int:
    rows = []
    cases = [
        ("aes-gcm", AESGCM, 16, 12, [0]),
        ("aes-gcm", AESGCM, 32, 12, [16, 16, 5]),
        ("aes-gcm-siv", AESGCMSIV, 32, 12, [20, 20, 20, 1]),
        ("chacha20-poly1305", ChaCha20Poly1305, 32, 12, [64, 64, 0]),
        ("xchacha20-poly1305", XChaCha20Poly1305, 32, 24, [100, 37]),
    ]
    for index, (name, make, key_length, nonce_length, lengths) in enumerate(cases):
        key = pattern(key_length, 67, index)
        prefix = pattern(nonce_length - 5, 71, index)
        aad = pattern(index * 3, 5, 9)
        segments = [pattern(n, 73, index + k) for k, n in enumerate(lengths)]
        if make is AESGCMSIV and any(len(s) == 0 for s in segments):
            raise SystemExit("cryptography refuses empty AES-GCM-SIV plaintexts")
        sealed = seal_stream(make(key), prefix, aad, segments)
        rows.append([name, key.hex(), prefix.hex(), aad.hex(), str(len(segments)),
                     ":".join(s.hex() for s in segments), ":".join(s.hex() for s in sealed)])
    print(write_module(
        "crypto-symmetric", "stream-vectors.nra", "StreamVectors",
        ["python3 scripts/crypto-vectors/gen-stream.py: an independent Python reference of the STREAM",
         f"construction over cryptography {cryptography.__version__} AEADs (no standard vectors exist)."],
        [("streams", ["algorithm", "key", "nonce prefix", "associated data", "segment count",
                      "plaintext segments joined by ':'", "sealed segments joined by ':'"], rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
