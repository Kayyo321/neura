#!/usr/bin/env python3
"""Known answers for HPKE (RFC 9180) in Std.Crypto.Asymmetric.

Sources (downloads recorded in /home/dev/neura-gate/030/crypto2/sources/asymmetric/SOURCES.txt):
- The official test-vectors.json of the cfrg/draft-irtf-cfrg-hpke repository
  (commit b1f7cb0cdeab6906c61b3d6574e8bdfdbe1cd3fb). It covers the KEMs
  0x0010 (P-256), 0x0012 (P-521), 0x0020 (X25519) and 0x0021 (X448), the KDFs
  0x0001 and 0x0003, every AEAD and every mode: 128 vectors, all kept (rows
  marked "rfc9180"), with the encryptions of sequence numbers 0, 1 and 256 and
  the three exports.
- The file has no vector for KEM 0x0011 (P-384) or KDF 0x0002 (HKDF-SHA384).
  Those rows (marked "reference") come from the independent Python reference
  of RFC 9180 below, over Python's hmac/hashlib and the X25519, X448, ECDH and
  AEADs of `cryptography`. The reference first reproduces every value of all
  128 official vectors (shared secret, key, base nonce, exporter secret, every
  ciphertext and export), so the new rows are computed by code checked against
  the RFC's vectors.
- Wycheproof hpke_x25519_decrypt_test.json (commit in SOURCES.txt): its
  invalid cases (altered ciphertexts and enc values) for decryption failures.

    python3 scripts/crypto-vectors/gen-asymmetric-hpke.py
"""

import hashlib
import hmac
import json
import sys

import cryptography
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, x448, x25519
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305

import rfc_text
from nra_vectors import pattern, write_module

SOURCES = rfc_text.SOURCES
KEMS = {16: ("P256", ec.SECP256R1(), 32, 65, 32, "sha256"), 17: ("P384", ec.SECP384R1(), 48, 97, 48, "sha384"),
        18: ("P521", ec.SECP521R1(), 64, 133, 66, "sha512"), 32: ("X25519", None, 32, 32, 32, "sha256"),
        33: ("X448", None, 64, 56, 56, "sha512")}
KDFS = {1: "sha256", 2: "sha384", 3: "sha512"}
ORDERS = {16: ec.SECP256R1, 17: ec.SECP384R1, 18: ec.SECP521R1}


def extract(name, salt, ikm):
    if not salt:
        salt = bytes(hashlib.new(name).digest_size)
    return hmac.new(salt, ikm, name).digest()


def expand(name, prk, info, length):
    out, block, counter = b"", b"", 1
    while len(out) < length:
        block = hmac.new(prk, block + info + bytes([counter]), name).digest()
        out += block
        counter += 1
    return out[:length]


def labeled_extract(name, suite, salt, label, ikm):
    return extract(name, salt, b"HPKE-v1" + suite + label + ikm)


def labeled_expand(name, suite, prk, label, info, length):
    return expand(name, prk, length.to_bytes(2, "big") + b"HPKE-v1" + suite + label + info, length)


class Kem:
    def __init__(self, kem_id):
        self.id = kem_id
        self.label, self.curve, self.n_secret, self.n_pk, self.n_sk, self.hash = KEMS[kem_id]
        self.suite = b"KEM" + kem_id.to_bytes(2, "big")

    def public(self, sk):
        if self.id == 32:
            key = x25519.X25519PrivateKey.from_private_bytes(sk).public_key()
            return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        if self.id == 33:
            key = x448.X448PrivateKey.from_private_bytes(sk).public_key()
            return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        key = ec.derive_private_key(int.from_bytes(sk, "big"), self.curve).public_key()
        return key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)

    def derive(self, ikm):
        prk = labeled_extract(self.hash, self.suite, b"", b"dkp_prk", ikm)
        if self.id in (32, 33):
            return labeled_expand(self.hash, self.suite, prk, b"sk", b"", self.n_sk)
        order = {16: int("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551", 16),
                 17: int("ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973", 16),
                 18: int("01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffa51868783bf2f966b7fcc0148f709a5d03bb5c9b8899c47aebb6fb71e91386409", 16)}[self.id]
        for counter in range(256):
            candidate = bytearray(labeled_expand(self.hash, self.suite, prk, b"candidate", bytes([counter]), self.n_sk))
            candidate[0] &= 0x01 if self.id == 18 else 0xFF
            value = int.from_bytes(candidate, "big")
            if 0 < value < order:
                return bytes(candidate)
        raise RuntimeError("DeriveKeyPairError")

    def dh(self, sk, pk):
        if self.id == 32:
            return x25519.X25519PrivateKey.from_private_bytes(sk).exchange(x25519.X25519PublicKey.from_public_bytes(pk))
        if self.id == 33:
            return x448.X448PrivateKey.from_private_bytes(sk).exchange(x448.X448PublicKey.from_public_bytes(pk))
        private = ec.derive_private_key(int.from_bytes(sk, "big"), self.curve)
        return private.exchange(ec.ECDH(), ec.EllipticCurvePublicKey.from_encoded_point(self.curve, pk))

    def shared(self, dh, context):
        prk = labeled_extract(self.hash, self.suite, b"", b"eae_prk", dh)
        return labeled_expand(self.hash, self.suite, prk, b"shared_secret", context, self.n_secret)


def schedule(kem_id, kdf_id, aead_id, mode, shared, info, psk, psk_id):
    name = KDFS[kdf_id]
    suite = b"HPKE" + kem_id.to_bytes(2, "big") + kdf_id.to_bytes(2, "big") + aead_id.to_bytes(2, "big")
    context = bytes([mode]) + labeled_extract(name, suite, b"", b"psk_id_hash", psk_id) + labeled_extract(
        name, suite, b"", b"info_hash", info)
    secret = labeled_extract(name, suite, shared, b"secret", psk)
    nk = {1: 16, 2: 32, 3: 32, 65535: 0}[aead_id]
    key = labeled_expand(name, suite, secret, b"key", context, nk) if nk else b""
    nonce = labeled_expand(name, suite, secret, b"base_nonce", context, 12) if nk else b""
    exporter = labeled_expand(name, suite, secret, b"exp", context, hashlib.new(name).digest_size)
    return suite, key, nonce, exporter


def seal(aead_id, key, nonce, seq, aad, pt):
    n = bytes(a ^ b for a, b in zip(nonce, seq.to_bytes(12, "big")))
    cipher = ChaCha20Poly1305(key) if aead_id == 3 else AESGCM(key)
    return cipher.encrypt(n, pt, aad)


def run(kem_id, kdf_id, aead_id, mode, info, ikm_e, ikm_r, ikm_s, psk, psk_id, encryptions, exports):
    kem = Kem(kem_id)
    sk_e, sk_r = kem.derive(ikm_e), kem.derive(ikm_r)
    pk_e, pk_r = kem.public(sk_e), kem.public(sk_r)
    sk_s = kem.derive(ikm_s) if mode in (2, 3) else b""
    pk_s = kem.public(sk_s) if sk_s else b""
    if mode in (2, 3):
        dh = kem.dh(sk_e, pk_r) + kem.dh(sk_s, pk_r)
        context = pk_e + pk_r + pk_s
    else:
        dh = kem.dh(sk_e, pk_r)
        context = pk_e + pk_r
    shared = kem.shared(dh, context)
    suite, key, nonce, exporter = schedule(kem_id, kdf_id, aead_id, mode, shared, info, psk, psk_id)
    out = dict(skEm=sk_e, skRm=sk_r, skSm=sk_s, pkEm=pk_e, pkRm=pk_r, pkSm=pk_s, enc=pk_e, shared_secret=shared,
               key=key, base_nonce=nonce, exporter_secret=exporter, cts={}, exported=[])
    for seq, aad, pt in encryptions:
        out["cts"][seq] = seal(aead_id, key, nonce, seq, aad, pt)
    for ctx, length in exports:
        out["exported"].append(labeled_expand(KDFS[kdf_id], suite, exporter, b"sec", ctx, length))
    return out


COLUMNS = ["source", "mode", "kem", "kdf", "aead", "info", "ikmE", "ikmR", "ikmS", "skRm", "skSm", "skEm", "pkRm",
           "pkSm", "pkEm", "psk", "psk_id", "enc", "shared_secret", "pt", "aad0", "ct0", "aad1", "ct1", "aad256",
           "ct256", "ctx0", "export0", "ctx1", "export1", "ctx2", "export2"]


def row(source, v, result, encryptions, exports):
    by_seq = {seq: (aad, pt) for seq, aad, pt in encryptions}
    cells = [source, str(v["mode"]), str(v["kem_id"]), str(v["kdf_id"]), str(v["aead_id"]), v["info"], v["ikmE"],
             v["ikmR"], v.get("ikmS", ""), result["skRm"].hex(), result["skSm"].hex(), result["skEm"].hex(),
             result["pkRm"].hex(), result["pkSm"].hex(), result["pkEm"].hex(), v.get("psk", ""), v.get("psk_id", ""),
             result["enc"].hex(), result["shared_secret"].hex()]
    if encryptions:
        cells += [by_seq[0][1].hex(), by_seq[0][0].hex(), result["cts"][0].hex(), by_seq[1][0].hex(),
                  result["cts"][1].hex(), by_seq[256][0].hex(), result["cts"][256].hex()]
    else:
        cells += ["", "", "", "", "", "", ""]
    for (ctx, length), value in zip(exports, result["exported"]):
        assert length == 32
        cells += [ctx.hex(), value.hex()]
    return cells


def official():
    vectors = json.load(open(SOURCES + "/hpke-test-vectors.json"))
    rows = []
    for v in vectors:
        unhex = lambda name: bytes.fromhex(v.get(name, ""))
        encryptions = [(seq, bytes.fromhex(e["aad"]), bytes.fromhex(e["pt"])) for seq, e in enumerate(v["encryptions"])]
        exports = [(bytes.fromhex(e["exporter_context"]), e["L"]) for e in v["exports"]]
        result = run(v["kem_id"], v["kdf_id"], v["aead_id"], v["mode"], unhex("info"), unhex("ikmE"), unhex("ikmR"),
                     unhex("ikmS"), unhex("psk"), unhex("psk_id"), encryptions, exports)
        for name in ["skEm", "skRm", "pkEm", "pkRm", "enc", "shared_secret", "key", "base_nonce", "exporter_secret"]:
            assert result[name].hex() == v[name], (name, v["mode"], v["kem_id"])
        if v["mode"] in (2, 3):
            assert result["skSm"].hex() == v["skSm"] and result["pkSm"].hex() == v["pkSm"]
        for seq, e in enumerate(v["encryptions"]):
            assert result["cts"][seq].hex() == e["ct"]
        for e, value in zip(v["exports"], result["exported"]):
            assert value.hex() == e["exported_value"]
        assert len(exports) == 3
        if encryptions:
            assert len(encryptions) == 257
        rows.append(row("rfc9180", v, result, [x for x in encryptions if x[0] in (0, 1, 256)], exports))
    return rows, vectors


def reference(vectors):
    rows = []
    template = vectors[0]
    exports = [(bytes.fromhex(e["exporter_context"]), e["L"]) for e in template["exports"]]
    pt = bytes.fromhex(template["encryptions"][0]["pt"])
    encryptions = [(seq, bytes.fromhex("436f756e742d") + bytes([0x30 + seq % 10]), pt) for seq in (0, 1, 256)]
    combos = [(17, 2, 1), (17, 2, 2), (17, 2, 3), (17, 2, 65535), (17, 1, 1), (17, 3, 3), (32, 2, 1), (16, 2, 3),
              (33, 2, 2), (18, 2, 65535)]
    for index, (kem_id, kdf_id, aead_id) in enumerate(combos):
        for mode in range(4):
            v = {"mode": mode, "kem_id": kem_id, "kdf_id": kdf_id, "aead_id": aead_id,
                 "info": template["info"], "ikmE": pattern(KEMS[kem_id][4], 13, 1 + index).hex(),
                 "ikmR": pattern(KEMS[kem_id][4], 17, 2 + index).hex()}
            if mode in (2, 3):
                v["ikmS"] = pattern(KEMS[kem_id][4], 19, 3 + index).hex()
            if mode in (1, 3):
                v["psk"] = template_psk(vectors)
                v["psk_id"] = template_psk_id(vectors)
            unhex = lambda name: bytes.fromhex(v.get(name, ""))
            chosen = encryptions if aead_id != 65535 else []
            result = run(kem_id, kdf_id, aead_id, mode, unhex("info"), unhex("ikmE"), unhex("ikmR"), unhex("ikmS"),
                         unhex("psk"), unhex("psk_id"), chosen, exports)
            rows.append(row("reference", v, result, chosen, exports))
    return rows


def template_psk(vectors):
    return next(v["psk"] for v in vectors if v["mode"] == 1)


def template_psk_id(vectors):
    return next(v["psk_id"] for v in vectors if v["mode"] == 1)


def reference_open(kdf_id, aead_id, sk_r, enc, info, aad, ct):
    """Base-mode Open with the reference, or None on any failure."""
    kem = Kem(32)
    try:
        dh = kem.dh(sk_r, enc)
    except ValueError:
        return None
    shared = kem.shared(dh, enc + kem.public(sk_r))
    _, key, nonce, _ = schedule(32, kdf_id, aead_id, 0, shared, info, b"", b"")
    cipher = ChaCha20Poly1305(key) if aead_id == 3 else AESGCM(key)
    try:
        return cipher.decrypt(nonce, ct, aad)
    except Exception:
        return None


def wycheproof():
    data = json.load(open(SOURCES + "/wycheproof/hpke_x25519_decrypt_test.json"))
    rows = []
    for group in data["testGroups"]:
        for t in group["tests"]:
            unhex = lambda name: bytes.fromhex(t[name])
            if len(unhex("enc")) != 32:
                plaintext = None
            else:
                plaintext = reference_open(group["kdfId"], group["aeadId"], unhex("skRm"), unhex("enc"), unhex("info"),
                                           unhex("aad"), unhex("ct"))
            if t["result"] != "acceptable":
                assert (plaintext is not None) == (t["result"] == "valid"), t["tcId"]
            if plaintext is not None:
                assert plaintext.hex() == t["pt"]
            rows.append([str(t["tcId"]), str(group["kdfId"]), str(group["aeadId"]), t["skRm"], t["enc"], t["info"],
                         t["aad"], t["ct"], "1" if plaintext is not None else "0", t["pt"] if plaintext is not None else "",
                         t["result"]])
    return rows


def main():
    rows, vectors = official()
    rows += reference(vectors)
    print(write_module(
        "crypto-asymmetric", "hpke-vectors.nra", "HpkeVectors",
        ["python3 scripts/crypto-vectors/gen-asymmetric-hpke.py: RFC 9180 test-vectors.json (cfrg/draft-irtf-cfrg-hpke",
         "b1f7cb0c), and for P-384 and HKDF-SHA384 an independent reference (checked against all 128 official",
         "vectors) over hashlib and cryptography " + cryptography.__version__ + ". Encryptions: sequence 0, 1 and 256."],
        [("vectors", COLUMNS, rows),
         ("wycheproof_x25519", ["tcId", "kdf", "aead", "skRm", "enc", "info", "aad", "ct", "opens", "pt", "result"],
          wycheproof())]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
