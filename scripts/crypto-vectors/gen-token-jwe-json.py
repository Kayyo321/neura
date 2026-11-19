#!/usr/bin/env python3
"""Verify RFC 7516 Appendix A.5 and RFC 7520 §§5.10–5.13 JWE JSON specimens.

    python3 scripts/crypto-vectors/gen-token-jwe-json.py [rfc-source-directory]

Only editorial formatting of the JSON wrappers is removed. Header, key,
AAD, ciphertext and tag encodings remain the published values. Cryptography
43.0.0 independently checks the recipient wrapping and content encryption.
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
    "rfc7516": "722cfcddc15f58e746891ba4b846853d3711153a774e4dd55ad6db100474a21f",
    "rfc7520": "14798c3b2d72e35375d5fb86d280a1e8df6109a6ee6cbfc091a0c57a8cc076c6",
}


def source(root, name):
    raw = (root / f"{name}.txt").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PINS[name]:
        raise ValueError(f"unexpected SHA-256 for {name}")
    return re.sub(r"(?m)^.*\[Page \d+\]\n\f\nRFC \d+[^\n]*\n", "", raw.decode("utf-8"))


def encode(raw):
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def decode(text):
    raw = base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))
    assert encode(raw) == text
    return raw


def minified(value):
    return json.dumps(value, ensure_ascii=True, separators=(",", ":")).encode("ascii")


def before_figure(text, number):
    return text[:re.search(rf"(?m)^ +Figure {number}:", text).start()]


def figure_json(text, number, opening="{"):
    previous = before_figure(text, number).rstrip()
    start = previous.rindex("\n   " + opening + "\n")
    # Within JSON strings, only encoded values are folded across source lines.
    return json.loads(re.sub(r"\n *", "", previous[start:]))


def figure_encoded(text, number):
    previous = before_figure(text, number).rstrip()
    block = previous.rsplit("\n\n", 1)[1]
    return decode("".join(block.split()))


def rfc7520_plaintext(text):
    intro = before_figure(text, 72).split("\n5.  JSON Web Encryption Examples\n", 1)[1]
    return b"".join(ast.literal_eval("b" + line) for line in re.findall(r'(?m)^   ("[^"\n]*")$', intro))


def content_encrypt(enc, cek, iv, plaintext, aad):
    if enc == "A128GCM":
        return AESGCM(cek).encrypt(iv, plaintext, aad)
    assert enc == "A128CBC-HS256"
    padder = PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    encryptor = Cipher(algorithms.AES(cek[16:]), modes.CBC(iv)).encryptor()
    ciphertext = encryptor.update(padded) + encryptor.finalize()
    mac = hmac.digest(cek[:16], aad + iv + ciphertext + (8 * len(aad)).to_bytes(8, "big"), "sha256")
    return ciphertext + mac[:16]


def p384_point(jwk):
    assert jwk["kty"] == "EC" and jwk["crv"] == "P-384"
    x, y = (int.from_bytes(decode(jwk[member]), "big") for member in ("x", "y"))
    return ec.EllipticCurvePublicNumbers(x, y, ec.SECP384R1())


def ecdh_kek(secret, header):
    key = ec.derive_private_key(int.from_bytes(secret, "big"), ec.SECP384R1())
    shared = key.exchange(ec.ECDH(), p384_point(header["epk"]).public_key())
    fields = (header["alg"].encode("ascii"), decode(header.get("apu", "")), decode(header.get("apv", "")))
    other = b"".join(len(field).to_bytes(4, "big") + field for field in fields) + (256).to_bytes(4, "big")
    return ConcatKDFHash(algorithm=hashes.SHA256(), length=32, otherinfo=other).derive(shared)


def verify(name, doc, index, key, cek, message):
    recipient = doc["recipients"][index] if "recipients" in doc else doc
    encoded_protected = doc.get("protected", "")
    protected = json.loads(decode(encoded_protected)) if encoded_protected else {}
    shared = doc.get("unprotected", {})
    own = recipient.get("header", {})
    assert not (protected.keys() & shared.keys() or protected.keys() & own.keys() or shared.keys() & own.keys())
    header = protected | shared | own
    alg, enc = header["alg"], header["enc"]
    aad = encoded_protected.encode("ascii")
    if "aad" in doc:
        aad += b"." + doc["aad"].encode("ascii")
    assert content_encrypt(enc, cek, decode(doc["iv"]), message, aad) == decode(doc["ciphertext"]) + decode(doc["tag"])

    wrapped = decode(recipient.get("encrypted_key", ""))
    if alg == "ECDH-ES+A256KW":
        assert aes_key_wrap(ecdh_kek(key, header), cek) == wrapped
    elif alg == "A256GCMKW":
        assert AESGCM(key).encrypt(decode(header["iv"]), cek, b"") == wrapped + decode(header["tag"])
    elif alg == "A128KW":
        assert aes_key_wrap(key, cek) == wrapped
    else:
        assert alg == "dir" and not wrapped and key == cek

    return [name, alg, enc, str(index), key.hex(), message.hex(), decode(doc.get("aad", "")).hex(), minified(doc).hex()]


def rfc7516_flattened(text):
    section = text.split("\nA.5.  Example JWE Using Flattened", 1)[1]
    doc = json.loads(re.search(r"(?ms)^     \{\n.*?^     \}", section)[0])
    inputs = text.split("\nA.3.  Example JWE Using AES Key Wrap", 1)[1].split("\nA.4.  ", 1)[0]
    message = bytes(json.loads(re.search(r"(?m)^   (\[[\d, \n]+\])", inputs)[1]))
    cek_text = inputs.split("\nA.3.2.  ", 1)[1]
    cek = bytes(json.loads(re.search(r"(?m)^   (\[[\d, \n]+\])", cek_text)[1]))
    key = decode(re.search(r'"k":"([A-Za-z0-9_-]+)"', inputs)[1])
    return verify("rfc7516-a5", doc, 0, key, cek, message)


def independent_direct():
    key = hashlib.sha256(b"JWE JSON open fixtures").digest()[:16]
    message = b"authenticated external data"
    for label, empty_aad in (("absent-aad", False), ("present-empty-aad", True)):
        iv = hashlib.sha256(label.encode("ascii")).digest()[:12]
        doc = {"header": {"alg": "dir", "enc": "A128GCM"}, "iv": encode(iv)}
        aad = b""
        if empty_aad:
            doc["aad"] = ""
            aad = b"."
        sealed = AESGCM(key).encrypt(iv, message, aad)
        doc["ciphertext"], doc["tag"] = encode(sealed[:-16]), encode(sealed[-16:])
        yield verify(label, doc, 0, key, key, message)


def rfc7520_cases(text):
    message = rfc7520_plaintext(text)
    kw = decode(figure_json(text, 151)["k"])
    external_aad = minified(figure_json(text, 173, "["))
    assert external_aad == figure_encoded(text, 176)
    for section, cek_figure, general, flattened in ((10, 174, 182, 183), (11, 184, 192, 193), (12, 194, 200, 201)):
        cek = figure_encoded(text, cek_figure)
        for shape, number in (("general", general), ("flattened", flattened)):
            doc = figure_json(text, number)
            assert (decode(doc["aad"]) == external_aad) if section == 10 else ("aad" not in doc)
            yield verify(f"rfc7520-5.{section}-{shape}", doc, 0, kw, cek, message)

    general = figure_json(text, 221)
    cek = figure_encoded(text, 202)
    ecdh = figure_json(text, 108)
    secret = decode(ecdh["d"])
    reference = ec.derive_private_key(int.from_bytes(secret, "big"), ec.SECP384R1())
    assert reference.public_key().public_numbers() == p384_point(ecdh)
    yield verify("rfc7520-5.13-ecdh", general, 1, secret, cek, message)
    gcm = decode(figure_json(text, 138)["k"])
    yield verify("rfc7520-5.13-gcmkw", general, 2, gcm, cek, message)



def rejected_kw_cases(rfc7520):
    mixed = figure_json(rfc7520, 192)
    external = figure_json(rfc7520, 183)
    changed_aad = decode(external["aad"]) + b"!"
    yield ["wire AAD changed", minified(external | {"aad": encode(changed_aad)}).hex(),
           changed_aad.hex(), "crypto.authentication_failed"]
    header = b" " + decode(external["protected"])
    yield ["protected bytes changed", minified(external | {"protected": encode(header)}).hex(),
           decode(external["aad"]).hex(), "crypto.authentication_failed"]

    shared = mixed["unprotected"]
    first = mixed["recipients"][0]
    for name, document in (
        ("protected and shared duplicate", mixed | {"unprotected": shared | {"enc": "A128GCM"}}),
        ("protected and recipient duplicate", mixed | {"recipients": [first | {"header": {"enc": "A128GCM"}}]}),
        ("shared and recipient duplicate", mixed | {"recipients": [first | {"header": {"alg": "A128KW"}}]}),
        ("mixed general and flattened", mixed | {"encrypted_key": first["encrypted_key"]}),
        ("empty recipients", mixed | {"recipients": []}),
        ("non-object recipient", mixed | {"recipients": [False]}),
    ):
        yield [name, minified(document).hex(), "", "crypto.invalid_jwe"]
    duplicate = b'{"\\u0074ag":"AA",' + minified(mixed)[1:]
    yield ["duplicate tag by escape", duplicate.hex(), "", "crypto.invalid_jwe"]

def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    rfc7516, rfc7520 = (source(root, name) for name in PINS)
    rows = [*rfc7520_cases(rfc7520), rfc7516_flattened(rfc7516), *independent_direct()]
    print(write_module("crypto-token", "jwe-json-vectors.nra", "JweJsonVectors", [
        "python3 scripts/crypto-vectors/gen-token-jwe-json.py",
        "RFC 7516 Appendix A.5 and RFC 7520 sections 5.10–5.13, verified via cryptography 43.0.0.",
        "RFC 7516 text SHA-256 " + PINS["rfc7516"],
        "RFC 7520 text SHA-256 " + PINS["rfc7520"],
    ], [
        ("cases", ["case", "alg", "enc", "recipient index", "key", "plaintext", "expected external AAD", "JSON wire"], rows),
        ("rejected", ["case", "JSON wire", "expected external AAD", "error code"], list(rejected_kw_cases(rfc7520))),
    ]))


if __name__ == "__main__":
    main()
