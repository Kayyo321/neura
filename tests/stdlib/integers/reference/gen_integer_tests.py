#!/usr/bin/env python3
"""Writes tests/stdlib/integers/<type>-tests.nra with reference vectors.

Run: python3 -I tests/stdlib/integers/reference/gen_integer_tests.py

Every expected value below is computed with Python's unbounded integers,
independently of the Neura implementation.
"""
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", ".."))
KINDS = {"Empty": 0, "InvalidCharacter": 1, "OutOfRange": 2, "UnexpectedEnd": 3, "Syntax": 4}


def tname(signed, w):
    return ("int" if signed else "uint") + str(w)


def mname(signed, w):
    return ("Int" if signed else "UInt") + str(w)


def bounds(signed, w):
    if signed:
        return -(1 << (w - 1)), (1 << (w - 1)) - 1
    return 0, (1 << w) - 1


def boundary_values(signed, w):
    lo, hi = bounds(signed, w)
    half = 1 << (w // 2)
    pattern = int("10100101" * (w // 8), 2)
    vals = [0, 1, 2, hi, hi - 1, lo, half, half - 1, half + 1]
    if signed:
        vals += [-1, -2, lo + 1, -half, pattern - (1 << w)]
    else:
        vals += [1 << (w - 1), (1 << (w - 1)) - 1, pattern]
    seen = []
    for v in vals:
        if v not in seen and lo <= v <= hi:
            seen.append(v)
    return seen


def fits(signed, w, v):
    lo, hi = bounds(signed, w)
    return lo <= v <= hi


def wrap(signed, w, v):
    v &= (1 << w) - 1
    if signed and v >= 1 << (w - 1):
        v -= 1 << w
    return v


def sat(signed, w, v):
    lo, hi = bounds(signed, w)
    return max(lo, min(hi, v))


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def trunc_rem(a, b):
    return a - trunc_div(a, b) * b


def pattern(signed, w, v):
    return v & ((1 << w) - 1)


def popcount(x):
    return bin(x).count("1")


def lz(w, x):
    return w - x.bit_length()


def tz(w, x):
    if x == 0:
        return w
    return (x & -x).bit_length() - 1


def rotl(w, x, n):
    n %= w
    m = (1 << w) - 1
    return ((x << n) | (x >> (w - n))) & m if n else x


def hash32(w, v):
    # Section 5.2: up to 32 bits, the value's 32-bit two's complement
    # pattern; 64-bit values fold the high half into the low half.
    if w <= 32:
        return v & 0xFFFFFFFF
    x = v & 0xFFFFFFFFFFFFFFFF
    return (x & 0xFFFFFFFF) ^ (x >> 32)


def radix_text(v, radix):
    digits = "0123456789abcdefghijklmnopqrstuvwxyz"
    if v == 0:
        return "0"
    neg = v < 0
    v = abs(v)
    out = ""
    while v:
        out = digits[v % radix] + out
        v //= radix
    return ("-" if neg else "") + out


def format_ref(v, radix, width, pad):
    text = radix_text(v, radix)
    missing = width - len(text)
    if missing <= 0:
        return text
    if pad == "0" and text.startswith("-"):
        return "-" + pad * missing + text[1:]
    return pad * missing + text


def parse_ref(signed, w, text, radix):
    """Returns (ok, value, kind, offset, message)."""
    data = text.encode()
    if not data:
        return (False, 0, KINDS["Empty"], 0, "empty input")
    start = 1 if data[0] in (43, 45) else 0
    if start == len(data):
        return (False, 0, KINDS["UnexpectedEnd"], len(data), "expected a digit")
    for i in range(start, len(data)):
        b = data[i]
        if 48 <= b <= 57:
            d = b - 48
        elif 97 <= b <= 122:
            d = b - 87
        elif 65 <= b <= 90:
            d = b - 55
        else:
            d = 36
        if d >= radix:
            return (False, 0, KINDS["InvalidCharacter"], i, "invalid character")
    value = int(data[start:].decode(), radix)
    if data[0] == 45:
        value = -value
    if not fits(signed, w, value):
        return (False, 0, KINDS["OutOfRange"], 0, "value out of range for " + tname(signed, w))
    return (True, value, 0, 0, "")


def lit(v):
    return str(v)


def nra_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def array(t, items, per_line=12):
    items = list(items)
    if not items:
        return "[]"
    lines = []
    for i in range(0, len(items), per_line):
        lines.append("        " + ", ".join(items[i:i + per_line]))
    return "[\n" + ",\n".join(lines) + "\n    ]"


def decl(name, t, items, per_line=12):
    return f"    let {name}: {t}[] = {array(t, items, per_line)};\n"


def b(x):
    return "true" if x else "false"


def generate(signed, w):
    T = tname(signed, w)
    M = mname(signed, w)
    lo, hi = bounds(signed, w)
    vals = boundary_values(signed, w)
    pairs = [(a, c) for a in vals for c in vals]
    out = []
    out.append(f"""let error = import:Std.Core.Error;
let ints = import:Std.Core.{M};
let optional = import:Std.Core.Optional;
let ordering = import:Std.Core.Ordering;
let result = import:Std.Core.Result;
let test = import:Std.Test;

// {M} against reference vectors computed with Python's unbounded integers
// (generated; the operands are the boundary values of Stdlib.md section 17:
// zero, one, the largest value, the sign boundary, and operands whose exact
// product needs the full double width).
let {M}Tests = module (
""")
    out.append(decl("values", T, map(lit, vals)))

    # Pairwise expectations.
    def opt(fn):
        present, values = [], []
        for a, c in pairs:
            r = fn(a, c)
            ok = r is not None and fits(signed, w, r)
            present.append(b(ok))
            values.append(lit(r if ok else 0))
        return present, values

    checked = {
        "add": lambda a, c: a + c,
        "sub": lambda a, c: a - c,
        "mul": lambda a, c: a * c,
        "div": lambda a, c: None if c == 0 else trunc_div(a, c),
        "rem": lambda a, c: None if c == 0 else trunc_rem(a, c),
    }
    for name, fn in checked.items():
        p, v = opt(fn)
        out.append(decl(f"checked_{name}_present", "bool", p))
        out.append(decl(f"checked_{name}_values", T, v))
    out.append(decl("wrapping_add_values", T, [lit(wrap(signed, w, a + c)) for a, c in pairs]))
    out.append(decl("wrapping_sub_values", T, [lit(wrap(signed, w, a - c)) for a, c in pairs]))
    out.append(decl("wrapping_mul_values", T, [lit(wrap(signed, w, a * c)) for a, c in pairs]))
    out.append(decl("saturating_add_values", T, [lit(sat(signed, w, a + c)) for a, c in pairs]))
    out.append(decl("saturating_sub_values", T, [lit(sat(signed, w, a - c)) for a, c in pairs]))
    out.append(decl("saturating_mul_values", T, [lit(sat(signed, w, a * c)) for a, c in pairs]))
    floor_ok = [c != 0 and fits(signed, w, a // c) for a, c in pairs]
    out.append(decl("floor_valid", "bool", [b(x) for x in floor_ok]))
    out.append(decl("floor_div_values", T, [lit(a // c if ok else 0) for (a, c), ok in zip(pairs, floor_ok)]))
    out.append(decl("floor_mod_values", T, [lit(a % c if c != 0 else 0) for (a, c) in pairs]))
    import math
    gcds = [math.gcd(a, c) for a, c in pairs]
    out.append(decl("gcd_valid", "bool", [b(fits(signed, w, g)) for g in gcds]))
    out.append(decl("gcd_values", T, [lit(g if fits(signed, w, g) else 0) for g in gcds]))
    out.append(decl("compare_values", "int", [str(0 if a < c else 1 if a == c else 2) for a, c in pairs], 24))

    # Single-value expectations.
    pats = [pattern(signed, w, v) for v in vals]
    out.append(decl("count_ones_values", "int", [str(popcount(p)) for p in pats]))
    out.append(decl("leading_zeros_values", "int", [str(lz(w, p)) for p in pats]))
    out.append(decl("trailing_zeros_values", "int", [str(tz(w, p)) for p in pats]))
    counts = [0, 1, 3, w - 1, w, w + 1, 2 * w + 5, -1, -w, -w - 3, -2147483648, 2147483647]
    out.append(decl("rotation_counts", "int", map(str, counts)))
    rl, rr = [], []
    for v in vals:
        p = pattern(signed, w, v)
        for n in counts:
            rl.append(lit(wrap(signed, w, rotl(w, p, n))))
            rr.append(lit(wrap(signed, w, rotl(w, p, -n % w))))
    out.append(decl("rotate_left_values", T, rl))
    out.append(decl("rotate_right_values", T, rr))
    U = tname(False, w)
    S = tname(True, w)
    out.append(decl("unsigned_values", U, [lit(p) for p in pats]))
    out.append(decl("signed_values", S, [lit(wrap(True, w, p)) for p in pats]))
    out.append(decl("hash_values", "uint32", [lit(hash32(w, v)) for v in vals]))
    out.append(decl("decimal_texts", "string", [nra_string(str(v)) for v in vals], 6))
    radixes = [2, 3, 8, 10, 16, 36]
    out.append(decl("radixes", "int", map(str, radixes)))
    out.append(decl("radix_texts", "string", [nra_string(radix_text(v, r)) for v in vals for r in radixes], 4))
    formats = [(10, 0, " "), (10, 6, "0"), (16, w // 2 + 3, "0"), (2, 12, " "), (10, 5, "é"), (10, -3, "*"),
               (36, 4, "_")]
    out.append(decl("format_radixes", "int", [str(f[0]) for f in formats]))
    out.append(decl("format_widths", "int", [str(f[1]) for f in formats]))
    out.append(decl("format_pads", "string", [nra_string(f[2]) for f in formats]))
    out.append(decl("format_texts", "string",
                    [nra_string(format_ref(v, *f)) for v in vals for f in formats], 4))

    # Parsing.
    inputs = []
    for v in vals:
        inputs.append((str(v), 10))
        inputs.append(("+" + str(v) if v >= 0 else str(v), 10))
    inputs += [
        (str(hi + 1), 10), (str(lo - 1), 10), ("-0", 10), ("+0", 10), ("000", 10), ("-000", 10),
        ("0000000000000000000000000000000000000000017", 10), ("", 10), ("-", 10), ("+", 10),
        ("1a", 10), (" 1", 10), ("1 ", 10), ("+-1", 10), ("--1", 10), ("0x10", 10), ("é", 10),
        ("1é", 10), ("9" * 40, 10), ("-" + "9" * 40, 10), ("9" * 40 + "x", 10), ("-1", 10), ("-5", 10),
        (str(-hi), 10), ("1_000", 10), ("1.0", 10),
        (radix_text(hi, 2), 2), (radix_text(lo, 2), 2), (radix_text(hi, 16), 16), (radix_text(hi, 16).upper(), 16),
        (radix_text(lo, 16), 16), (radix_text(hi, 36), 36), (radix_text(hi, 36).upper(), 36),
        (radix_text(lo, 36), 36), (radix_text(hi + 1, 16), 16), (radix_text(lo - 1, 16), 16),
        ("2", 2), ("z", 35), ("Z", 36), ("Zz", 36), ("g", 16), ("G", 16), ("fF", 16), ("-", 16), ("", 2),
        ("777", 8), ("8", 8), ("+11", 3), ("12", 3),
    ]
    refs = [parse_ref(signed, w, s, r) for s, r in inputs]
    out.append(decl("parse_inputs", "string", [nra_string(s) for s, _ in inputs], 4))
    out.append(decl("parse_radixes", "int", [str(r) for _, r in inputs], 24))
    out.append(decl("parse_ok", "bool", [b(x[0]) for x in refs]))
    out.append(decl("parse_values", T, [lit(x[1]) for x in refs]))
    out.append(decl("parse_kinds", "int", [str(x[2]) for x in refs], 24))
    out.append(decl("parse_offsets", "int", [str(x[3]) for x in refs], 24))
    out.append(decl("parse_messages", "string", [nra_string(x[4]) for x in refs], 3))

    clamp_cases = [(v, l, h) for v in vals for (l, h) in [(lo, hi), (0, 0), (1, 2), (lo, 0), (0, hi), (hi, hi)]]
    out.append(decl("clamp_inputs", T, [lit(x) for c in clamp_cases for x in c]))
    out.append(decl("clamp_values", T, [lit(max(l, min(h, v))) for v, l, h in clamp_cases]))

    pow_cases = []
    for base in vals + ([-3, 3, 10] if signed else [3, 10]):
        for e in [0, 1, 2, 3, 5, 7, w - 1, w, 2 * w, 100]:
            r = base ** e
            if fits(signed, w, r):
                pow_cases.append((base, e, r))
    out.append(decl("pow_bases", T, [lit(c[0]) for c in pow_cases]))
    out.append(decl("pow_exponents", "int", [str(c[1]) for c in pow_cases]))
    out.append(decl("pow_values", T, [lit(c[2]) for c in pow_cases]))

    out.append(f"""
    let check = func(holds: bool, what: string, index: int) -> void {{
        if (!holds) {{
            test.fail(what + " differs from the reference for case " + index.to_string());
        }}
    }};

    let check_optional = func(actual: optional.Optional<{T}>, present: bool, expected: {T}, what: string, index: int) -> void {{
        match (actual) {{
            optional.Optional.None => {{
                check(!present, what, index);
            }},
            optional.Optional.Some(value) => {{
                check(present && value == expected, what, index);
            }},
        }}
    }};

    let check_parse = func(actual: result.Result<{T}>, index: int) -> void {{
        match (actual) {{
            result.Result.Ok(value) => {{
                check(parse_ok[index] && value == parse_values[index], "parse value", index);
            }},
            result.Result.Err(failure) => {{
                check(!parse_ok[index], "parse success", index);
                match (failure) {{
                    error.Error.Parse {{ kind, offset, message }} => {{
                        check((kind as int) == parse_kinds[index], "parse error kind", index);
                        check(offset == parse_offsets[index], "parse error offset", index);
                        check(message == parse_messages[index], "parse error message", index);
                    }},
                    _ => {{
                        check(false, "parse error variant", index);
                    }},
                }}
            }},
        }}
    }};

    export let test_constants = func() -> void {{
        test.assert(ints.Min == {lo});
        test.assert(ints.Max == {hi});
        test.assert_equal_int32({w}, ints.Bits);
    }};

    export let test_sign_min_max_compare = func() -> void {{
        let k = 0;
        for (let a in values) {{
            let expected_sign = 0;
            if (a < 0) {{
                expected_sign = -1;
            }} else if (a > 0) {{
                expected_sign = 1;
            }}
            check(ints.sign(a) == expected_sign, "sign", k);
            for (let b in values) {{
                let index = k * values.length;
                k = k;
            }}
            k = k + 1;
        }}
        k = 0;
        for (let a in values) {{
            for (let b in values) {{
                let smaller = a;
                let larger = b;
                if (b < a) {{
                    smaller = b;
                    larger = a;
                }}
                check(ints.min(a, b) == smaller && ints.max(a, b) == larger, "min/max", k);
                check((ints.compare(a, b) as int) == compare_values[k], "compare", k);
                k = k + 1;
            }}
        }}
        test.assert(ints.compare(ints.Min, ints.Max) == ordering.Ordering.Less);
    }};

    export let test_clamp = func() -> void {{
        for (let k = 0; k < clamp_values.length; k = k + 1) {{
            let v = clamp_inputs[3 * k];
            let low = clamp_inputs[3 * k + 1];
            let high = clamp_inputs[3 * k + 2];
            check(ints.clamp(v, low, high) == clamp_values[k], "clamp", k);
        }}
    }};

    export let test_pow = func() -> void {{
        for (let k = 0; k < pow_values.length; k = k + 1) {{
            check(ints.pow(pow_bases[k], pow_exponents[k]) == pow_values[k], "pow", k);
        }}
        test.assert(ints.pow(0, 0) == 1);
    }};

    export let test_gcd = func() -> void {{
        let k = 0;
        for (let a in values) {{
            for (let b in values) {{
                if (gcd_valid[k]) {{
                    check(ints.gcd(a, b) == gcd_values[k], "gcd", k);
                }}
                k = k + 1;
            }}
        }}
        test.assert(ints.gcd(0, 0) == 0);
        test.assert(ints.gcd(12, 18) == 6);
    }};

    export let test_checked = func() -> void {{
        let k = 0;
        for (let a in values) {{
            for (let b in values) {{
                check_optional(ints.checked_add(a, b), checked_add_present[k], checked_add_values[k], "checked_add", k);
                check_optional(ints.checked_sub(a, b), checked_sub_present[k], checked_sub_values[k], "checked_sub", k);
                check_optional(ints.checked_mul(a, b), checked_mul_present[k], checked_mul_values[k], "checked_mul", k);
                check_optional(ints.checked_div(a, b), checked_div_present[k], checked_div_values[k], "checked_div", k);
                check_optional(ints.checked_rem(a, b), checked_rem_present[k], checked_rem_values[k], "checked_rem", k);
                k = k + 1;
            }}
        }}
    }};

    export let test_wrapping = func() -> void {{
        let k = 0;
        for (let a in values) {{
            for (let b in values) {{
                check(ints.wrapping_add(a, b) == wrapping_add_values[k], "wrapping_add", k);
                check(ints.wrapping_sub(a, b) == wrapping_sub_values[k], "wrapping_sub", k);
                check(ints.wrapping_mul(a, b) == wrapping_mul_values[k], "wrapping_mul", k);
                k = k + 1;
            }}
        }}
        let i = 0;
        for (let a in values) {{
            // wrapping_neg(a) is 0 - a modulo 2^Bits; values[0] is 0.
            check(ints.wrapping_neg(a) == wrapping_sub_values[i], "wrapping_neg", i);
            i = i + 1;
        }}
    }};

    export let test_saturating = func() -> void {{
        let k = 0;
        for (let a in values) {{
            for (let b in values) {{
                check(ints.saturating_add(a, b) == saturating_add_values[k], "saturating_add", k);
                check(ints.saturating_sub(a, b) == saturating_sub_values[k], "saturating_sub", k);
                check(ints.saturating_mul(a, b) == saturating_mul_values[k], "saturating_mul", k);
                k = k + 1;
            }}
        }}
    }};

    export let test_floor_div_mod = func() -> void {{
        let k = 0;
        for (let a in values) {{
            for (let b in values) {{
                if (floor_valid[k]) {{
                    check(ints.floor_div(a, b) == floor_div_values[k], "floor_div", k);
                }}
                if (b != 0) {{
                    check(ints.floor_mod(a, b) == floor_mod_values[k], "floor_mod", k);
                }}
                k = k + 1;
            }}
        }}
    }};

    export let test_bit_counts = func() -> void {{
        let k = 0;
        for (let v in values) {{
            check(ints.count_ones(v) == count_ones_values[k], "count_ones", k);
            check(ints.leading_zeros(v) == leading_zeros_values[k], "leading_zeros", k);
            check(ints.trailing_zeros(v) == trailing_zeros_values[k], "trailing_zeros", k);
            k = k + 1;
        }}
    }};

    export let test_rotations = func() -> void {{
        let k = 0;
        for (let v in values) {{
            for (let n in rotation_counts) {{
                check(ints.rotate_left(v, n) == rotate_left_values[k], "rotate_left", k);
                check(ints.rotate_right(v, n) == rotate_right_values[k], "rotate_right", k);
                k = k + 1;
            }}
        }}
    }};

    export let test_conversions = func() -> void {{
        let k = 0;
        for (let v in values) {{
            check(ints.to_unsigned(v) == unsigned_values[k], "to_unsigned", k);
            check(ints.to_signed(v) == signed_values[k], "to_signed", k);
            k = k + 1;
        }}
    }};

    export let test_hash_and_to_string = func() -> void {{
        let k = 0;
        for (let v in values) {{
            check(ints.hash(v) == hash_values[k], "hash", k);
            check(ints.to_string(v) == decimal_texts[k], "to_string", k);
            k = k + 1;
        }}
    }};

    export let test_to_string_radix = func() -> void {{
        let k = 0;
        for (let v in values) {{
            for (let radix in radixes) {{
                check(ints.to_string_radix(v, radix) == radix_texts[k], "to_string_radix", k);
                k = k + 1;
            }}
        }}
    }};

    export let test_format = func() -> void {{
        let k = 0;
        for (let v in values) {{
            for (let f = 0; f < format_radixes.length; f = f + 1) {{
                let text = ints.format(v, format_radixes[f], format_widths[f], format_pads[f]);
                check(text == format_texts[k], "format", k);
                k = k + 1;
            }}
        }}
    }};

    export let test_parse = func() -> void {{
        for (let k = 0; k < parse_inputs.length; k = k + 1) {{
            if (parse_radixes[k] == 10) {{
                check_parse(ints.parse(parse_inputs[k]), k);
            }}
            check_parse(ints.parse_radix(parse_inputs[k], parse_radixes[k]), k);
        }}
        let failure = test.assert_err<{T}>(ints.parse("1x"));
        test.assert_equal_string("parse error at 1: invalid character", error.message(failure));
    }};

    export let test_round_trip_text = func() -> void {{
        for (let v in values) {{
            for (let radix in radixes) {{
                let back = test.assert_ok<{T}>(ints.parse_radix(ints.to_string_radix(v, radix), radix));
                test.assert(back == v);
            }}
        }}
    }};
""")
    if signed:
        out.append(f"""
    export let test_abs = func() -> void {{
        let k = 0;
        for (let v in values) {{
            if (v != ints.Min) {{
                let expected = v;
                if (v < 0) {{
                    expected = -v;
                }}
                check(ints.abs(v) == expected, "abs", k);
            }}
            k = k + 1;
        }}
    }};
""")
    out.append(");\n")
    text = "".join(out)
    # Remove the placeholder inner loop that only advanced nothing.
    text = text.replace("""            for (let b in values) {
                let index = k * values.length;
                k = k;
            }
""", "")
    return text


for signed in (True, False):
    for w in (8, 16, 32, 64):
        path = f"{ROOT}/tests/stdlib/integers/{tname(signed, w)}-tests.nra"
        with open(path, "w") as f:
            f.write(generate(signed, w))
        print(path)
