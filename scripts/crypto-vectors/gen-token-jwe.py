#!/usr/bin/env python3
"""Extract and independently check compact JWE vectors.

    python3 scripts/crypto-vectors/gen-token-jwe.py [sources-dir]

The RFC 7516 A.3 and RFC 7520 section 5.6 compact serializations are used
verbatim. The RSA-OAEP and RSA1_5 examples of RFC 7516 A.1/A.2 are
rejections. The six RFC 3394 test cases are carried as AES-KW key management
under GCM, and the three RFC 7518 Appendix B inputs are tested through the
compact format with its required protected-header AAD. AES, AES-GCM, CBC and
AES-KW are computed using Python cryptography 43.0.0. HMAC comes from Python.
"""

import ast
import base64
import hashlib
import hmac
import json
import pathlib
import re
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
from cryptography.hazmat.primitives.padding import PKCS7

from nra_vectors import write_module

SOURCES = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/token")
DIGESTS = {
    "rfc3394": "faa400c69c22e5f4a911222dc19c17091931a4b8c0658adc7011163e63b253ff",
    "rfc7516": "722cfcddc15f58e746891ba4b846853d3711153a774e4dd55ad6db100474a21f",
    "rfc7518": "9a9ae524b09ea700ad3f189bac115df95ba69af84b26ffdbe3cdfb8d2152b1fc",
    "rfc7520": "14798c3b2d72e35375d5fb86d280a1e8df6109a6ee6cbfc091a0c57a8cc076c6",
}


def read(root, name):
    source = (root / f"{name}.txt").read_bytes()
    if hashlib.sha256(source).hexdigest() != DIGESTS[name]:
        raise ValueError(f"unexpected SHA-256 for {name}")
    return source.decode("utf-8")


def section(text, start, end):
    marker = text.index("\n" + start)
    return text[marker:text.index("\n" + end, marker + 1)]


def decode(part):
    return base64.urlsafe_b64decode(part + "=" * (-len(part) % 4))


def encode(raw):
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def aad_bytes(header):
    return encode(header).encode("ascii")


def header(alg, enc):
    return json.dumps({"alg": alg, "enc": enc}, separators=(",", ":")).encode("ascii")


def cbc_tag(cek, aad, iv, ciphertext):
    size = len(cek) // 2
    return hmac.digest(cek[:size], aad + iv + ciphertext + (8 * len(aad)).to_bytes(8, "big"),
                       f"sha{8 * len(cek)}")[:size]


def cbc_encrypt(cek, iv, plaintext, *, padding=True):
    if padding:
        padder = PKCS7(128).padder()
        plaintext = padder.update(plaintext) + padder.finalize()
    encryptor = Cipher(algorithms.AES(cek[len(cek) // 2:]), modes.CBC(iv)).encryptor()
    return encryptor.update(plaintext) + encryptor.finalize()


def compact(protected, encrypted_key, iv, ciphertext, tag):
    return ".".join(map(encode, (protected, encrypted_key, iv, ciphertext, tag)))


def rfc7516_token(text, appendix):
    block = section(text, f"A.{appendix}.7.  Complete Representation", f"A.{appendix}.8.  Validation")
    token = "".join(re.findall(r"(?m)^ {5}([\w.-]+)$", block.split("purposes only) is:")[1]))
    if token.count(".") != 4:
        raise ValueError("RFC 7516 compact token was not extracted")
    return token


def rfc7516_case(text):
    data = section(text, "A.3.  Example JWE", "A.4.  Example JWE")
    plaintext, cek, wrapped, iv, aad, ciphertext, tag = [
        bytes(json.loads(s)) for s in re.findall(r"\[(?:\d|,|\s)+\]", data)
    ]
    kek = decode(re.search(r'"k":"([\w-]+)"', data)[1])
    token = rfc7516_token(text, 3)
    protected = decode(token.split(".")[0])
    assert wrapped == aes_key_wrap(kek, cek)
    assert ciphertext == cbc_encrypt(cek, iv, plaintext)
    assert tag == cbc_tag(cek, aad, iv, ciphertext)
    assert aad == aad_bytes(protected)
    assert token == compact(protected, wrapped, iv, ciphertext, tag)
    return ["rfc7516-a3", "A128KW", "A128CBC-HS256", kek.hex(), cek.hex(), iv.hex(),
            plaintext.hex(), protected.hex(), token]


def rfc7520_case(text):
    introduction = section(text, "5.  JSON Web Encryption Examples", "5.1.  Key Encryption")
    quoted = re.findall(r'(?m)^   ("[^"\n]*")$', introduction)
    plaintext = b"".join(ast.literal_eval("b" + line) for line in quoted)
    direct = section(text, "5.6.  Direct Encryption Using AES-GCM", "5.7.  Key Wrap")
    cek = decode(re.search(r'"k": "([\w-]+)"', direct)[1])
    compact_text = direct.split("The resulting JWE object using the JWE Compact Serialization:")[1].split("Figure 136")[0]
    token = "".join(re.findall(r"(?m)^ {3}([\w.-]+)$", compact_text))
    protected, encrypted_key, iv, ciphertext, tag = map(decode, token.split("."))
    assert not encrypted_key
    assert AESGCM(cek).encrypt(iv, plaintext, aad_bytes(protected)) == ciphertext + tag
    return ["rfc7520-5.6", "A128GCM", cek.hex(), iv.hex(), plaintext.hex(), protected.hex(), token]


def hex_field(block, label):
    pattern = rf"(?m)^ +{re.escape(label)} *([a-fA-F0-9 ]*)\n((?: +[a-fA-F0-9 ]+\n)*)"
    found = re.search(pattern, block)
    if found is None:
        raise ValueError(f"missing field {label}")
    return bytes.fromhex(found[1] + found[2])


def rfc3394_cases(text, plaintext):
    for number in range(1, 7):
        until = f"4.{number + 1} Wrap" if number < 6 else "5. Security Considerations"
        block = section(text, f"4.{number} Wrap", until)
        kek = hex_field(block, "KEK:")
        cek = hex_field(block, "Key Data:")
        wrapped = hex_field(block, "Ciphertext:" if number <= 3 else "Ciphertext")
        assert aes_key_wrap(kek, cek) == wrapped
        iv = hashlib.sha256(f"RFC 3394 wrap test {number}".encode("ascii")).digest()[:12]
        alg, enc = f"A{8 * len(kek)}KW", f"A{8 * len(cek)}GCM"
        protected = header(alg, enc)
        sealed = AESGCM(cek).encrypt(iv, plaintext, aad_bytes(protected))
        token = compact(protected, wrapped, iv, sealed[:-16], sealed[-16:])
        yield [f"rfc3394-4.{number}", alg, enc, kek.hex(), cek.hex(), iv.hex(), plaintext.hex(),
               protected.hex(), token]


def extra_direct(wrapped_rows):
    for enc, plaintext in (("A192GCM", b""), ("A256GCM", bytes.fromhex(wrapped_rows[0][6]))):
        cek = bytes.fromhex(next(row[4] for row in wrapped_rows if row[2] == enc))
        iv = hashlib.sha256(f"JWE direct {enc} fixture".encode("ascii")).digest()[:12]
        protected = header("dir", enc)
        sealed = AESGCM(cek).encrypt(iv, plaintext, aad_bytes(protected))
        token = compact(protected, b"", iv, sealed[:-16], sealed[-16:])
        yield [f"direct-{enc}", enc, cek.hex(), iv.hex(), plaintext.hex(), protected.hex(), token]


def rfc7518_cases(text):
    rejections = []
    cases = []
    for number in range(1, 4):
        until = f"B.{number + 1}.  Test Cases" if number < 3 else "Appendix C."
        block = section(text, f"B.{number}.  Test Cases", until)
        cek, iv, plaintext, aad, ciphertext, tag = [hex_field(block, label + " =")
                                                     for label in ("K", "IV", "P", "A", "E", "T")]
        assert ciphertext == cbc_encrypt(cek, iv, plaintext)
        assert tag == cbc_tag(cek, aad, iv, ciphertext)
        enc = f"A{4 * len(cek)}CBC-HS{8 * len(cek)}"
        protected = header("dir", enc)
        jwe_tag = cbc_tag(cek, aad_bytes(protected), iv, ciphertext)
        token = compact(protected, b"", iv, ciphertext, jwe_tag)
        cases.append([f"rfc7518-b{number}", enc, cek.hex(), iv.hex(), plaintext.hex(), protected.hex(), token])
        if number == 1:
            # A valid MAC with a decrypted final octet of zero is invalid PKCS #7.
            bad_iv = hashlib.sha256(b"invalid JWE PKCS7 fixture").digest()[:16]
            bad_ciphertext = cbc_encrypt(cek, bad_iv, bytes(16), padding=False)
            bad_tag = cbc_tag(cek, aad_bytes(protected), bad_iv, bad_ciphertext)
            rejections.append([enc, cek.hex(), compact(protected, b"", bad_iv, bad_ciphertext, bad_tag)])
    return cases, rejections


def main():
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCES
    rfc3394, rfc7516, rfc7518, rfc7520 = (read(root, name) for name in DIGESTS)
    wrap_case = rfc7516_case(rfc7516)
    wrap_rows = [wrap_case, *rfc3394_cases(rfc3394, bytes.fromhex(wrap_case[6]))]
    direct_rows, bad_padding = rfc7518_cases(rfc7518)
    direct_rows = [rfc7520_case(rfc7520), *extra_direct(wrap_rows), *direct_rows]
    unsupported = [[rfc7516_token(rfc7516, appendix)] for appendix in (1, 2)]

    print(write_module("crypto-token", "jwe-vectors.nra", "JweVectors",
                       ["python3 scripts/crypto-vectors/gen-token-jwe.py",
                        "RFC 3394 section 4, RFC 7516 Appendix A, RFC 7518 Appendix B and RFC 7520 section 5.6.",
                        "Reference implementations: Python cryptography 43.0.0 and hashlib/hmac."],
                       [("wrapped", ["case", "alg", "enc", "kek", "cek", "iv", "plaintext", "header", "token"], wrap_rows),
                        ("direct", ["case", "enc", "cek", "iv", "plaintext", "header", "token"], direct_rows),
                        ("bad_padding", ["enc", "cek", "token"], bad_padding),
                        ("unsupported", ["token"], unsupported)]))


if __name__ == "__main__":
    main()
