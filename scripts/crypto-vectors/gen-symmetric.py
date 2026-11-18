#!/usr/bin/env python3
"""Known answers for Std.Crypto.Symmetric.

* AES-128/192/256 blocks, AES-GCM, AES-GCM-SIV (RFC 8452), AES-SIV (RFC 5297),
  ChaCha20 and ChaCha20-Poly1305: the Python `cryptography` package (version
  recorded in the file) over nra_vectors.pattern keys, nonces and messages.
* HChaCha20, XChaCha20 and XChaCha20-Poly1305: copied from
  draft-irtf-cfrg-xchacha-03 (sections 2.2.1, A.3.1 and A.3.2.1),
  https://www.ietf.org/archive/id/draft-irtf-cfrg-xchacha-03.txt, plus
  further XChaCha20-Poly1305 cases computed with `cryptography`: the HChaCha20
  subkey is the ChaCha20 keystream block for the 16-byte nonce minus the
  initial state words (the block function adds them back), and the AEAD is
  ChaCha20-Poly1305 under that subkey with nonce 0^4 || nonce[16:24].
* AES-GCM-SIV also: every vector of RFC 8452 Appendix C,
  https://www.rfc-editor.org/rfc/rfc8452.txt (each non-empty one is checked
  against `cryptography`, which refuses an empty plaintext).
* AES-SIV also: the two examples of RFC 5297 Appendix A,
  https://www.rfc-editor.org/rfc/rfc5297.txt (checked against `cryptography`).
* FF1: every sample of NIST's FF1samples.pdf,
  https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/FF1samples.pdf
  (converted with `pdftotext -layout`).

Writes tests/stdlib/crypto-symmetric/support/symmetric-vectors.nra.

    python3 scripts/crypto-vectors/gen-symmetric.py [--source-dir DIR]
"""

import argparse
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import urllib.request

import cryptography
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, AESGCMSIV, AESSIV, ChaCha20Poly1305

from nra_vectors import pattern, write_module

XCHACHA_URL = "https://www.ietf.org/archive/id/draft-irtf-cfrg-xchacha-03.txt"
RFC8452_URL = "https://www.rfc-editor.org/rfc/rfc8452.txt"
RFC5297_URL = "https://www.rfc-editor.org/rfc/rfc5297.txt"
FF1_URL = ("https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/"
           "examples/FF1samples.pdf")


def fetch(url: str, directory: pathlib.Path) -> pathlib.Path:
    path = directory / url.rsplit("/", 1)[1]
    if not path.exists():
        with urllib.request.urlopen(url, timeout=60) as response:
            path.write_bytes(response.read())
    return path


def labeled_hex(text: str) -> dict[str, bytes]:
    """Hex blocks under "Label:" lines of an RFC-style section."""
    values: dict[str, str] = {}
    label = None
    for line in text.split("\n"):
        stripped = line.strip()
        if re.fullmatch(r"[A-Za-z0-9 \-]+:", stripped):
            label = stripped[:-1]
            values[label] = ""
        elif label and re.fullmatch(r"[0-9a-f]+", stripped):
            values[label] += stripped
    return {k: bytes.fromhex(v) for k, v in values.items() if v}


def section(text: str, start: str, end: str) -> str:
    body = text[text.rindex(start):]
    return body[:body.index(end)] if end else body


def hchacha20(key: bytes, nonce16: bytes) -> bytes:
    block = Cipher(algorithms.ChaCha20(key, nonce16), mode=None).encryptor().update(bytes(64))
    words = struct.unpack("<16I", block)
    initial = struct.unpack("<4I", b"expand 32-byte k") + struct.unpack("<8I", key) + struct.unpack("<4I", nonce16)
    raw = [(w - i) % 2**32 for w, i in zip(words, initial)]
    return struct.pack("<8I", *(raw[0:4] + raw[12:16]))


def xchacha_seal(key: bytes, nonce: bytes, plaintext: bytes, aad: bytes) -> bytes:
    return ChaCha20Poly1305(hchacha20(key, nonce[:16])).encrypt(bytes(4) + nonce[16:], plaintext, aad)


def rfc8452_vectors(text: str) -> list[list[str]]:
    """Every vector of RFC 8452 Appendix C (C.1, C.2 and the C.3 counter wrap tests)."""
    body = text[text.index("C.1.  AEAD_AES_128_GCM_SIV\n\n"):]
    body = body[:body.index("\nAcknowledgements")]
    body = "\n".join(line for line in body.replace("\f", "").split("\n")
                     if not re.search(r"\[Page \d+\]$", line) and not line.startswith("RFC 8452 "))
    rows = []
    for chunk in re.split(r"\n\s*(?=Plaintext \(\d+ bytes\) =)", body)[1:]:
        fields: dict[str, str] = {}
        label = None
        for line in chunk.split("\n"):
            match = re.match(r"\s*([A-Za-z .()0-9]+?)\s*=\s*([0-9a-f]*)\s*$", line)
            if match and not re.fullmatch(r"\s*[0-9a-f]+\s*", line):
                label = re.sub(r" \(\d+ bytes\)", "", match.group(1)).strip()
                fields[label] = match.group(2)
            elif label and re.fullmatch(r"\s+[0-9a-f]+\s*", line):
                fields[label] += line.strip()
        rows.append(["aes-gcm-siv", fields["Key"], fields["Nonce"], fields["AAD"], fields["Plaintext"],
                     fields["Result"]])
    return rows


def rfc5297_vectors(text: str) -> list[list[str]]:
    """The two examples of RFC 5297 Appendix A, as AES-SIV rows (A.2's nonce is its last AD item)."""
    body = text[text.rindex("\nA.1.  Deterministic Authenticated Encryption Example\n"):text.rindex("Author's Address")]
    body = "\n".join(line for line in body.replace("\f", "").split("\n")
                     if not re.search(r"\[Page \d+\]$", line) and not line.startswith("RFC 5297 "))
    rows = []
    for part in body.split("A.2.  Nonce-Based Authenticated Encryption Example"):
        fields: dict[str, str] = {}
        label = None
        for line in part.split("\n"):
            stripped = line.strip()
            if re.fullmatch(r"[A-Za-z0-9 |()]+:", stripped):
                label = stripped[:-1]
                fields.setdefault(label, "")
            elif label and re.fullmatch(r"[0-9a-f]{2,8}( [0-9a-f]{2,8})*", stripped):
                if fields[label] is not None:
                    fields[label] += stripped.replace(" ", "")
            elif stripped:
                label = None
        ads = [fields[name] for name in ("AD", "AD1", "AD2", "Nonce") if name in fields]
        rows.append([fields["Key"], ":".join(ads), str(len(ads)), fields["Plaintext"], fields["IV || C"]])
    return rows


def ff1_samples(text: str) -> list[list[str]]:
    rows = []
    for chunk in re.split(r"\nSample #\d+\s*\n", text)[1:]:
        key_text = re.search(r"Key is ([0-9A-F \n]+?)\nRadix", chunk).group(1)
        radix = re.search(r"Radix = (\d+)", chunk).group(1)
        tweak = re.search(r"Tweak is ([^\n]*)", chunk).group(1).strip()
        plaintext = re.search(r"PT is <([0-9a-z]+)>", chunk).group(1)
        ciphertext = re.search(r"CT is <([0-9a-z]+)>", chunk).group(1)
        rows.append([bytes.fromhex(key_text).hex(), radix,
                     "" if tweak == "<empty>" else bytes.fromhex(tweak).hex(), plaintext, ciphertext])
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path)
    args = parser.parse_args()
    directory = args.source_dir or pathlib.Path(tempfile.mkdtemp())

    aes_rows = []
    for key_length in (16, 24, 32):
        key = bytes(range(key_length))
        block = bytes.fromhex("00112233445566778899aabbccddeeff")  # FIPS 197 Appendix C plaintext
        for plaintext in (block, pattern(16, 37, key_length)):
            out = Cipher(algorithms.AES(key), modes.ECB()).encryptor().update(plaintext)
            aes_rows.append([key.hex(), plaintext.hex(), out.hex()])

    aead_rows = []
    cases = [(0, 0), (16, 0), (0, 20), (13, 17), (64, 32), (100, 70)]
    for name, key_lengths, nonce_length, make in [
            ("aes-gcm", (16, 24, 32), 12, AESGCM), ("aes-gcm-siv", (16, 32), 12, AESGCMSIV),
            ("chacha20-poly1305", (32,), 12, ChaCha20Poly1305)]:
        for key_length in key_lengths:
            for message_length, aad_length in cases:
                key = pattern(key_length, 19, message_length + key_length)
                nonce = pattern(nonce_length, 23, aad_length)
                message = pattern(message_length)
                aad = pattern(aad_length, 3, 5)
                if make is AESGCMSIV and message_length == 0:
                    continue  # cryptography refuses it; RFC 8452 Appendix C has empty plaintexts.
                sealed = make(key).encrypt(nonce, message, aad)
                aead_rows.append([name, key.hex(), nonce.hex(), aad.hex(), message.hex(), sealed.hex()])
    for message_length, aad_length in cases:
        key = pattern(32, 41, message_length)
        nonce = pattern(24, 43, aad_length)
        message = pattern(message_length)
        aad = pattern(aad_length, 3, 5)
        aead_rows.append(["xchacha20-poly1305", key.hex(), nonce.hex(), aad.hex(), message.hex(),
                          xchacha_seal(key, nonce, message, aad).hex()])

    for row in rfc8452_vectors(fetch(RFC8452_URL, directory).read_text()):
        key, nonce, aad, message, result = (bytes.fromhex(c) for c in row[1:])
        if message and AESGCMSIV(key).encrypt(nonce, message, aad or None) != result:
            raise SystemExit(f"RFC 8452 vector disagrees with cryptography: {row}")
        aead_rows.append(row)

    draft = fetch(XCHACHA_URL, directory).read_text()
    aead = labeled_hex(section(draft, "A.3.1.  AEAD_XCHACHA20_POLY1305", "A.3.2.  XChaCha20"))
    if xchacha_seal(aead["Key"], aead["IV"], aead["Plaintext"], aead["AAD"]) != aead["Ciphertext"] + aead["Tag"]:
        raise SystemExit("the cryptography-based XChaCha20-Poly1305 disagrees with the draft")
    aead_rows.append(["xchacha20-poly1305", aead["Key"].hex(), aead["IV"].hex(), aead["AAD"].hex(),
                      aead["Plaintext"].hex(), (aead["Ciphertext"] + aead["Tag"]).hex()])
    stream = labeled_hex(section(draft, "A.3.2.1.  Block Counter = 0", "A.3.2.2."))

    hchacha_key = bytes(range(32))
    hchacha_nonce = bytes.fromhex("000000090000004a0000000031415927")
    hchacha_subkey = bytes.fromhex("82413b4227b27bfed30e42508a877d73a0f9e4d58a74a853c12ec41326d3ecdc")
    if hchacha20(hchacha_key, hchacha_nonce) != hchacha_subkey:
        raise SystemExit("HChaCha20 derivation disagrees with draft section 2.2.1")

    stream_rows = [["xchacha20", stream["Key"].hex(), stream["IV"].hex(), "0", stream["Plaintext"].hex(),
                    stream["Ciphertext"].hex()]]
    for message_length, counter in [(0, 0), (1, 1), (64, 7), (65, 0), (200, 4294967290)]:
        key = pattern(32, 47, message_length)
        nonce = pattern(12, 53, counter % 251)
        message = pattern(message_length)
        out = Cipher(algorithms.ChaCha20(key, struct.pack("<I", counter) + nonce), mode=None).encryptor().update(message)
        stream_rows.append(["chacha20", key.hex(), nonce.hex(), str(counter), message.hex(), out.hex()])

    siv_rows = []
    for key_length in (32, 48, 64):
        for message_length, ad_lengths in [(1, []), (14, [24]), (16, [0, 10]), (47, [3, 16, 40])]:
            key = pattern(key_length, 59, message_length)
            message = pattern(message_length)
            ads = [pattern(n, 61, i) for i, n in enumerate(ad_lengths)]
            sealed = AESSIV(key).encrypt(message, ads if ads else None)
            siv_rows.append([key.hex(), ":".join(a.hex() for a in ads), str(len(ads)), message.hex(), sealed.hex()])

    for row in rfc5297_vectors(fetch(RFC5297_URL, directory).read_text()):
        key, plaintext, result = bytes.fromhex(row[0]), bytes.fromhex(row[3]), bytes.fromhex(row[4])
        ads = [bytes.fromhex(a) for a in row[1].split(":")]
        if AESSIV(key).encrypt(plaintext, ads) != result:
            raise SystemExit(f"RFC 5297 vector disagrees with cryptography: {row}")
        siv_rows.append(row)

    ff1_text = subprocess.run(["pdftotext", "-layout", str(fetch(FF1_URL, directory)), "-"], check=True,
                              capture_output=True, text=True).stdout
    ff1_rows = ff1_samples(ff1_text)

    print(write_module(
        "crypto-symmetric", "symmetric-vectors.nra", "SymmetricVectors",
        [f"python3 scripts/crypto-vectors/gen-symmetric.py: cryptography {cryptography.__version__};",
         f"{RFC8452_URL} (Appendix C); {RFC5297_URL} (Appendix A);",
         f"{XCHACHA_URL} (2.2.1, A.3.1, A.3.2.1);", f"{FF1_URL}."],
        [("aes_blocks", ["key", "plaintext block", "ciphertext block"], aes_rows),
         ("aeads", ["algorithm", "key", "nonce", "associated data", "plaintext", "ciphertext || tag"], aead_rows),
         ("streams", ["algorithm", "key", "nonce", "initial block counter", "plaintext", "ciphertext"], stream_rows),
         ("hchacha20", ["key", "16-byte nonce", "subkey"],
          [[hchacha_key.hex(), hchacha_nonce.hex(), hchacha_subkey.hex()]]),
         ("aes_siv", ["key", "associated data items joined by ':'", "item count", "plaintext", "synthetic IV || ciphertext"],
          siv_rows),
         ("ff1", ["key", "radix", "tweak", "plaintext digits", "ciphertext digits"], ff1_rows)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
