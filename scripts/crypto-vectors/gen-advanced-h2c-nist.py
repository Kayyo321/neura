#!/usr/bin/env python3
"""Reproduce the P-384 and P-521 hash-to-curve vectors from RFC 9380 J.2.1 and J.3.1.

The unoptimized affine Section 6.6.2 formulas independently check the intermediate
u and Q values and the final point. Extra cases exercise a 256-byte domain
separation tag and a change to that tag. Python's OpenSSL-backed cryptography
checks both SEC1 encodings.
"""

import hashlib
import pathlib
import re
import sys

from cryptography.hazmat.primitives.asymmetric import ec
from nra_vectors import write_module

RFC = pathlib.Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9380.txt")
RFC_SHA256 = "a2e6ab02b2117d6f52ac293020857752972b2e23ae005c33f612e40436ff4b6d"
P384 = 2**384 - 2**128 - 2**96 + 2**32 - 1
P521 = 2**521 - 1
B384 = 0xB3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF
B521 = 0x51953EB9618E1C9A1F929A21A0B68540EEA2DA725B99B315F3B8B489918EF109E156193951EC7E937B1652C0BD3BB1BF073573DF883D2C34F1EF451FD46B503F00
FIELDS = ("P.x", "P.y", "u[0]", "u[1]", "Q0.x", "Q0.y", "Q1.x", "Q1.y")


def expand(message, dst, length, digest):
    blocksize = digest().block_size
    digests = (length + digest().digest_size - 1) // digest().digest_size
    if len(dst) > 255:
        dst = digest(b"H2C-OVERSIZE-DST-" + dst).digest()
    domain = dst + bytes([len(dst)])
    b0 = digest(bytes(blocksize) + message + length.to_bytes(2, "big") + b"\0" + domain).digest()
    last = digest(b0 + b"\x01" + domain).digest()
    output = bytearray(last)
    for index in range(2, digests + 1):
        prefix = bytes(a ^ b for a, b in zip(b0, last, strict=True))
        last = digest(prefix + bytes([index]) + domain).digest()
        output.extend(last)
    return bytes(output[:length])


def map_affine(u, p, b, z):
    a = -3
    zu2 = z * u**2 % p
    d = zu2 * (zu2 + 1) % p
    if d == 0:
        x = b * pow(z * a, -1, p) % p
    else:
        x = -b * pow(a, -1, p) * (1 + pow(d, -1, p)) % p
    for candidate in (x, zu2 * x % p):
        y2 = (candidate**3 + a * candidate + b) % p
        y = pow(y2, (p + 1) // 4, p)
        if y * y % p == y2:
            return candidate, y if (y - u) % 2 == 0 else p - y
    raise ValueError("no Simplified SWU point")


def add_affine(left, right, p):
    x1, y1 = left
    x2, y2 = right
    if x1 != x2:
        slope = (y2 - y1) * pow(x2 - x1, -1, p) % p
    else:
        assert y1 == y2 and y1 != 0
        slope = (3 * x1**2 - 3) * pow(2 * y1, -1, p) % p
    x3 = (slope**2 - x1 - x2) % p
    return x3, (slope * (x1 - x3) - y1) % p


def encoding(point, curve):
    x, y = point
    width = (curve.key_size + 7) // 8
    xb = x.to_bytes(width, "big")
    compressed = bytes([2 | (y & 1)]) + xb
    uncompressed = b"\x04" + xb + y.to_bytes(width, "big")
    for data in (compressed, uncompressed):
        verified = ec.EllipticCurvePublicKey.from_encoded_point(curve, data).public_numbers()
        assert (verified.x, verified.y) == point
    return uncompressed.hex(), compressed.hex()


def group(suite, section, p, b, z, length, digest, curve, rfc):
    body = rfc.split(f"\n{section}.1.  {suite}\n", 1)[1].split(f"\n{section}.2. ", 1)[0]
    dst = re.search(r"^   dst *= (\S+)$", body, re.M)[1].encode()
    records = []
    for part in re.split(r"^   msg *= ?", body, flags=re.M)[1:]:
        lines = part.splitlines()
        message = "".join(line.strip() for line in lines[:next(i for i, line in enumerate(lines) if "P.x " in line)]).encode()
        values = []
        for field in FIELDS:
            match = re.search(r"^   " + re.escape(field) + r" *= ([a-f0-9]+(?:\n {13}[a-f0-9]+)*)$", part, re.M)
            values.append(int("".join(match[1].split()), 16))
        records.append((message, dst, tuple(values)))
    assert [msg for msg, _, _ in records] == [b"", b"abc", b"abcdef0123456789", b"q128_" + b"q" * 128, b"a512_" + b"a" * 512]
    long_dst = bytes(range(256))
    records += [(b"domain check", long_dst, None), (b"domain check", long_dst[:-1] + b"\x01", None)]
    rows = []
    for msg, domain, expected in records:
        uniform = expand(msg, domain, 2 * length, digest)
        u0 = int.from_bytes(uniform[:length], "big") % p
        u1 = int.from_bytes(uniform[length:], "big") % p
        q0, q1 = map_affine(u0, p, b, z), map_affine(u1, p, b, z)
        point = add_affine(q0, q1, p)
        if expected is not None:
            assert (*point, u0, u1, *q0, *q1) == expected
        rows.append([msg.hex(), domain.hex(), *encoding(point, curve)])
    assert rows[-1][-1] != rows[-2][-1]
    return rows


def main():
    source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else RFC
    data = source.read_bytes()
    assert hashlib.sha256(data).hexdigest() == RFC_SHA256
    rfc = data.decode()
    p384 = group("P384_XMD:SHA-384_SSWU_RO_", "J.2", P384, B384, -12, 72, hashlib.sha384, ec.SECP384R1(), rfc)
    p521 = group("P521_XMD:SHA-512_SSWU_RO_", "J.3", P521, B521, -4, 98, hashlib.sha512, ec.SECP521R1(), rfc)
    cols = ["message", "DST", "SEC1 uncompressed", "SEC1 compressed"]
    print(write_module("crypto-advanced", "h2c-nist-vectors.nra", "H2cNistVectors",
                       ["python3 scripts/crypto-vectors/gen-advanced-h2c-nist.py",
                        "RFC 9380 Appendix J.2.1 and J.3.1; cross-checked by an affine Section 6.6.2 oracle.",
                        "The final two rows per suite independently check a 256-byte DST and a changed DST."],
                       [("p384", cols, p384), ("p521", cols, p521)]))


if __name__ == "__main__":
    main()
