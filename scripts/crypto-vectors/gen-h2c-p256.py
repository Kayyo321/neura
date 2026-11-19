#!/usr/bin/env python3
"""Extract and independently verify RFC 9380 J.1.1 P-256 hash-to-curve vectors.

Usage: python3 scripts/crypto-vectors/gen-h2c-p256.py [rfc9380.txt]
"""

import argparse
from hashlib import sha256
from pathlib import Path
import re

from cryptography.hazmat.primitives.asymmetric import ec

from nra_vectors import write_module

DEFAULT = Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9380.txt")
DIGEST = "a2e6ab02b2117d6f52ac293020857752972b2e23ae005c33f612e40436ff4b6d"
P = 2**256 - 2**224 + 2**192 + 2**96 - 1
A, B, Z = -3, int("5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b", 16), -10


def transcript(text: str):
    body = text.split("\nJ.1.1.  P256_XMD:SHA-256_SSWU_RO_\n", 1)[1].split("\nJ.1.2.", 1)[0]
    rows: list[dict[str, str]] = [{}]
    key = None
    for line in body.splitlines():
        start = re.fullmatch(r"   (suite|dst|msg|P\.[xy]|u\[[01]\]|Q[01]\.[xy])\s*= ?(.*)", line)
        if start:
            key, value = start.groups()
            if key == "msg":
                rows.append({})
            rows[-1][key] = value
        elif key is not None and re.fullmatch(r"             \S+", line):
            rows[-1][key] += line.strip()
        else:
            key = None
    return rows[0]["dst"].encode(), rows[1:]


def xmd(message: bytes, dst: bytes, length: int) -> bytes:
    if len(dst) > 255:
        dst = sha256(b"H2C-OVERSIZE-DST-" + dst).digest()
    domain = dst + bytes([len(dst)])
    b0 = sha256(bytes(64) + message + length.to_bytes(2, "big") + b"\0" + domain).digest()
    current = bytes(32)
    blocks = []
    for idx in range(1, (length + 31) // 32 + 1):
        prefix = bytes(x ^ y for x, y in zip(b0, current))
        current = sha256(prefix + bytes([idx]) + domain).digest()
        blocks.append(current)
    return b"".join(blocks)[:length]


def map_affine(u: int) -> tuple[int, int]:
    """Non-straight-line RFC 9380 section 6.6.2, independent of the projective Appendix F.2 code."""
    term = Z * u**2 % P
    denominator = (term**2 + term) % P
    if denominator == 0:
        x = B * pow(Z * A, -1, P) % P
    else:
        x = -B * pow(A, -1, P) * (1 + pow(denominator, -1, P)) % P
    for candidate in (x, x * term % P):
        value = (candidate**3 + A * candidate + B) % P
        y = pow(value, (P + 1) // 4, P)
        if y**2 % P == value:
            if (u ^ y) & 1:
                y = -y % P
            return candidate, y
    raise AssertionError("neither SSWU candidate was square")


def add(first: tuple[int, int], second: tuple[int, int]) -> tuple[int, int] | None:
    x1, y1 = first
    x2, y2 = second
    if x1 != x2:
        slope = (y2 - y1) * pow(x2 - x1, -1, P) % P
    elif y1 == y2 and y1 != 0:
        slope = (3 * x1 * x1 + A) * pow(2 * y1, -1, P) % P
    else:
        return None
    x = (slope * slope - x1 - x2) % P
    return x, (slope * (x1 - x) - y1) % P


def encodings(point: tuple[int, int] | None) -> tuple[str, str]:
    if point is None:
        return "00", "00"
    x, y = point
    xb, yb = x.to_bytes(32, "big"), y.to_bytes(32, "big")
    first, second = b"\x04" + xb + yb, bytes([2 | (y & 1)]) + xb
    for value in (first, second):
        key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), value).public_numbers()
        assert (key.x, key.y) == point
    return first.hex(), second.hex()


def derive(message: bytes, dst: bytes):
    uniform = xmd(message, dst, 96)
    elements = tuple(int.from_bytes(uniform[i:i + 48], "big") % P for i in (0, 48))
    mapped = tuple(map_affine(u) for u in elements)
    return elements, mapped, add(*mapped)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rfc", nargs="?", type=Path, default=DEFAULT)
    args = parser.parse_args()
    rfc = args.rfc.read_bytes()
    if sha256(rfc).hexdigest() != DIGEST:
        raise SystemExit(f"unexpected RFC 9380 SHA-256 for {args.rfc}")

    sqrt_ten = int("da538e3be1d89b99c978fc675180aab27b8d1ff84c55d5b62ccd3427e433c47f", 16)
    assert sqrt_ten**2 % P == -Z
    dst, official = transcript(rfc.decode("utf-8"))
    assert len(official) == 5
    rows = []
    for index, case in enumerate(official):
        message = case["msg"].encode()
        elements, mapped, point = derive(message, dst)
        assert elements == tuple(int(case[f"u[{i}]"], 16) for i in (0, 1))
        assert mapped == tuple(tuple(int(case[f"Q{i}.{axis}"], 16) for axis in "xy") for i in (0, 1))
        assert point == tuple(int(case[f"P.{axis}"], 16) for axis in "xy")
        rows.append([f"RFC 9380 J.1.1 case {index + 1}", message.hex(), dst.hex(), *encodings(point)])

    binary = bytes(range(256))
    for name, message, domain in [
        ("one-byte DST", b"\0\x80\xff", b"\x01"),
        ("DST exactly 255 bytes", binary, binary[:255]),
        ("DST exactly 256 bytes", binary, binary),
        ("oversize DST", b"", binary + binary),
    ]:
        *_, result = derive(message, domain)
        rows.append([name, message.hex(), domain.hex(), *encodings(result)])
    print(write_module("crypto-advanced", "h2c-p256-vectors.nra", "H2cP256Vectors",
                       ["python3 scripts/crypto-vectors/gen-h2c-p256.py",
                        f"RFC 9380 Appendix J.1.1. SHA-256 of source: {DIGEST}.",
                        "All points checked against independent hashlib XMD and affine SSWU. SEC1 checked with cryptography."],
                       [("records", ["case", "message", "DST", "SEC1 uncompressed", "SEC1 compressed"], rows)]))


if __name__ == "__main__":
    main()
