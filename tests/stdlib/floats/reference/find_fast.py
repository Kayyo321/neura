"""Searches for short inputs that the float64 fast path of float parsing
would round wrongly without its midpoint guard (GUARD_CASES in
gen_float_tests.py)."""
import random
import sys
import os
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import floatref as fr
from fractions import Fraction
from gen_float_tests import significant_text
POW = [10.0 ** i for i in range(23)]
def fast(digits, scale):
    mant = float(int(digits))
    mag = abs(scale)
    power = 1.0
    rem = mag
    while rem > 22:
        power = power * POW[22]; rem -= 22
    power = power * POW[rem]
    return mant * power if scale >= 0 else mant / power
found = {}
for name in ("float16", "bfloat16", "float32"):
    fmt = fr.Fmt(name)
    rng = random.Random(5)
    hits = []
    for trial in range(200000):
        lo_bits = rng.randrange(1 << fmt.f, fmt.max_bits)
        mid = (fmt.value(lo_bits) + fmt.value(lo_bits + 1)) / 2
        n = rng.choice([13, 14, 15])
        t = significant_text(mid, n)
        mant, e = t.split('e')
        digits = mant.replace('.', '')
        scale = int(e) - (len(digits) - 1)
        # strip trailing zeros like the parser
        while digits.endswith('0'):
            digits = digits[:-1]; scale += 1
        if len(digits) > 15 or abs(scale) > 66:
            continue
        r = fast(digits, scale)
        approx = fmt.round_fraction(Fraction(r))
        exact = fr.parse(fmt, t)[1]
        if approx != exact:
            hits.append(t)
            if len(hits) >= 4:
                break
    found[name] = hits
    print(name, hits)
