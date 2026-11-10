#!/usr/bin/env python3
"""Generates tests/stdlib/math/support/math-vectors.nra.

Usage: python3 -I generate.py <scratch-directory>

For every Std.Math.Math function the module lists cases as flat uint64 bit
patterns: the argument(s), the result of fdlibm.c (the C transliteration of
fdlibm 5.3 in this directory, compiled with -ffp-contract=off), and the
result of the platform libm through Python's math module. NaN results are
written as the canonical NaN 0x7FF8000000000000, which is what the Neura port
must produce. The cases are fdlibm's documented special values, branch
boundaries, and seeded random arguments.

The script also prints, per function, how many port results equal libm and
the largest difference in ulps; the summary is copied into the generated
file's header.
"""
import ctypes
import math
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "support", "math-vectors.nra")
CANONICAL_NAN = 0x7FF8000000000000


def bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def frombits(b):
    return struct.unpack("<d", struct.pack("<Q", b))[0]


def canonical(x):
    return CANONICAL_NAN if x != x else bits(x)


def ordered(b):
    return -(b & 0x7FFFFFFFFFFFFFFF) if b >> 63 else b


def ulp_distance(a, b):
    if a == b:
        return 0
    if a == CANONICAL_NAN or b == CANONICAL_NAN:
        return 1 << 62
    return abs(ordered(a) - ordered(b))


INF = float("inf")
NAN = float("nan")
MIN_SUB = frombits(1)
MAX_SUB = frombits(0x000FFFFFFFFFFFFF)
MIN_NORM = frombits(0x0010000000000000)
MAX = frombits(0x7FEFFFFFFFFFFFFF)
NEG_NAN = frombits(0xFFF8000000000000)
PAYLOAD_NAN = frombits(0x7FF0000000000001)
COMMON = [0.0, -0.0, INF, -INF, NAN, NEG_NAN, PAYLOAD_NAN, MIN_SUB, -MIN_SUB, MAX_SUB, -MAX_SUB,
          MIN_NORM, -MIN_NORM, MAX, -MAX, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, 10.0, 1e-300, 1e300,
          -1e300, 0.1, -0.1, 100.0]


def around(b):
    """The float with bit pattern b and its two neighbours."""
    return [frombits(b - 1), frombits(b), frombits(b + 1)]


def high(h):
    """Values whose high word is h, for fdlibm's high-word thresholds."""
    return [frombits(((h - 1) << 32) | 0xFFFFFFFF), frombits(h << 32), frombits((h << 32) | 1)]


HALF_PI = [frombits(0x3FF921FB54442D18) * n for n in range(1, 40)]
HUGE_TRIG = [1e22, 1e100, 2.0 ** 120, 2.0 ** 1023, MAX, 6381956970095103 * 2.0 ** 797,
             5.319372648326541e+255, 1.0e308, frombits(0x413921FB54442D18), frombits(0x413921FC54442D18)]

SPECIAL = {
    "sqrt": [4.0, 9.0, 0.25, 1e-310, 2.0 ** -1074, 2.0 ** -1073, 2.0 ** 1023, 1.5, 1 + 2.0 ** -52],
    "cbrt": [8.0, -8.0, 27.0, -27.0, 1e-310, -1e-310, 64.0, 0.001, 1e-320],
    "exp": around(0x40862E42FEFA39EF) + around(0xC0874910D52D3051) + high(0x40862E42) + high(0x3fd62e42)
           + high(0x3FF0A2B2) + high(0x3e300000) + [-708.4, -709.0, -745.0, -745.2, 709.0, 1e-20, -1e-20],
    "expm1": high(0x4043687A) + high(0x40862E42) + around(0x40862E42FEFA39EF) + high(0x3fd62e42)
             + high(0x3FF0A2B2) + high(0x3c900000) + [-40.0, -38.0, 1e-20, -1e-20, 13.9, 39.0, 40.0, 0.25, -0.3],
    "log": [frombits(0x3FF0000000000001), frombits(0x3FEFFFFFFFFFFFFF), math.e, 1e-310, 1e-320, -1e-310]
           + high(0x3FF00000) + high(0x3FEFFFFF),
    "log1p": [-1.0, -1.5, -0.2929, 0.41422, 2.0 ** -54, -2.0 ** -54, 2.0 ** -29, 2.0 ** -30, 1e300, 2.0 ** 53]
             + high(0x3FDA827A) + high(0x3e200000) + high(0x3c900000) + high(0x43400000)
             + [frombits(0xBFD2BEC300000000), frombits(0xBFD2BEC400000000), frombits(0xBFD2BEC2FFFFFFFF)],
    "log10": [10.0 ** k for k in range(-5, 23)] + [1e-310, 1e-320],
    "log2": [2.0 ** k for k in range(-1074, 1024, 97)] + [3.0, 5.0, 0.75, 1e-310, 1e-320, 1 + 2.0 ** -52],
    "sin": HALF_PI + HUGE_TRIG + high(0x3fe921fb) + high(0x4002d97c) + high(0x3e400000)
           + [frombits(0x3FF921FB54442D18), -frombits(0x3FF921FB54442D18), 1e-300],
    "cos": HALF_PI + HUGE_TRIG + high(0x3fe921fb) + high(0x4002d97c) + high(0x3e400000) + high(0x3FD33333)
           + high(0x3fe90000) + [frombits(0x3FF921FB54442D18), 1e-300],
    "tan": HALF_PI + HUGE_TRIG + high(0x3fe921fb) + high(0x3FE59428) + high(0x3e300000)
           + [frombits(0x3FF921FB54442D18), -frombits(0x3FF921FB54442D18), 0.6744, -0.6744, 1e-300],
    "asin": [0.975, -0.975, 0.9999999999999999, -0.9999999999999999, 1e-300, 1.0000000000000002]
            + high(0x3fe00000) + high(0x3FEF3333) + high(0x3e400000),
    "acos": [0.975, -0.975, 0.9999999999999999, -0.9999999999999999, 1e-300, 1.0000000000000002]
            + high(0x3fe00000) + high(0x3c600000),
    "atan": [0.4375, 11 / 16, 19 / 16, 2.4375, 2.0 ** 66, -2.0 ** 66, 1e300]
            + high(0x3fdc0000) + high(0x3fe60000) + high(0x3ff30000) + high(0x40038000) + high(0x44100000)
            + high(0x3e200000),
    "sinh": [22.0, -22.0, 709.78, -710.0, 710.4758600739439, -710.4758600739439, 711.0]
            + around(0x408633CE8FB9F87D) + high(0x40360000) + high(0x40862E42) + high(0x3e300000)
            + high(0x3ff00000),
    "cosh": [22.0, -22.0, 709.78, -710.0, 710.4758600739439, 711.0]
            + around(0x408633CE8FB9F87D) + high(0x40360000) + high(0x40862E42) + high(0x3fd62e43)
            + high(0x3c800000),
    "tanh": [22.0, -22.0, 30.0, 1.0, -1.0] + high(0x40360000) + high(0x3c800000) + high(0x3ff00000),
    "to_radians": [180.0, 90.0, -45.0, 360.0, 1e308],
    "to_degrees": [frombits(0x400921FB54442D18), 1.0, -0.5, 1e307],
}

PAIRS = [0.0, -0.0, 1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 3.0, -3.0, INF, -INF, NAN, MIN_SUB, MAX, -MAX, 1e-300, 1e300]
POW_SPECIAL = [(x, y) for x in PAIRS for y in PAIRS] + [
    (-8.0, 1.0 / 3.0), (-2.0, 3.0), (-2.0, 4.0), (-2.0, 0.5), (2.0, -1074.0), (2.0, -1075.0), (2.0, -1076.0),
    (2.0, 1023.0), (2.0, 1024.0), (-2.0, 1023.0), (-2.0, -1075.0), (0.5, 1074.0), (10.0, 308.0), (10.0, 309.0),
    (10.0, -323.0), (10.0, -324.0), (1.0000000001, 2.0 ** 40), (0.9999999999, 2.0 ** 40), (1.0000000001, -2.0 ** 40),
    (1.0000000001, 2.0 ** 65), (0.9999999999, 2.0 ** 65), (1.5, 2.0 ** 65), (0.5, -2.0 ** 65),
    (-1.0000000001, 2.0 ** 40 + 1), (-1.0, 2.0 ** 60), (-1.0, 3.0), (-1.0, 0.5), (4.0, 0.5), (-4.0, 0.5),
    (-0.0, 0.5), (MIN_SUB, 0.5), (MAX, 0.5), (MIN_SUB, -1.0), (2.0, 2.0), (-3.0, 2.0), (1e-310, 2.0),
    (2.0, 1024.0 - 2.0 ** -43), (2.0, -1075.0 + 2.0 ** -43), (frombits(0x3FEFFFFFFFFFFFFF), 2.0 ** 62),
    (frombits(0x3FF0000000000001), -2.0 ** 62), (frombits(0x3FEFFFFF00000000), 2.0 ** 50),
    (-8.0, 2.0 ** 52 + 1), (-8.0, 2.0 ** 53), (-8.0, 2.0 ** 52 - 1), (-8.0, 2.0 ** 21 + 1), (-8.0, 2.0 ** 21 + 0.5),
    (-0.5, -3.0), (-0.5, 3.0), (-0.0, -3.0), (-0.0, -4.0), (-0.0, 3.0), (-INF, 3.0), (-INF, -3.0), (-INF, 4.0),
]
ATAN2_SPECIAL = [(y, x) for y in PAIRS for x in PAIRS] + [(1e300, 1e-300), (1e-300, 1e300), (-1e-300, -1e300),
                                                         (1e-300, -1e300), (1.0, 1e-300), (3.0, 1.0)]
HYPOT_SPECIAL = [(x, y) for x in PAIRS for y in PAIRS] + [
    (3.0, 4.0), (1e300, 1e300), (1e-300, 1e-300), (MIN_SUB, MIN_SUB), (MAX_SUB, MIN_SUB), (1e308, 1e308),
    (1e200, 1.0), (1.0, 1e-200), (1e-310, 3e-310), (2.0 ** 500, 2.0 ** 440), (2.0 ** -500, 2.0 ** -560),
    (INF, PAYLOAD_NAN), (PAYLOAD_NAN, -INF), (5e-324, 1e-320), (3e-170, 4e-170), (1.5, 1.5), (1e10, 1.0)]


def lin(r, lo, hi):
    return lambda: r.uniform(lo, hi)


def logu(r, lo, hi, signed=True):
    return lambda: (r.choice([-1.0, 1.0]) if signed else 1.0) * math.exp(r.uniform(math.log(lo), math.log(hi)))


def build(scratch):
    os.makedirs(scratch, exist_ok=True)
    library = os.path.join(scratch, "libfdlibm.so")
    subprocess.check_call(["cc", "-O0", "-ffp-contract=off", "-fno-builtin", "-shared", "-fPIC", "-o", library,
                           os.path.join(HERE, "fdlibm.c")])
    lib = ctypes.CDLL(library)
    for name in ["sqrt", "cbrt", "exp", "expm1", "log", "log1p", "log10", "log2", "sin", "cos", "tan", "asin", "acos",
                 "atan", "sinh", "cosh", "tanh"]:
        f = getattr(lib, "fd_" + name)
        f.restype = ctypes.c_double
        f.argtypes = [ctypes.c_double]
    for name in ["pow", "atan2", "hypot"]:
        f = getattr(lib, "fd_" + name)
        f.restype = ctypes.c_double
        f.argtypes = [ctypes.c_double, ctypes.c_double]
    return lib


def main():
    lib = build(sys.argv[1])
    r = random.Random(20261109)
    count = 160
    ranges = {
        "sqrt": [logu(r, 1e-310, 1e308, False)],
        "cbrt": [logu(r, 1e-310, 1e308)],
        "exp": [lin(r, -745.2, 709.8), lin(r, -2, 2), logu(r, 1e-12, 1)],
        "expm1": [lin(r, -40, 709.8), logu(r, 1e-12, 2)],
        "log": [logu(r, 1e-310, 1e308, False), lin(r, 0.5, 2)],
        "log1p": [lin(r, -0.999, 10), logu(r, 1e-12, 1e100, False)],
        "log10": [logu(r, 1e-310, 1e308, False), lin(r, 0.5, 2)],
        "log2": [logu(r, 1e-310, 1e308, False), lin(r, 0.5, 2)],
        "sin": [lin(r, -10, 10), logu(r, 1e-8, 1e300)],
        "cos": [lin(r, -10, 10), logu(r, 1e-8, 1e300)],
        "tan": [lin(r, -10, 10), logu(r, 1e-8, 1e300)],
        "asin": [lin(r, -1, 1), logu(r, 1e-10, 1)],
        "acos": [lin(r, -1, 1), logu(r, 1e-10, 1)],
        "atan": [lin(r, -10, 10), logu(r, 1e-10, 1e20)],
        "sinh": [lin(r, -30, 30), logu(r, 1e-10, 711)],
        "cosh": [lin(r, -30, 30), logu(r, 1e-10, 711)],
        "tanh": [lin(r, -5, 5), logu(r, 1e-10, 30)],
        "to_radians": [lin(r, -720, 720)],
        "to_degrees": [lin(r, -10, 10)],
    }
    libm = {"sqrt": math.sqrt, "cbrt": math.cbrt, "exp": math.exp, "expm1": math.expm1, "log": math.log,
            "log1p": math.log1p, "log10": math.log10, "log2": math.log2, "sin": math.sin, "cos": math.cos,
            "tan": math.tan, "asin": math.asin, "acos": math.acos, "atan": math.atan, "sinh": math.sinh,
            "cosh": math.cosh, "tanh": math.tanh, "pow": math.pow, "atan2": math.atan2, "hypot": math.hypot}

    def call_libm(f, *args):
        # Python's math raises where C's libm returns a value; map those
        # cases back to the C results (C99 Annex F).
        try:
            return f(*args)
        except (ValueError, ZeroDivisionError):
            if f in (math.log, math.log10, math.log2) and args[0] == 0:
                return -INF
            if f is math.log1p and args[0] == -1:
                return -INF
            if f is math.pow and args[0] == 0 and args[1] < 0:
                y = args[1]
                odd = y == int(y) and int(y) % 2 == 1
                return math.copysign(INF, args[0]) if odd else INF
            return NAN
        except OverflowError:
            # math raises where C returns +-inf; pow and the hyperbolics keep their sign.
            if f in (math.pow,):
                x, y = args
                negative = x < 0 and y == int(y) and int(y) % 2 == 1
                return -INF if negative else INF
            if f is math.sinh:
                return math.copysign(INF, args[0])
            return INF

    sections = []
    summary = []
    documented = []
    for name in ["sqrt", "cbrt", "exp", "expm1", "log", "log1p", "log10", "log2", "sin", "cos", "tan", "asin",
                 "acos", "atan", "sinh", "cosh", "tanh", "to_radians", "to_degrees"]:
        args = list(COMMON) + SPECIAL.get(name, [])
        for gen in ranges[name]:
            args += [gen() for _ in range(count)]
        flat = []
        same = 0
        worst = 0
        for x in args:
            if name == "to_radians":
                port = x * frombits(0x3F91DF46A2529D39)
                ref = math.radians(x)
            elif name == "to_degrees":
                port = x * frombits(0x404CA5DC1A63C1F8)
                ref = math.degrees(x)
            else:
                port = getattr(lib, "fd_" + name)(x)
                ref = call_libm(libm[name], x)
            p, l = canonical(port), canonical(ref)
            flat += [bits(x), p, l]
            distance = ulp_distance(p, l)
            same += distance == 0
            worst = max(worst, distance)
        summary.append((name, len(args), same, worst))
        sections.append((name, 1, flat, worst))
    for name, specials, gens in [
        ("pow", POW_SPECIAL, [(lin(r, 0, 10), lin(r, -50, 50)), (lin(r, 0.9, 1.1), lin(r, -1000, 1000)),
                              (lin(r, -10, 0), lambda: float(r.randint(-60, 60))),
                              (logu(r, 1e-300, 1e300, False), lin(r, -1, 1))]),
        ("atan2", ATAN2_SPECIAL, [(lin(r, -10, 10), lin(r, -10, 10)), (logu(r, 1e-200, 1e200), logu(r, 1e-200, 1e200))]),
        ("hypot", HYPOT_SPECIAL, [(lin(r, -1e10, 1e10), lin(r, -1e3, 1e3)), (logu(r, 1e-300, 1e300), logu(r, 1e-300, 1e300))]),
    ]:
        pairs = list(specials)
        for gx, gy in gens:
            pairs += [(gx(), gy()) for _ in range(count)]
        if name == "pow":
            # |y| > 2^31 with x within a few thousand ulps of 1 (the series
            # branch), and results in the subnormal range (scalbn).
            for _ in range(40):
                x = 1 + (r.randint(-4096, 4096) or 1) * 2.0 ** -52
                pairs.append((x, r.choice([-1.0, 1.0]) * r.uniform(2.0 ** 31, 2.0 ** 42)))
            for _ in range(40):
                x = r.uniform(0.1, 0.9)
                pairs.append((x, r.uniform(-1074, -1023) * math.log(2) / math.log(x)))
        flat = []
        same = 0
        worst = 0
        for x, y in pairs:
            port = getattr(lib, "fd_" + name)(x, y)
            ref = call_libm(libm[name], x, y)
            p, l = canonical(port), canonical(ref)
            flat += [bits(x), bits(y), p, l]
            distance = ulp_distance(p, l)
            same += distance == 0
            if name == "pow" and abs(x) == 1 and (math.isinf(y) or y != y):
                # fdlibm 5.3: pow(+-1, +-inf) and pow(1, NaN) are NaN; C99
                # libm returns 1. The test skips the libm comparison here.
                documented.append((x, y))
                continue
            worst = max(worst, distance)
        summary.append((name, len(pairs), same, worst))
        sections.append((name, 2, flat, worst))

    lines = [
        "// Generated by tests/stdlib/math/vectors/generate.py; do not edit.",
        "//",
        "// For each Std.Math.Math function, cases as flat uint64 bit patterns:",
        "// the argument(s), the fdlibm 5.3 result (tests/stdlib/math/vectors/fdlibm.c),",
        "// and the platform libm result (Python's math module). NaN results are the",
        "// canonical NaN. `<name>_libm_ulps` is the largest distance in ulps between",
        "// the fdlibm and libm columns over these cases, leaving out pow(+-1, +-inf)",
        "// and pow(1, NaN): fdlibm 5.3 returns NaN there and C99 libm returns 1.",
        "//",
        "// Agreement with the platform libm (cases, equal, largest distance in ulps):",
    ]
    for name, n, same, worst in summary:
        lines.append(f"//   {name}: {n} cases, {same} equal, {worst} ulp")
    lines.append("export let MathVectors = module (")
    for name, arity, flat, worst in sections:
        lines.append(f"    // [{'x, y' if arity == 2 else 'x'}, fdlibm, libm] per case.")
        lines.append(f"    export let {name}_cases = func() -> uint64[list] {{")
        lines.append("        let cases: uint64[list] = [")
        width = 3 + arity - 1
        for i in range(0, len(flat), width):
            row = ", ".join(str(v) for v in flat[i:i + width])
            comma = "," if i + width < len(flat) else ""
            lines.append(f"            {row}{comma}")
        lines.append("        ];")
        lines.append("        return cases;")
        lines.append("    };")
        lines.append("")
        lines.append(f"    export let {name}_libm_ulps = func() -> uint64 {{")
        lines.append(f"        return {worst};")
        lines.append("    };")
        lines.append("")
    lines[-1] = ");"
    with open(OUT, "w") as out:
        out.write("\n".join(lines) + "\n")
    for name, n, same, worst in summary:
        print(f"{name}: {n} cases, {same} equal to libm, largest distance {worst} ulp")
    print("documented fdlibm/C99 differences (pow, libm comparison skipped):", documented)


if __name__ == "__main__":
    main()
