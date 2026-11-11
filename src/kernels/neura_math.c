/* Std.Math.Math kernels (neura_math.h): fdlibm 5.3 in C, operation for
 * operation the port in stdlib/std/math/math.nra and
 * stdlib/std/math/internal/{trig,words}.nra. The body is
 * tests/stdlib/math/vectors/fdlibm.c, the transliteration that generated the
 * vectors that port is tested against, with bit access by union instead of
 * memcpy; fd_sqrt is the exact integer method and fd_log2 this library's
 * log2, as math.nra documents. The public kernels at the end replace every
 * NaN result by the canonical NaN, as Neura's float operations produce.
 *
 * k_tan.c and e_pow.c: Copyright (C) 2004 by Sun Microsystems, Inc. All
 * rights reserved. Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice is preserved.
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
#include <float.h>

#include "neura_math.h"

/* Bit access by union, which C11 defines; no libc call. */
typedef union {
    double f;
    uint64_t u;
} f64_pun;
static uint64_t TOBITS(double x) { f64_pun p; p.f = x; return p.u; }
static double BITS(uint64_t b) { f64_pun p; p.u = b; return p.f; }
static int HI(double x) { return (int)(uint32_t)(TOBITS(x) >> 32); }
static uint32_t LO(double x) { return (uint32_t)TOBITS(x); }
static double WORDS(int hi, uint32_t lo) { return BITS(((uint64_t)(uint32_t)hi << 32) | lo); }
static double SETHI(double x, int hi) { return WORDS(hi, LO(x)); }
static double SETLO(double x, uint32_t lo) { return WORDS(HI(x), lo); }
static double FABS(double x) { return WORDS(HI(x) & 0x7fffffff, LO(x)); }
/* k << 20 for a possibly negative exponent adjustment, shifted as unsigned
 * (which C defines) where fdlibm shifts a negative int. */
static int SHL20(int k) { return (int)((unsigned)k << 20); }

/* Correctly rounded square root by integer square root of the significand. */
static double fd_sqrt(double x) {
    uint64_t b = TOBITS(x);
    if (x != x) return x + x;
    if (x == 0.0) return x;
    if (b >> 63) return (x - x) / (x - x);
    int e = (int)((b >> 52) & 0x7ff);
    uint64_t m = b & 0xfffffffffffffULL;
    if (e == 0x7ff) return x;
    if (e == 0) { e = 1; while ((m & (1ULL << 52)) == 0) { m <<= 1; e -= 1; } }
    else m |= 1ULL << 52;
    e -= 1023; /* value = m * 2^(e-52) */
    if (e & 1) { m <<= 1; e -= 1; }
    /* sqrt(m * 2^54) = q, 54 bits */
    uint64_t q = 0, r = 0;
    for (int i = 0; i < 54; i++) {
        uint64_t pair = (i < 27) ? ((m >> (52 - 2 * i)) & 3) : 0;
        r = (r << 2) | pair;
        uint64_t t = (q << 2) | 1;
        if (r >= t) { r -= t; q = (q << 1) | 1; } else q <<= 1;
    }
    uint64_t sig = q >> 1;
    if ((q & 1) && (r != 0 || (sig & 1))) sig += 1;
    int re = e / 2; /* result = sig * 2^(re - 52) */
    uint64_t out = ((uint64_t)(re + 1023) << 52) + (sig - (1ULL << 52));
    return BITS(out);
}

static double fd_scalbn(double x, int n) {
    static const double two54 = 1.80143985094819840000e+16, twom54 = 5.55111512312578270212e-17,
        huge = 1.0e+300, tiny = 1.0e-300;
    int k, hx, lx;
    hx = HI(x); lx = (int)LO(x);
    k = (hx & 0x7ff00000) >> 20;
    if (k == 0) {
        if ((lx | (hx & 0x7fffffff)) == 0) return x;
        x *= two54; hx = HI(x);
        k = ((hx & 0x7ff00000) >> 20) - 54;
        if (n < -50000) return tiny * x;
    }
    if (k == 0x7ff) return x + x;
    k = k + n;
    if (k > 0x7fe) return huge * (hx < 0 ? -huge : huge);
    if (k > 0) return SETHI(x, (int)(((unsigned)hx & 0x800fffffu) | ((unsigned)k << 20)));
    if (k <= -54) {
        if (n > 50000) return huge * (hx < 0 ? -huge : huge);
        else return tiny * (hx < 0 ? -tiny : tiny);
    }
    k += 54;
    x = SETHI(x, (int)(((unsigned)hx & 0x800fffffu) | ((unsigned)k << 20)));
    return x * twom54;
}

static double fd_exp(double x) {
    static const double one = 1.0, halF[2] = {0.5, -0.5}, huge = 1.0e+300,
        twom1000 = 9.33263618503218878990e-302, o_threshold = 7.09782712893383973096e+02,
        u_threshold = -7.45133219101941108420e+02,
        ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01},
        ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10},
        invln2 = 1.44269504088896338700e+00, P1 = 1.66666666666666019037e-01,
        P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
        P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
    double y, hi = 0, lo = 0, c, t;
    int k = 0, xsb;
    unsigned hx;
    hx = (unsigned)HI(x);
    xsb = (int)((hx >> 31) & 1);
    hx &= 0x7fffffff;
    if (hx >= 0x40862E42) {
        if (hx >= 0x7ff00000) {
            if (((hx & 0xfffff) | LO(x)) != 0) return x + x;
            else return (xsb == 0) ? x : 0.0;
        }
        if (x > o_threshold) return huge * huge;
        if (x < u_threshold) return twom1000 * twom1000;
    }
    if (hx > 0x3fd62e42) {
        if (hx < 0x3FF0A2B2) {
            hi = x - ln2HI[xsb]; lo = ln2LO[xsb]; k = 1 - xsb - xsb;
        } else {
            k = (int)(invln2 * x + halF[xsb]);
            t = k;
            hi = x - t * ln2HI[0];
            lo = t * ln2LO[0];
        }
        x = hi - lo;
    } else if (hx < 0x3e300000) {
        if (huge + x > one) return one + x;
    } else k = 0;
    t = x * x;
    c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0) return one - ((x * c) / (c - 2.0) - x);
    else y = one - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021) {
        return SETHI(y, HI(y) + SHL20(k));
    } else {
        y = SETHI(y, HI(y) + SHL20(k + 1000));
        return y * twom1000;
    }
}

static const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10,
    two54 = 1.80143985094819840000e+16;

static double fd_log(double x) {
    static const double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
        Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
        Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
        Lg7 = 1.479819860511658591e-01;
    volatile double zero = 0.0;
    double hfsq, f, s, z, R, w, t1, t2, dk;
    int k, hx, i, j;
    unsigned lx;
    hx = HI(x); lx = LO(x);
    k = 0;
    if (hx < 0x00100000) {
        if (((hx & 0x7fffffff) | lx) == 0) return -two54 / zero;
        if (hx < 0) return (x - x) / zero;
        k -= 54; x *= two54;
        hx = HI(x);
    }
    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    i = (hx + 0x95f64) & 0x100000;
    x = SETHI(x, hx | (i ^ 0x3ff00000));
    k += (i >> 20);
    f = x - 1.0;
    if ((0x000fffff & (2 + hx)) < 3) {
        if (f == zero) {
            if (k == 0) return zero;
            else { dk = (double)k; return dk * ln2_hi + dk * ln2_lo; }
        }
        R = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0) return f - R;
        else { dk = (double)k; return dk * ln2_hi - ((R - dk * ln2_lo) - f); }
    }
    s = f / (2.0 + f);
    dk = (double)k;
    z = s * s;
    i = hx - 0x6147a;
    w = z * z;
    j = 0x6b851 - hx;
    t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    R = t2 + t1;
    if (i > 0) {
        hfsq = 0.5 * f * f;
        if (k == 0) return f - (hfsq - s * (hfsq + R));
        else return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    } else {
        if (k == 0) return f - s * (f - R);
        else return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
    }
}

static double fd_log10(double x) {
    static const double ivln10 = 4.34294481903251816668e-01, log10_2hi = 3.01029995663611771306e-01,
        log10_2lo = 3.69423907715893078616e-13;
    volatile double zero = 0.0;
    double y, z;
    int i, k, hx;
    unsigned lx;
    hx = HI(x); lx = LO(x);
    k = 0;
    if (hx < 0x00100000) {
        if (((hx & 0x7fffffff) | lx) == 0) return -two54 / zero;
        if (hx < 0) return (x - x) / zero;
        k -= 54; x *= two54;
        hx = HI(x);
    }
    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    i = (int)(((unsigned)k & 0x80000000u) >> 31);
    hx = (hx & 0x000fffff) | ((0x3ff - i) << 20);
    y = (double)(k + i);
    x = SETHI(x, hx);
    z = y * log10_2lo + ivln10 * fd_log(x);
    return z + y * log10_2hi;
}

/* log2: the structure of e_log10.c with log2(2) = 1 exact: y + ivln2*log(m). */
static double fd_log2(double x) {
    static const double ivln2 = 1.44269504088896338700e+00;
    volatile double zero = 0.0;
    double y, z;
    int i, k, hx;
    unsigned lx;
    hx = HI(x); lx = LO(x);
    k = 0;
    if (hx < 0x00100000) {
        if (((hx & 0x7fffffff) | lx) == 0) return -two54 / zero;
        if (hx < 0) return (x - x) / zero;
        k -= 54; x *= two54;
        hx = HI(x); lx = LO(x);
    }
    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    if (((hx & 0x000fffff) | lx) == 0) return (double)k; /* exact power of two */
    i = (int)(((unsigned)k & 0x80000000u) >> 31);
    hx = (hx & 0x000fffff) | ((0x3ff - i) << 20);
    y = (double)(k + i);
    x = SETHI(x, hx);
    z = ivln2 * fd_log(x);
    return z + y;
}

static double fd_log1p(double x) {
    static const double Lp1 = 6.666666666666735130e-01, Lp2 = 3.999999999940941908e-01,
        Lp3 = 2.857142874366239149e-01, Lp4 = 2.222219843214978396e-01,
        Lp5 = 1.818357216161805012e-01, Lp6 = 1.531383769920937332e-01,
        Lp7 = 1.479819860511658591e-01;
    volatile double zero = 0.0;
    double hfsq, f = 0, c = 0, s, z, R, u;
    int k, hx, hu = 0, ax;
    hx = HI(x);
    ax = hx & 0x7fffffff;
    k = 1;
    if (hx < 0x3FDA827A) {
        if (ax >= 0x3ff00000) {
            if (x == -1.0) return -two54 / zero;
            else return (x - x) / (x - x);
        }
        if (ax < 0x3e200000) {
            if (two54 + x > zero && ax < 0x3c900000) return x;
            else return x - x * x * 0.5;
        }
        if (hx > 0 || hx <= ((int)0xbfd2bec3)) { k = 0; f = x; hu = 1; }
    }
    if (hx >= 0x7ff00000) return x + x;
    if (k != 0) {
        if (hx < 0x43400000) {
            u = 1.0 + x;
            hu = HI(u);
            k = (hu >> 20) - 1023;
            c = (k > 0) ? 1.0 - (u - x) : x - (u - 1.0);
            c /= u;
        } else {
            u = x;
            hu = HI(u);
            k = (hu >> 20) - 1023;
            c = 0;
        }
        hu &= 0x000fffff;
        if (hu < 0x6a09e) {
            u = SETHI(u, hu | 0x3ff00000);
        } else {
            k += 1;
            u = SETHI(u, hu | 0x3fe00000);
            hu = (0x00100000 - hu) >> 2;
        }
        f = u - 1.0;
    }
    hfsq = 0.5 * f * f;
    if (hu == 0) {
        if (f == zero) {
            if (k == 0) return zero;
            else { c += k * ln2_lo; return k * ln2_hi + c; }
        }
        R = hfsq * (1.0 - 0.66666666666666666 * f);
        if (k == 0) return f - R;
        else return k * ln2_hi - ((R - (k * ln2_lo + c)) - f);
    }
    s = f / (2.0 + f);
    z = s * s;
    R = z * (Lp1 + z * (Lp2 + z * (Lp3 + z * (Lp4 + z * (Lp5 + z * (Lp6 + z * Lp7))))));
    if (k == 0) return f - (hfsq - s * (hfsq + R));
    else return k * ln2_hi - ((hfsq - (s * (hfsq + R) + (k * ln2_lo + c))) - f);
}

static double fd_expm1(double x) {
    static const double one = 1.0, huge = 1.0e+300, tiny = 1.0e-300,
        o_threshold = 7.09782712893383973096e+02, invln2 = 1.44269504088896338700e+00,
        Q1 = -3.33333333333331316428e-02, Q2 = 1.58730158725481460165e-03,
        Q3 = -7.93650757867487942473e-05, Q4 = 4.00821782732936239552e-06,
        Q5 = -2.01099218183624371326e-07;
    double y, hi, lo, c = 0, t, e, hxs, hfx, r1;
    int k, xsb;
    unsigned hx;
    hx = (unsigned)HI(x);
    xsb = (int)(hx & 0x80000000u);
    if (xsb == 0) y = x; else y = -x;
    hx &= 0x7fffffff;
    if (hx >= 0x4043687A) {
        if (hx >= 0x40862E42) {
            if (hx >= 0x7ff00000) {
                if (((hx & 0xfffff) | LO(x)) != 0) return x + x;
                else return (xsb == 0) ? x : -1.0;
            }
            if (x > o_threshold) return huge * huge;
        }
        if (xsb != 0) {
            if (x + tiny < 0.0) return tiny - one;
        }
    }
    if (hx > 0x3fd62e42) {
        if (hx < 0x3FF0A2B2) {
            if (xsb == 0) { hi = x - ln2_hi; lo = ln2_lo; k = 1; }
            else { hi = x + ln2_hi; lo = -ln2_lo; k = -1; }
        } else {
            k = (int)(invln2 * x + ((xsb == 0) ? 0.5 : -0.5));
            t = k;
            hi = x - t * ln2_hi;
            lo = t * ln2_lo;
        }
        x = hi - lo;
        c = (hi - x) - lo;
    } else if (hx < 0x3c900000) {
        t = huge + x;
        return x - (t - (huge + x));
    } else k = 0;
    hfx = 0.5 * x;
    hxs = x * hfx;
    r1 = one + hxs * (Q1 + hxs * (Q2 + hxs * (Q3 + hxs * (Q4 + hxs * Q5))));
    t = 3.0 - r1 * hfx;
    e = hxs * ((r1 - t) / (6.0 - x * t));
    if (k == 0) return x - (x * e - hxs);
    else {
        e = (x * (e - c) - c);
        e -= hxs;
        if (k == -1) return 0.5 * (x - e) - 0.5;
        if (k == 1) {
            if (x < -0.25) return -2.0 * (e - (x + 0.5));
            else return one + 2.0 * (x - e);
        }
        if (k <= -2 || k > 56) {
            y = one - (e - x);
            y = SETHI(y, HI(y) + SHL20(k));
            return y - one;
        }
        t = one;
        if (k < 20) {
            t = SETHI(t, 0x3ff00000 - (0x200000 >> k));
            y = t - (e - x);
            y = SETHI(y, HI(y) + SHL20(k));
        } else {
            t = SETHI(t, ((0x3ff - k) << 20));
            y = x - (e + t);
            y += one;
            y = SETHI(y, HI(y) + SHL20(k));
        }
    }
    return y;
}

static double fd_pow(double x, double y) {
    static const double bp[] = {1.0, 1.5}, dp_h[] = {0.0, 5.84962487220764160156e-01},
        dp_l[] = {0.0, 1.35003920212974897128e-08}, zero = 0.0, one = 1.0, two = 2.0,
        two53 = 9007199254740992.0, huge = 1.0e300, tiny = 1.0e-300,
        L1 = 5.99999999999994648725e-01, L2 = 4.28571428578550184252e-01,
        L3 = 3.33333329818377432918e-01, L4 = 2.72728123808534006489e-01,
        L5 = 2.30660745775561754067e-01, L6 = 2.06975017800338417784e-01,
        P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03,
        P3 = 6.61375632143793436117e-05, P4 = -1.65339022054652515390e-06,
        P5 = 4.13813679705723846039e-08, lg2 = 6.93147180559945286227e-01,
        lg2_h = 6.93147182464599609375e-01, lg2_l = -1.90465429995776804525e-09,
        ovt = 8.0085662595372944372e-0017, cp = 9.61796693925975554329e-01,
        cp_h = 9.61796700954437255859e-01, cp_l = -7.02846165095275826516e-09,
        ivln2 = 1.44269504088896338700e+00, ivln2_h = 1.44269502162933349609e+00,
        ivln2_l = 1.92596299112661746887e-08;
    double z, ax, z_h, z_l, p_h, p_l;
    double y1, t1, t2, r, s, t, u, v, w;
    int i, j, k, yisint, n;
    int hx, hy, ix, iy;
    unsigned lx, ly;
    hx = HI(x); lx = LO(x);
    hy = HI(y); ly = LO(y);
    ix = hx & 0x7fffffff; iy = hy & 0x7fffffff;
    if (((unsigned)iy | ly) == 0) return one;
    if (ix > 0x7ff00000 || ((ix == 0x7ff00000) && (lx != 0)) ||
        iy > 0x7ff00000 || ((iy == 0x7ff00000) && (ly != 0)))
        return x + y;
    yisint = 0;
    if (hx < 0) {
        if (iy >= 0x43400000) yisint = 2;
        else if (iy >= 0x3ff00000) {
            k = (iy >> 20) - 0x3ff;
            if (k > 20) {
                j = (int)(ly >> (52 - k));
                if (((unsigned)j << (52 - k)) == ly) yisint = 2 - (j & 1);
            } else if (ly == 0) {
                j = iy >> (20 - k);
                if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
            }
        }
    }
    if (ly == 0) {
        if (iy == 0x7ff00000) {
            if ((((unsigned)ix - 0x3ff00000u) | lx) == 0) return y - y;
            else if (ix >= 0x3ff00000) return (hy >= 0) ? y : zero;
            else return (hy < 0) ? -y : zero;
        }
        if (iy == 0x3ff00000) {
            if (hy < 0) return one / x; else return x;
        }
        if (hy == 0x40000000) return x * x;
        if (hy == 0x3fe00000) {
            if (hx >= 0) return fd_sqrt(x);
        }
    }
    ax = FABS(x);
    if (lx == 0) {
        if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {
            z = ax;
            if (hy < 0) z = one / z;
            if (hx < 0) {
                if (((ix - 0x3ff00000) | yisint) == 0) {
                    z = (z - z) / (z - z);
                } else if (yisint == 1)
                    z = -z;
            }
            return z;
        }
    }
    n = (hx >> 31) + 1;
    if ((n | yisint) == 0) return (x - x) / (x - x);
    s = one;
    if ((n | (yisint - 1)) == 0) s = -one;
    if (iy > 0x41e00000) {
        if (iy > 0x43f00000) {
            if (ix <= 0x3fefffff) return (hy < 0) ? huge * huge : tiny * tiny;
            if (ix >= 0x3ff00000) return (hy > 0) ? huge * huge : tiny * tiny;
        }
        if (ix < 0x3fefffff) return (hy < 0) ? s * huge * huge : s * tiny * tiny;
        if (ix > 0x3ff00000) return (hy > 0) ? s * huge * huge : s * tiny * tiny;
        t = ax - one;
        w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
        u = ivln2_h * t;
        v = t * ivln2_l - w * ivln2;
        t1 = u + v;
        t1 = SETLO(t1, 0);
        t2 = v - (t1 - u);
    } else {
        double ss, s2, s_h, s_l, t_h, t_l;
        n = 0;
        if (ix < 0x00100000) { ax *= two53; n -= 53; ix = HI(ax); }
        n += ((ix) >> 20) - 0x3ff;
        j = ix & 0x000fffff;
        ix = j | 0x3ff00000;
        if (j <= 0x3988E) k = 0;
        else if (j < 0xBB67A) k = 1;
        else { k = 0; n += 1; ix -= 0x00100000; }
        ax = SETHI(ax, ix);
        u = ax - bp[k];
        v = one / (ax + bp[k]);
        ss = u * v;
        s_h = ss;
        s_h = SETLO(s_h, 0);
        t_h = zero;
        t_h = SETHI(t_h, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
        t_l = ax - (t_h - bp[k]);
        s_l = v * ((u - s_h * t_h) - s_h * t_l);
        s2 = ss * ss;
        r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
        r += s_l * (s_h + ss);
        s2 = s_h * s_h;
        t_h = 3.0 + s2 + r;
        t_h = SETLO(t_h, 0);
        t_l = r - ((t_h - 3.0) - s2);
        u = s_h * t_h;
        v = s_l * t_h + t_l * ss;
        p_h = u + v;
        p_h = SETLO(p_h, 0);
        p_l = v - (p_h - u);
        z_h = cp_h * p_h;
        z_l = cp_l * p_h + p_l * cp + dp_l[k];
        t = (double)n;
        t1 = (((z_h + z_l) + dp_h[k]) + t);
        t1 = SETLO(t1, 0);
        t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
    }
    y1 = y;
    y1 = SETLO(y1, 0);
    p_l = (y - y1) * t1 + y * t2;
    p_h = y1 * t1;
    z = p_l + p_h;
    j = HI(z);
    i = (int)LO(z);
    if (j >= 0x40900000) {
        if (((j - 0x40900000) | i) != 0) return s * huge * huge;
        else {
            if (p_l + ovt > z - p_h) return s * huge * huge;
        }
    } else if ((j & 0x7fffffff) >= 0x4090cc00) {
        if (((j - (int)0xc090cc00) | i) != 0) return s * tiny * tiny;
        else {
            if (p_l <= z - p_h) return s * tiny * tiny;
        }
    }
    i = j & 0x7fffffff;
    k = (i >> 20) - 0x3ff;
    n = 0;
    if (i > 0x3fe00000) {
        n = j + (0x00100000 >> (k + 1));
        k = ((n & 0x7fffffff) >> 20) - 0x3ff;
        t = zero;
        t = SETHI(t, (n & ~(0x000fffff >> k)));
        n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
        if (j < 0) n = -n;
        p_h -= t;
    }
    t = p_l + p_h;
    t = SETLO(t, 0);
    u = t * lg2_h;
    v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
    z = u + v;
    w = v - (z - u);
    t = z * z;
    t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    r = (z * t1) / (t1 - two) - (w + z * w);
    z = one - (r - z);
    j = HI(z);
    j += SHL20(n);
    if ((j >> 20) <= 0) z = fd_scalbn(z, n);
    else z = SETHI(z, HI(z) + SHL20(n));
    return s * z;
}

static double k_sin(double x, double y, int iy) {
    static const double half = 5.00000000000000000000e-01, S1 = -1.66666666666666324348e-01,
        S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
        S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08,
        S6 = 1.58969099521155010221e-10;
    double z, r, v;
    int ix;
    ix = HI(x) & 0x7fffffff;
    if (ix < 0x3e400000) { if ((int)x == 0) return x; }
    z = x * x;
    v = z * x;
    r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    else return x - ((z * (half * y - v * r) - y) - v * S1);
}

static double k_cos(double x, double y) {
    static const double one = 1.0, C1 = 4.16666666666666019037e-02,
        C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
        C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09,
        C6 = -1.13596475577881948265e-11;
    double a, hz, z, r, qx;
    int ix;
    ix = HI(x) & 0x7fffffff;
    if (ix < 0x3e400000) { if (((int)x) == 0) return one; }
    z = x * x;
    r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333) return one - (0.5 * z - (z * r - x * y));
    else {
        if (ix > 0x3fe90000) qx = 0.28125;
        else qx = WORDS(ix - 0x00200000, 0);
        hz = 0.5 * z - qx;
        a = one - qx;
        return a - (hz - (z * r - x * y));
    }
}

static double k_tan(double x, double y, int iy) {
    static const double T[] = {
        3.33333333333334091986e-01, 1.33333333333201242699e-01, 5.39682539762260521377e-02,
        2.18694882948595424599e-02, 8.86323982359930005737e-03, 3.59207910759131235356e-03,
        1.45620945432529025516e-03, 5.88041240820264096874e-04, 2.46463134818469906812e-04,
        7.81794442939557092300e-05, 7.14072491382608190305e-05, -1.85586374855275456654e-05,
        2.59073051863633712884e-05};
    static const double one = 1.0, pio4 = 7.85398163397448278999e-01,
        pio4lo = 3.06161699786838301793e-17;
    double z, r, v, w, s;
    int ix, hx;
    hx = HI(x);
    ix = hx & 0x7fffffff;
    if (ix < 0x3e300000) {
        if ((int)x == 0) {
            if ((((ix | (int)LO(x)) | (iy + 1))) == 0) return one / FABS(x);
            else {
                if (iy == 1) return x;
                else {
                    double a, t;
                    z = w = x + y;
                    z = SETLO(z, 0);
                    v = y - (z - x);
                    t = a = -one / w;
                    t = SETLO(t, 0);
                    s = one + t * z;
                    return t + a * (s + t * v);
                }
            }
        }
    }
    if (ix >= 0x3FE59428) {
        if (hx < 0) { x = -x; y = -y; }
        z = pio4 - x;
        w = pio4lo - y;
        x = z + w;
        y = 0.0;
    }
    z = x * x;
    w = z * z;
    r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    s = z * x;
    r = y + z * (s * (r + v) + y);
    r += T[0] * s;
    w = x + r;
    if (ix >= 0x3FE59428) {
        v = (double)iy;
        return (double)(1 - ((hx >> 30) & 2)) * (v - 2.0 * (x - (w * w / (w + v) - r)));
    }
    if (iy == 1) return w;
    else {
        double a, t;
        z = w;
        z = SETLO(z, 0);
        v = r - (z - x);
        t = a = -1.0 / w;
        t = SETLO(t, 0);
        s = 1.0 + t * z;
        return t + a * (s + t * v);
    }
}

static const int two_over_pi[] = {
0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62,
0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A,
0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41,
0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08,
0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
};

static const int npio2_hw[] = {
0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
0x404858EB, 0x404921FB,
};

static double fd_floor_nonneg(double x) { return (double)(int64_t)x; }

static int k_rem_pio2(double *x, double *y, int e0, int nx, int prec, const int *ipio2) {
    static const int init_jk[] = {2, 3, 4, 6};
    static const double PIo2[] = {
        1.57079625129699707031e+00, 7.54978941586159635335e-08, 5.39030252995776476554e-15,
        3.28200341580791294123e-22, 1.27065575308067607349e-29, 1.22933308981111328932e-36,
        2.73370053816464559624e-44, 2.16741683877804819444e-51};
    static const double zero = 0.0, one = 1.0, two24 = 1.67772160000000000000e+07,
        twon24 = 5.96046447753906250000e-08;
    int jz, jx, jv, jp, jk, carry, n, iq[20], i, j, k, m, q0, ih;
    double z, fw, f[20], fq[20], q[20];
    jk = init_jk[prec];
    jp = jk;
    jx = nx - 1;
    jv = (e0 - 3) / 24; if (jv < 0) jv = 0;
    q0 = e0 - 24 * (jv + 1);
    j = jv - jx; m = jx + jk;
    for (i = 0; i <= m; i++, j++) f[i] = (j < 0) ? zero : (double)ipio2[j];
    for (i = 0; i <= jk; i++) {
        for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
        q[i] = fw;
    }
    jz = jk;
recompute:
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
        fw = (double)((int)(twon24 * z));
        iq[i] = (int)(z - two24 * fw);
        z = q[j - 1] + fw;
    }
    z = fd_scalbn(z, q0);
    z -= 8.0 * fd_floor_nonneg(z * 0.125);
    n = (int)z;
    z -= (double)n;
    ih = 0;
    if (q0 > 0) {
        i = (iq[jz - 1] >> (24 - q0)); n += i;
        iq[jz - 1] -= i << (24 - q0);
        ih = iq[jz - 1] >> (23 - q0);
    } else if (q0 == 0) ih = iq[jz - 1] >> 23;
    else if (z >= 0.5) ih = 2;
    if (ih > 0) {
        n += 1; carry = 0;
        for (i = 0; i < jz; i++) {
            j = iq[i];
            if (carry == 0) {
                if (j != 0) { carry = 1; iq[i] = 0x1000000 - j; }
            } else iq[i] = 0xffffff - j;
        }
        if (q0 > 0) {
            switch (q0) {
            case 1: iq[jz - 1] &= 0x7fffff; break;
            case 2: iq[jz - 1] &= 0x3fffff; break;
            }
        }
        if (ih == 2) {
            z = one - z;
            if (carry != 0) z -= fd_scalbn(one, q0);
        }
    }
    if (z == zero) {
        j = 0;
        for (i = jz - 1; i >= jk; i--) j |= iq[i];
        if (j == 0) {
            for (k = 1; iq[jk - k] == 0; k++);
            for (i = jz + 1; i <= jz + k; i++) {
                f[jx + i] = (double)ipio2[jv + i];
                for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
                q[i] = fw;
            }
            jz += k;
            goto recompute;
        }
    }
    if (z == 0.0) {
        jz -= 1; q0 -= 24;
        while (iq[jz] == 0) { jz--; q0 -= 24; }
    } else {
        z = fd_scalbn(z, -q0);
        if (z >= two24) {
            fw = (double)((int)(twon24 * z));
            iq[jz] = (int)(z - two24 * fw);
            jz += 1; q0 += 24;
            iq[jz] = (int)fw;
        } else iq[jz] = (int)z;
    }
    fw = fd_scalbn(one, q0);
    for (i = jz; i >= 0; i--) { q[i] = fw * (double)iq[i]; fw *= twon24; }
    for (i = jz; i >= 0; i--) {
        for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) fw += PIo2[k] * q[i + k];
        fq[jz - i] = fw;
    }
    fw = 0.0;
    for (i = jz; i >= 0; i--) fw += fq[i];
    y[0] = (ih == 0) ? fw : -fw;
    fw = fq[0] - fw;
    for (i = 1; i <= jz; i++) fw += fq[i];
    y[1] = (ih == 0) ? fw : -fw;
    return n & 7;
}

static int fd_rem_pio2(double x, double *y) {
    static const double zero = 0.0, half = 0.5, two24 = 1.67772160000000000000e+07,
        invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
        pio2_1t = 6.07710050650619224932e-11, pio2_2 = 6.07710050630396597660e-11,
        pio2_2t = 2.02226624879595063154e-21, pio2_3 = 2.02226624871116645580e-21,
        pio2_3t = 8.47842766036889956997e-32;
    double z, w, t, r, fn;
    double tx[3];
    int e0, i, j, nx, n, ix, hx;
    hx = HI(x);
    ix = hx & 0x7fffffff;
    if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; return 0; }
    if (ix < 0x4002d97c) {
        if (hx > 0) {
            z = x - pio2_1;
            if (ix != 0x3ff921fb) {
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
            } else {
                z -= pio2_2;
                y[0] = z - pio2_2t;
                y[1] = (z - y[0]) - pio2_2t;
            }
            return 1;
        } else {
            z = x + pio2_1;
            if (ix != 0x3ff921fb) {
                y[0] = z + pio2_1t;
                y[1] = (z - y[0]) + pio2_1t;
            } else {
                z += pio2_2;
                y[0] = z + pio2_2t;
                y[1] = (z - y[0]) + pio2_2t;
            }
            return -1;
        }
    }
    if (ix <= 0x413921fb) {
        t = FABS(x);
        n = (int)(t * invpio2 + half);
        fn = (double)n;
        r = t - fn * pio2_1;
        w = fn * pio2_1t;
        if (n < 32 && ix != npio2_hw[n - 1]) {
            y[0] = r - w;
        } else {
            j = ix >> 20;
            y[0] = r - w;
            i = j - (((HI(y[0])) >> 20) & 0x7ff);
            if (i > 16) {
                t = r;
                w = fn * pio2_2;
                r = t - w;
                w = fn * pio2_2t - ((t - r) - w);
                y[0] = r - w;
                i = j - (((HI(y[0])) >> 20) & 0x7ff);
                if (i > 49) {
                    t = r;
                    w = fn * pio2_3;
                    r = t - w;
                    w = fn * pio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
        else return n;
    }
    if (ix >= 0x7ff00000) { y[0] = y[1] = x - x; return 0; }
    e0 = (ix >> 20) - 1046;
    /* e0 can be as low as -3; shift it as unsigned, which C defines. */
    z = WORDS((int)((unsigned)ix - ((unsigned)e0 << 20)), LO(x));
    for (i = 0; i < 2; i++) {
        tx[i] = (double)((int)(z));
        z = (z - tx[i]) * two24;
    }
    tx[2] = z;
    nx = 3;
    while (tx[nx - 1] == zero) nx--;
    n = k_rem_pio2(tx, y, e0, nx, 2, two_over_pi);
    if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
    return n;
}

static double fd_sin(double x) {
    double y[2], z = 0.0;
    int n, ix;
    ix = HI(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return k_sin(x, z, 0);
    else if (ix >= 0x7ff00000) return x - x;
    else {
        n = fd_rem_pio2(x, y);
        switch (n & 3) {
        case 0: return k_sin(y[0], y[1], 1);
        case 1: return k_cos(y[0], y[1]);
        case 2: return -k_sin(y[0], y[1], 1);
        default: return -k_cos(y[0], y[1]);
        }
    }
}

static double fd_cos(double x) {
    double y[2], z = 0.0;
    int n, ix;
    ix = HI(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return k_cos(x, z);
    else if (ix >= 0x7ff00000) return x - x;
    else {
        n = fd_rem_pio2(x, y);
        switch (n & 3) {
        case 0: return k_cos(y[0], y[1]);
        case 1: return -k_sin(y[0], y[1], 1);
        case 2: return -k_cos(y[0], y[1]);
        default: return k_sin(y[0], y[1], 1);
        }
    }
}

static double fd_tan(double x) {
    double y[2], z = 0.0;
    int n, ix;
    ix = HI(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return k_tan(x, z, 1);
    else if (ix >= 0x7ff00000) return x - x;
    else {
        n = fd_rem_pio2(x, y);
        return k_tan(y[0], y[1], 1 - ((n & 1) << 1));
    }
}

static const double one = 1.0, huge = 1.000e+300,
    pio2_hi = 1.57079632679489655800e+00, pio2_lo = 6.12323399573676603587e-17,
    pio4_hi = 7.85398163397448278999e-01,
    pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01,
    pS2 = 2.01212532134862925881e-01, pS3 = -4.00555345006794114027e-02,
    pS4 = 7.91534994289814532176e-04, pS5 = 3.47933107596021167570e-05,
    qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00,
    qS3 = -6.88283971605453293030e-01, qS4 = 7.70381505559019352791e-02;

static double fd_asin(double x) {
    double t = 0, w, p, q, c, r, s;
    int hx, ix;
    hx = HI(x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {
        if (((ix - 0x3ff00000) | (int)LO(x)) == 0) return x * pio2_hi + x * pio2_lo;
        return (x - x) / (x - x);
    } else if (ix < 0x3fe00000) {
        if (ix < 0x3e400000) {
            if (huge + x > one) return x;
        } else
            t = x * x;
        p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
        q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
        w = p / q;
        return x + x * w;
    }
    w = one - FABS(x);
    t = w * 0.5;
    p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
    q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
    s = fd_sqrt(t);
    if (ix >= 0x3FEF3333) {
        w = p / q;
        t = pio2_hi - (2.0 * (s + s * w) - pio2_lo);
    } else {
        w = s;
        w = SETLO(w, 0);
        c = (t - w * w) / (s + w);
        r = p / q;
        p = 2.0 * s * r - (pio2_lo - 2.0 * c);
        q = pio4_hi - 2.0 * w;
        t = pio4_hi - (p - q);
    }
    if (hx > 0) return t; else return -t;
}

static double fd_acos(double x) {
    static const double pi = 3.14159265358979311600e+00;
    double z, p, q, r, w, s, c, df;
    int hx, ix;
    hx = HI(x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {
        if (((ix - 0x3ff00000) | (int)LO(x)) == 0) {
            if (hx > 0) return 0.0;
            else return pi + 2.0 * pio2_lo;
        }
        return (x - x) / (x - x);
    }
    if (ix < 0x3fe00000) {
        if (ix <= 0x3c600000) return pio2_hi + pio2_lo;
        z = x * x;
        p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        r = p / q;
        return pio2_hi - (x - (pio2_lo - x * r));
    } else if (hx < 0) {
        z = (one + x) * 0.5;
        p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        s = fd_sqrt(z);
        r = p / q;
        w = r * s - pio2_lo;
        return pi - 2.0 * (s + w);
    } else {
        z = (one - x) * 0.5;
        s = fd_sqrt(z);
        df = s;
        df = SETLO(df, 0);
        c = (z - df * df) / (s + df);
        p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        r = p / q;
        w = r * s + c;
        return 2.0 * (df + w);
    }
}

static double fd_atan(double x) {
    static const double atanhi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
        9.82793723247329054082e-01, 1.57079632679489655800e+00};
    static const double atanlo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
        1.39033110312309984516e-17, 6.12323399573676603587e-17};
    static const double aT[] = {
        3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01,
        -1.11111104054623557880e-01, 9.09088713343650656196e-02, -7.69187620504482999495e-02,
        6.66107313738753120669e-02, -5.83357013379057348645e-02, 4.97687799461593236017e-02,
        -3.65315727442169155270e-02, 1.62858201153657823623e-02};
    double w, s1, s2, z;
    int ix, hx, id;
    hx = HI(x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x44100000) {
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && (LO(x) != 0))) return x + x;
        if (hx > 0) return atanhi[3] + atanlo[3];
        else return -atanhi[3] - atanlo[3];
    }
    if (ix < 0x3fdc0000) {
        if (ix < 0x3e200000) {
            if (huge + x > one) return x;
        }
        id = -1;
    } else {
        x = FABS(x);
        if (ix < 0x3ff30000) {
            if (ix < 0x3fe60000) { id = 0; x = (2.0 * x - one) / (2.0 + x); }
            else { id = 1; x = (x - one) / (x + one); }
        } else {
            if (ix < 0x40038000) { id = 2; x = (x - 1.5) / (one + 1.5 * x); }
            else { id = 3; x = -1.0 / x; }
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    else {
        z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
        return (hx < 0) ? -z : z;
    }
}

static double fd_atan2(double y, double x) {
    static const double tiny = 1.0e-300, zero = 0.0, pi_o_4 = 7.8539816339744827900E-01,
        pi_o_2 = 1.5707963267948965580E+00, pi = 3.1415926535897931160E+00,
        pi_lo = 1.2246467991473531772E-16;
    double z;
    int k, m, hx, hy, ix, iy;
    unsigned lx, ly;
    hx = HI(x); ix = hx & 0x7fffffff;
    lx = LO(x);
    hy = HI(y); iy = hy & 0x7fffffff;
    ly = LO(y);
    if ((((unsigned)ix | ((lx | -lx) >> 31)) > 0x7ff00000u) || (((unsigned)iy | ((ly | -ly) >> 31)) > 0x7ff00000u))
        return x + y;
    if ((((unsigned)hx - 0x3ff00000u) | lx) == 0) return fd_atan(y);
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
    if (((unsigned)iy | ly) == 0) {
        switch (m) {
        case 0:
        case 1: return y;
        case 2: return pi + tiny;
        case 3: return -pi - tiny;
        }
    }
    if (((unsigned)ix | lx) == 0) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;
    if (ix == 0x7ff00000) {
        if (iy == 0x7ff00000) {
            switch (m) {
            case 0: return pi_o_4 + tiny;
            case 1: return -pi_o_4 - tiny;
            case 2: return 3.0 * pi_o_4 + tiny;
            case 3: return -3.0 * pi_o_4 - tiny;
            }
        } else {
            switch (m) {
            case 0: return zero;
            case 1: return -zero;
            case 2: return pi + tiny;
            case 3: return -pi - tiny;
            }
        }
    }
    if (iy == 0x7ff00000) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;
    k = (iy - ix) >> 20;
    if (k > 60) z = pi_o_2 + 0.5 * pi_lo;
    else if (hx < 0 && k < -60) z = 0.0;
    else z = fd_atan(FABS(y / x));
    switch (m) {
    case 0: return z;
    case 1: return SETHI(z, (int)((unsigned)HI(z) ^ 0x80000000u));
    case 2: return pi - (z - pi_lo);
    default: return (z - pi_lo) - pi;
    }
}

static double fd_sinh(double x) {
    static const double shuge = 1.0e307;
    double t, w, h;
    int ix, jx;
    unsigned lx;
    jx = HI(x);
    ix = jx & 0x7fffffff;
    if (ix >= 0x7ff00000) return x + x;
    h = 0.5;
    if (jx < 0) h = -h;
    if (ix < 0x40360000) {
        if (ix < 0x3e300000)
            if (shuge + x > one) return x;
        t = fd_expm1(FABS(x));
        if (ix < 0x3ff00000) return h * (2.0 * t - t * t / (t + one));
        return h * (t + t / (t + one));
    }
    if (ix < 0x40862E42) return h * fd_exp(FABS(x));
    lx = LO(x);
    if (ix < 0x408633CE || ((ix == 0x408633ce) && (lx <= (unsigned)0x8fb9f87d))) {
        w = fd_exp(0.5 * FABS(x));
        t = h * w;
        return t * w;
    }
    return x * shuge;
}

static double fd_cosh(double x) {
    static const double half = 0.5, huge2 = 1.0e300;
    double t, w;
    int ix;
    unsigned lx;
    ix = HI(x);
    ix &= 0x7fffffff;
    if (ix >= 0x7ff00000) return x * x;
    if (ix < 0x3fd62e43) {
        t = fd_expm1(FABS(x));
        w = one + t;
        if (ix < 0x3c800000) return w;
        return one + (t * t) / (w + w);
    }
    if (ix < 0x40360000) {
        t = fd_exp(FABS(x));
        return half * t + half / t;
    }
    if (ix < 0x40862E42) return half * fd_exp(FABS(x));
    lx = LO(x);
    if (ix < 0x408633CE || ((ix == 0x408633ce) && (lx <= (unsigned)0x8fb9f87d))) {
        w = fd_exp(half * FABS(x));
        t = half * w;
        return t * w;
    }
    return huge2 * huge2;
}

static double fd_tanh(double x) {
    static const double two = 2.0, tiny = 1.0e-300;
    double t, z;
    int jx, ix;
    jx = HI(x);
    ix = jx & 0x7fffffff;
    if (ix >= 0x7ff00000) {
        if (jx >= 0) return one / x + one;
        else return one / x - one;
    }
    if (ix < 0x40360000) {
        if (ix < 0x3c800000) return x * (one + x);
        if (ix >= 0x3ff00000) {
            t = fd_expm1(two * FABS(x));
            z = one - two / (t + two);
        } else {
            t = fd_expm1(-two * FABS(x));
            z = -t / (t + two);
        }
    } else {
        z = one - tiny;
    }
    return (jx >= 0) ? z : -z;
}

static double fd_cbrt(double x) {
    static const unsigned B1 = 715094163, B2 = 696219795;
    static const double C = 5.42857142857142815906e-01, D = -7.05306122448979611050e-01,
        E = 1.41428571428571436819e+00, F = 1.60714285714285720630e+00,
        G = 3.57142857142857150787e-01;
    int hx;
    double r, s, t = 0.0, w;
    unsigned sign;
    hx = HI(x);
    sign = (unsigned)hx & 0x80000000u;
    hx = (int)((unsigned)hx ^ sign);
    if (hx >= 0x7ff00000) return (x + x);
    if ((hx | (int)LO(x)) == 0) return (x);
    x = SETHI(x, hx);
    if (hx < 0x00100000) {
        t = SETHI(t, 0x43500000);
        t *= x;
        t = SETHI(t, (int)((unsigned)(HI(t) / 3) + B2));
    } else
        t = SETHI(t, (int)((unsigned)(hx / 3) + B1));
    r = t * t / x;
    s = C + r * t;
    t *= G + F / (s + E + D / s);
    t = SETLO(t, 0);
    t = SETHI(t, HI(t) + 0x00000001);
    s = t * t;
    r = x / s;
    w = t + t;
    r = (r - t) / (w + r);
    t = t + t * r;
    t = SETHI(t, (int)((unsigned)HI(t) | sign));
    return (t);
}

static double fd_hypot(double x, double y) {
    double a = x, b = y, t1, t2, y1, y2, w;
    int j, k, ha, hb;
    ha = HI(x) & 0x7fffffff;
    hb = HI(y) & 0x7fffffff;
    if (hb > ha) { a = y; b = x; j = ha; ha = hb; hb = j; } else { a = x; b = y; }
    a = SETHI(a, ha);
    b = SETHI(b, hb);
    if ((ha - hb) > 0x3c00000) { return a + b; }
    k = 0;
    if (ha > 0x5f300000) {
        if (ha >= 0x7ff00000) {
            w = a + b;
            if (((ha & 0xfffff) | (int)LO(a)) == 0) w = a;
            if (((hb ^ 0x7ff00000) | (int)LO(b)) == 0) w = b;
            return w;
        }
        ha -= 0x25800000; hb -= 0x25800000; k += 600;
        a = SETHI(a, ha);
        b = SETHI(b, hb);
    }
    if (hb < 0x20b00000) {
        if (hb <= 0x000fffff) {
            if ((hb | (int)(LO(b))) == 0) return a;
            t1 = 0;
            t1 = SETHI(t1, 0x7fd00000);
            b *= t1;
            a *= t1;
            k -= 1022;
        } else {
            ha += 0x25800000;
            hb += 0x25800000;
            k -= 600;
            a = SETHI(a, ha);
            b = SETHI(b, hb);
        }
    }
    w = a - b;
    if (w > b) {
        t1 = 0;
        t1 = SETHI(t1, ha);
        t2 = a - t1;
        w = fd_sqrt(t1 * t1 - (b * (-b) - t2 * (t1 + a)));
    } else {
        a = a + a;
        y1 = 0;
        y1 = SETHI(y1, hb);
        y2 = b - y1;
        t1 = 0;
        t1 = SETHI(t1, ha + 0x00100000);
        t2 = a - t1;
        w = fd_sqrt(t1 * y1 - (w * (-w) - (t1 * y2 + t2 * b)));
    }
    if (k != 0) {
        t1 = 1.0;
        t1 = SETHI(t1, HI(t1) + SHL20(k));
        return t1 * w;
    } else return w;
}

/* C's double arithmetic must be binary64 without extended precision. */
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "neura kernels need FLT_EVAL_METHOD == 0 (SSE2 or another binary64 unit, not x87)"
#endif

static neura_f64 canonical(double x) {
    return x != x ? BITS(UINT64_C(0x7FF8000000000000)) : x;
}

#define NEURA_MATH_UNARY(name) \
    neura_f64 neura_k_math_##name(neura_f64 x) { return canonical(fd_##name(x)); }
#define NEURA_MATH_BINARY(name, a, b) \
    neura_f64 neura_k_math_##name(neura_f64 a, neura_f64 b) { return canonical(fd_##name(a, b)); }

NEURA_MATH_UNARY(sqrt)
NEURA_MATH_UNARY(cbrt)
NEURA_MATH_BINARY(hypot, x, y)
NEURA_MATH_UNARY(exp)
NEURA_MATH_UNARY(expm1)
NEURA_MATH_UNARY(log)
NEURA_MATH_UNARY(log1p)
NEURA_MATH_UNARY(log10)
NEURA_MATH_UNARY(log2)
NEURA_MATH_BINARY(pow, x, y)
NEURA_MATH_UNARY(sin)
NEURA_MATH_UNARY(cos)
NEURA_MATH_UNARY(tan)
NEURA_MATH_UNARY(asin)
NEURA_MATH_UNARY(acos)
NEURA_MATH_UNARY(atan)
NEURA_MATH_BINARY(atan2, y, x)
NEURA_MATH_UNARY(sinh)
NEURA_MATH_UNARY(cosh)
NEURA_MATH_UNARY(tanh)

/* Math.to_radians and to_degrees: one multiplication by the nearest float64
 * to pi/180 and to 180/pi. */
neura_f64 neura_k_math_to_radians(neura_f64 degrees) {
    return canonical(degrees * BITS(UINT64_C(0x3F91DF46A2529D39)));
}

neura_f64 neura_k_math_to_degrees(neura_f64 radians) {
    return canonical(radians * BITS(UINT64_C(0x404CA5DC1A63C1F8)));
}
