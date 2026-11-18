#!/usr/bin/env python3
"""Known answers for Std.Crypto.Kdf.

* HKDF: the Python `cryptography` package (HKDF, version recorded), with the
  pseudorandom key from Python hmac.
* PBKDF2: hashlib.pbkdf2_hmac. scrypt: hashlib.scrypt (OpenSSL).
* Argon2id: the RFC 9106 section 5.3 vector, copied from the fetched text of
  https://www.rfc-editor.org/rfc/rfc9106.txt, and `openssl kdf ... ARGON2ID`
  (version recorded) for further memory, pass, lane and length choices.
* Balloon: no standard vectors exist; scripts/crypto-vectors/balloon_reference.py
  is an independent Python reference written from the paper's pseudocode.

Inputs are nra_vectors.pattern bytes. Writes tests/stdlib/crypto-kdf/support/kdf-vectors.nra.

    python3 scripts/crypto-vectors/gen-kdf.py [--source-dir DIR]
"""

import argparse
import hashlib
import hmac
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.request

import cryptography
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

from balloon_reference import balloon
from nra_vectors import pattern, write_module

RFC9106_URL = "https://www.rfc-editor.org/rfc/rfc9106.txt"
HASHES = {"sha224": hashes.SHA224, "sha256": hashes.SHA256, "sha384": hashes.SHA384, "sha512": hashes.SHA512,
          "sha3_256": hashes.SHA3_256}


def openssl_argon2id(password, salt, secret, ad, memory, passes, lanes, length):
    command = ["openssl", "kdf", "-binary", "-keylen", str(length), "-kdfopt", "hexpass:" + password.hex(),
               "-kdfopt", "hexsalt:" + salt.hex(), "-kdfopt", f"iter:{passes}", "-kdfopt", f"memcost:{memory}",
               "-kdfopt", f"lanes:{lanes}"]
    if secret:
        command += ["-kdfopt", "hexsecret:" + secret.hex()]
    if ad:
        command += ["-kdfopt", "hexad:" + ad.hex()]
    command.append("ARGON2ID")
    return subprocess.run(command, check=True, capture_output=True).stdout


def rfc9106_vector(text):
    section = text[text.index("5.3.  Argon2id Test Vectors\n\n"):]
    section = section[:section.index("\n6.  ")]

    def field(name):
        match = re.search(re.escape(name) + r"\[\d+\]: ((?:[0-9a-f]{2}\s+)+)", section)
        return bytes.fromhex(match.group(1))

    tag = bytes.fromhex(re.search(r"Tag: ((?:[0-9a-f]{2}\s+)+)", section).group(1))
    memory = int(re.search(r"Memory: (\d+) KiB", section).group(1))
    passes = int(re.search(r"Passes: (\d+)", section).group(1))
    lanes = int(re.search(r"Parallelism: (\d+) lanes", section).group(1))
    return (field("Password"), field("Salt"), field("Secret"), field("Associated data"), memory, passes, lanes, tag)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path)
    args = parser.parse_args()
    directory = args.source_dir or pathlib.Path(tempfile.mkdtemp())

    hkdf_rows = []
    for name, algorithm in HASHES.items():
        size = hashlib.new(name).digest_size
        for ikm_length, salt_length, info_length, length in [(22, 13, 10, 42), (80, 0, 0, size), (16, 80, 80, 3 * size + 5)]:
            ikm = pattern(ikm_length, 3, 11)
            salt = pattern(salt_length, 5, 7)
            info = pattern(info_length, 9, 2)
            okm = HKDF(algorithm=algorithm(), length=length, salt=salt or None, info=info).derive(ikm)
            prk = hmac.new(salt or bytes(size), ikm, name).digest()
            hkdf_rows.append([name, ikm.hex(), salt.hex(), info.hex(), str(length), prk.hex(), okm.hex()])

    pbkdf2_rows = []
    for name in ["sha224", "sha256", "sha384", "sha512", "sha3_256"]:
        for password_length, salt_length, iterations, length in [(8, 16, 1, 20), (0, 8, 2, 64), (40, 20, 10, 100), (200, 0, 3, 7)]:
            password = pattern(password_length, 13, 1)
            salt = pattern(salt_length, 17, 2)
            dk = hashlib.pbkdf2_hmac(name, password, salt, iterations, length)
            pbkdf2_rows.append([name, password.hex(), salt.hex(), str(iterations), str(length), dk.hex()])

    scrypt_rows = []
    for password_length, salt_length, n, r, p, length in [(0, 0, 16, 1, 1, 64), (8, 16, 16, 1, 1, 32), (8, 16, 32, 2, 2, 80),
                                                          (20, 8, 4, 3, 1, 16), (3, 3, 2, 1, 3, 33)]:
        password = pattern(password_length, 13, 3)
        salt = pattern(salt_length, 17, 4)
        dk = hashlib.scrypt(password, salt=salt, n=n, r=r, p=p, dklen=length)
        scrypt_rows.append([password.hex(), salt.hex(), str(n), str(r), str(p), str(length), dk.hex()])

    rfc_path = directory / "rfc9106.txt"
    if not rfc_path.exists():
        with urllib.request.urlopen(RFC9106_URL, timeout=60) as response:
            rfc_path.write_bytes(response.read())
    password, salt, secret, ad, memory, passes, lanes, tag = rfc9106_vector(rfc_path.read_text())
    if openssl_argon2id(password, salt, secret, ad, memory, passes, lanes, len(tag)) != tag:
        raise SystemExit("openssl disagrees with the RFC 9106 Argon2id vector")
    argon_rows = [[password.hex(), salt.hex(), secret.hex(), ad.hex(), str(memory), str(passes), str(lanes),
                   str(len(tag)), tag.hex(), "RFC 9106 section 5.3"]]
    version = subprocess.run(["openssl", "version"], check=True, capture_output=True, text=True).stdout.strip()
    for password_length, salt_length, secret_length, ad_length, memory, passes, lanes, length in [
            (8, 8, 0, 0, 8, 1, 1, 32), (32, 16, 0, 0, 16, 3, 1, 64), (5, 16, 0, 0, 32, 2, 2, 100),
            (16, 12, 4, 6, 24, 1, 3, 16), (0, 8, 0, 0, 9, 2, 1, 4), (12, 16, 0, 0, 40, 2, 1, 32)]:
        password = pattern(password_length, 7, 9)
        salt = pattern(salt_length, 11, 3)
        secret = pattern(secret_length, 5, 5)
        ad = pattern(ad_length, 3, 6)
        tag = openssl_argon2id(password, salt, secret, ad, memory, passes, lanes, length)
        argon_rows.append([password.hex(), salt.hex(), secret.hex(), ad.hex(), str(memory), str(passes), str(lanes),
                           str(length), tag.hex(), version])

    balloon_rows = []
    for name, password_length, salt_length, space, time in [("sha256", 8, 16, 1, 1), ("sha256", 0, 16, 4, 1),
                                                             ("sha256", 12, 16, 16, 3), ("sha3_256", 6, 8, 5, 2),
                                                             ("sha512", 10, 32, 8, 1)]:
        password = pattern(password_length, 19, 2)
        salt = pattern(salt_length, 23, 1)
        balloon_rows.append([name, password.hex(), salt.hex(), str(space), str(time),
                             balloon(password, salt, space, time, name).hex()])

    print(write_module(
        "crypto-kdf", "kdf-vectors.nra", "KdfVectors",
        [f"python3 scripts/crypto-vectors/gen-kdf.py: HKDF from cryptography {cryptography.__version__},",
         "PBKDF2 and scrypt from hashlib, Argon2id from RFC 9106 section 5.3 (" + RFC9106_URL + ")",
         f"and `openssl kdf` ({version}), Balloon from scripts/crypto-vectors/balloon_reference.py",
         "(an independent reference of the paper's Algorithm 1; no standard vectors exist)."],
        [("hkdf", ["hashlib name", "ikm", "salt", "info", "length", "prk", "okm"], hkdf_rows),
         ("pbkdf2", ["hashlib name", "password", "salt", "iterations", "length", "derived key"], pbkdf2_rows),
         ("scrypt", ["password", "salt", "N", "r", "p", "length", "derived key"], scrypt_rows),
         ("argon2id", ["password", "salt", "secret", "associated data", "memory KiB", "passes", "lanes",
                       "tag length", "tag", "source"], argon_rows),
         ("balloon", ["hashlib name", "password", "salt", "space cost", "time cost", "output"], balloon_rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
