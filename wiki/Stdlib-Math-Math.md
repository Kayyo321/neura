# `Std.Math.Math`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Math.Math`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Math.Math`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Math** · [`export const Pi: float64 = float64.from_bits(4614256656552045848)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L11)
  The nearest float64 approximation to pi.
- **Math** · [`export const Tau: float64 = float64.from_bits(4618760256179416344)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L13)
  The nearest float64 approximation to twice pi.
- **Math** · [`export const E: float64 = float64.from_bits(4613303445314885481)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L15)
  The nearest float64 approximation to Euler's number.
- **Math** · [`export const Ln2: float64 = float64.from_bits(4604418534313441775)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L17)
  The nearest float64 approximation to the natural logarithm of two.
- **Math** · [`export const Ln10: float64 = float64.from_bits(4612367379483415830)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L19)
  The nearest float64 approximation to the natural logarithm of ten.
- **Math** · [`export const Sqrt2: float64 = float64.from_bits(4609047870845172685)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L21)
  The nearest float64 approximation to the square root of two.
- **Math** · [`export let sqrt = func(x: float64) -> float64 kernel("neura.math.sqrt", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L26)
  Correctly rounded square root, to nearest with ties to even. sqrt(-0.0) is -0.0; other negatives give NaN. Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let cbrt = func(x: float64) -> float64 kernel("neura.math.cbrt", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L30)
  The cube root (s_cbrt.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let hypot = func(x: float64, y: float64) -> float64 kernel("neura.math.hypot", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L34)
  sqrt(x*x + y*y) without undue overflow or underflow (e_hypot.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let exp = func(x: float64) -> float64 kernel("neura.math.exp", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L38)
  e raised to x (e_exp.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let expm1 = func(x: float64) -> float64 kernel("neura.math.expm1", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L42)
  exp(x) - 1, accurate near zero (s_expm1.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let log = func(x: float64) -> float64 kernel("neura.math.log", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L46)
  The natural logarithm (e_log.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let log1p = func(x: float64) -> float64 kernel("neura.math.log1p", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L50)
  log(1 + x), accurate near zero (s_log1p.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let log10 = func(x: float64) -> float64 kernel("neura.math.log10", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L54)
  The base-10 logarithm (e_log10.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let log2 = func(x: float64) -> float64 kernel("neura.math.log2", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L59)
  The base-2 logarithm, shaped like e_log10.c: for x = 2^k * m it returns (k + i) + log(m') / ln 2, where m' is m or m/2 so that the logarithm is taken near 1, and exactly k for a power of two. Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let pow = func(x: float64, y: float64) -> float64 kernel("neura.math.pow", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L63)
  x raised to y (e_pow.c, 2004). pow(±1, ±∞) and pow(1, NaN) are NaN, as in fdlibm. Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let sin = func(x: float64) -> float64 kernel("neura.math.sin", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L67)
  The sine (s_sin.c, with the argument reduction of e_rem_pio2.c and k_rem_pio2.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let cos = func(x: float64) -> float64 kernel("neura.math.cos", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L71)
  The cosine (s_cos.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let tan = func(x: float64) -> float64 kernel("neura.math.tan", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L75)
  The tangent (s_tan.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let asin = func(x: float64) -> float64 kernel("neura.math.asin", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L79)
  The arc sine (e_asin.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let acos = func(x: float64) -> float64 kernel("neura.math.acos", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L83)
  The arc cosine (e_acos.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let atan = func(x: float64) -> float64 kernel("neura.math.atan", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L87)
  The arc tangent (s_atan.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let atan2 = func(y: float64, x: float64) -> float64 kernel("neura.math.atan2", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L91)
  The angle of the point (x, y): the arc tangent of y/x in the point's quadrant (e_atan2.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let sinh = func(x: float64) -> float64 kernel("neura.math.sinh", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L95)
  The hyperbolic sine (e_sinh.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let cosh = func(x: float64) -> float64 kernel("neura.math.cosh", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L99)
  The hyperbolic cosine (e_cosh.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let tanh = func(x: float64) -> float64 kernel("neura.math.tanh", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L103)
  The hyperbolic tangent (s_tanh.c). Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let to_radians = func(degrees: float64) -> float64 kernel("neura.math.to_radians", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L107)
  Degrees to radians: x times the nearest float64 to pi/180. Tagged `#Pure`, `#ThreadSafe`.
- **Math** · [`export let to_degrees = func(radians: float64) -> float64 kernel("neura.math.to_degrees", 1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L111)
  Radians to degrees: x times the nearest float64 to 180/pi. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 10.1 `Math`

`Math` works on `float64` only and needs N1. Code using another float type converts to `float64`, calls `Math`, and converts back.

- **Constants:** `Pi`, `Tau`, `E`, `Ln2`, `Ln10`, `Sqrt2`, each the nearest `float64`.
- **`sqrt`** is correctly rounded. It unpacks the operand, computes the integer square root of the significand shifted to an even exponent, one result bit per step with a running remainder, and rounds to nearest, ties to even. Negative operands other than `-0.0` give NaN.
- **Other functions:** `cbrt`, `hypot`, `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `to_radians`, and `to_degrees`.

These functions are a port of fdlibm 5.3, operation for operation, including its argument reduction, its polynomial coefficients (written as exact bit patterns through `Float64.from_bits`), and its special cases. fdlibm relies only on IEEE 754 double arithmetic with round-to-nearest and no fused or reassociated operations, which is what [Numbers](Numbers) guarantees, and on reading and writing the high and low 32-bit words of a double, which is N1. The functions are therefore defined by their source: every runtime produces bit-identical results. Every NaN result is the canonical NaN. fdlibm's copyright notice must be kept in the ported files, following the third-party rule in [CONTRIBUTING](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md).

Every function binds its `neura.math.*` kernel (P2a, [Kernels](Kernels)), a C transliteration of the same port, so a call costs one to four steps and adds no float circuit to the image. The Neura port is the kernels' oracle: `tests/stdlib/math/kernel-oracle.nra` compares the two bit for bit on an edge corpus and seeded inputs, and the vector tests check the kernels against fdlibm's results.

### 12.2 `Std.Random`

**`Random`** implements the xoshiro256** generator using shifts, rotations, exclusive ors, and multiplication by 5 and 9. Use the `uint64` recipes in [section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics). `next_gaussian` depends on the library's `Math` module, and `from_entropy` requires a registered entropy provider.

- **State.** Four `uint64` words `s0` through `s3`.
- **Step.** The output is `rotate_left(s1 × 5, 7) × 9`, wrapping. Then, with `t` = `s1` shifted left by 17: `s2 ^= s0`; `s3 ^= s1`; `s1 ^= s2`; `s0 ^= s3`; `s2 ^= t`; `s3 = rotate_left(s3, 45)`.
- **Seeding.** The module function `seeded(seed: uint64)` fills the four words with successive outputs of SplitMix64 started at the seed: add 0x9E3779B97F4A7C15 to the state; take `z` as the new state; `z = (z ^ (z >> 30)) × 0xBF58476D1CE4E5B9`; `z = (z ^ (z >> 27)) × 0x94D049BB133111EB`; output `z ^ (z >> 31)`; all wrapping. The source writes these constants in decimal (A5). `from_entropy() → Result<Random>` requests 8 bytes from `neura.entropy.fill`, reads them as a little-endian `uint64`, and seeds with that.

| Member | Behavior |
| --- | --- |
| `next_uint64() → uint64` | One step. |
| `next_uint32() → uint32` | The high 32 bits of one step. |
| `next_bool() → bool` | The top bit of one step. |
| `next_int_below(bound: int) → int` | Uniform in 0 up to but excluding `bound`. Take the smallest all-ones mask covering `bound - 1`; draw `next_uint32` masked until the result is below `bound`. A bound below 1 traps. |
| `next_int_between(low: int, high: int) → int` | Uniform from `low` up to but excluding `high`. The span is computed in `int64`, sampled with the same mask-and-reject loop on `uint32`, and added to `low` in `int64`. `low` not below `high` traps. |
| `next_float64() → float64` | The top 53 bits of one step, converted to `float64` (exact) and multiplied by 2^-53: uniform in 0 up to but excluding 1. |
| `next_gaussian() → float64` | Marsaglia's polar method. Each accepted pair yields two values; the second is kept in the generator and returned by the next call. |
| `fill_bytes(count: int)` | A new built-in `uint8` list. Each step supplies 8 bytes, least significant first; a final partial group uses the low bytes. |
| `shuffle<T>(items: T[list])` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. Generic, with no callback. |
| `choose<T>(items: T[list]) → T` | Traps with `no such element` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets, and the library offers no cryptographic generator.

**`Uuid`** is an immutable value of 16 bytes. The module function `random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `to_string` writes the 36-character hyphenated form in lowercase. `parse(text) → Result<Uuid>` accepts either case and needs T1. It has `compare_to`, `equals`, and `hash_code` over its bytes.

### 18.10 Math

- **fdlibm port.** `Math` is a port of fdlibm 5.3 written without a reference copy of the sources; Sun's notice is kept in the Neura port (`tests/stdlib/math/support/reference-math.nra` and `reference-math-trig.nra`, the kernels' oracle since `Math` binds the kernels), in `src/kernels/`, and in the test transliteration. The Neura functions agree bit for bit with a C transliteration of the same port on 9,148 vector cases (`tests/stdlib/math/vectors`), and that transliteration stays within 1 ulp of a 70-digit reference for the functions fdlibm documents as such, with the same larger errors as glibc for `sinh`, `cosh`, `tanh`, and `log10`. Agreement with a reference fdlibm 5.3 build has not been checked. Expressions fdlibm evaluates only to raise the inexact flag are omitted. `sqrt` is the correctly rounded integer method of [section 10.1](Stdlib-Design#101-math). `log2`, which fdlibm 5.3 lacks, follows `e_log10.c` and returns exact results for powers of two. `pow(±1, ±∞)` and `pow(1, NaN)` are NaN, as in fdlibm.
- **BigInteger** keeps its fields private (`signum`, `magnitude`); its constructor normalizes the value and adopts the list. `mod_inverse` traps for a modulus that is not positive. `floor_mod` takes the modulus's sign.
- **BigDecimal** adds the read-only fields `unscaled` and `scale`. `round(0, mode)` returns the value unchanged; a rounding carry drops one zero, as Java does. `of_float64` gives the exact value at the smallest scale. `-0.00` parses as zero. `parse` accepts an optional sign, digits with an optional point, and an optional `e` or `E` exponent; a scale outside `int` fails with `OutOfRange` at offset 0. Zero with a negative scale prints as `0`.
- **Trap details.** `negative exponent`, `negative shift count`, `negative bit index`, `modulus not positive`, `not a finite value`, `negative precision`, `magnitude too large`, and `zero sign with a nonzero magnitude` after `invalid argument: `.
- **Cost.** A `Math` call is one kernel call of one to four steps. While `Math` was Neura code, every float64 operation in it compiled to a large circuit: the `elementary` test program, which calls every function, had an image of 14,719,526 bytes, and has one of 15,480 bytes since it binds the kernels. `Math` is not in the cost baseline.

### 18.11 Test layout

- **Layout.** Each area of `tests/stdlib` holds test files, `support/` modules, `traps/` programs with `// status:` and `// stderr:` headers, and `programs/` with expected output (`tests/stdlib/area.cmake`). Every area runs against a fresh installation (`stdlib-install`), its tests optimized twice and unoptimized, its programs and traps in both modes, and its program images through `disassemble`, `assemble`, and `decompile`.
- **Reference evaluator.** `backend-reference` runs every library test function under the NeuraIR reference evaluator and NVM and compares the outcome, the console output, and any trap message. A call of a kernel-bound function runs the runtime's own kernel through the kernel table, so those tests are compared. Tests that need host responses (files, sockets, console input), open a channel, or exceed the evaluator's budget or call depth are counted and not compared.
- **Host services** are tested with the capability denied, with real providers in temporary directories and on localhost, and by replaying checked-in traces.
- **Cost baseline.** `tests/benchmarks` holds the workloads of this section's cost baseline and a few more (ArrayList, TreeMap, strings, JSON, dates); `benchmarks` compares their steps, peak heap cells, neurons, and image bytes with `baseline.txt` and checks that steps grow as expected between two sizes.
- **Not established.** Agreement of `Math` with a reference fdlibm 5.3 build (no reference build was available); `with_capacity(2^29)` and other limits that need billions of elements were only inspected.
