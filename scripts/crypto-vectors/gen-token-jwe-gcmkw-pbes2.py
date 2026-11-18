#!/usr/bin/env python3
"""Verify RFC 7520 §5.3 and §5.7 compact JWEs and generate the remaining width cases.

Uses cryptography 43.0.0 for AES-GCM, AES-KW and CBC, and Python hashlib/hmac
for PBKDF2 and CBC-HMAC tags. Tests consume complete compact serializations.
"""

import ast
import base64
import hashlib
import hmac
import json
from pathlib import Path
import re
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
from cryptography.hazmat.primitives.padding import PKCS7

from nra_vectors import write_module

SOURCE = Path("/home/dev/neura-gate/030/crypto2/sources/token/rfc7520.txt")
SOURCE_SHA256 = "14798c3b2d72e35375d5fb86d280a1e8df6109a6ee6cbfc091a0c57a8cc076c6"
COLUMNS = [
    "case", "alg", "enc", "key or password", "CEK", "content IV", "wrapping IV or salt",
    "p2c or empty", "plaintext", "protected header", "compact token",
]


def b64(data):
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def unb64(encoded):
    return base64.urlsafe_b64decode(encoded + "=" * (-len(encoded) % 4))


def minified(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def figure(text, number):
    marker = re.search(rf"(?m)^ +Figure {number}:", text)
    if not marker:
        raise ValueError(f"RFC 7520 Figure {number} missing")
    block = text[:marker.start()].rstrip().rsplit("\n\n", 1)[-1]
    return block.strip()


def figure_wire(text, number):
    return "".join(figure(text, number).split())


def cbc_seal(cek, iv, plaintext, aad):
    width = len(cek) // 2
    padder = PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    encryptor = Cipher(algorithms.AES(cek[width:]), modes.CBC(iv)).encryptor()
    ciphertext = encryptor.update(padded) + encryptor.finalize()
    mac = hmac.digest(cek[:width], aad + iv + ciphertext + (8 * len(aad)).to_bytes(8, "big"), f"sha{width * 16}")
    return ciphertext, mac[:width]


def gcm_seal(key, iv, plaintext, aad):
    sealed = AESGCM(key).encrypt(iv, plaintext, aad)
    return sealed[:-16], sealed[-16:]


def pbes_wrap(alg, password, salt_input, count, cek):
    digest, size = re.fullmatch(r"PBES2-HS(256|384|512)\+A(128|192|256)KW", alg).groups()
    salt = alg.encode("ascii") + b"\0" + salt_input
    kek = hashlib.pbkdf2_hmac(f"sha{digest}", password, salt, count, int(size) // 8)
    return aes_key_wrap(kek, cek)


def encrypt_content(enc, cek, iv, plaintext, protected):
    aad = b64(protected).encode("ascii")
    if "CBC" in enc:
        return cbc_seal(cek, iv, plaintext, aad)
    return gcm_seal(cek, iv, plaintext, aad)


def compact(protected, wrapped, iv, ciphertext, tag):
    return ".".join(map(b64, (protected, wrapped, iv, ciphertext, tag)))


def verify_or_create(case, key, cek, iv, material, plaintext, protected, expected=None):
    header = json.loads(protected)
    alg, enc = header["alg"], header["enc"]
    if alg.endswith("GCMKW"):
        wrapped, wrap_tag = gcm_seal(key, material, cek, b"")
        assert unb64(header["iv"]) == material
        assert unb64(header["tag"]) == wrap_tag
        count = ""
    else:
        count = header["p2c"]
        assert unb64(header["p2s"]) == material
        wrapped = pbes_wrap(alg, key, material, count, cek)
    ciphertext, tag = encrypt_content(enc, cek, iv, plaintext, protected)
    wire = compact(protected, wrapped, iv, ciphertext, tag)
    if expected is not None and wire != expected:
        raise ValueError(f"{case}: independent JWE encryption disagrees with the official wire value")
    return [
        case, alg, enc, key.hex(), cek.hex(), iv.hex(), material.hex(), str(count),
        plaintext.hex(), protected.hex(), wire,
    ]


def cookbook_cases(text):
    gcm_key = unb64(json.loads(figure(text, 138))["k"])
    gcm_cek, gcm_iv, wrap_iv = (unb64(figure_wire(text, n)) for n in (139, 140, 141))
    gcm_wire = figure_wire(text, 148)
    intro = text.split("\n5.  JSON Web Encryption Examples\n", 1)[1].split("\n5.1.  Key Encryption", 1)[0]
    message = b"".join(ast.literal_eval("b" + line) for line in re.findall(r'(?m)^   ("[^"\n]*")$', intro))
    yield verify_or_create(
        "rfc7520-5.7", gcm_key, gcm_cek, gcm_iv, wrap_iv, message, unb64(gcm_wire.split(".")[0]), gcm_wire
    )

    pbes_wire = figure_wire(text, 105)
    pbes_cek, pbes_iv, salt = (unb64(figure_wire(text, n)) for n in (97, 98, 99))
    # Figure 96 writes the UTF-8 en dashes as escaped octets.
    password = ast.literal_eval('b"' + figure(text, 96) + '"')
    message = minified(json.loads(figure(text, 95)))
    yield verify_or_create(
        "rfc7520-5.3", password, pbes_cek, pbes_iv, salt, message, unb64(pbes_wire.split(".")[0]), pbes_wire
    )


def reference_entropy(label, size):
    return hashlib.shake_256(label.encode("ascii")).digest(size)


def supplemental_cases():
    for width in (128, 192):
        alg, enc = f"A{width}GCMKW", f"A{width}GCM"
        key = reference_entropy(alg + " KEK", width // 8)
        cek = reference_entropy(alg + " CEK", width // 8)
        iv = reference_entropy(alg + " content IV", 12)
        wrap_iv = reference_entropy(alg + " wrapping IV", 12)
        _, tag = gcm_seal(key, wrap_iv, cek, b"")
        protected = minified({"alg": alg, "enc": enc, "iv": b64(wrap_iv), "tag": b64(tag), "kid": alg})
        yield verify_or_create("independent-" + alg, key, cek, iv, wrap_iv, b"Key management example", protected)

    for digest, width in ((256, 128), (384, 192)):
        alg, enc = f"PBES2-HS{digest}+A{width}KW", f"A{width}GCM"
        password = ("fixture password " + alg + " \N{EN DASH}").encode("utf-8")
        cek = reference_entropy(alg + " CEK", width // 8)
        iv = reference_entropy(alg + " content IV", 12)
        salt = reference_entropy(alg + " salt", 16)
        protected = minified({"alg": alg, "enc": enc, "p2s": b64(salt), "p2c": 1000, "cty": "text/plain"})
        yield verify_or_create("independent-" + alg, password, cek, iv, salt, b"", protected)


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError(f"unexpected RFC 7520 SHA-256: {digest}")
    rows = [*cookbook_cases(data.decode("utf-8")), *supplemental_cases()]
    print(write_module("crypto-token", "jwe-gcmkw-pbes2-vectors.nra", "JweGcmkwPbes2Vectors", [
        "python3 scripts/crypto-vectors/gen-token-jwe-gcmkw-pbes2.py",
        "RFC 7520 §5.3 and §5.7 independently verified. Remaining widths generated with cryptography 43.0.0.",
        "RFC text SHA-256 " + digest,
    ], [("cases", COLUMNS, rows)]))


if __name__ == "__main__":
    main()
