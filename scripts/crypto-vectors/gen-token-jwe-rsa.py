#!/usr/bin/env python3
"""Known answers for JWE with RSA-OAEP-256 (RFC 7518 section 4.3).

    python3 scripts/crypto-vectors/gen-token-jwe-rsa.py [sources-dir]

Uses the published RFC 7516 Appendix A.1 RSA key, message, CEK, and IV.
Constructs the SHA-256 OAEP encoding independently with Python hashlib and
integer exponentiation, then checks the recovered CEK with cryptography 43.
The published SHA-1 RSA-OAEP ciphertext is verified separately as an
independent check of the RSA key input. This library rejects that algorithm.
"""

import hashlib
import importlib.util
import json
import pathlib
import re
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from nra_vectors import write_module


def base_generator():
    script = pathlib.Path(__file__).with_name("gen-token-jwe.py")
    spec = importlib.util.spec_from_file_location("gen_token_jwe", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mgf(seed, length):
    return b"".join(hashlib.sha256(seed + counter.to_bytes(4, "big")).digest()
                    for counter in range((length + 31) // 32))[:length]


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b, strict=True))


def rsa_oaep(message, seed, n, e):
    key_len = (n.bit_length() + 7) // 8
    block = hashlib.sha256(b"").digest() + bytes(key_len - len(message) - 66) + b"\x01" + message
    masked_block = xor(block, mgf(seed, len(block)))
    masked_seed = xor(seed, mgf(masked_block, 32))
    encoded = b"\x00" + masked_seed + masked_block
    return pow(int.from_bytes(encoded, "big"), e, n).to_bytes(key_len, "big")


def main():
    source = base_generator()
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else source.SOURCES
    rfc = source.read(root, "rfc7516")
    block = source.section(rfc, "A.1.  Example JWE", "A.2.  Example JWE")
    raw_jwk = re.search(r'\{"kty":"RSA",.*?\n +\}', block, re.DOTALL)[0]
    jwk = json.loads(re.sub(r"\n +", "", raw_jwk))
    components = [source.decode(jwk[field]) for field in ("n", "e", "d", "p", "q", "dp", "dq", "qi")]
    n, e, d, p, q, dp, dq, qi = (int.from_bytes(raw, "big") for raw in components)
    ref_key = rsa.RSAPrivateNumbers(p, q, d, dp, dq, qi, rsa.RSAPublicNumbers(e, n)).private_key()

    plaintext, cek, *_ = [bytes(json.loads(value)) for value in re.findall(r"\[(?:\d|,|\s)+\]", block)]
    legacy = source.rfc7516_token(rfc, 1).split(".")
    assert ref_key.decrypt(source.decode(legacy[1]), padding.OAEP(padding.MGF1(hashes.SHA1()), hashes.SHA1(), None)) == cek
    old_iv = source.decode(legacy[2])
    # Derive fresh deterministic fixture values so the new token doesn't repeat the RFC example's GCM IV.
    iv = hashlib.sha256(b"Neura RFC7516 A.1 JWE RSA-OAEP-256 IV" + old_iv).digest()[:12]
    seed = hashlib.sha256(b"Neura RFC7516 A.1 JWE RSA-OAEP-256 seed" + cek).digest()
    wrapped = rsa_oaep(cek, seed, n, e)
    oaep = padding.OAEP(padding.MGF1(hashes.SHA256()), hashes.SHA256(), None)
    assert ref_key.decrypt(wrapped, oaep) == cek
    short = rsa_oaep(cek[:16], hashlib.sha256(seed).digest(), n, e)
    assert ref_key.decrypt(short, oaep) == cek[:16]

    header = source.header("RSA-OAEP-256", "A256GCM")
    sealed = source.AESGCM(cek).encrypt(iv, plaintext, source.aad_bytes(header))
    token = source.compact(header, wrapped, iv, sealed[:-16], sealed[-16:])
    wrong_cek = source.compact(header, short, iv, sealed[:-16], sealed[-16:])
    row = [value.hex() for value in (*components, cek, iv, seed, plaintext, header)]
    row.extend((token, wrong_cek))
    print(write_module("crypto-token", "jwe-rsa-vectors.nra", "JweRsaVectors",
                       ["python3 scripts/crypto-vectors/gen-token-jwe-rsa.py",
                        "RFC 7516 Appendix A.1 key, message and CEK, with independently computed SHA-256 OAEP.",
                        "Reference decryption and content encryption: Python cryptography 43.0.0."],
                       [("rsa256", ["n", "e", "d", "p", "q", "dp", "dq", "qi", "cek", "iv", "seed",
                                    "plaintext", "header", "token", "wrong length CEK token"], [row])]))


if __name__ == "__main__":
    main()
