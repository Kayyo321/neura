"""Exact reference model of Stdlib.md section 7.5 float text, using Fractions.

Independent of the Neura implementation and of the NVM's C++ formatter.
"""
from fractions import Fraction

FORMATS = {
    # name: (fraction bits, exponent bits)
    "float16": (10, 5),
    "bfloat16": (7, 8),
    "float32": (23, 8),
    "float64": (52, 11),
}


class Fmt:
    def __init__(self, name):
        self.name = name
        self.f, self.w = FORMATS[name]
        self.width = 1 + self.f + self.w
        self.bias = (1 << (self.w - 1)) - 1
        self.emin = 1 - self.bias
        self.emax = self.bias
        self.p = self.f + 1
        self.exp_all = ((1 << self.w) - 1) << self.f
        self.sign = 1 << (self.width - 1)
        self.nan = self.exp_all | (1 << (self.f - 1))
        self.inf = self.exp_all
        self.max_bits = self.exp_all - 1

    def classify(self, bits):
        e = (bits >> self.f) & ((1 << self.w) - 1)
        frac = bits & ((1 << self.f) - 1)
        if e == (1 << self.w) - 1:
            return "nan" if frac else "inf"
        return "finite"

    def value(self, bits):
        """Exact Fraction of a finite pattern (sign included)."""
        e = (bits >> self.f) & ((1 << self.w) - 1)
        frac = bits & ((1 << self.f) - 1)
        if e == 0:
            m, x = frac, self.emin - self.f
        else:
            m, x = frac | (1 << self.f), e - self.bias - self.f
        v = Fraction(m) * (Fraction(2) ** x)
        return -v if bits & self.sign else v

    def round_fraction(self, q):
        """Bits of the rational q rounded once, ties to even."""
        sign = 0
        if q < 0:
            sign, q = self.sign, -q
        if q == 0:
            return sign
        # exponent of the leading bit
        k = q.numerator.bit_length() - q.denominator.bit_length()
        while Fraction(2) ** k > q:
            k -= 1
        while Fraction(2) ** (k + 1) <= q:
            k += 1
        lowest = max(k, self.emin) - self.f
        scaled = q / (Fraction(2) ** lowest)
        n = scaled.numerator // scaled.denominator
        rem = scaled - n
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and n % 2 == 1):
            n += 1
        if n == 1 << (self.f + 1):
            n >>= 1
            lowest += 1
        if n == 0:
            return sign
        if n >= 1 << self.f:
            biased = lowest + self.f + self.bias
            if biased >= (1 << self.w) - 1:
                return sign | self.inf
            return sign | (biased << self.f) | (n - (1 << self.f))
        return sign | n

    def neighbours(self, bits):
        """Exact values of the previous and next magnitudes of a positive finite pattern."""
        mag = bits & ~self.sign
        prev = self.value(mag - 1) if mag > 0 else None
        if mag == self.max_bits:
            # The next value continues the binade: 2^(emax+1).
            nxt = Fraction(2) ** (self.emax + 1)
        else:
            nxt = self.value(mag + 1)
        return prev, nxt


def decimal_exponent(v):
    """k with 10^k <= v < 10^(k+1) for positive Fraction v."""
    k = len(str(v.numerator)) - len(str(v.denominator))
    while Fraction(10) ** k > v:
        k -= 1
    while Fraction(10) ** (k + 1) <= v:
        k += 1
    return k


def round_half_even(q):
    n = q.numerator // q.denominator
    rem = q - n
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and n % 2 == 1):
        n += 1
    return n


def shortest(fmt, bits):
    """(digits string, exponent) per section 7.5 rule 2."""
    mag = bits & ~fmt.sign
    v = fmt.value(mag)
    prev, nxt = fmt.neighbours(mag)
    low = (v + prev) / 2
    high = (v + nxt) / 2
    even = (mag & 1) == 0 if mag != 0 else True
    # Significand parity decides inclusivity (ties-to-even parsing).
    sig = mag & ((1 << fmt.f) - 1)
    even = (sig & 1) == 0

    def inside(c):
        lo_ok = c > low or (even and c == low)
        hi_ok = c < high or (even and c == high)
        return lo_ok and hi_ok

    k = decimal_exponent(v)
    for n in range(1, 40):
        unit = Fraction(10) ** (k - n + 1)
        down = (v / unit).numerator // (v / unit).denominator
        cands = []
        for c in (down, down + 1):
            cv = c * unit
            if inside(cv):
                cands.append((abs(cv - v), c % 2, c))
        if cands:
            cands.sort(key=lambda t: (t[0], t[1]))
            best = cands[0][2]
            digits = str(best)
            exp = k - n + 1 + len(digits) - 1
            digits = digits.rstrip("0") or "0"
            return digits, exp
    raise AssertionError("no shortest representation")


def to_string(fmt, bits):
    kind = fmt.classify(bits)
    neg = bool(bits & fmt.sign)
    sign = "-" if neg else ""
    if kind == "nan":
        return "NaN"
    if kind == "inf":
        return sign + "Infinity"
    if bits & ~fmt.sign == 0:
        return sign + "0.0"
    digits, e = shortest(fmt, bits)
    if e < -4 or e >= 16:
        return sign + digits[0] + "." + (digits[1:] or "0") + "e" + str(e)
    if e < 0:
        return sign + "0." + "0" * (-e - 1) + digits
    split = e + 1
    if split >= len(digits):
        return sign + digits + "0" * (split - len(digits)) + ".0"
    return sign + digits[:split] + "." + digits[split:]


def to_fixed(fmt, bits, d):
    kind = fmt.classify(bits)
    sign = "-" if bits & fmt.sign else ""
    if kind == "nan":
        return "NaN"
    if kind == "inf":
        return sign + "Infinity"
    v = abs(fmt.value(bits))
    n = round_half_even(v * Fraction(10) ** d)
    text = str(n)
    if d > 0:
        text = text.rjust(d + 1, "0")
        text = text[:-d] + "." + text[-d:]
    return sign + text


def to_exponential(fmt, bits, d):
    kind = fmt.classify(bits)
    sign = "-" if bits & fmt.sign else ""
    if kind == "nan":
        return "NaN"
    if kind == "inf":
        return sign + "Infinity"
    v = abs(fmt.value(bits))
    if v == 0:
        digits, k = "0" * (d + 1), 0
    else:
        k = decimal_exponent(v)
        n = round_half_even(v / Fraction(10) ** (k - d))
        if n == 10 ** (d + 1):
            n //= 10
            k += 1
        digits = str(n)
    body = digits[0] + ("." + digits[1:] if d > 0 else "")
    return sign + body + "e" + str(k)


def parse(fmt, text):
    """("ok", bits) or ("err", kind, offset, message) following the grammar."""
    data = text.encode()
    if not data:
        return ("err", 0, 0, "empty input")
    if text == "NaN":
        return ("ok", fmt.nan)
    if text in ("Infinity", "+Infinity"):
        return ("ok", fmt.inf)
    if text == "-Infinity":
        return ("ok", fmt.inf | fmt.sign)
    i = 0
    neg = False
    if data[0] in (43, 45):
        neg = data[0] == 45
        i = 1
    mant = ""
    frac_digits = 0
    seen_point = False
    seen_digit = False
    while i < len(data):
        b = data[i]
        if 48 <= b <= 57:
            mant += chr(b)
            seen_digit = True
            if seen_point:
                frac_digits += 1
        elif b == 46 and not seen_point:
            seen_point = True
        elif b in (101, 69) and seen_digit:
            break
        else:
            return ("err", 1, i, "invalid character")
        i += 1
    if not seen_digit:
        return ("err", 3, len(data), "expected a digit")
    exp = 0
    if i < len(data):
        i += 1
        eneg = False
        if i < len(data) and data[i] in (43, 45):
            eneg = data[i] == 45
            i += 1
        if i == len(data):
            return ("err", 3, len(data), "expected a digit")
        start = i
        while i < len(data):
            if not 48 <= data[i] <= 57:
                return ("err", 1, i, "invalid character")
            i += 1
        exp = int(data[start:].decode())
        if eneg:
            exp = -exp
    scale = exp - frac_digits
    if int(mant) == 0:
        bits = 0
    elif scale > 100000:
        bits = fmt.inf
    elif scale < -100000 - len(mant):
        bits = 0
    else:
        bits = fmt.round_fraction(Fraction(int(mant)) * Fraction(10) ** scale)
    if neg:
        bits |= fmt.sign
    return ("ok", bits)


def exact_decimal(q):
    """Exact decimal text of a Fraction whose denominator is a power of 2."""
    neg = q < 0
    q = abs(q)
    den = q.denominator
    k = 0
    while den % 2 == 0:
        den //= 2
        k += 1
    assert den == 1
    n = q.numerator * 5 ** k
    s = str(n)
    if k == 0:
        out = s
    else:
        s = s.rjust(k + 1, "0")
        out = s[:-k] + "." + s[-k:]
    return ("-" if neg else "") + out
