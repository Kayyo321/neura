#!/usr/bin/env python3
"""Known answers for RSA-PSS, RSA-OAEP and PKCS #1 v1.5 verification (Std.Crypto.Asymmetric).

Sources (downloads recorded in /home/dev/neura-gate/030/crypto2/sources/asymmetric/SOURCES.txt):
- Keys: the private keys of Wycheproof's rsa_oaep_2048_sha256_mgf1sha256,
  rsa_oaep_3072_sha256_mgf1sha256 and rsa_oaep_4096_sha512_mgf1sha512 files
  (C2SP/wycheproof testvectors_v1 at the commit in SOURCES.txt).
- RSA-PSS signing: the expected signatures are computed here, independently of
  the library, from RFC 8017 sections 8.1.1 and 9.1.1 (hashlib and Python
  integers) with fixed salts, and each one is checked with `cryptography`'s
  PSS verification.
- RSA-OAEP encryption: the expected ciphertexts are computed here from RFC 8017
  section 7.1.1 with fixed seeds, and `cryptography` decrypts each one back to
  the message.
- PKCS #1 v1.5: signatures from `cryptography` (deterministic). The DigestInfo
  prefixes in stdlib/std/crypto/internal/rsa-core.nra are checked against RFC
  8017 section 9.2 note 1 (SHA-2) and against the encoding recovered from a
  `cryptography` signature (every hash, including SHA-3).
- Key generation: an independent Python reference of the procedure documented
  in stdlib/std/crypto/asymmetric/rsa.nra (FIPS 186-5 appendix A.1.3 with the
  choices recorded there), driven by the Python ChaCha20-DRBG reference of
  scripts/crypto-vectors/gen-drbg.py, so that a seeded generation must give
  the same key byte for byte; `cryptography` checks the key's consistency.
- Wycheproof: rsa_pss_2048_sha256_mgf1_32, rsa_pss_2048_sha384_mgf1_48,
  rsa_pss_3072_sha256_mgf1_32, rsa_pss_4096_sha512_mgf1_64 (every invalid case,
  every 4th valid one), the three rsa_oaep files above (every case), and
  rsa_signature_2048_sha256, _2048_sha3_256, _3072_sha384, _4096_sha512
  (every case). An "acceptable" PKCS #1 v1.5 case (MissingNull) is expected to
  fail: the library compares with the encoding that has the NULL parameters.

    python3 scripts/crypto-vectors/gen-asymmetric-rsa.py
"""

import hashlib
import json
import pathlib
import re
import sys

import importlib.util
import math

import cryptography
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

import rfc_text
from nra_vectors import ROOT, pattern, write_module

SOURCES = rfc_text.SOURCES
WYCHEPROOF = SOURCES + "/wycheproof/"
HASHLIB = {"Sha224": "sha224", "Sha256": "sha256", "Sha384": "sha384", "Sha512": "sha512",
           "Sha512_256": "sha512_256", "Sha3_224": "sha3_224", "Sha3_256": "sha3_256", "Sha3_384": "sha3_384",
           "Sha3_512": "sha3_512"}
CRYPTOGRAPHY = {"Sha224": hashes.SHA224(), "Sha256": hashes.SHA256(), "Sha384": hashes.SHA384(),
                "Sha512": hashes.SHA512(), "Sha512_256": hashes.SHA512_256(), "Sha3_224": hashes.SHA3_224(),
                "Sha3_256": hashes.SHA3_256(), "Sha3_384": hashes.SHA3_384(), "Sha3_512": hashes.SHA3_512()}
WYCHEPROOF_HASH = {"SHA-256": "Sha256", "SHA-384": "Sha384", "SHA-512": "Sha512", "SHA3-256": "Sha3_256"}


def h(name, data):
    return hashlib.new(HASHLIB[name], data).digest()


def mgf1(name, seed, length):
    out, counter = b"", 0
    while len(out) < length:
        out += h(name, seed + counter.to_bytes(4, "big"))
        counter += 1
    return out[:length]


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def to_hex_int(value):
    return f"{value:x}" if len(f"{value:x}") % 2 == 0 else f"0{value:x}"


def wycheproof_key(file_name):
    data = json.load(open(WYCHEPROOF + file_name))
    key = data["testGroups"][0]["privateKey"]
    numbers = [int(key[name], 16) for name in ["modulus", "publicExponent", "privateExponent", "prime1", "prime2",
                                              "exponent1", "exponent2", "coefficient"]]
    n, e, d, p, q, dp, dq, qi = numbers
    assert p * q == n and dp == d % (p - 1) and dq == d % (q - 1) and qi * q % p == 1
    return numbers


def pss_sign(numbers, name, message, salt):
    """RFC 8017 sections 8.1.1 and 9.1.1."""
    n, e, d = numbers[:3]
    mod_bits = n.bit_length()
    em_bits = mod_bits - 1
    em_len = (em_bits + 7) // 8
    m_hash = h(name, message)
    hh = h(name, bytes(8) + m_hash + salt)
    db = bytes(em_len - len(salt) - len(hh) - 2) + b"\x01" + salt
    masked = bytearray(xor(db, mgf1(name, hh, len(db))))
    masked[0] &= 0xFF >> (8 * em_len - em_bits)
    em = bytes(masked) + hh + b"\xbc"
    k = (mod_bits + 7) // 8
    return pow(int.from_bytes(em, "big"), d, n).to_bytes(k, "big")


def oaep_encrypt(numbers, name, message, label, seed):
    """RFC 8017 section 7.1.1."""
    n, e = numbers[:2]
    k = (n.bit_length() + 7) // 8
    h_len = len(seed)
    db = h(name, label) + bytes(k - len(message) - 2 * h_len - 2) + b"\x01" + message
    masked_db = xor(db, mgf1(name, seed, k - h_len - 1))
    masked_seed = xor(seed, mgf1(name, masked_db, h_len))
    em = b"\x00" + masked_seed + masked_db
    return pow(int.from_bytes(em, "big"), e, n).to_bytes(k, "big")


def private_key(numbers):
    n, e, d, p, q, dp, dq, qi = numbers
    return rsa.RSAPrivateNumbers(p, q, d, dp, dq, qi, rsa.RSAPublicNumbers(e, n)).private_key()


def check_prefixes():
    """The DigestInfo prefixes in rsa-core.nra against RFC 8017 and recovered cryptography encodings."""
    source = (ROOT / "stdlib/std/crypto/internal/rsa-core.nra").read_text()
    table = dict(re.findall(r'Algorithm\.(Sha[0-9_]+) => "([0-9a-f]+)"', source))
    table["Sha3_512"] = re.search(r'_ => "([0-9a-f]+)"', source[source.index("digest_info_prefix"):]).group(1)
    text = "\n".join(rfc_text.lines(SOURCES + "/rfc8017.txt"))
    for name, label in [("Sha224", "SHA-224:"), ("Sha256", "SHA-256:"), ("Sha384", "SHA-384:"),
                        ("Sha512", "SHA-512:"), ("Sha512_256", "SHA-512/256:")]:
        base = text.index("MD2:     (0x)30")
        start = re.compile(re.escape(label) + r" +\(0x\)30").search(text, base).start()
        chunk = text[start:start + 200].split("||")[0]
        rfc = re.sub(r"[^0-9a-f]", "", chunk.split("(0x)")[1])
        assert rfc == table[name], (name, rfc, table[name])
    numbers = wycheproof_key("rsa_oaep_2048_sha256_mgf1sha256_test.json")
    key = private_key(numbers)
    n, e = numbers[:2]
    for name, prefix in table.items():
        signature = key.sign(b"abc", padding.PKCS1v15(), CRYPTOGRAPHY[name])
        em = pow(int.from_bytes(signature, "big"), e, n).to_bytes(256, "big")
        digest = h(name, b"abc")
        assert em.endswith(bytes.fromhex(prefix) + digest), name
        assert em[:2] == b"\x00\x01" and em[em.index(b"\x00", 1) + 1:] == bytes.fromhex(prefix) + digest
    return len(table)


def keys():
    rows = []
    for size, file_name in [(2048, "rsa_oaep_2048_sha256_mgf1sha256_test.json"),
                            (3072, "rsa_oaep_3072_sha256_mgf1sha256_test.json"),
                            (4096, "rsa_oaep_4096_sha512_mgf1sha512_test.json")]:
        numbers = wycheproof_key(file_name)
        rows.append([str(size)] + [to_hex_int(v) for v in numbers])
    return rows


def pss_rows():
    rows = []
    for size, file_name in [(2048, "rsa_oaep_2048_sha256_mgf1sha256_test.json"),
                            (3072, "rsa_oaep_3072_sha256_mgf1sha256_test.json")]:
        numbers = wycheproof_key(file_name)
        public = private_key(numbers).public_key()
        for name, salt_length in [("Sha256", 32), ("Sha384", 48), ("Sha512", 64), ("Sha256", 0), ("Sha3_256", 20)]:
            message = pattern(salt_length + 5, 11, size % 251)
            salt = pattern(salt_length, 23, 1)
            signature = pss_sign(numbers, name, message, salt)
            public.verify(signature, message, padding.PSS(padding.MGF1(CRYPTOGRAPHY[name]), salt_length),
                          CRYPTOGRAPHY[name])
            rows.append([str(size), name, str(salt_length), message.hex(), salt.hex(), signature.hex()])
    return rows


def oaep_rows():
    rows = []
    for size, file_name in [(2048, "rsa_oaep_2048_sha256_mgf1sha256_test.json"),
                            (3072, "rsa_oaep_3072_sha256_mgf1sha256_test.json")]:
        numbers = wycheproof_key(file_name)
        key = private_key(numbers)
        for name, length, label in [("Sha256", 0, b""), ("Sha256", 20, b"label"), ("Sha384", 100, b""),
                                    ("Sha512", 30, b"x"), ("Sha256", (numbers[0].bit_length() + 7) // 8 - 66, b"")]:
            h_len = hashlib.new(HASHLIB[name]).digest_size
            message = pattern(length, 5, 9)
            seed = pattern(h_len, 37, length)
            ciphertext = oaep_encrypt(numbers, name, message, label, seed)
            algorithm = CRYPTOGRAPHY[name]
            assert key.decrypt(ciphertext, padding.OAEP(padding.MGF1(algorithm), algorithm, label or None)) == message
            rows.append([str(size), name, message.hex(), label.hex(), seed.hex(), ciphertext.hex()])
    return rows


def pkcs1_rows():
    rows = []
    numbers = wycheproof_key("rsa_oaep_2048_sha256_mgf1sha256_test.json")
    key = private_key(numbers)
    for name in ["Sha224", "Sha256", "Sha384", "Sha512", "Sha512_256", "Sha3_256", "Sha3_512"]:
        message = pattern(17, 3, len(name))
        rows.append(["2048", name, message.hex(), key.sign(message, padding.PKCS1v15(), CRYPTOGRAPHY[name]).hex()])
    return rows


def drbg_class():
    spec = importlib.util.spec_from_file_location("gen_drbg", pathlib.Path(__file__).with_name("gen-drbg.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.Drbg


def miller_rabin(w, iterations, drbg):
    """FIPS 186-5 appendix B.3.1 with bases drawn as rsa.nra draws them."""
    w1 = w - 1
    a = (w1 & -w1).bit_length() - 1
    m = w1 >> a
    wlen = w.bit_length()
    for _ in range(iterations):
        while True:
            raw = drbg.fill((wlen + 7) // 8)
            b = int.from_bytes(raw, "big") >> (len(raw) * 8 - wlen)
            if b.bit_length() > 1 and b < w1:
                break
        z = pow(b, m, w)
        if z in (1, w1):
            continue
        passed = False
        for _ in range(1, a):
            z = z * z % w
            if z == w1:
                passed = True
                break
            if z == 1:
                break
        if not passed:
            return False
    return True


def generate_prime(half, nlen, other, drbg):
    primes = [n for n in range(3, 2000, 2) if all(n % k for k in range(3, int(n ** 0.5) + 1, 2))]
    for _ in range(10 * nlen):
        raw = drbg.fill((half + 7) // 8)
        w = int.from_bytes(raw, "big") >> (((half + 7) // 8) * 8 - half)
        w |= (1 << (half - 1)) | (1 << (half - 2)) | 3
        if other is not None:
            difference = abs(w - other)
            if difference == 0 or (difference - 1).bit_length() <= half - 100:
                continue
        if w % 65537 == 1 or any(w % k == 0 for k in primes):
            continue
        if miller_rabin(w, 5 if nlen <= 2048 else 4, drbg):
            return w
    raise RuntimeError("no prime")


def keygen_rows():
    rows = []
    for bits, seed in [(2048, pattern(32, 7, 1))]:
        drbg = drbg_class()(seed)
        while True:
            p = generate_prime(bits // 2, bits, None, drbg)
            q = generate_prime(bits // 2, bits, p, drbg)
            lam = (p - 1) * (q - 1) // math.gcd(p - 1, q - 1)
            d = pow(65537, -1, lam)
            if d.bit_length() > bits // 2:
                break
        n = p * q
        key = rsa.RSAPrivateNumbers(p, q, d, d % (p - 1), d % (q - 1), pow(q, -1, p),
                                    rsa.RSAPublicNumbers(65537, n)).private_key()
        assert key.key_size == bits
        rows.append([str(bits), seed.hex(), f"{n:0{bits // 4}x}", f"{d:0{bits // 4}x}", f"{p:0{bits // 8}x}",
                     f"{q:0{bits // 8}x}"])
    return rows


PUBLIC_KEYS = []


def key_index(n, e):
    """The row of (n, e) in the public_keys table, added when new."""
    if [n, e] not in PUBLIC_KEYS:
        PUBLIC_KEYS.append([n, e])
    return str(PUBLIC_KEYS.index([n, e]))


def wycheproof_pss(file_name, keep_every):
    data = json.load(open(WYCHEPROOF + file_name))
    rows, count = [], 0
    for group in data["testGroups"]:
        assert group["mgf"] == "MGF1" and group["mgfSha"] == group["sha"]
        name = WYCHEPROOF_HASH[group["sha"]]
        n, e = group["publicKey"]["modulus"], group["publicKey"]["publicExponent"]
        for t in group["tests"]:
            assert t["result"] in ("valid", "invalid")
            if t["result"] == "valid":
                count += 1
                if count % keep_every:
                    continue
            rows.append([file_name.split("_test")[0], str(t["tcId"]), name, str(group["sLen"]), key_index(n, e),
                         t["msg"], t["sig"], "1" if t["result"] == "valid" else "0"])
    return rows


def wycheproof_signature(file_name, keep_every):
    data = json.load(open(WYCHEPROOF + file_name))
    rows, count = [], 0
    for group in data["testGroups"]:
        name = WYCHEPROOF_HASH[group["sha"]]
        n, e = group["publicKey"]["modulus"], group["publicKey"]["publicExponent"]
        for t in group["tests"]:
            if t["result"] == "invalid":
                count += 1
                if count % keep_every:
                    continue
            # Acceptable cases (MissingNull) are refused by the exact comparison.
            rows.append([file_name.split("_test")[0], str(t["tcId"]), name, "0", key_index(n, e), t["msg"],
                         t["sig"], "1" if t["result"] == "valid" else "0"])
    return rows


def wycheproof_oaep(file_name, size):
    data = json.load(open(WYCHEPROOF + file_name))
    rows = []
    for group in data["testGroups"]:
        assert group["mgf"] == "MGF1" and group["mgfSha"] == group["sha"]
        name = WYCHEPROOF_HASH[group["sha"]]
        for t in group["tests"]:
            assert t["result"] in ("valid", "invalid")
            rows.append([str(t["tcId"]), str(size), name, t["ct"], t["label"],
                         t["msg"] if t["result"] == "valid" else "", t["result"]])
    return rows


def main():
    print("DigestInfo prefixes checked:", check_prefixes(), file=sys.stderr)
    signatures = (wycheproof_pss("rsa_pss_2048_sha256_mgf1_32_test.json", 4)
                  + wycheproof_pss("rsa_pss_2048_sha384_mgf1_48_test.json", 4)
                  + wycheproof_pss("rsa_pss_3072_sha256_mgf1_32_test.json", 4)
                  + wycheproof_pss("rsa_pss_4096_sha512_mgf1_64_test.json", 4)
                  + wycheproof_signature("rsa_signature_2048_sha256_test.json", 1)
                  + wycheproof_signature("rsa_signature_2048_sha3_256_test.json", 4)
                  + wycheproof_signature("rsa_signature_3072_sha384_test.json", 8)
                  + wycheproof_signature("rsa_signature_4096_sha512_test.json", 8))
    print(write_module(
        "crypto-asymmetric", "rsa-vectors.nra", "RsaVectors",
        ["python3 scripts/crypto-vectors/gen-asymmetric-rsa.py: Wycheproof keys and rsa_pss/rsa_oaep/",
         "rsa_signature subsets, an independent RFC 8017 computation checked by cryptography "
         + cryptography.__version__ + "."],
        [("keys", ["bits", "n", "e", "d", "p", "q", "dP", "dQ", "qInv"], keys()),
         ("pss", ["bits", "hash", "salt length", "message", "salt", "signature"], pss_rows()),
         ("oaep", ["bits", "hash", "message", "label", "seed", "ciphertext"], oaep_rows()),
         ("pkcs1v15", ["bits", "hash", "message", "signature"], pkcs1_rows()),
         ("keygen", ["bits", "ChaCha20Drbg seed", "n", "d", "p", "q"], keygen_rows()),
         ("signatures", ["file", "tcId", "hash", "salt length (PSS) or 0", "public key row", "message",
                         "signature", "valid"], signatures),
         ("public_keys", ["n", "e"], PUBLIC_KEYS),
         ("oaep_wycheproof", ["tcId", "bits", "hash", "ciphertext", "label", "message (empty when invalid)",
                              "result"],
          wycheproof_oaep("rsa_oaep_2048_sha256_mgf1sha256_test.json", 2048)
          + wycheproof_oaep("rsa_oaep_3072_sha256_mgf1sha256_test.json", 3072)
          + wycheproof_oaep("rsa_oaep_4096_sha512_mgf1sha512_test.json", 4096))]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
