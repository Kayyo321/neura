#!/usr/bin/env python3
"""Generates tests/stdlib/big-integer/support/big-integer-vectors.nra.

Usage: python3 -I generate.py

Reference results come from Python's arbitrary-precision int. Numbers are
written as flat uint32 lists: a sign code (0 zero, 1 positive, 2 negative),
a limb count, and the limbs, least significant first. Text cases are
strings. Operands are seeded random values of many sizes, including both
operands at or above the 48-limb Karatsuba threshold, values made of
extreme limbs, and divisions that exercise Knuth's Algorithm D corrections:
qhat corrected by the third limb and the add-back step.
"""
import os
import random
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "support", "big-integer-vectors.nra")
B = 1 << 32
r = random.Random(20261109)

# Divisions found by knuth-search.py, which simulates the library's
# Algorithm D step for step: ADD needs the add-back step (D6), CORR corrects
# qhat at least twice in D3.
KNUTH = [
    (0x100000000fffffffe000000007fffffff7fffffff, 0x17ffffffe8000000100000000),  # ADD
    (0x800000017ffffffefffffffe4a604ee280000001, 0xffffffff00000001fffffffe80000001),  # ADD
    (0x80000000fffffffe00000001800000017fffffff00000000, 0x80000000fffffffe00000002),  # ADD, 5 corrections
    (0x80000000000000027ffffffe800000017ffffffe00000002ffffffff, 0xffffffff00000001fffffffeffffffff),  # ADD
    (0xfffffffe000000007fffffff0000000200000002, 0x10000000000000001d56ffb6f),  # ADD
    (0xfffffffeffffffff000000020000000038ace0e6fffffffe12f4e996, 0x800000007ffffffefffffffe00000000),  # ADD
    (0x800000019212824c7ffffffe80000000, 0x200000002),  # CORR
    (0x17ffffffe800000017ffffffe000000017fffffff, 0x63087e52730f37f17ffffffe),  # CORR
    (0xf0d1ab56ffffffffcfdcc257ffffffff, 0x8000000180000001),  # CORR
    (0x80000000fffffffe80000001fffffffe8000000100000000, 0x207b3de07fffffff804dffe8),  # CORR, 6 corrections
    (0xffffffff7ffffffffffffffe00000002, 0x80000000ffffffff00000002),  # CORR
    (0xfffffffe7fffffff800000007ffffffe, 0x100000002ffffffff),  # CORR
]

SPECIAL_LIMBS = [0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF]


def magnitude(limbs, style=None):
    style = style or r.choice(["random", "random", "random", "extreme", "power", "ones"])
    if limbs == 0:
        return 0
    if style == "power":
        return 1 << r.randint(32 * (limbs - 1), 32 * limbs - 1)
    if style == "ones":
        return (1 << (32 * limbs)) - 1
    values = []
    for i in range(limbs):
        if style == "extreme":
            values.append(r.choice(SPECIAL_LIMBS))
        else:
            values.append(r.getrandbits(32))
    if values[-1] == 0:
        values[-1] = 1
    return sum(v << (32 * i) for i, v in enumerate(values))


def signed(limbs, style=None):
    m = magnitude(limbs, style)
    return -m if r.random() < 0.5 else m


def encode(x):
    if x == 0:
        return [0, 0]
    m = abs(x)
    limbs = []
    while m:
        limbs.append(m & (B - 1))
        m >>= 32
    return [1 if x > 0 else 2, len(limbs)] + limbs


def trunc_divmod(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q, a - q * b


def float_bits(x):
    try:
        f = float(x)
    except OverflowError:
        f = float("inf") if x > 0 else float("-inf")
    return struct.unpack("<Q", struct.pack("<d", f))[0]


def hash_of(x):
    h = 0
    m = abs(x)
    limbs = []
    while m:
        limbs.append(m & (B - 1))
        m >>= 32
    for limb in reversed(limbs):
        h = (31 * h + limb) & (B - 1)
    if x < 0:
        h = (-h) & (B - 1)
    return h


def to_radix(x, radix):
    digits = "0123456789abcdefghijklmnopqrstuvwxyz"
    if x == 0:
        return "0"
    m = abs(x)
    out = []
    while m:
        m, d = divmod(m, radix)
        out.append(digits[d])
    return ("-" if x < 0 else "") + "".join(reversed(out))


SIZES = [0, 1, 1, 2, 2, 3, 4, 5, 7, 9, 12, 16, 20]
BIG_SIZES = [(48, 48), (48, 49), (60, 50), (64, 64), (100, 48), (96, 70), (130, 130), (47, 100), (200, 48), (49, 1)]


def main():
    sections = []

    # add/subtract/compare: [a, b, a + b, a - b, compare]
    data = []
    for _ in range(90):
        a, b = signed(r.choice(SIZES)), signed(r.choice(SIZES))
        if r.random() < 0.2:
            b = -a if r.random() < 0.5 else a
        data += encode(a) + encode(b) + encode(a + b) + encode(a - b) + [(a > b) - (a < b) + 1]
    sections.append(("add_cases", data, "[a, b, a + b, a - b, compare(a, b) + 1]"))

    # multiply: [a, b, a * b]; schoolbook sizes then Karatsuba sizes
    data = []
    for _ in range(60):
        a, b = signed(r.choice(SIZES)), signed(r.choice(SIZES))
        data += encode(a) + encode(b) + encode(a * b)
    sections.append(("multiply_cases", data, "[a, b, a * b], schoolbook sizes"))
    data = []
    for la, lb in BIG_SIZES:
        for style in ["random", "ones"]:
            a, b = signed(la, style), signed(lb, style)
            data += encode(a) + encode(b) + encode(a * b)
    sections.append(("karatsuba_cases", data, "[a, b, a * b], both operands of 48 limbs or more in most cases"))

    # divide: [a, b, trunc quotient, remainder, floor_mod]
    data = []
    divisions = []
    for _ in range(80):
        a, b = signed(r.choice(SIZES + [30, 40])), signed(r.choice(SIZES[1:]))
        if b == 0:
            b = 1
        divisions.append((a, b))
    for la, lb in [(60, 30), (100, 3), (130, 60), (64, 2)]:
        divisions.append((signed(la), signed(lb)))
    for a, b in KNUTH:
        divisions += [(a, b), (-a, b), (a, -b), (-a, -b)]
    for a, b in divisions:
        q, rest = trunc_divmod(a, b)
        data += encode(a) + encode(b) + encode(q) + encode(rest) + encode(a % b)
    sections.append(("divide_cases", data, "[a, b, a / b truncated, remainder, floor_mod(a, b)]"))

    # shifts: [a, n, a << n, floor(a / 2^n)]
    data = []
    for _ in range(70):
        a = signed(r.choice(SIZES))
        n = r.choice([0, 1, 5, 31, 32, 33, 63, 64, 65, 100, r.randint(0, 700)])
        data += encode(a) + [n] + encode(a << n) + encode(a >> n)
    sections.append(("shift_cases", data, "[a, n, a * 2^n, floor(a / 2^n)]"))

    # pow: [a, e, a^e]
    data = []
    for a, e in [(0, 0), (0, 5), (1, 1000), (-1, 999), (-1, 1000), (2, 64), (-3, 41), (10, 30), (7, 0)]:
        data += encode(a) + [e] + encode(a ** e)
    for _ in range(20):
        a = signed(r.choice([1, 2, 3]))
        e = r.randint(0, 12)
        data += encode(a) + [e] + encode(a ** e)
    sections.append(("pow_cases", data, "[a, e, a^e]"))

    # gcd: [a, b, gcd]
    import math
    data = []
    for _ in range(40):
        g = magnitude(r.choice([0, 1, 2, 3]))
        a = signed(r.choice(SIZES[:10])) * (g or 1)
        b = signed(r.choice(SIZES[:10])) * (g or 1)
        data += encode(a) + encode(b) + encode(math.gcd(a, b))
    data += encode(0) + encode(0) + encode(0)
    sections.append(("gcd_cases", data, "[a, b, gcd(a, b)]"))

    # mod_pow: [a, e, m, a^e mod m]
    data = []
    for _ in range(30):
        a = signed(r.choice([0, 1, 2, 4, 8]))
        e = magnitude(r.choice([0, 1, 2, 3]))
        m = magnitude(r.choice([1, 2, 4, 8])) or 1
        data += encode(a) + encode(e) + encode(m) + encode(pow(a, e, m))
    for a, e, m in [(5, 0, 1), (5, 0, 7), (0, 0, 7), (-3, 3, 7), (2, 10, 1024), (2, 10, 1025)]:
        data += encode(a) + encode(e) + encode(m) + encode(pow(a, e, m))
    sections.append(("mod_pow_cases", data, "[a, e, m, a^e mod m]"))

    # mod_inverse: [a, m, flag, inverse]; flag 0 means no inverse (nil).
    data = []
    for _ in range(30):
        a = signed(r.choice([0, 1, 2, 3, 6]))
        m = magnitude(r.choice([1, 2, 4])) or 1
        if r.random() < 0.3:
            g = magnitude(1) | 2
            a, m = a * g, m * g
        try:
            inv = pow(a, -1, m)
            flag = 1
        except ValueError:
            inv, flag = 0, 0
        data += encode(a) + encode(m) + [flag] + encode(inv)
    for a, m in [(3, 1), (0, 1), (0, 7), (10, 4), (-1, 5)]:
        try:
            inv, flag = pow(a, -1, m), 1
        except ValueError:
            inv, flag = 0, 0
        data += encode(a) + encode(m) + [flag] + encode(inv)
    sections.append(("mod_inverse_cases", data, "[a, m, 1 when an inverse exists, inverse]"))

    # conversions: [a, float bits low, float bits high, hash, bit_length,
    # int64 flag, int64 low, int64 high]
    data = []
    values = [0, 1, -1, 2 ** 53, 2 ** 53 + 1, 2 ** 53 + 2, 2 ** 53 + 3, 2 ** 54 + 2, 2 ** 54 + 6, 2 ** 63 - 1, 2 ** 63,
              -(2 ** 63), -(2 ** 63) - 1, 2 ** 64 - 1, 2 ** 64, (2 ** 53 - 1) * 2 ** 971, 2 ** 1024 - 2 ** 970,
              2 ** 1024 - 2 ** 971, 2 ** 1024, -(2 ** 1024 - 2 ** 970), (2 ** 53 + 1) * 2 ** 100, (2 ** 53 + 1) * 2 ** 100 + 1,
              -((2 ** 53 + 1) * 2 ** 100 + 1), 2 ** 3000]
    values += [signed(r.choice(SIZES[1:] + [30, 33, 40])) for _ in range(50)]
    for x in values:
        fb = float_bits(x)
        in64 = -(2 ** 63) <= x < 2 ** 63
        v64 = x & (2 ** 64 - 1) if in64 else 0
        data += encode(x) + [fb & (B - 1), fb >> 32, hash_of(x), abs(x).bit_length(), 1 if in64 else 0,
                             v64 & (B - 1), v64 >> 32]
    sections.append(("conversion_cases", data,
                     "[a, float64 bits low, high, hash_code, bit_length, 1 when it fits int64, int64 bits low, high]"))

    # test_bit: [a, n, bit of |a|]
    data = []
    for _ in range(40):
        a = signed(r.choice(SIZES))
        n = r.randint(0, 32 * 21)
        data += encode(a) + [n, (abs(a) >> n) & 1]
    sections.append(("bit_cases", data, "[a, n, bit n of |a|]"))

    # text: decimal and other radices
    texts = []
    for x in [0, 1, -1, 999999999, 1000000000, -1000000000, 10 ** 18, 10 ** 27 - 1, 2 ** 64, -(2 ** 127)] + \
            [signed(r.choice(SIZES[1:] + [25])) for _ in range(30)]:
        texts.append((x, 10, str(x)))
    for radix in [2, 3, 7, 16, 35, 36]:
        for x in [0, -1, radix, radix ** 7 - 1] + [signed(r.choice([1, 2, 3, 5, 8])) for _ in range(5)]:
            texts.append((x, radix, to_radix(x, radix)))
    data = []
    for x, radix, text in texts:
        data += encode(x) + [radix]
    sections.append(("text_values", data, "[a, radix] for text_strings"))

    lines = [
        "// Generated by tests/stdlib/big-integer/vectors/generate.py; do not edit.",
        "// Expected values come from Python's int. A number is a sign code (0 zero,",
        "// 1 positive, 2 negative), a limb count, and the limbs, least significant first.",
        "export let BigIntegerVectors = module (",
    ]
    for name, data, doc in sections:
        lines.append(f"    // {doc}")
        lines.append(f"    export let {name} = func() -> uint32[list] {{")
        lines.append("        let cases: uint32[list] = [")
        for i in range(0, len(data), 8):
            row = ", ".join(str(v) for v in data[i:i + 8])
            lines.append(f"            {row}{',' if i + 8 < len(data) else ''}")
        lines.append("        ];")
        lines.append("        return cases;")
        lines.append("    };")
        lines.append("")
    lines.append("    // The text of each text_values case in its radix.")
    lines.append("    export let text_strings = func() -> string[list] {")
    lines.append("        let cases: string[list] = [")
    for i, (x, radix, text) in enumerate(texts):
        lines.append(f'            "{text}"{"," if i + 1 < len(texts) else ""}')
    lines.append("        ];")
    lines.append("        return cases;")
    lines.append("    };")
    lines.append(");")
    with open(OUT, "w") as out:
        out.write("\n".join(lines) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
