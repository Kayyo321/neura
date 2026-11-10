#!/usr/bin/env python3
"""Generates tests/stdlib/big-decimal/support/big-decimal-vectors.nra.

Usage: python3 -I generate.py

Reference results come from Python's decimal module with an exact context
(precision far above every operand) and from fractions.Fraction for the
rounded divisions, independently of the library. Decimal values are written
in Python's own text forms (scientific notation for small and negative
exponents), so the cases also exercise BigDecimal.parse. Expected text uses
the library's plain notation: exactly `scale` fraction digits, or -scale
appended zeros (zero with a negative scale is 0).
"""
import decimal
import math
import os
import random
import struct
from decimal import Decimal
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "support", "big-decimal-vectors.nra")
r = random.Random(20261110)
EXACT = decimal.Context(prec=100000, Emax=decimal.MAX_EMAX, Emin=decimal.MIN_EMIN, traps=[decimal.InvalidOperation])
MODES = [decimal.ROUND_UP, decimal.ROUND_DOWN, decimal.ROUND_CEILING, decimal.ROUND_FLOOR, decimal.ROUND_HALF_UP,
         decimal.ROUND_HALF_DOWN, decimal.ROUND_HALF_EVEN]
B32 = 1 << 32


def parts(d):
    sign, digits, exponent = d.as_tuple()
    unscaled = int("".join(map(str, digits)) or "0")
    return (-unscaled if sign else unscaled), -exponent


def plain(unscaled, scale):
    digits = str(abs(unscaled))
    sign = "-" if unscaled < 0 else ""
    if scale <= 0:
        if unscaled == 0:
            return "0"
        return sign + digits + "0" * (-scale)
    if len(digits) > scale:
        return sign + digits[:-scale] + "." + digits[-scale:]
    return sign + "0." + "0" * (scale - len(digits)) + digits


def text(d):
    return plain(*parts(d))


def make(unscaled, scale):
    return Decimal(unscaled).scaleb(-scale, EXACT) if unscaled else Decimal((0, (0,), -scale))


def random_decimal():
    digits = r.choice([1, 1, 2, 3, 5, 8, 12, 18, 19, 20, 25, 40])
    unscaled = r.randint(0, 10 ** digits - 1)
    if r.random() < 0.15:
        unscaled *= 10 ** r.randint(1, 5)
    if r.random() < 0.5:
        unscaled = -unscaled
    scale = r.choice([0, 0, 1, 2, 3, 5, 8, 12, 20, -1, -3, -8, r.randint(-15, 40)])
    return make(unscaled, scale)


def source_text(d):
    """A spelling for parse: Python's str, or plain text, or an exponent form."""
    choice = r.random()
    if choice < 0.5:
        return str(d)
    if choice < 0.8:
        # Plain text keeps the scale only when it is not negative.
        return text(d) if parts(d)[1] >= 0 else str(d)
    unscaled, scale = parts(d)
    shift = r.randint(-5, 5)
    # unscaled * 10^-scale = unscaled * 10^-(scale + shift) * 10^shift ... keep the
    # digits and move the exponent: digits with (scale + k) fraction digits and
    # exponent k give the same scale.
    k = r.randint(0, 6)
    digits = str(abs(unscaled)).rjust(k + 1, "0")
    point = len(digits) - k
    mantissa = digits[:point] + ("." + digits[point:] if k else "")
    exponent = k - scale
    sign = "-" if unscaled < 0 else ""
    return f"{sign}{mantissa}e{exponent}" if r.random() < 0.5 else f"{sign}{mantissa}E{exponent:+d}"


def round_fraction(value, mode):
    """value rounded to an integer with the decimal rounding mode."""
    floor = math.floor(value)
    if floor == value:
        return floor
    negative = value < 0
    truncated = math.ceil(value) if negative else floor
    rest = abs(value - truncated)
    half = Fraction(1, 2)
    if mode == decimal.ROUND_UP:
        away = True
    elif mode == decimal.ROUND_DOWN:
        away = False
    elif mode == decimal.ROUND_CEILING:
        away = not negative
    elif mode == decimal.ROUND_FLOOR:
        away = negative
    elif mode == decimal.ROUND_HALF_UP:
        away = rest >= half
    elif mode == decimal.ROUND_HALF_DOWN:
        away = rest > half
    else:
        away = rest > half or (rest == half and truncated % 2 != 0)
    if not away:
        return truncated
    return truncated - 1 if negative else truncated + 1


def combine(seed, value):
    return (31 * seed + value) & (B32 - 1)


def big_hash(x):
    h = 0
    m = abs(x)
    limbs = []
    while m:
        limbs.append(m & (B32 - 1))
        m >>= 32
    for limb in reversed(limbs):
        h = combine(h, limb)
    return (-h) & (B32 - 1) if x < 0 else h


def decimal_hash(d):
    unscaled, scale = parts(d)
    if unscaled == 0:
        scale = 0
    else:
        while unscaled % 10 == 0:
            unscaled //= 10
            scale -= 1
    return combine(combine(17, big_hash(unscaled)), scale & (B32 - 1))


def main():
    strings = {}

    # [a, b, a + b, a - b, a * b, compare + 1, hash(a)]
    rows = []
    pairs = [(random_decimal(), random_decimal()) for _ in range(120)]
    pairs += [(Decimal("2.0"), Decimal("2.00")), (Decimal("0.000"), Decimal("0E+3")), (Decimal("-1.5"), Decimal("1.5")),
              (Decimal("1E-40"), Decimal("1E+40")), (Decimal("123.4500"), Decimal("123.45")),
              (Decimal("-0.1"), Decimal("-0.10000000000000000000000000001"))]
    for a, b in pairs:
        compare = (a > b) - (a < b) + 1
        rows += [source_text(a), source_text(b), text(EXACT.add(a, b)), text(EXACT.subtract(a, b)),
                 text(EXACT.multiply(a, b)), str(compare), str(decimal_hash(a))]
    strings["arithmetic_cases"] = (rows, "[a, b, a + b, a - b, a * b, compare(a, b) + 1, hash_code(a)]")

    # [a, b, scale, mode, a / b rounded]
    rows = []
    for _ in range(160):
        a, b = random_decimal(), random_decimal()
        if b == 0:
            b = Decimal("7")
        scale = r.randint(-3, 25)
        mode = r.randrange(7)
        exact = Fraction(a) / Fraction(b) * Fraction(10) ** scale
        q = round_fraction(exact, MODES[mode])
        rows += [source_text(a), source_text(b), str(scale), str(mode), plain(q, scale)]
    for mode in range(7):
        for a, b, scale in [("1", "8", 2), ("-1", "8", 2), ("5", "2", 0), ("-5", "2", 0), ("7", "2", 0), ("-7", "2", 0),
                            ("1", "3", 5), ("-2", "3", 5), ("0", "3", 4), ("1", "-16", 3), ("12345", "1", -2)]:
            exact = Fraction(Decimal(a)) / Fraction(Decimal(b)) * Fraction(10) ** scale
            rows += [a, b, str(scale), str(mode), plain(round_fraction(exact, MODES[mode]), scale)]
    strings["divide_cases"] = (rows, "[a, b, scale, mode ordinal, a / b rounded to scale]")

    # [a, scale, mode, set_scale] and [a, precision, mode, round] and
    # [a, mode, to_big_integer]
    rows = []
    for _ in range(90):
        a = random_decimal()
        scale = r.randint(-8, 30)
        mode = r.randrange(7)
        q = a.quantize(Decimal((0, (1,), -scale)), rounding=MODES[mode], context=EXACT)
        rows += [source_text(a), str(scale), str(mode), text(q)]
    for mode in range(7):
        for a, scale in [("2.5", 0), ("-2.5", 0), ("0.05", 1), ("-0.05", 1), ("1.0000001", 3), ("123.456", -2),
                         ("0.000001", 2), ("-0.000001", 2), ("0.5E-30", 0), ("9.99", 1)]:
            q = Decimal(a).quantize(Decimal((0, (1,), -scale)), rounding=MODES[mode], context=EXACT)
            rows += [a, str(scale), str(mode), text(q)]
    strings["set_scale_cases"] = (rows, "[a, scale, mode ordinal, set_scale(scale, mode)]")
    rows = []
    for _ in range(90):
        a = random_decimal()
        precision = r.randint(0, 25)
        mode = r.randrange(7)
        expected = a if precision == 0 else decimal.Context(prec=precision, rounding=MODES[mode],
                                                            Emax=decimal.MAX_EMAX, Emin=decimal.MIN_EMIN).plus(a)
        rows += [source_text(a), str(precision), str(mode), text(expected)]
    for mode in range(7):
        for a, precision in [("999.9", 3), ("-999.9", 3), ("0.0012345", 2), ("12345", 2), ("1.25", 2), ("-1.35", 2),
                             ("0.000", 1), ("7", 1)]:
            expected = decimal.Context(prec=precision, rounding=MODES[mode]).plus(Decimal(a))
            rows += [a, str(precision), str(mode), text(expected)]
    strings["round_cases"] = (rows, "[a, precision, mode ordinal, round(precision, mode)]")
    rows = []
    for _ in range(50):
        a = random_decimal()
        mode = r.randrange(7)
        q = a.quantize(Decimal(1), rounding=MODES[mode], context=EXACT)
        rows += [source_text(a), str(mode), str(int(q))]
    strings["to_big_integer_cases"] = (rows, "[a, mode ordinal, to_big_integer(mode)]")

    # [a, strip_trailing_zeros, n, pow(n), negate, abs]
    rows = []
    for _ in range(60):
        a = random_decimal()
        stripped = EXACT.normalize(a) if a != 0 else Decimal(0)
        n = r.randint(0, 6)
        unscaled, scale = parts(a)
        rows += [source_text(a), text(stripped), str(n), plain(unscaled ** n, scale * n), text(EXACT.minus(a)), text(EXACT.abs(a))]
    strings["unary_cases"] = (rows, "[a, strip_trailing_zeros, n, pow(n), negate, abs]")

    # [a, float64 bits] for to_float64, and [float64 bits, exact text] for
    # of_float64
    rows = []
    values = [random_decimal() for _ in range(80)]
    values += [Decimal(s) for s in ["0.1", "1E-400", "-1E-400", "1E+400", "-1E+400", "4.9406564584124654E-324",
                                    "2.4703282292062327E-324", "2.4703282292062328E-324", "1.7976931348623157E+308",
                                    "1.7976931348623158E+308", "1.797693134862315807937289714053E+308",
                                    "2.2250738585072011E-308", "0.30000000000000004", "9007199254740993",
                                    "9007199254740993.000000000001", "123456789012345678901234567890.123456789"]]
    for d in values:
        f = float(d)
        rows += [source_text(d), str(struct.unpack("<Q", struct.pack("<d", f))[0])]
    strings["to_float64_cases"] = (rows, "[a, float64 bits of the nearest value]")
    rows = []
    floats = [0.1, 0.5, 2.0, -0.0, 1e20, 1e-320, 5e-324, 1.7976931348623157e308, -123.456, 3.0, 0.25]
    floats += [r.uniform(-1000, 1000) for _ in range(20)] + [math.ldexp(r.random(), r.randint(-1074, 1023)) for _ in range(10)]
    for f in floats:
        rows += [str(struct.unpack("<Q", struct.pack("<d", f))[0]), text(Decimal(f))]
    strings["of_float64_cases"] = (rows, "[float64 bits, exact value as text]")

    lines = [
        "// Generated by tests/stdlib/big-decimal/vectors/generate.py; do not edit.",
        "// Expected values come from Python's decimal and fractions modules.",
        "// Rounding modes are ordinals of RoundingMode: Up, Down, Ceiling, Floor,",
        "// HalfUp, HalfDown, HalfEven.",
        "export let BigDecimalVectors = module (",
    ]
    for name, (rows, doc) in strings.items():
        lines.append(f"    // {doc}")
        lines.append(f"    export let {name} = func() -> string[list] {{")
        lines.append("        let cases: string[list] = [")
        for i, value in enumerate(rows):
            lines.append(f'            "{value}"{"," if i + 1 < len(rows) else ""}')
        lines.append("        ];")
        lines.append("        return cases;")
        lines.append("    };")
        lines.append("")
    lines[-1] = ");"
    with open(OUT, "w") as out:
        out.write("\n".join(lines) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
