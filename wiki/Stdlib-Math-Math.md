# `Std.Math.Math`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Math.Math`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Math.Math`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Math** · [`export const Pi: float64 = float64.from_bits(4614256656552045848)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L40)
  The nearest float64 approximation to pi.
- **Math** · [`export const Tau: float64 = float64.from_bits(4618760256179416344)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L42)
  The nearest float64 approximation to twice pi.
- **Math** · [`export const E: float64 = float64.from_bits(4613303445314885481)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L44)
  The nearest float64 approximation to Euler's number.
- **Math** · [`export const Ln2: float64 = float64.from_bits(4604418534313441775)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L46)
  The nearest float64 approximation to the natural logarithm of two.
- **Math** · [`export const Ln10: float64 = float64.from_bits(4612367379483415830)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L48)
  The nearest float64 approximation to the natural logarithm of ten.
- **Math** · [`export const Sqrt2: float64 = float64.from_bits(4609047870845172685)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L50)
  The nearest float64 approximation to the square root of two.
- **Math** · [`export let sqrt = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L55)
  Correctly rounded square root: the integer square root of the significand, shifted to an even exponent and extended by 54 zero bits, one result bit per step with a running remainder; then round to nearest, ties to even. sqrt(-0.0) is -0.0; other negatives give NaN.
- **Math** · [`export let cbrt = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L117)
  s_cbrt.c
- **Math** · [`export let hypot = func(x: float64, y: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L164)
  e_hypot.c: sqrt(x*x + y*y) without undue overflow or underflow.
- **Math** · [`export let exp = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L239)
  e_exp.c
- **Math** · [`export let expm1 = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L318)
  s_expm1.c: exp(x) - 1, accurate near zero.
- **Math** · [`export let log = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L426)
  e_log.c
- **Math** · [`export let log1p = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L500)
  s_log1p.c: log(1 + x), accurate near zero.
- **Math** · [`export let log10 = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L595)
  e_log10.c
- **Math** · [`export let log2 = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L630)
  Base-2 logarithm, shaped like e_log10.c (see the file comment).
- **Math** · [`export let pow = func(x: float64, y: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L666)
  e_pow.c: x raised to y.
- **Math** · [`export let sin = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L947)
  s_sin.c
- **Math** · [`export let cos = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L971)
  s_cos.c
- **Math** · [`export let tan = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L995)
  s_tan.c
- **Math** · [`export let asin = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1026)
  e_asin.c
- **Math** · [`export let acos = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1067)
  e_acos.c
- **Math** · [`export let atan = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1108)
  s_atan.c
- **Math** · [`export let atan2 = func(y: float64, x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1191)
  e_atan2.c: the angle of the point (x, y).
- **Math** · [`export let sinh = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1282)
  e_sinh.c
- **Math** · [`export let cosh = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1322)
  e_cosh.c
- **Math** · [`export let tanh = func(x: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1361)
  s_tanh.c
- **Math** · [`export let to_radians = func(degrees: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1396)
  Degrees to radians: x times the nearest float64 to pi/180.
- **Math** · [`export let to_degrees = func(radians: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/math.nra#L1401)
  Radians to degrees: x times the nearest float64 to 180/pi.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 10.1 `Math`

`Math` works on `float64` only and needs N1. Code using another float type converts to `float64`, calls `Math`, and converts back.

- **Constants:** `Pi`, `Tau`, `E`, `Ln2`, `Ln10`, `Sqrt2`, each the nearest `float64`.
- **`sqrt`** is correctly rounded. It unpacks the operand, computes the integer square root of the significand shifted to an even exponent, one result bit per step with a running remainder, and rounds to nearest, ties to even. Negative operands other than `-0.0` give NaN.
- **Other functions:** `cbrt`, `hypot`, `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `to_radians`, and `to_degrees`.

**Decision:** these functions are a port of fdlibm 5.3, operation for operation, including its argument reduction, its polynomial coefficients (written as exact bit patterns through `Float64.from_bits`), and its special cases. fdlibm relies only on IEEE 754 double arithmetic with round-to-nearest and no fused or reassociated operations, which is what [Numbers](https://github.com/Kayyo321/neura/wiki/Numbers) guarantees, and on reading and writing the high and low 32-bit words of a double, which is N1. The functions are therefore defined by their source: every runtime produces bit-identical results. Every NaN result is the canonical NaN. fdlibm's copyright notice must be kept in the ported files, following the third-party rule in [CONTRIBUTING](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md).

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
| `shuffle<T>(items: T[list])` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. Generic, with no callback, so it works today. |
| `choose<T>(items: T[list]) → T` | Traps with `no such element` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets, and the library offers no cryptographic generator.

**`Uuid`** is an immutable value of 16 bytes. The module function `random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `to_string` writes the 36-character hyphenated form in lowercase. `parse(text) → Result<Uuid>` accepts either case and needs T1. It has `compare_to`, `equals`, and `hash_code` over its bytes.

### 18.10 Math

- **fdlibm port.** `Math` is a port of fdlibm 5.3 written without a reference copy of the sources; Sun's notice is kept in `math.nra`, `internal/trig.nra`, and the test transliteration. The Neura functions agree bit for bit with a C transliteration of the same port on 9,148 vector cases (`tests/stdlib/math/vectors`), and that transliteration stays within 1 ulp of a 70-digit reference for the functions fdlibm documents as such, with the same larger errors as glibc for `sinh`, `cosh`, `tanh`, and `log10`. Agreement with a reference fdlibm 5.3 build is not yet established. Expressions fdlibm evaluates only to raise the inexact flag are omitted. `sqrt` is the correctly rounded integer method of [section 10.1](Stdlib-Design#101-math). `log2`, which fdlibm 5.3 lacks, follows `e_log10.c` and returns exact results for powers of two. `pow(±1, ±∞)` and `pow(1, NaN)` are NaN, as in fdlibm.
- **BigInteger** keeps its fields private (`signum`, `magnitude`); its constructor normalizes the value and adopts the list. `mod_inverse` traps for a modulus that is not positive. `floor_mod` takes the modulus's sign.
- **BigDecimal** adds `unscaled()` and `scale()`. `round(0, mode)` returns the value unchanged; a rounding carry drops one zero, as Java does. `of_float64` gives the exact value at the smallest scale. `-0.00` parses as zero. `parse` accepts an optional sign, digits with an optional point, and an optional `e` or `E` exponent; a scale outside `int` fails with `OutOfRange` at offset 0. Zero with a negative scale prints as `0`.
- **Trap details.** `negative exponent`, `negative shift count`, `negative bit index`, `modulus not positive`, `not a finite value`, `negative precision`, `magnitude too large`, and `zero sign with a nonzero magnitude` after `invalid argument: `.
- **Cost.** A float64 operation compiles to a large circuit, so a program that calls several `Math` functions has an image of several megabytes; `Math` is not in the cost baseline for that reason.

### 18.11 Verification as built

- **Layout.** Each area of `tests/stdlib` holds test files, `support/` modules, `traps/` programs with `// status:` and `// stderr:` headers, and `programs/` with expected output (`tests/stdlib/area.cmake`). Every area runs against a fresh installation (`stdlib-install`), its tests optimized twice and unoptimized, its programs and traps in both modes, and its program images through `disassemble`, `assemble`, and `decompile`.
- **Reference evaluator.** `backend-reference` runs every library test function under the NeuraIR reference evaluator and NVM and compares the outcome, the console output, and any trap message. Tests that need host responses (files, sockets, console input) or exceed the evaluator's budget are counted and not compared.
- **Host services** are tested with the capability denied, with real providers in temporary directories and on localhost, and by replaying checked-in traces.
- **Cost baseline.** `tests/benchmarks` holds the workloads of this section's cost baseline and a few more (ArrayList, TreeMap, strings, JSON, dates); `benchmarks` compares their steps, peak heap cells, neurons, and image bytes with `baseline.txt` and checks that steps grow as expected between two sizes.
- **Not established.** Agreement of `Math` with a reference fdlibm 5.3 build (no reference build was available); `with_capacity(2^29)` and other limits that need billions of elements were only inspected.
