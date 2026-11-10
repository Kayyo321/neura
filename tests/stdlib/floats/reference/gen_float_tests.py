#!/usr/bin/env python3
"""Writes tests/stdlib/floats/<type>-tests.nra from the Fraction reference.

Run: python3 -I tests/stdlib/floats/reference/gen_float_tests.py [type ...]
"""
import math
import random
import sys
from fractions import Fraction

import os
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import floatref as fr  # noqa: E402

TYPES = {
    "float16": ("Float16", "float16", "uint16"),
    "bfloat16": ("BFloat16", "b-float16", "uint16"),
    "float32": ("Float32", "float32", "uint32"),
    "float64": ("Float64", "float64", "uint64"),
}


def nra_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def array(items, per_line=8):
    items = list(items)
    if not items:
        return "[]"
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("        " + ", ".join(items[i:i + per_line]))
    return "[\n" + ",\n".join(lines) + "\n    ]"


def decl(name, t, items, per_line=8):
    return f"    let {name}: {t}[] = {array(items, per_line)};\n"


def b(x):
    return "true" if x else "false"


def decimal_text(q, extra=0):
    """Exact decimal text of q (denominator 2^a 5^b) with `extra` more zeros."""
    neg = q < 0
    q = abs(q)
    den = q.denominator
    k = 0
    d = den
    while d % 2 == 0:
        d //= 2
    while d % 5 == 0:
        d //= 5
    assert d == 1
    while (10 ** k) % den != 0:
        k += 1
    n = q.numerator * (10 ** k // den)
    s = str(n)
    if k == 0:
        out = s
    else:
        s = s.rjust(k + 1, "0")
        out = s[:-k] + "." + s[-k:]
    if extra:
        if "." not in out:
            out += "."
        out += "0" * extra
    return ("-" if neg else "") + out


GUARD_CASES = {
    "float16": ["6.67870044708252e-5", "7.25090503692627e-5", "8.58604907989502e-5", "9.91523265838623e-5"],
    "bfloat16": ["1.20508047984913e-10", "3.12385750947386e-18", "5.45905565419731e24", "1.74782900098398e21"],
    "float32": ["5.26404068315958e34", "3.44457885511351e35", "7.19061903655529e-2", "1.63031084086675e32"],
}


def significant_text(q, n):
    """q rounded to n significant digits (ties to even) as d.ddde<k>."""
    k = fr.decimal_exponent(q)
    m = fr.round_half_even(q / Fraction(10) ** (k - n + 1))
    if m == 10 ** n:
        m //= 10
        k += 1
    digits = str(m)
    return digits[0] + "." + digits[1:] + "e" + str(k)


def corpus(fmt, rng):
    vals = []
    mag_max = fmt.max_bits
    one = fmt.bias << fmt.f
    min_normal = 1 << fmt.f
    vals += [0, fmt.sign, fmt.inf, fmt.inf | fmt.sign, fmt.nan, fmt.nan | fmt.sign, fmt.nan | 1,
             fmt.exp_all | 1]
    vals += [1, 2, 3, min_normal - 1, min_normal, min_normal + 1, one - 1, one, one + 1,
             mag_max, mag_max - 1, fmt.sign | 1, fmt.sign | mag_max, fmt.sign | one]
    # Powers of two across the range.
    lo = fmt.emin - fmt.f
    hi = fmt.emax
    step = max(1, (hi - lo) // 40)
    for k in list(range(lo, hi + 1, step)) + [hi, -1, 1, 10]:
        vals.append(fmt.round_fraction(Fraction(2) ** k))
    # Powers of ten (nearest values) across the range.
    klo = math.floor((fmt.emin - fmt.f) * 0.30103)
    khi = math.floor((fmt.emax + 1) * 0.30103)
    kstep = max(1, (khi - klo) // 30)
    for k in list(range(klo, khi + 1, kstep)) + [-1, 1, 2, 3]:
        bits = fmt.round_fraction(Fraction(10) ** k)
        if fmt.classify(bits) == "finite" and bits != 0:
            vals.append(bits)
    # Familiar decimals and halves.
    for text in ["0.1", "0.2", "0.3", "0.5", "1.5", "2.5", "-2.5", "3.5", "0.25", "123.456", "-7.75",
                 "1e-3", "4.35", "999.5", "1024.75"]:
        vals.append(fr.parse(fmt, text)[1])
    # Seeded random finite patterns.
    count = 0
    while count < 40:
        bits = rng.getrandbits(fmt.width)
        if fmt.classify(bits) == "finite":
            vals.append(bits)
            count += 1
    out = []
    for v in vals:
        if v not in out:
            out.append(v)
    return out


def rounding_ref(fmt, bits, mode):
    kind = fmt.classify(bits)
    if kind == "nan":
        return fmt.nan
    if kind == "inf":
        return bits
    v = fmt.value(bits)
    neg = bool(bits & fmt.sign)
    if mode == 0:
        r = math.floor(v)
    elif mode == 1:
        r = math.ceil(v)
    elif mode == 2:
        r = math.trunc(v)
    elif mode == 3:
        a = abs(v)
        r = math.floor(a + Fraction(1, 2))
        r = -r if v < 0 else r
    else:
        a = abs(v)
        r = fr.round_half_even(a)
        r = -r if v < 0 else r
    out = fmt.round_fraction(Fraction(r))
    assert fmt.value(out) == r
    if r == 0 and neg:
        out |= fmt.sign
    return out


def hash_ref(fmt, bits):
    if fmt.classify(bits) == "nan":
        bits = fmt.nan
    if fmt.width == 64:
        return (bits & 0xFFFFFFFF) ^ (bits >> 32)
    return bits


def total_key(fmt, bits):
    kind = fmt.classify(bits)
    if kind == "nan":
        return (2, 0)
    if kind == "inf":
        return (1 if not bits & fmt.sign else -1, 0)
    v = fmt.value(bits)
    neg = 1 if bits & fmt.sign else 0
    return (0, v, -neg if v == 0 else 0)


def parse_corpus(fmt, vals, rng):
    inputs = []
    for v in vals:
        inputs.append(fr.to_string(fmt, v))
    # Midpoints between neighbours, exact and nudged either way.
    picks = [0, 1, 2, (1 << fmt.f) - 1, 1 << fmt.f, fmt.bias << fmt.f, (fmt.bias << fmt.f) + 1,
             fmt.max_bits - 1]
    for _ in range(12):
        picks.append(rng.randrange(0, fmt.max_bits))
    for lo_bits in picks:
        lo = fmt.value(lo_bits)
        hi = fmt.value(lo_bits + 1)
        mid = (lo + hi) / 2
        text = decimal_text(mid)
        inputs.append(text)
        digits = len(text.split(".")[1]) if "." in text else 0
        tiny = Fraction(1, 10 ** (digits + 3))
        inputs.append(decimal_text(mid + tiny))
        inputs.append(decimal_text(mid - tiny))
        inputs.append("-" + text)
    # Midpoints rounded to 15 and 14 significant digits: short enough for
    # the float64 fast path, and within its rounding error of a midpoint at
    # large and small magnitudes.
    for _ in range(16):
        lo_bits = rng.randrange(1 << fmt.f, fmt.max_bits)
        mid = (fmt.value(lo_bits) + fmt.value(lo_bits + 1)) / 2
        for n in (15, 14):
            inputs.append(significant_text(mid, n))
    # Inputs that the float64 fast path would round wrongly without its
    # midpoint guard (found by a seeded search over 13- to 15-digit roundings
    # of midpoints; see find_fast.py beside this script).
    inputs += GUARD_CASES.get(fmt.name, [])
    # Overflow boundary: max + half ulp, and either side of it.
    top = fmt.value(fmt.max_bits)
    over = top + (Fraction(2) ** (fmt.emax + 1) - top) / 2
    inputs += [decimal_text(over), decimal_text(over - Fraction(1, 1000)), decimal_text(over + 1)]
    # Underflow boundary: half the smallest subnormal, and either side of it.
    half_min = fmt.value(1) / 2
    hm = decimal_text(half_min)
    hd = len(hm.split(".")[1])
    inputs += [hm, decimal_text(half_min + Fraction(1, 10 ** (hd + 2))),
               decimal_text(half_min - Fraction(1, 10 ** (hd + 2))), "-" + hm]
    # More than 800 significant digits: the tail decides only through the
    # sticky flag.
    one_mid = (fmt.value(fmt.bias << fmt.f) + fmt.value((fmt.bias << fmt.f) + 1)) / 2
    om = decimal_text(one_mid)
    inputs += [om + "0" * 900, om + "0" * 900 + "1", "1" + "0" * 850 + "e-850", "9" * 820 + "e-820",
               "0." + "0" * 830 + "1" + "e830"]
    # Syntax forms.
    inputs += ["1e5", "1E5", "1e+5", "1e-5", ".5", "5.", "+.5e1", "-0", "+0.0", "0e999999999999999999",
               "1e-999999999999999999", "1e999999999999999999", "-1e999999999999999999", "00000.000001e6",
               "0.000", "1.0e0", "123456789012345678901234567890", "0.1e-1", "65504", "65520", "3.4028235e38",
               "3.4028236e38", "1.7976931348623157e308", "1.7976931348623159e308", "4.9e-324", "2.5e-324",
               "1.4e-45", "7.0e-46", "6.0e-8", "2.9e-8", "9.2e-41", "4.6e-41", "+Infinity", "Infinity",
               "-Infinity", "NaN"]
    # Errors.
    inputs += ["", "-", "+", ".", "-.", "e5", ".e5", "1e", "1e+", "1e-x", "1.2.3", "1x", " 1", "1 ",
               "inf", "nan", "-NaN", "+NaN", "Infinityx", "infinity", "1,5", "é", "1e5.0", "--1", "+-1"]
    out = []
    for s in inputs:
        if s not in out:
            out.append(s)
    return out


def generate(name):
    fmt = fr.Fmt(name)
    module, fname, bt = TYPES[name]
    rng = random.Random(fmt.width * 1000 + fmt.f)
    vals = corpus(fmt, rng)
    T = name
    out = []
    out.append(f"""let error = import:Std.Core.Error;
let floats = import:Std.Core.{module};
let ordering = import:Std.Core.Ordering;
let result = import:Std.Core.Result;
let test = import:Std.Test;

// {module} against vectors from an exact Fraction model of Stdlib.md
// sections 5.2, 5.3, 7.3, and 7.5 (generated with Python; the corpus holds
// signed zeros, infinities, NaNs, subnormals, the largest finite values,
// powers of 2 and 10, exact midpoints, and seeded random patterns).
let {module}Tests = module (
""")
    out.append(decl("values", bt, map(str, vals), 6))
    out.append(decl("is_nan_values", "bool", [b(fr.Fmt(name).classify(v) == "nan") for v in vals], 12))
    out.append(decl("is_infinite_values", "bool", [b(fmt.classify(v) == "inf") for v in vals], 12))
    out.append(decl("is_negative_values", "bool", [b(v & fmt.sign) for v in vals], 12))
    out.append(decl("hash_values", "uint32", [str(hash_ref(fmt, v)) for v in vals], 6))
    out.append(decl("text_values", "string", [nra_string(fr.to_string(fmt, v)) for v in vals], 4))
    for mode, mname in enumerate(["floor", "ceil", "trunc", "round", "round_even"]):
        out.append(decl(f"{mname}_values", bt, [str(rounding_ref(fmt, v, mode)) for v in vals], 6))
    # Ordering over a compact sorted sample.
    sample = [v for v in vals if fmt.classify(v) != "nan"][:28] + [fmt.nan, fmt.nan | fmt.sign | 5]
    out.append(decl("order_values", bt, map(str, sample), 6))
    ranks = []
    for a in sample:
        for c in sample:
            ka, kc = total_key(fmt, a), total_key(fmt, c)
            ranks.append(str(0 if ka < kc else 1 if ka == kc else 2))
    out.append(decl("order_expected", "int", ranks, 30))
    # to_fixed and to_exponential.
    fixed_vals = vals[:60]
    fixed_digits = [0, 1, 2, 5, 17]
    out.append(decl("fixed_values", bt, map(str, fixed_vals), 6))
    out.append(decl("fixed_digits", "int", map(str, fixed_digits), 12))
    out.append(decl("fixed_texts", "string",
                    [nra_string(fr.to_fixed(fmt, v, d)) for v in fixed_vals for d in fixed_digits], 3))
    exp_digits = [0, 1, 3, 8, 16, 30]
    out.append(decl("exponential_digits", "int", map(str, exp_digits), 12))
    out.append(decl("exponential_texts", "string",
                    [nra_string(fr.to_exponential(fmt, v, d)) for v in fixed_vals for d in exp_digits], 3))
    wide_vals = [1, fmt.max_bits, fmt.bias << fmt.f, fr.parse(fmt, "0.1")[1], fmt.sign | 3]
    out.append(decl("wide_values", bt, map(str, wide_vals), 6))
    out.append(decl("wide_fixed_texts", "string", [nra_string(fr.to_fixed(fmt, v, 100)) for v in wide_vals], 1))
    out.append(decl("wide_exponential_texts", "string",
                    [nra_string(fr.to_exponential(fmt, v, 100)) for v in wide_vals], 1))
    # Parsing.
    inputs = parse_corpus(fmt, vals, rng)
    refs = [fr.parse(fmt, s) for s in inputs]
    out.append(decl("parse_inputs", "string", map(nra_string, inputs), 2))
    out.append(decl("parse_ok", "bool", [b(r[0] == "ok") for r in refs], 12))
    out.append(decl("parse_bits", bt, [str(r[1]) if r[0] == "ok" else "0" for r in refs], 6))
    out.append(decl("parse_kinds", "int", [str(r[1]) if r[0] == "err" else "0" for r in refs], 20))
    out.append(decl("parse_offsets", "int", [str(r[2]) if r[0] == "err" else "0" for r in refs], 20))
    out.append(decl("parse_messages", "string", [nra_string(r[3]) if r[0] == "err" else '""' for r in refs], 3))
    # Constants.
    one = fmt.bias << fmt.f
    constants = {
        "Max": fmt.max_bits, "MinPositive": 1, "MinNormal": 1 << fmt.f,
        "Epsilon": (fmt.bias - fmt.f) << fmt.f, "Infinity": fmt.inf,
        "NegativeInfinity": fmt.inf | fmt.sign, "NaN": fmt.nan,
    }
    assert fmt.value(constants["Epsilon"]) == fmt.value(one + 1) - 1
    const_checks = "\n".join(
        f"        test.assert(floats.to_bits(floats.{k}) == {v});" for k, v in constants.items())
    out.append(f"""
    let check = func(bool holds, string what, int index) -> void {{
        if (!holds) {{
            test.fail(what + " differs from the reference for case " + index.to_string());
        }}
    }};

    let same = func({T} actual, {bt} expected) -> bool {{
        return actual.to_bits() == expected;
    }};

    export let test_constants = func() -> void {{
{const_checks}
        test.assert(floats.Max > floats.MinNormal && floats.MinNormal > floats.MinPositive);
        test.assert(floats.is_nan(floats.NaN) && floats.NegativeInfinity < floats.Infinity);
    }};

    export let test_classification = func() -> void {{
        for (let k = 0; k < values.length; k = k + 1) {{
            let v = floats.from_bits(values[k]);
            check(floats.is_nan(v) == is_nan_values[k], "is_nan", k);
            check(floats.is_infinite(v) == is_infinite_values[k], "is_infinite", k);
            check(floats.is_finite(v) == (!is_nan_values[k] && !is_infinite_values[k]), "is_finite", k);
            check(floats.is_negative(v) == is_negative_values[k], "is_negative", k);
            check(floats.to_bits(v) == values[k], "to_bits", k);
        }}
    }};

    export let test_abs_and_copy_sign = func() -> void {{
        let negative = floats.from_bits({fmt.sign});
        let positive = floats.from_bits(0);
        for (let k = 0; k < values.length; k = k + 1) {{
            let v = floats.from_bits(values[k]);
            let magnitude = values[k] & {(1 << (fmt.width - 1)) - 1};
            check(same(floats.abs(v), magnitude), "abs", k);
            check(same(floats.copy_sign(v, negative), magnitude | {fmt.sign}), "copy_sign negative", k);
            check(same(floats.copy_sign(v, positive), magnitude), "copy_sign positive", k);
        }}
    }};

    export let test_rounding = func() -> void {{
        for (let k = 0; k < values.length; k = k + 1) {{
            let v = floats.from_bits(values[k]);
            check(same(floats.floor(v), floor_values[k]), "floor", k);
            check(same(floats.ceil(v), ceil_values[k]), "ceil", k);
            check(same(floats.trunc(v), trunc_values[k]), "trunc", k);
            check(same(floats.round(v), round_values[k]), "round", k);
            check(same(floats.round_even(v), round_even_values[k]), "round_even", k);
        }}
    }};

    export let test_total_compare_min_max = func() -> void {{
        let k = 0;
        for (let a_bits in order_values) {{
            for (let b_bits in order_values) {{
                let a = floats.from_bits(a_bits);
                let b = floats.from_bits(b_bits);
                let expected = order_expected[k];
                check((floats.total_compare(a, b) as int) == expected, "total_compare", k);
                let low = floats.min(a, b);
                let high = floats.max(a, b);
                if (floats.is_nan(a) || floats.is_nan(b)) {{
                    check(same(low, {fmt.nan}) && same(high, {fmt.nan}), "min/max NaN", k);
                }} else if (expected == 0) {{
                    check(same(low, a_bits) && same(high, b_bits), "min/max less", k);
                }} else if (expected == 2) {{
                    check(same(low, b_bits) && same(high, a_bits), "min/max greater", k);
                }} else {{
                    check(low.equals(a) && high.equals(a), "min/max equal", k);
                }}
                k = k + 1;
            }}
        }}
    }};

    export let test_clamp = func() -> void {{
        let low = floats.from_bits({fmt.sign});
        let high = floats.from_bits({one});
        test.assert(same(floats.clamp(floats.from_bits(0), low, high), 0));
        test.assert(same(floats.clamp(floats.from_bits({fmt.sign}), floats.from_bits(0), high), 0));
        test.assert(same(floats.clamp(floats.Infinity, low, high), {one}));
        test.assert(same(floats.clamp(floats.NegativeInfinity, low, high), {fmt.sign}));
        test.assert(same(floats.clamp(floats.NaN, low, high), {fmt.nan}));
        test.assert(same(floats.clamp(floats.MinPositive, low, high), 1));
        test.assert(same(floats.clamp(floats.Max, floats.Max, floats.Max), {fmt.max_bits}));
    }};

    export let test_equals_and_hash = func() -> void {{
        for (let k = 0; k < values.length; k = k + 1) {{
            let v = floats.from_bits(values[k]);
            check(floats.hash(v) == hash_values[k], "hash", k);
            for (let j = 0; j < values.length; j = j + 1) {{
                let w = floats.from_bits(values[j]);
                let expected = values[k] == values[j] || (is_nan_values[k] && is_nan_values[j]);
                check(floats.equals(v, w) == expected, "equals", k);
            }}
        }}
    }};

    export let test_to_string = func() -> void {{
        for (let k = 0; k < values.length; k = k + 1) {{
            check(floats.to_string(floats.from_bits(values[k])) == text_values[k], "to_string", k);
        }}
    }};

    export let test_to_fixed = func() -> void {{
        let k = 0;
        for (let bits in fixed_values) {{
            for (let digits in fixed_digits) {{
                check(floats.to_fixed(floats.from_bits(bits), digits) == fixed_texts[k], "to_fixed", k);
                k = k + 1;
            }}
        }}
        for (let i = 0; i < wide_values.length; i = i + 1) {{
            check(floats.to_fixed(floats.from_bits(wide_values[i]), 100) == wide_fixed_texts[i], "to_fixed 100", i);
        }}
    }};

    export let test_to_exponential = func() -> void {{
        let k = 0;
        for (let bits in fixed_values) {{
            for (let digits in exponential_digits) {{
                let text = floats.to_exponential(floats.from_bits(bits), digits);
                check(text == exponential_texts[k], "to_exponential", k);
                k = k + 1;
            }}
        }}
        for (let i = 0; i < wide_values.length; i = i + 1) {{
            let text = floats.to_exponential(floats.from_bits(wide_values[i]), 100);
            check(text == wide_exponential_texts[i], "to_exponential 100", i);
        }}
    }};
""")
    # Split parse checks into chunks so each test stays small.
    chunk = 60
    for start in range(0, len(inputs), chunk):
        end = min(len(inputs), start + chunk)
        out.append(f"""
    export let test_parse_{start // chunk} = func() -> void {{
        for (let k = {start}; k < {end}; k = k + 1) {{
            check_parse(k);
        }}
    }};
""")
    out.append(f"""
    let check_parse = func(int k) -> void {{
        match (floats.parse(parse_inputs[k])) {{
            result.Result.Ok(value) => {{
                check(parse_ok[k] && same(value, parse_bits[k]), "parse value", k);
            }},
            result.Result.Err(failure) => {{
                check(!parse_ok[k], "parse success", k);
                match (failure) {{
                    error.Error.Parse {{ kind, offset, message }} => {{
                        check((kind as int) == parse_kinds[k], "parse error kind", k);
                        check(offset == parse_offsets[k], "parse error offset", k);
                        check(message == parse_messages[k], "parse error message", k);
                    }},
                    _ => {{
                        check(false, "parse error variant", k);
                    }},
                }}
            }},
        }}
    }};

    export let test_round_trip = func() -> void {{
        for (let k = 0; k < values.length; k = k + 1) {{
            let v = floats.from_bits(values[k]);
            let back = test.assert_ok<{T}>(floats.parse(floats.to_string(v)));
            check(floats.equals(back, v), "round trip", k);
        }}
    }};
);
""")
    path = f"{ROOT}/tests/stdlib/floats/{fname}-tests.nra"
    with open(path, "w") as f:
        f.write("".join(out))
    print(path, len(vals), "values", len(inputs), "parse inputs")


if __name__ == "__main__":
    for name in (sys.argv[1:] or TYPES):
        generate(name)
