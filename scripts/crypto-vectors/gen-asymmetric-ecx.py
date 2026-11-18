#!/usr/bin/env python3
"""Known answers for X25519, X448, Ed25519 and Ed448 (Std.Crypto.Asymmetric).

Sources (downloads recorded in /home/dev/neura-gate/030/crypto2/sources/asymmetric/SOURCES.txt):
- RFC 7748 section 5.2 (the X25519 and X448 function vectors, and the
  results after 1 and 1,000 iterations; the 1,000,000-iteration value is not
  used) and section 6 (the Diffie-Hellman examples), read from the RFC text.
- RFC 8032 section 7 (every Ed25519, Ed25519ctx, Ed25519ph, Ed448 and
  Ed448ph vector), read from the RFC text.
- Wycheproof (C2SP/wycheproof testvectors_v1 at the commit in SOURCES.txt):
  x25519_test.json and x448_test.json (a fixed subset: every case flagged
  ZeroSharedSecret, LowOrderPublic, NonCanonicalPublic, SmallPublicKey or
  invalid, plus every 8th other case), ed25519_test.json and ed448_test.json
  (every case). The expected verdict of each signature case is Wycheproof's for
  "valid" and "invalid"; an "acceptable" Ed25519 case (small- or mixed-order
  public key) takes PyNaCl's (libsodium's) verdict, the strict rule this
  library implements, and the generator checks that PyNaCl and
  `cryptography` agree with every valid and invalid case.
- Extra positives: Ed25519 signatures from PyNaCl and Ed448 signatures from
  `cryptography` over nra_vectors.pattern keys and messages.

    python3 scripts/crypto-vectors/gen-asymmetric-ecx.py
"""

import json
import sys

import cryptography
import nacl
import nacl.exceptions
import nacl.signing
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric import ed448, ed25519

import rfc_text
from nra_vectors import pattern, write_module

SOURCES = rfc_text.SOURCES
WYCHEPROOF = SOURCES + "/wycheproof/"


def rfc7748():
    text = rfc_text.lines(SOURCES + "/rfc7748.txt")
    vectors = rfc_text.section(text, "5.2.  Test Vectors", "6.  Diffie-Hellman")
    iterations_start = next(i for i, line in enumerate(vectors) if "For each iteration" in line)
    x25519_part = vectors[:next(i for i, line in enumerate(vectors) if line.strip() == "X448:")]
    x448_part = vectors[len(x25519_part):iterations_start]
    iteration_part = vectors[iterations_start:]
    function_rows, iteration_rows, dh_rows = [], [], []
    for which, (name, part, size) in enumerate([("x25519", x25519_part, 32), ("x448", x448_part, 56)]):
        for index in range(2):
            row = [name, rfc_text.hex_after(part, "Input scalar:", index),
                   rfc_text.hex_after(part, "Input u-coordinate:", index),
                   rfc_text.hex_after(part, "Output u-coordinate:", index)]
            assert all(len(cell) == 2 * size for cell in row[1:]), row
            function_rows.append(row)
        for label, count in [("After one iteration:", "1"), ("After 1,000 iterations:", "1000")]:
            value = rfc_text.hex_after(iteration_part, label, which)
            assert len(value) == 2 * size
            iteration_rows.append([name, count, value])
    for name, start, end, size in [("x25519", "6.1.  Curve25519", "6.2.  Curve448", 32),
                                   ("x448", "6.2.  Curve448", "7.  Security Considerations", 56)]:
        part = rfc_text.section(text, start, end)
        labels = [line.strip() for line in part if line.strip().startswith(("Alice's", "Bob's", "Their shared"))]
        hexes = [rfc_text.hex_after(part, label) for label in labels]
        assert len(hexes) == 5 and all(len(h) == 2 * size for h in hexes), (name, labels)
        dh_rows.append([name] + hexes)
    return function_rows, iteration_rows, dh_rows


def rfc8032():
    text = rfc_text.lines(SOURCES + "/rfc8032.txt")
    rows = []
    for start, end, variant, size in [("7.1.  Test Vectors for Ed25519", "7.2.", "ed25519", 32),
                                      ("7.2.  Test Vectors for Ed25519ctx", "7.3.", "ed25519ctx", 32),
                                      ("7.3.  Test Vectors for Ed25519ph", "7.4.", "ed25519ph", 32),
                                      ("7.4.  Test Vectors for Ed448", "7.5.", "ed448", 57),
                                      ("7.5.  Test Vectors for Ed448ph", "8.  ", "ed448ph", 57)]:
        part = rfc_text.section(text, start, end)
        starts = [i for i, line in enumerate(part) if line.strip().startswith("-----") and line.strip() != "-----"]
        for k, first in enumerate(starts):
            block = part[first:(starts[k + 1] if k + 1 < len(starts) else len(part))]
            message_label = next(line.strip() for line in block if line.strip().startswith("MESSAGE"))
            context = rfc_text.hex_after(block, "CONTEXT:") if any(
                line.strip() == "CONTEXT:" for line in block) else ""
            row = [variant, rfc_text.hex_after(block, "SECRET KEY:"), rfc_text.hex_after(block, "PUBLIC KEY:"),
                   rfc_text.hex_after(block, message_label), context, rfc_text.hex_after(block, "SIGNATURE:")]
            length = int(message_label.split("length ")[1].split(" ")[0])
            assert len(row[3]) == 2 * length, (variant, k)
            assert len(row[1]) == 2 * size and len(row[2]) == 2 * size and len(row[5]) == 4 * size, (variant, k)
            rows.append(row)
    return rows


def x_wycheproof(name, size):
    data = json.load(open(WYCHEPROOF + f"{name}_test.json"))
    rows = []
    special = {"ZeroSharedSecret", "LowOrderPublic", "NonCanonicalPublic", "SmallPublicKey"}
    count = 0
    for group in data["testGroups"]:
        for t in group["tests"]:
            keep = t["result"] == "invalid" or special & set(t["flags"])
            if not keep:
                count += 1
                keep = count % 8 == 0
            if not keep:
                continue
            # An invalid case has a public key of the wrong length: the library rejects it at parsing.
            if t["result"] == "invalid":
                assert len(t["public"]) != 2 * size, t
            rows.append([str(t["tcId"]), t["private"], t["public"], t["shared"], t["result"]])
    return rows


def nacl_verdict(public, message, signature):
    try:
        nacl.signing.VerifyKey(public).verify(message, signature)
        return True
    except (nacl.exceptions.BadSignatureError, ValueError, nacl.exceptions.CryptoError):
        return False


def cryptography_verdict(cls, public, message, signature):
    try:
        cls.from_public_bytes(public).verify(signature, message)
        return True
    except (InvalidSignature, ValueError):
        return False


def ed_wycheproof(name):
    data = json.load(open(WYCHEPROOF + f"{name}_test.json"))
    rows = []
    for group in data["testGroups"]:
        public = bytes.fromhex(group["publicKey"]["pk"])
        for t in group["tests"]:
            message, signature = bytes.fromhex(t["msg"]), bytes.fromhex(t["sig"])
            if name == "ed25519":
                verdict = nacl_verdict(public, message, signature)
            else:
                verdict = cryptography_verdict(ed448.Ed448PublicKey, public, message, signature)
            if t["result"] != "acceptable":
                assert verdict == (t["result"] == "valid"), (name, t["tcId"])
            rows.append([str(t["tcId"]), public.hex(), t["msg"], t["sig"], "1" if verdict else "0", t["result"]])
    return rows


def extra_positives():
    rows = []
    for length in [0, 1, 63, 64, 65, 200]:
        seed = pattern(32, 13, length)
        key = nacl.signing.SigningKey(seed)
        message = pattern(length, 3, 1)
        rows.append(["ed25519", seed.hex(), bytes(key.verify_key).hex(), message.hex(), "",
                     key.sign(message).signature.hex()])
    for length in [0, 1, 113, 114, 200]:
        seed = pattern(57, 17, length)
        key = ed448.Ed448PrivateKey.from_private_bytes(seed)
        message = pattern(length, 5, 2)
        from cryptography.hazmat.primitives import serialization
        public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        rows.append(["ed448", seed.hex(), public.hex(), message.hex(), "", key.sign(message).hex()])
    return rows


def main():
    function_rows, iteration_rows, dh_rows = rfc7748()
    print(write_module(
        "crypto-asymmetric", "ecx-vectors.nra", "EcxVectors",
        ["python3 scripts/crypto-vectors/gen-asymmetric-ecx.py: RFC 7748 sections 5.2 and 6, RFC 8032 section 7,",
         "Wycheproof x25519/x448/ed25519/ed448 (subsets, see the script), PyNaCl " + nacl.__version__
         + " and cryptography " + cryptography.__version__ + "."],
        [("x_function", ["function", "scalar", "u", "output"], function_rows),
         ("x_iterations", ["function", "iterations", "k after the iterations"], iteration_rows),
         ("x_diffie_hellman", ["function", "Alice's private", "Alice's public", "Bob's private", "Bob's public",
                               "shared"], dh_rows),
         ("x25519_wycheproof", ["tcId", "private", "public", "shared", "result"], x_wycheproof("x25519", 32)),
         ("x448_wycheproof", ["tcId", "private", "public", "shared", "result"], x_wycheproof("x448", 56)),
         ("eddsa", ["variant", "secret", "public", "message", "context", "signature"], rfc8032() + extra_positives()),
         ("ed25519_wycheproof", ["tcId", "public", "message", "signature", "expected verdict", "result"],
          ed_wycheproof("ed25519")),
         ("ed448_wycheproof", ["tcId", "public", "message", "signature", "expected verdict", "result"],
          ed_wycheproof("ed448"))]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
