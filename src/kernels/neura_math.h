/* Std.Math.Math kernels: every function the module exports, bit-identical
 * to stdlib/std/math/math.nra (fdlibm 5.3), including subnormals, signed
 * zeros, infinities, and NaN. Every NaN result is the canonical NaN
 * 0x7FF8000000000000. No kernel calls libm. The step charges are constants:
 * the work of each function is bounded independently of its arguments. */
#ifndef NEURA_MATH_H
#define NEURA_MATH_H

#include "neura_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Math.sqrt: correctly rounded square root; sqrt(-0.0) is -0.0. */
NEURA_KERNEL("neura.math.sqrt", 1, "1")
neura_f64 neura_k_math_sqrt(neura_f64 x);

/* Math.cbrt: cube root (s_cbrt.c). */
NEURA_KERNEL("neura.math.cbrt", 1, "2")
neura_f64 neura_k_math_cbrt(neura_f64 x);

/* Math.hypot: the square root of x squared plus y squared, without undue overflow (e_hypot.c). */
NEURA_KERNEL("neura.math.hypot", 1, "2")
neura_f64 neura_k_math_hypot(neura_f64 x, neura_f64 y);

/* Math.exp: e to the x (e_exp.c). */
NEURA_KERNEL("neura.math.exp", 1, "2")
neura_f64 neura_k_math_exp(neura_f64 x);

/* Math.expm1: e to the x, minus 1 (s_expm1.c). */
NEURA_KERNEL("neura.math.expm1", 1, "2")
neura_f64 neura_k_math_expm1(neura_f64 x);

/* Math.log: natural logarithm (e_log.c). */
NEURA_KERNEL("neura.math.log", 1, "2")
neura_f64 neura_k_math_log(neura_f64 x);

/* Math.log1p: log(1 + x) (s_log1p.c). */
NEURA_KERNEL("neura.math.log1p", 1, "2")
neura_f64 neura_k_math_log1p(neura_f64 x);

/* Math.log10: base-10 logarithm (e_log10.c). */
NEURA_KERNEL("neura.math.log10", 1, "2")
neura_f64 neura_k_math_log10(neura_f64 x);

/* Math.log2: base-2 logarithm, exact for powers of two (after e_log10.c). */
NEURA_KERNEL("neura.math.log2", 1, "2")
neura_f64 neura_k_math_log2(neura_f64 x);

/* Math.pow: x to the y (e_pow.c, 2004). */
NEURA_KERNEL("neura.math.pow", 1, "4")
neura_f64 neura_k_math_pow(neura_f64 x, neura_f64 y);

/* Math.sin: sine (s_sin.c, with the reduction of e_rem_pio2.c and k_rem_pio2.c). */
NEURA_KERNEL("neura.math.sin", 1, "4")
neura_f64 neura_k_math_sin(neura_f64 x);

/* Math.cos: cosine (s_cos.c). */
NEURA_KERNEL("neura.math.cos", 1, "4")
neura_f64 neura_k_math_cos(neura_f64 x);

/* Math.tan: tangent (s_tan.c, k_tan.c 2004). */
NEURA_KERNEL("neura.math.tan", 1, "4")
neura_f64 neura_k_math_tan(neura_f64 x);

/* Math.asin: arc sine (e_asin.c). */
NEURA_KERNEL("neura.math.asin", 1, "2")
neura_f64 neura_k_math_asin(neura_f64 x);

/* Math.acos: arc cosine (e_acos.c). */
NEURA_KERNEL("neura.math.acos", 1, "2")
neura_f64 neura_k_math_acos(neura_f64 x);

/* Math.atan: arc tangent (s_atan.c). */
NEURA_KERNEL("neura.math.atan", 1, "2")
neura_f64 neura_k_math_atan(neura_f64 x);

/* Math.atan2: arc tangent of y/x in the quadrant of (x, y) (e_atan2.c). */
NEURA_KERNEL("neura.math.atan2", 1, "2")
neura_f64 neura_k_math_atan2(neura_f64 y, neura_f64 x);

/* Math.sinh: hyperbolic sine (e_sinh.c). */
NEURA_KERNEL("neura.math.sinh", 1, "2")
neura_f64 neura_k_math_sinh(neura_f64 x);

/* Math.cosh: hyperbolic cosine (e_cosh.c). */
NEURA_KERNEL("neura.math.cosh", 1, "2")
neura_f64 neura_k_math_cosh(neura_f64 x);

/* Math.tanh: hyperbolic tangent (s_tanh.c). */
NEURA_KERNEL("neura.math.tanh", 1, "2")
neura_f64 neura_k_math_tanh(neura_f64 x);

/* Math.to_radians: degrees to radians. */
NEURA_KERNEL("neura.math.to_radians", 1, "1")
neura_f64 neura_k_math_to_radians(neura_f64 degrees);

/* Math.to_degrees: radians to degrees. */
NEURA_KERNEL("neura.math.to_degrees", 1, "1")
neura_f64 neura_k_math_to_degrees(neura_f64 radians);

#ifdef __cplusplus
}
#endif

#endif
