#!/usr/bin/env python3
"""Extract the compact RFC 7520 ECDH-ES vectors and verify them independently.

    python3 scripts/crypto-vectors/gen-token-jwe-ecdh.py [sources-dir]

RFC 7518 Appendix C gives an independent Concat KDF result. RFC 7520 §§5.4
and 5.5 give exact compact outputs, and §5.13 provides an exact A256KW
wrapped key for the second recipient. Its inputs are used to build a separate
compact fixture. A192KW and a two-block Concat KDF have additional standalone
fixtures. All ECDH, Concat KDF, AES-KW, GCM and CBC work uses cryptography.
"""

import ast
import base64
import hashlib
import hmac
import json
from pathlib import Path
import re
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.concatkdf import ConcatKDFHash
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
from cryptography.hazmat.primitives.padding import PKCS7

from nra_vectors import write_module

SOURCES = Path("/home/dev/neura-gate/030/crypto2/sources/token")
PINS = {
    "rfc7518": "9a9ae524b09ea700ad3f189bac115df95ba69af84b26ffdbe3cdfb8d2152b1fc",
    "rfc7520": "14798c3b2d72e35375d5fb86d280a1e8df6109a6ee6cbfc091a0c57a8cc076c6",
}
CURVES = {"P-256": ec.SECP256R1, "P-384": ec.SECP384R1, "P-521": ec.SECP521R1}
CEKS = {"A128GCM": 16, "A192GCM": 24, "A256GCM": 32, "A128CBC-HS256": 32, "A256CBC-HS512": 64}
WRAPS = {"ECDH-ES+A128KW": 16, "ECDH-ES+A192KW": 24, "ECDH-ES+A256KW": 32}


def source(root, name):
    data = (root / f"{name}.txt").read_bytes()
    if hashlib.sha256(data).hexdigest() != PINS[name]:
        raise ValueError(f"unexpected SHA-256 for {name}")
    return data.decode()


def encode(raw):
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def figure(text, number):
    match = re.search(rf"(?m)((?:^ +[\w.-]+\n)+)\n +Figure {number}:", text)
    return "".join(match[1].split())


def figure_key(text, number):
    item = text.split(f"Figure {number}:", maxsplit=1)[0].rsplit("{", maxsplit=1)[1]
    return json.loads("{" + re.sub(r"\n +", "", item.split("}", maxsplit=1)[0]) + "}")


def serialized(value):
    return json.dumps(value, ensure_ascii=True, separators=(",", ":")).encode("ascii")


def private(key):
    result = ec.derive_private_key(int.from_bytes(decode(key["d"]), "big"), CURVES[key["crv"]]())
    assert public(result.public_key(), key["crv"]) == {name: key[name] for name in ("kty", "crv", "x", "y")}
    return result


def public(key, curve):
    point = key.public_numbers()
    width = (key.curve.key_size + 7) // 8
    return {"kty": "EC", "crv": curve, "x": encode(point.x.to_bytes(width, "big")),
            "y": encode(point.y.to_bytes(width, "big"))}


def new_key(curve, secret):
    key = ec.derive_private_key(secret, CURVES[curve]())
    width = (key.curve.key_size + 7) // 8
    return public(key.public_key(), curve) | {"d": encode(secret.to_bytes(width, "big"))}


def size_prefix(raw):
    return len(raw).to_bytes(4, "big") + raw


def derive(alg, enc, static, ephemeral, apu, apv):
    direct = alg == "ECDH-ES"
    length = CEKS[enc] if direct else WRAPS[alg]
    algorithm_id = (enc if direct else alg).encode("ascii")
    info = b"".join(map(size_prefix, (algorithm_id, apu, apv))) + (length * 8).to_bytes(4, "big")
    shared = private(ephemeral).exchange(ec.ECDH(), private(static).public_key())
    return ConcatKDFHash(algorithm=hashes.SHA256(), length=length, otherinfo=info).derive(shared)


def encrypt(enc, cek, iv, plaintext, aad):
    if enc.endswith("GCM"):
        output = AESGCM(cek).encrypt(iv, plaintext, aad)
        return output[:-16], output[-16:]
    size = len(cek) // 2
    padder = PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    cipher = Cipher(algorithms.AES(cek[size:]), modes.CBC(iv)).encryptor()
    ciphertext = cipher.update(padded) + cipher.finalize()
    mac = hmac.digest(cek[:size], aad + iv + ciphertext + (len(aad) * 8).to_bytes(8, "big"), f"sha{len(cek) * 8}")
    return ciphertext, mac[:size]


def case(name, static, ephemeral, iv, plaintext, protected, cek=None, *, expected_key=None, expected_wrap=None):
    fields = json.loads(protected)
    alg, enc = fields["alg"], fields["enc"]
    apu, apv = (decode(fields.get(field, "")) for field in ("apu", "apv"))
    agreed = derive(alg, enc, static, ephemeral, apu, apv)
    if expected_key is not None:
        assert agreed == expected_key
    assert fields["epk"] == public(private(ephemeral).public_key(), ephemeral["crv"])
    if cek is None:
        cek, wrapped = agreed, b""
    else:
        wrapped = aes_key_wrap(agreed, cek)
    if expected_wrap is not None:
        assert wrapped == expected_wrap
    ciphertext, tag = encrypt(enc, cek, iv, plaintext, encode(protected).encode("ascii"))
    token = ".".join(map(encode, (protected, wrapped, iv, ciphertext, tag)))
    return [name, alg, enc, static["crv"], decode(static["d"]).hex(), decode(ephemeral["d"]).hex(),
            cek.hex(), iv.hex(), plaintext.hex(), protected.hex(), token]


def protected(alg, enc, ephemeral, apu=b"", apv=b""):
    header = {"alg": alg, "enc": enc}
    if apu:
        header["apu"] = encode(apu)
    if apv:
        header["apv"] = encode(apv)
    header["epk"] = public(private(ephemeral).public_key(), ephemeral["crv"])
    return serialized(header)


def malformed_headers(reference):
    valid = bytes.fromhex(reference[9])
    base = json.loads(valid)
    point = base["epk"]
    for label, mutation in (
        ("missing epk", {name: value for name, value in base.items() if name != "epk"}),
        ("epk string", base | {"epk": point["x"]}),
        ("wrong kty", base | {"epk": point | {"kty": "OKP"}}),
        ("private epk", base | {"epk": point | {"d": "AQ"}}),
        ("other curve", base | {"epk": point | {"crv": "P-384"}}),
        ("short x", base | {"epk": point | {"x": "AQ"}}),
        ("noncanonical x", base | {"epk": point | {"x": point["x"] + "="}}),
        ("off curve", base | {"epk": point | {"x": encode(bytes(32)), "y": encode(bytes(32))}}),
        ("padded apu", base | {"apu": "QQ=="}),
        ("apv number", base | {"apv": 7}),
        ("identical parties", base | {"apu": "QWxpY2U", "apv": "QWxpY2U"}),
        ("identical empty parties", base | {"apu": "", "apv": ""}),
    ):
        yield [label, serialized(mutation).hex(), "crypto.invalid_jwe"]
    duplicate = valid.replace(b'"kty":"EC"', b'"kty":"EC","\\u006bty":"EC"')
    yield ["duplicate epk member", duplicate.hex(), "crypto.invalid_jwe"]
    yield ["mutated apu", serialized(base | {"apu": "RWF2ZQ"}).hex(), "crypto.authentication_failed"]


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    rfc7518, rfc7520 = (source(root, name) for name in PINS)
    intro = rfc7520.split("\n5.  JSON Web Encryption Examples", maxsplit=1)[1].split("Figure 72:", maxsplit=1)[0]
    plaintext = b"".join(ast.literal_eval("b" + line) for line in re.findall(r'(?m)^   ("[^"\n]*")$', intro))

    direct = case("rfc7520-5.5", figure_key(rfc7520, 120), figure_key(rfc7520, 122), decode(figure(rfc7520, 121)),
                  plaintext, decode(figure(rfc7520, 125)), expected_key=decode(figure(rfc7520, 123)))
    assert direct[10] == figure(rfc7520, 128)
    wrapped = case("rfc7520-5.4", figure_key(rfc7520, 108), figure_key(rfc7520, 111), decode(figure(rfc7520, 110)),
                   plaintext, decode(figure(rfc7520, 114)), decode(figure(rfc7520, 109)),
                   expected_wrap=decode(figure(rfc7520, 112)))
    assert wrapped[10] == figure(rfc7520, 117)

    appendix = rfc7518.split("\nAppendix C.  Example ECDH-ES Key Agreement Computation", maxsplit=1)[1]
    alice, bob = [json.loads(key) for key in re.findall(r'\{"kty":"EC",[^{}]+?\}', appendix)[:2]]
    party_example = case("rfc7518-app-c", bob, alice, bytes(range(12)), b"RFC 7518 Appendix C",
                         protected("ECDH-ES", "A128GCM", alice, b"Alice", b"Bob"),
                         expected_key=decode(re.search(r"(?m)^     (VqqN[\w-]+)$", appendix)[1]))

    kw256_ephemeral = figure_key(rfc7520, 207)
    kw256 = case("rfc7520-5.13-wrap-in-compact", figure_key(rfc7520, 108), kw256_ephemeral, bytes(range(16)), plaintext,
                 protected("ECDH-ES+A256KW", "A128CBC-HS256", kw256_ephemeral), decode(figure(rfc7520, 202)),
                 expected_wrap=decode(figure(rfc7520, 208)))

    kw192_static = new_key("P-521", 3)
    kw192_ephemeral = new_key("P-521", 2)
    kw192 = case("p521-a192kw", kw192_static, kw192_ephemeral, b"\x20" * 12, b"",
                 protected("ECDH-ES+A192KW", "A192GCM", kw192_ephemeral, apv=b"receiver"), bytes(range(24)))

    two_block_static = new_key("P-521", 5)
    two_block_ephemeral = new_key("P-521", 7)
    two_blocks = case("p521-direct-512", two_block_static, two_block_ephemeral, b"\x33" * 16, bytes(range(45)),
                      protected("ECDH-ES", "A256CBC-HS512", two_block_ephemeral, b"producer", b"receiver"))

    print(write_module("crypto-token", "jwe-ecdh-vectors.nra", "JweEcdhVectors",
                       ["python3 scripts/crypto-vectors/gen-token-jwe-ecdh.py",
                        "RFC 7520 sections 5.4, 5.5, 5.13 and RFC 7518 Appendix C.",
                        "Reference implementations: Python cryptography 43.0.0 and hmac."],
                       [("cases", ["case", "alg", "enc", "curve", "recipient d", "ephemeral d", "cek", "iv",
                                   "plaintext", "protected", "compact"],
                         [party_example, direct, wrapped, kw192, kw256, two_blocks]),
                        ("malformed", ["case", "protected", "error code"], list(malformed_headers(party_example)))]))


if __name__ == "__main__":
    main()
