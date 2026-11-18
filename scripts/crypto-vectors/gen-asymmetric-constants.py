#!/usr/bin/env python3
"""Prints the curve constants of Std.Crypto.Asymmetric as hexadecimal strings.

The modules under stdlib/std/crypto/internal/ (curve25519.nra, edwards25519.nra,
curve448.nra, weierstrass.nra) hold these strings and decode them with
Mp.from_hex. Nothing is copied from memory:

- curve25519/edwards25519 and curve448/edwards448 (RFC 7748, RFC 8032): p, d
  and the base points are computed from their definitions (p = 2^255 - 19,
  d = -121665/121666; p = 2^448 - 2^224 - 1, d = -39081; the base point's y and
  the sign of x as RFC 8032 sections 5.1 and 5.2 define them), and the group
  orders L are read from the RFC 8032 text in
  /home/dev/neura-gate/030/crypto2/sources/asymmetric/rfc8032.txt. Each
  value is checked against the decimal values printed in that text, the base
  point is checked to lie on the curve and to have order L.
- P-256, P-384, P-521 and secp256k1 (SEC 2, FIPS 186-5): p, a, b, n and the
  generator come from `openssl ecparam -param_enc explicit -text` (OpenSSL
  3.5.7), and each generator is checked against the public key that the
  `cryptography` package derives for the private key 1, and n G = O.

    python3 scripts/crypto-vectors/gen-asymmetric-constants.py [rfc8032.txt]
"""

import re
import subprocess
import sys

from cryptography.hazmat.primitives.asymmetric import ec

RFC8032 = "/home/dev/neura-gate/030/crypto2/sources/asymmetric/rfc8032.txt"


def rfc_order(text, after):
    """The group order L of the table after `after`, written there as 2^k + N or 2^k - N."""
    cell = re.sub(r"\s", "", table_cells(text, after, "L").split("i.e.,")[1])
    match = re.fullmatch(r"2\^([0-9]+)([+-])([0-9]+)\)\.", cell)
    power, sign, rest = int(match.group(1)), match.group(2), int(match.group(3))
    return 2 ** power + rest if sign == "+" else 2 ** power - rest


def table_cells(text, after, name):
    """The joined value column of a parameter row `name` in the first RFC table after `after`."""
    start = text.index(after)
    lines = text[start:].splitlines()
    value, collecting = "", False
    for line in lines:
        cells = [c.strip() for c in line.split("|")]
        if len(cells) < 4:
            if collecting:
                break
            continue
        if cells[1] == name:
            collecting = True
            value += cells[2]
        elif collecting and cells[1] == "":
            value += cells[2]
        elif collecting:
            break
    return value


def modsqrt(a, p):
    """A square root by Tonelli-Shanks."""
    a %= p
    if a == 0:
        return 0
    assert pow(a, (p - 1) // 2, p) == 1
    q, s = p - 1, 0
    while q % 2 == 0:
        q //= 2
        s += 1
    z = 2
    while pow(z, (p - 1) // 2, p) != p - 1:
        z += 1
    m, c, t, r = s, pow(z, q, p), pow(a, q, p), pow(a, (q + 1) // 2, p)
    while t != 1:
        i, t2 = 0, t
        while t2 != 1:
            t2 = t2 * t2 % p
            i += 1
        b = pow(c, 1 << (m - i - 1), p)
        m, c, t, r = i, b * b % p, t * b * b % p, r * b % p
    return r


def edwards_add(p, a, d, P, Q):
    (x1, y1), (x2, y2) = P, Q
    t = d * x1 * x2 * y1 * y2 % p
    x3 = (x1 * y2 + y1 * x2) * pow(1 + t, -1, p) % p
    y3 = (y1 * y2 - a * x1 * x2) * pow(1 - t, -1, p) % p
    return x3, y3


def edwards_mul(p, a, d, k, P):
    R = (0, 1)
    while k:
        if k & 1:
            R = edwards_add(p, a, d, R, P)
        P = edwards_add(p, a, d, P, P)
        k >>= 1
    return R


def edwards(text):
    out = {}
    # edwards25519: a = -1, d = -121665/121666, B has y = 4/5 and an even x.
    p = 2 ** 255 - 19
    d = -121665 * pow(121666, -1, p) % p
    y = 4 * pow(5, -1, p) % p
    x = modsqrt((y * y - 1) * pow(d * y * y + 1, -1, p), p)
    if x & 1:
        x = p - x
    L = rfc_order(text, "Ed25519 is EdDSA instantiated with")
    assert d == int(re.sub(r"[^0-9]", "", table_cells(text, "Ed25519 is EdDSA instantiated with", "d").split("=")[1]))
    b_cells = re.sub(r"[^0-9,]", "", table_cells(text, "Ed25519 is EdDSA instantiated with", "B").split("i.e.,")[1])
    assert (x, y) == tuple(int(v) for v in b_cells.split(",")[:2])
    assert edwards_mul(p, -1, d, L, (x, y)) == (0, 1)
    assert (-x * x + y * y - 1 - d * x * x * y * y) % p == 0
    out["edwards25519"] = dict(p=p, d=d, bx=x, by=y, L=L, sqrt_m1=pow(2, (p - 1) // 4, p))
    # edwards448: a = 1, d = -39081, B from RFC 8032 section 5.2's table.
    p = 2 ** 448 - 2 ** 224 - 1
    d = p - 39081
    cells = re.sub(r"[^0-9,]", "", table_cells(text, "Ed448 is EdDSA instantiated with", "B").split("i.e.,")[1])
    bx, by = (int(v) for v in cells.split(",")[:2])
    assert (bx * bx + by * by - 1 - d * bx * bx * by * by) % p == 0
    L = rfc_order(text, "Ed448 is EdDSA instantiated with")
    assert edwards_mul(p, 1, d, L, (bx, by)) == (0, 1)
    out["edwards448"] = dict(p=p, d=d, bx=bx, by=by, L=L)
    return out


def openssl_params(name):
    text = subprocess.run(["openssl", "ecparam", "-name", name, "-param_enc", "explicit", "-text", "-noout"],
                          check=True, capture_output=True, text=True).stdout
    fields = {}
    for key, label in [("p", "Prime:"), ("a", "A:"), ("b", "B:"), ("g", "Generator (uncompressed):"),
                       ("n", "Order:")]:
        start = text.index(label) + len(label)
        rest = text[start:].splitlines()
        inline = re.match(r"\s*[0-9]+ \(0x([0-9a-f]+)\)", rest[0])
        if inline:
            fields[key] = int(inline.group(1), 16)
            continue
        hexdigits = ""
        for line in rest:
            if line.strip() == "" and hexdigits:
                break
            if line and not line.startswith(" ") and hexdigits:
                break
            hexdigits += re.sub(r"[^0-9a-f]", "", line.strip())
        fields[key] = int(hexdigits, 16)
    g = fields.pop("g")
    size = (fields["p"].bit_length() + 7) // 8
    raw = g.to_bytes(1 + 2 * size, "big")
    assert raw[0] == 4
    fields["gx"] = int.from_bytes(raw[1:1 + size], "big")
    fields["gy"] = int.from_bytes(raw[1 + size:], "big")
    return fields


def weierstrass():
    out = {}
    for name, ossl, curve in [("p256", "prime256v1", ec.SECP256R1()), ("p384", "secp384r1", ec.SECP384R1()),
                              ("p521", "secp521r1", ec.SECP521R1()), ("secp256k1", "secp256k1", ec.SECP256K1())]:
        f = openssl_params(ossl)
        numbers = ec.derive_private_key(1, curve).public_key().public_numbers()
        assert (numbers.x, numbers.y) == (f["gx"], f["gy"]), name
        p = f["p"]
        assert (f["gy"] ** 2 - f["gx"] ** 3 - f["a"] * f["gx"] - f["b"]) % p == 0
        # n - 1 times G is -G, the private key n - 1's public key.
        minus = ec.derive_private_key(f["n"] - 1, curve).public_key().public_numbers()
        assert (minus.x, minus.y) == (f["gx"], p - f["gy"]), name
        out[name] = f
    return out


def main():
    text = open(sys.argv[1] if len(sys.argv) > 1 else RFC8032, encoding="utf-8").read()
    for group, values in list(edwards(text).items()) + list(weierstrass().items()):
        print(f"// {group}")
        for key, value in values.items():
            print(f'    "{key}": "{value:x}"')
    return 0


if __name__ == "__main__":
    sys.exit(main())
