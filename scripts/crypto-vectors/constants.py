#!/usr/bin/env python3
"""Prints the constant tables of Std.Crypto in decimal (Neura has no hexadecimal literals).

Each table is computed from its definition, not copied: the SHA-2 words from
the fractional parts of square and cube roots of primes (FIPS 180-4, 4.2 and
5.3), the Keccak round constants from the LFSR of FIPS 202 (3.2.5) and the rho
offsets from 3.2.2, and the AES S-box from the inverse in GF(2^8) and the
affine map (FIPS 197, 5.1.1). The output was pasted into the modules under
stdlib/std/crypto/; rerun it to check them.

    python3 scripts/crypto-vectors/constants.py
"""

from math import isqrt


def primes(count):
    found, n = [], 2
    while len(found) < count:
        if all(n % p for p in found if p * p <= n):
            found.append(n)
        n += 1
    return found


def icbrt(n):
    x = int(round(n ** (1 / 3)))
    while x ** 3 > n:
        x -= 1
    while (x + 1) ** 3 <= n:
        x += 1
    return x


def frac_root(p, bits, root):
    scale = 1 << bits
    if root == 2:
        value = isqrt(p * scale * scale)
    else:
        value = icbrt(p * scale ** 3)
    return value % scale


def keccak_rc():
    def rc(t):
        if t % 255 == 0:
            return 1
        r = 1
        for _ in range(t % 255):
            r <<= 1
            if r & 0x100:
                r ^= 0x171
        return r & 1
    out = []
    for i in range(24):
        value = 0
        for j in range(7):
            value |= rc(j + 7 * i) << ((1 << j) - 1)
        out.append(value)
    return out


def keccak_rho():
    offsets = [0] * 25
    x, y = 1, 0
    for t in range(24):
        offsets[x + 5 * y] = ((t + 1) * (t + 2) // 2) % 64
        x, y = y, (2 * x + 3 * y) % 5
    return offsets


def aes_sbox():
    def mul(a, b):
        r = 0
        while b:
            if b & 1:
                r ^= a
            a = ((a << 1) ^ 0x11B) if a & 0x80 else a << 1
            b >>= 1
        return r
    box = []
    for x in range(256):
        inv = next((y for y in range(1, 256) if mul(x, y) == 1), 0)
        s = inv
        for shift in range(1, 5):
            s ^= ((inv << shift) | (inv >> (8 - shift))) & 0xFF
        box.append(s ^ 0x63)
    return box


def sha512_words(message, iv):
    """FIPS 180-4 SHA-512 over `message` from the words `iv`; checked against hashlib in main."""
    p = primes(80)
    k = [frac_root(q, 64, 3) for q in p[:80]]
    mask = (1 << 64) - 1
    rotr = lambda x, n: ((x >> n) | (x << (64 - n))) & mask
    length = len(message) * 8
    data = message + b"\x80" + b"\x00" * ((111 - len(message)) % 128) + length.to_bytes(16, "big")
    h = list(iv)
    for offset in range(0, len(data), 128):
        w = [int.from_bytes(data[offset + 8 * i:offset + 8 * i + 8], "big") for i in range(16)]
        for t in range(16, 80):
            s0 = rotr(w[t - 15], 1) ^ rotr(w[t - 15], 8) ^ (w[t - 15] >> 7)
            s1 = rotr(w[t - 2], 19) ^ rotr(w[t - 2], 61) ^ (w[t - 2] >> 6)
            w.append((w[t - 16] + s0 + w[t - 7] + s1) & mask)
        a, b, c, d, e, f, g, hh = h
        for t in range(80):
            t1 = (hh + (rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41)) + ((e & f) ^ (~e & g)) + k[t] + w[t]) & mask
            t2 = ((rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39)) + ((a & b) ^ (a & c) ^ (b & c))) & mask
            a, b, c, d, e, f, g, hh = (t1 + t2) & mask, a, b, c, (d + t1) & mask, e, f, g
        h = [(x + y) & mask for x, y in zip(h, [a, b, c, d, e, f, g, hh])]
    return h


def sha512_t_iv(t):
    """The SHA-512/t initial value (FIPS 180-4, 5.3.6)."""
    p = primes(16)
    iv = [frac_root(q, 64, 2) ^ 0xA5A5A5A5A5A5A5A5 for q in p[:8]]
    return sha512_words(f"SHA-512/{t}".encode(), iv)


def show(name, values, per_line=8):
    print(f"{name}:")
    for i in range(0, len(values), per_line):
        print("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")


def main():
    p = primes(80)
    show("SHA-256 K", [frac_root(q, 32, 3) for q in p[:64]])
    show("SHA-256 H0", [frac_root(q, 32, 2) for q in p[:8]])
    show("SHA-224 H0 (low 32 bits of the SHA-384 words)", [frac_root(q, 64, 2) & 0xFFFFFFFF for q in p[8:16]])
    show("SHA-512 K", [frac_root(q, 64, 3) for q in p[:80]], 4)
    show("SHA-512 H0", [frac_root(q, 64, 2) for q in p[:8]], 4)
    show("SHA-384 H0", [frac_root(q, 64, 2) for q in p[8:16]], 4)
    import hashlib
    words = sha512_words(b"abc", [frac_root(q, 64, 2) for q in p[:8]])
    assert b"".join(w.to_bytes(8, "big") for w in words) == hashlib.sha512(b"abc").digest()
    show("SHA-512/256 H0", sha512_t_iv(256), 4)
    show("Keccak RC", keccak_rc(), 4)
    show("Keccak rho", keccak_rho(), 25)
    show("AES S-box", aes_sbox(), 16)


if __name__ == "__main__":
    main()
