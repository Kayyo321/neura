#!/usr/bin/env python3
"""Known answers for ECDSA and ECDH over P-256, P-384, P-521 and secp256k1 (Std.Crypto.Asymmetric).

Sources (downloads recorded in /home/dev/neura-gate/030/crypto2/sources/asymmetric/SOURCES.txt):
- RFC 6979 appendix A.2.5 (P-256), A.2.6 (P-384) and A.2.7 (P-521): the key
  pairs and the deterministic signatures (k, r, s) over "sample" and "test" for
  SHA-224, SHA-256, SHA-384 and SHA-512 (the SHA-1 rows are left out:
  Std.Crypto has no SHA-1), read from the RFC text.
- secp256k1 and extra P-curve signatures: `openssl pkeyutl -sign -pkeyopt
  nonce-type:1` (OpenSSL 3.5.7's RFC 6979 deterministic ECDSA) over
  nra_vectors.pattern keys and messages, with each signature checked by
  `cryptography`.
- Wycheproof (C2SP/wycheproof testvectors_v1 at the commit in SOURCES.txt):
  ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384, ecdsa_secp521r1_sha512 and
  ecdsa_secp256k1_sha256 (DER), ecdsa_secp256r1_sha256_p1363 (r || s) and
  ecdsa_secp256k1_sha256_bitcoin (DER with the low-S rule). Every invalid case
  is kept; valid cases are thinned to every 4th (8th for P-521) to bound the
  test time. ecdh_secp256r1/384r1/521r1_ecpoint (SEC1 points): every invalid
  and acceptable case and every 8th valid one (16th for P-521).
- secp256k1 ECDH: `cryptography` over pattern keys.

    python3 scripts/crypto-vectors/gen-asymmetric-ecdsa.py
"""

import json
import os
import re
import subprocess
import sys
import tempfile

import cryptography
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

import rfc_text
from nra_vectors import pattern, write_module

SOURCES = rfc_text.SOURCES
WYCHEPROOF = SOURCES + "/wycheproof/"
CURVES = {"P256": ec.SECP256R1(), "P384": ec.SECP384R1(), "P521": ec.SECP521R1(), "Secp256k1": ec.SECP256K1()}
SIZES = {"P256": 32, "P384": 48, "P521": 66, "Secp256k1": 32}
HASHES = {"SHA-224": "Sha224", "SHA-256": "Sha256", "SHA-384": "Sha384", "SHA-512": "Sha512"}


def rfc6979_values(block):
    """name -> uppercase hex for `name = HEX` lines with continuation lines."""
    values, current = [], None
    for line in block:
        match = re.match(r"^\s*(x|Ux|Uy|k|r|s) = ([0-9A-F]+)\s*$", line)
        if match:
            current = [match.group(1), match.group(2)]
            values.append(current)
        elif current and re.fullmatch(r"\s+[0-9A-F]+\s*", line):
            current[1] += line.strip()
        else:
            current = None
    return values


def rfc6979():
    text = rfc_text.lines(SOURCES + "/rfc6979.txt")
    rows = []
    for start, end, curve in [("A.2.5.  ECDSA, 256 Bits", "A.2.6.", "P256"),
                              ("A.2.6.  ECDSA, 384 Bits", "A.2.7.", "P384"),
                              ("A.2.7.  ECDSA, 521 Bits", "A.2.8.", "P521")]:
        part = rfc_text.section(text, start, end)
        size = SIZES[curve]
        values = rfc6979_values(part)
        named = dict((k, v) for k, v in values[:3])
        x = int(named["x"], 16)
        public = ec.derive_private_key(x, CURVES[curve]).public_key().public_numbers()
        assert (public.x, public.y) == (int(named["Ux"], 16), int(named["Uy"], 16))
        headers = [line.strip() for line in part if line.strip().startswith("With SHA")]
        triples = values[3:]
        assert len(triples) == 3 * len(headers)
        for index, header in enumerate(headers):
            match = re.match(r'With (SHA-[0-9]+), message = "([a-z]+)":', header)
            digest, message = match.group(1), match.group(2)
            k, r, s = (int(triples[3 * index + j][1], 16) for j in range(3))
            if digest not in HASHES:
                continue
            rows.append([curve, HASHES[digest], f"{x:0{2 * size}x}", message.encode().hex(),
                         f"{r:0{2 * size}x}", f"{s:0{2 * size}x}"])
    return rows


def openssl_sign(curve, d, message, digest):
    key = ec.derive_private_key(d, CURVES[curve])
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    with tempfile.TemporaryDirectory() as directory:
        key_path = os.path.join(directory, "key.pem")
        data_path = os.path.join(directory, "data.bin")
        open(key_path, "wb").write(pem)
        open(data_path, "wb").write(message)
        der = subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", key_path, "-in", data_path,
                              "-digest", digest.lower().replace("-", ""), "-pkeyopt", "nonce-type:1"],
                             check=True, capture_output=True).stdout
    algorithm = {"SHA-256": hashes.SHA256(), "SHA-384": hashes.SHA384(), "SHA-512": hashes.SHA512()}[digest]
    key.public_key().verify(der, message, ec.ECDSA(algorithm))
    return decode_dss_signature(der)


def openssl_rows():
    rows = []
    for curve, digest in [("Secp256k1", "SHA-256"), ("Secp256k1", "SHA-512"), ("P256", "SHA-256"),
                          ("P384", "SHA-384"), ("P521", "SHA-512")]:
        size = SIZES[curve]
        for length in [0, 3, 100]:
            d = int.from_bytes(pattern(size, 29, length + size), "big") % (2 ** (8 * size - 9)) + 1
            message = pattern(length, 7, 3)
            r, s = openssl_sign(curve, d, message, digest)
            rows.append([curve, HASHES[digest], f"{d:0{2 * size}x}", message.hex(),
                         f"{r:0{2 * size}x}", f"{s:0{2 * size}x}"])
    return rows


PUBLIC_KEYS = []


def key_index(curve, sec1):
    """The row of (curve, SEC1 point) in the public_keys table, added when new."""
    if [curve, sec1] not in PUBLIC_KEYS:
        PUBLIC_KEYS.append([curve, sec1])
    return str(PUBLIC_KEYS.index([curve, sec1]))


def ecdsa_wycheproof(file_name, curve, hash_name, form, keep_every):
    data = json.load(open(WYCHEPROOF + file_name))
    rows, count = [], 0
    for group in data["testGroups"]:
        public = group["publicKey"]["uncompressed"]
        for t in group["tests"]:
            assert t["result"] in ("valid", "invalid"), t
            if t["result"] == "valid":
                count += 1
                if count % keep_every != 0:
                    continue
            rows.append([file_name.split("_test")[0], str(t["tcId"]), hash_name, form, key_index(curve, public),
                         t["msg"], t["sig"], "1" if t["result"] == "valid" else "0"])
    return rows


def ecdh_wycheproof(file_name, curve, keep_every):
    data = json.load(open(WYCHEPROOF + file_name))
    rows, count = [], 0
    size = SIZES[curve]
    for group in data["testGroups"]:
        for t in group["tests"]:
            if t["result"] == "valid":
                count += 1
                if count % keep_every != 0:
                    continue
            private = int(t["private"], 16)
            # Private keys are given as minimal signed integers; the library takes exactly `size` bytes.
            assert 0 < private
            expected = t["shared"] if t["result"] != "invalid" else ""
            rows.append([str(t["tcId"]), curve, f"{private:0{2 * size}x}", t["public"], expected, t["result"]])
    return rows


def secp256k1_ecdh():
    rows = []
    for length in [1, 2, 3]:
        a = ec.derive_private_key(int.from_bytes(pattern(32, 41, length), "big") % (2 ** 255) + 1, ec.SECP256K1())
        b = ec.derive_private_key(int.from_bytes(pattern(32, 43, length + 7), "big") % (2 ** 255) + 1, ec.SECP256K1())
        public = b.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        shared = a.exchange(ec.ECDH(), b.public_key())
        rows.append([str(length), "Secp256k1", f"{a.private_numbers().private_value:064x}", public.hex(),
                     shared.hex(), "valid"])
    # A point off the curve: y + 1.
    bad = bytearray(bytes.fromhex(rows[0][3]))
    bad[-1] ^= 1
    rows.append(["4", "Secp256k1", rows[0][2], bytes(bad).hex(), "", "invalid"])
    return rows


def main():
    signatures = (ecdsa_wycheproof("ecdsa_secp256r1_sha256_test.json", "P256", "Sha256", "der", 4)
                  + ecdsa_wycheproof("ecdsa_secp384r1_sha384_test.json", "P384", "Sha384", "der", 4)
                  + ecdsa_wycheproof("ecdsa_secp521r1_sha512_test.json", "P521", "Sha512", "der", 8)
                  + ecdsa_wycheproof("ecdsa_secp256k1_sha256_test.json", "Secp256k1", "Sha256", "der", 4)
                  + ecdsa_wycheproof("ecdsa_secp256r1_sha256_p1363_test.json", "P256", "Sha256", "raw", 4)
                  + ecdsa_wycheproof("ecdsa_secp256k1_sha256_bitcoin_test.json", "Secp256k1", "Sha256", "low-s", 4))
    print(write_module(
        "crypto-asymmetric", "ecdsa-vectors.nra", "EcdsaVectors",
        ["python3 scripts/crypto-vectors/gen-asymmetric-ecdsa.py: RFC 6979 A.2.5-A.2.7, openssl 3.5 deterministic",
         "ECDSA (nonce-type:1), Wycheproof ecdsa/ecdh subsets, cryptography " + cryptography.__version__ + "."],
        [("deterministic", ["curve", "hash", "private d", "message", "r", "s"], rfc6979() + openssl_rows()),
         ("wycheproof", ["file", "tcId", "hash", "form", "public key row", "message", "signature", "valid"],
          signatures),
         ("public_keys", ["curve", "public (SEC1)"], PUBLIC_KEYS),
         ("ecdh", ["tcId", "curve", "private", "public (SEC1)", "shared (empty when invalid)", "result"],
          ecdh_wycheproof("ecdh_secp256r1_ecpoint_test.json", "P256", 8)
          + ecdh_wycheproof("ecdh_secp384r1_ecpoint_test.json", "P384", 8)
          + ecdh_wycheproof("ecdh_secp521r1_ecpoint_test.json", "P521", 16)
          + secp256k1_ecdh())]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
