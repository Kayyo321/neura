#!/usr/bin/env python3
"""Extract all RFC 9474 Appendix A RSABSSA vectors.

Usage: python3 scripts/crypto-vectors/gen-rsa-blind.py [rfc9474.txt]
"""

import argparse
from hashlib import sha256, sha384
from math import gcd
from pathlib import Path
import re

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from nra_vectors import write_module

DEFAULT = Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9474.txt")
DIGEST = "e6fd49e5e35967f66154334806942e6ff9efa69b8beaf8d9ebe3e4e4439818b7"


def records(content: str):
    sections = re.split(r"\nA\.[1-4]\.  RSABSSA.* Test Vector\n", content)[1:]
    assert len(sections) == 4
    for section in sections:
        cells = {}
        field = None
        for line in section.splitlines():
            start = re.fullmatch(r"   (\w+) =\s*([0-9a-f]*)", line)
            if start:
                field = start[1]
                cells[field] = start[2]
            elif field is not None and re.fullmatch(r"   [0-9a-f]+", line):
                cells[field] += line.strip()
            else:
                field = None
        yield cells


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rfc", nargs="?", type=Path, default=DEFAULT)
    args = parser.parse_args()
    rfc = args.rfc.read_bytes()
    if sha256(rfc).hexdigest() != DIGEST:
        raise SystemExit(f"unexpected RFC 9474 SHA-256 for {args.rfc}")

    key_rows, cases, rejection = [], [], []
    for i, case in enumerate(records(rfc.decode("utf-8")), 1):
        p, q, n, e, d = (int(case[k], 16) for k in ("p", "q", "n", "e", "d"))
        assert n == p * q
        width = (n.bit_length() + 7) // 8
        ints = (d % (p - 1), d % (q - 1), pow(q, -1, p))
        crt = [x.to_bytes((x.bit_length() + 7) // 8, "big").hex() for x in ints]
        row = [case[k] for k in ("n", "e", "d", "p", "q")] + crt
        if not key_rows:
            key_rows.append(row)
        assert key_rows[0] == row

        prepared = bytes.fromhex(case["prepared_msg"])
        salt = bytes.fromhex(case["salt"])
        assert case["prepared_msg"] == case["msg_prefix"] + case["msg"]
        representative = int(case["encoded_msg"], 16)
        assert gcd(representative, n) == 1
        inv = int(case["inv"], 16)
        factor = pow(inv, -1, n)
        request = int(case["blinded_msg"], 16)
        response = int(case["blind_sig"], 16)
        assert request == representative * pow(factor, e, n) % n
        assert response == pow(request, d, n)
        assert int(case["sig"], 16) == response * inv % n
        pk = rsa.RSAPublicNumbers(e, n).public_key()
        pk.verify(bytes.fromhex(case["sig"]), prepared,
                  padding.PSS(mgf=padding.MGF1(hashes.SHA384()), salt_length=len(salt)), hashes.SHA384())
        cases.append([str(i), case["msg"], case["msg_prefix"], case["prepared_msg"], case["salt"],
                      factor.to_bytes(width, "big").hex(), (2).to_bytes(width, "big").hex(),
                      case["blinded_msg"], case["blind_sig"], case["sig"]])
        if i == 1:
            original = bytes.fromhex(case["blind_sig"])
            bad_response = original[:-1] + bytes([original[-1] ^ 1])
            rejection.append([bad_response.hex(), p.to_bytes(width, "big").hex(),
                              bytes(width).hex(), n.to_bytes(width, "big").hex()])

    # Make a deliberately invalid public modulus that shares factor 3 with a PSS encoding.
    # RFC 8017 encoding depends on the modulus width, so the small factor can be arranged independently.
    bad_modulus = 3 * ((1 << 2046) + 1)
    assert bad_modulus.bit_length() == 2048
    msg = next(bytes([i]) for i in range(256) if pss_zero(bytes([i]), 2047) % 3 == 0)
    malformed_key = [[bad_modulus.to_bytes(256, "big").hex(), msg.hex(), (2).to_bytes(256, "big").hex()]]
    print(write_module(
        "crypto-advanced", "rsa-blind-vectors.nra", "RsaBlindVectors",
        ["python3 scripts/crypto-vectors/gen-rsa-blind.py",
         f"RFC 9474 Appendix A. SHA-256 of source: {DIGEST}.",
         "Factors and CRT exponents derived with Python integers. RSA-PSS signatures checked with cryptography."],
        [("key", ["n", "e", "d", "p", "q", "dP", "dQ", "qInv"], key_rows),
         ("cases", ["appendix", "message", "prefix", "prepared message", "salt", "factor",
                    "server factor", "blinded message", "blinded signature", "final signature"], cases),
         ("reject", ["tampered response", "noninvertible factor", "zero factor", "out of range factor"], rejection),
         ("bad_encoding", ["invalid public modulus", "message", "factor"], malformed_key)],
    ))


def pss_zero(message: bytes, em_bits: int) -> int:
    em_len = (em_bits + 7) // 8
    h = sha384(bytes(8) + sha384(message).digest()).digest()
    db = bytes(em_len - len(h) - 2) + b"\x01"
    mask = b"".join(sha384(h + i.to_bytes(4, "big")).digest()
                    for i in range((len(db) + len(h) - 1) // len(h)))
    masked = bytearray(x ^ y for x, y in zip(db, mask))
    masked[0] &= 255 >> (8 * em_len - em_bits)
    return int.from_bytes(bytes(masked) + h + b"\xbc", "big")


if __name__ == "__main__":
    main()
