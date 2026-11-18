#!/usr/bin/env python3
"""Extract RFC 9496 B.1/B.2 and independently check B.1 through B.3 with affine arithmetic."""

import hashlib
from pathlib import Path
import re
import sys

from nra_vectors import write_module

SOURCE = Path("/home/dev/neura-gate/030/crypto2/sources/advanced/rfc9496.txt")
SOURCE_SHA256 = "d875c6e703827da487eae8f0656fd25e4a278f1dcb0f06041669adad27c03ff3"
P = 2**448 - 2**224 - 1
D = -39081
L = 2**446 - 13818066809895115352007386748515426880336692474882178609894547503885

# Edwards448 base point from RFC 8032, 5.2.
B = (
    224580040295924300187604334099896036246789641632564134246125461686950415467406032909029192869357953282578032075146446173674602635247710,
    298819210078481492676017930443930673437544040154080242095928241372331506189835876003536878655418784733982303233503462500531545062832660,
)


def abs_field(value):
    value %= P
    return P - value if value & 1 else value


def sqrt_ratio(u, v):
    """Compute the ratio first, with inversion and explicit square checking."""
    if not v % P:
        return not u % P, 0
    ratio = (u * pow(v, -1, P)) % P
    root = pow(ratio, (P + 1) // 4, P)
    if root * root % P == ratio:
        return True, abs_field(root)
    return False, abs_field(pow(-ratio, (P + 1) // 4, P))


SQRT_D = sqrt_ratio(-D, 1)[1]
INV_SQRT_D = pow(SQRT_D, -1, P)


def add(p, q):
    x, y = p
    a, b = q
    cross = D * x * y * a * b
    return (x * b + y * a) * pow(1 + cross, -1, P) % P, (y * b - x * a) * pow(1 - cross, -1, P) % P


def multiply(point, scalar):
    accumulator = (0, 1)
    for bit in bin(scalar)[2:]:
        accumulator = add(accumulator, accumulator)
        if bit == "1":
            accumulator = add(accumulator, point)
    return accumulator


def encode(point):
    x, y = point
    t = x * y % P
    u = (x + t) * (x - t) % P
    inverse = sqrt_ratio(1, u * (1 - D) * x**2)[1]
    ratio = abs_field(inverse * u * SQRT_D)
    s = abs_field((1 - D) * inverse * x * (INV_SQRT_D * ratio - t))
    return s.to_bytes(56, "little")


def decode(data):
    s = int.from_bytes(data, "little")
    if len(data) != 56 or s >= P or s & 1:
        return None
    u = 1 + s**2
    v = u**2 - 4 * D * s**2
    square, inverse = sqrt_ratio(1, v * u**2)
    if not square:
        return None
    x = abs_field(2 * s * inverse * u * SQRT_D) * inverse * v * INV_SQRT_D % P
    y = (1 - s**2) * inverse * u % P
    return x, y


def map_point(data):
    t = int.from_bytes(data, "little") % P
    r = -(t * t) % P
    u = D * (r - 1)
    square, v = sqrt_ratio(1 - 2 * D, (r + 1) * (u + 1) * (u - r))
    if not square:
        v *= t
    s = v * (r + 1) % P
    w0 = 2 * abs_field(s)
    w1 = s**2 + 1
    w2 = s**2 - 1
    w3 = v * s * (r - 1) * (1 - 2 * D) + (1 if square else -1)
    z = w1 * w3
    return w0 * w3 * pow(z, -1, P) % P, w2 * w1 * pow(z, -1, P) % P


def derive(data):
    return add(map_point(data[:56]), map_point(data[56:]))


def rfc_vectors(data):
    text = data.decode()
    appendix = text.split("\nB.1.  Multiples of the Generator\n", 1)[1].split("\nAcknowledgements\n", 1)[0]
    first, tail = appendix.split("\nB.2.  Invalid Encodings\n")
    second, third = tail.split("\nB.3.  Group Elements from Uniform Byte Strings\n")
    generator = [
        bytes.fromhex(line + next_line)
        for line, next_line in re.findall(r"^   B\[\s*\d+\]: ([0-9a-f ]+)\n          ([0-9a-f ]+)$", first, re.M)
    ]
    rejected = [
        bytes.fromhex(line + next_line)
        for line, next_line in re.findall(r"^   ([0-9a-f ]+)\n   ([0-9a-f ]+)$", second, re.M)
    ]
    mapped = [
        (bytes.fromhex(line + "".join(rest.split())), bytes.fromhex(out + next_out))
        for line, rest, out, next_out in re.findall(
            r"^   I: ([a-f0-9]+)\n((?:      [a-f0-9]+\n){3})   O: ([a-f0-9 ]+)\n      ([a-f0-9 ]+)$", third, re.M
        )
    ]
    if (len(generator), len(rejected), len(mapped)) != (16, 21, 7):
        raise ValueError(f"wrong RFC 9496 vector counts: {len(generator)}, {len(rejected)}, {len(mapped)}")
    if any(len(encoded) != 56 for encoded in generator + rejected):
        raise ValueError("wrong length for RFC 9496 encoding")
    if any(len(uniform) != 112 or len(encoded) != 56 for uniform, encoded in mapped):
        raise ValueError("wrong length for RFC 9496 uniform map vector")
    return generator, rejected, mapped


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError(f"unexpected RFC text SHA-256: {digest}")
    generator, rejected, mapped = rfc_vectors(data)
    base = add(B, B)
    for i, expected in enumerate(generator):
        point = decode(expected)
        if point is None or encode(point) != expected or encode(multiply(base, i)) != expected:
            raise ValueError(f"decaf448 generator case {i} disagrees with affine reference")
    for i, wire in enumerate(rejected):
        if decode(wire) is not None:
            raise ValueError(f"decaf448 invalid encoding {i} accepted by affine reference")
    for i, (uniform, expected) in enumerate(mapped):
        if encode(derive(uniform)) != expected:
            raise ValueError(f"decaf448 uniform map case {i} disagrees with affine reference")
    print(write_module("crypto-advanced", "decaf448-vectors.nra", "Decaf448Vectors", [
        "python3 scripts/crypto-vectors/gen-advanced-decaf448.py",
        "RFC 9496 Appendix B.1 and B.2. Affine reference also checks B.3.",
        "RFC text SHA-256 " + digest,
    ], [
        ("generator", ["encoding"], [[value.hex()] for value in generator]),
        ("rejected", ["encoding"], [[value.hex()] for value in rejected]),
    ]))


if __name__ == "__main__":
    main()
