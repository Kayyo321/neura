# `Std.Core.Float64`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Float64`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Float64`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Float64** · [`export const Max: float64 = float64.from_bits(9218868437227405311)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L13)
  The largest finite value.
- **Float64** · [`export const MinPositive: float64 = float64.from_bits(1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L15)
  The smallest positive (subnormal) value.
- **Float64** · [`export const MinNormal: float64 = float64.from_bits(4503599627370496)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L17)
  The smallest positive normal value.
- **Float64** · [`export const Epsilon: float64 = float64.from_bits(4372995238176751616)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L19)
  The difference between 1.0 and the next larger value.
- **Float64** · [`export const Infinity: float64 = float64.from_bits(9218868437227405312)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L21)
  The positive infinity of this format.
- **Float64** · [`export const NegativeInfinity: float64 = float64.from_bits(18442240474082181120)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L23)
  The negative infinity of this format.
- **Float64** · [`export const NaN: float64 = float64.from_bits(9221120237041090560)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L25)
  The canonical quiet NaN.
- **Float64** · [`export let is_nan = func(v: float64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L34)
  Reports whether the value is a NaN. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let is_infinite = func(v: float64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L38)
  Reports whether the value is either signed infinity. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let is_finite = func(v: float64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L42)
  Reports whether the value is finite, including zero and subnormal values. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let is_negative = func(v: float64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L47)
  Reads the sign bit, so it is true for -0.0 and for a NaN with the sign bit set. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let abs = func(v: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L51)
  Clears the sign bit; never rounds. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let copy_sign = func(magnitude: float64, sign: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L55)
  The magnitude of `magnitude` with the sign bit of `sign`. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let min = func(a: float64, b: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L60)
  NaN (canonical) if either operand is NaN; -0.0 is less than 0.0. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let max = func(a: float64, b: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L78)
  Returns the greater value, propagating NaN and preferring positive zero over negative zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let clamp = func(v: float64, low: float64, high: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L97)
  max(v, low), then min with high, so a NaN `v` gives NaN. Traps when either bound is NaN or `low` exceeds `high` (-0.0 is below 0.0). Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let floor = func(v: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L110)
  Rounding to an integral value. All keep the sign of zero (ceil(-0.5) is -0.0); infinities are unchanged and a NaN becomes the canonical NaN. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let ceil = func(v: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L114)
  Rounds toward positive infinity, retaining the sign of zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let trunc = func(v: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L118)
  Rounds toward zero, retaining the sign of zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let round = func(v: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L122)
  Halves round away from zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let round_even = func(v: float64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L126)
  Halves round to the even neighbour. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let to_bits = func(v: float64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L130)
  Returns the raw bit pattern as an unsigned integer. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let from_bits = func(bits: uint64) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L134)
  Interprets an unsigned integer as a raw floating-point bit pattern. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let total_compare = func(a: float64, b: float64) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L139)
  The total order of section 5.3: -Infinity, negative values, -0.0, 0.0, positive values, Infinity, then every NaN (all NaNs equal). Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let equals = func(a: float64, b: float64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L169)
  Same bit pattern, except that every NaN equals every NaN; 0.0 and -0.0 differ (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let hash = func(v: float64) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L173)
  Returns the primitive hash consistent with canonical-NaN and signed-zero equality. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let to_string = func(v: float64) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L177)
  The shortest text that parses back to the same value (section 7.5). Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let to_fixed = func(v: float64, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L182)
  Exactly `digits` fraction digits (0 through 100; no point for 0), correctly rounded, ties to even. Other digit counts trap. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let to_exponential = func(v: float64, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L188)
  `d.ddd` with `digits` fraction digits (0 through 100), `e`, and the decimal exponent; correctly rounded, ties to even. Tagged `#Pure`, `#ThreadSafe`.
- **Float64** · [`export let parse = func(text: string) -> Result<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float64.nra#L194)
  The grammar of section 7.5, converted exactly and rounded once, ties to even. Too large becomes an infinity and too small a signed zero. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`.
- **A comparator is a native function value** of type `(T, T) → Ordering`. There is no comparator interface or wrapper class. A named function, a bound method, or a closure all serve. A comparator must define a total order; if it does not, the library stays memory safe and terminates, and the resulting order or lookup is unspecified.
- **Natural orders** are named functions in the companion modules, usable as comparators directly: `Int32.compare`, `Float64.total_compare`, `Strings.compare`, and so on.

| Type | Natural order |
| --- | --- |
| Integers | Numeric value |
| `bool` | `false` before `true` |
| Floats | Negative infinity, negative finite values, `-0.0`, `0.0`, positive finite values, positive infinity, then NaN; all NaNs equal |
| `string` | Byte-wise lexicographic; a proper prefix sorts first. This equals ordering by Unicode scalar value. |
| Fieldless enum | Ordinal, obtained by the caller with `as` |

- The module **`Comparators`** uses generic function values (G1) for `reversed<T>(c)`, `by_key<T, K>(key: (T) → K, c: (K, K) → Ordering)`, and `then<T>(first, second)`.
- **`Comparable<T>`**, an interface with `compare_to(other: T) → Ordering`, arrives with G4. Nothing in the library requires it: sorted structures always take a comparator.

### 10.1 `Math`

`Math` works on `float64` only and needs N1. Code using another float type converts to `float64`, calls `Math`, and converts back.

- **Constants:** `Pi`, `Tau`, `E`, `Ln2`, `Ln10`, `Sqrt2`, each the nearest `float64`.
- **`sqrt`** is correctly rounded. It unpacks the operand, computes the integer square root of the significand shifted to an even exponent, one result bit per step with a running remainder, and rounds to nearest, ties to even. Negative operands other than `-0.0` give NaN.
- **Other functions:** `cbrt`, `hypot`, `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `to_radians`, and `to_degrees`.

These functions are a port of fdlibm 5.3, operation for operation, including its argument reduction, its polynomial coefficients (written as exact bit patterns through `Float64.from_bits`), and its special cases. fdlibm relies only on IEEE 754 double arithmetic with round-to-nearest and no fused or reassociated operations, which is what [Numbers](Numbers) guarantees, and on reading and writing the high and low 32-bit words of a double, which is N1. The functions are therefore defined by their source: every runtime produces bit-identical results. Every NaN result is the canonical NaN. fdlibm's copyright notice must be kept in the ported files, following the third-party rule in [CONTRIBUTING](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md).

Every function binds its `neura.math.*` kernel (P2a, [Kernels](Kernels)), a C transliteration of the same port, so a call costs one to four steps and adds no float circuit to the image. The Neura port is the kernels' oracle: `tests/stdlib/math/kernel-oracle.nra` compares the two bit for bit on an edge corpus and seeded inputs, and the vector tests check the kernels against fdlibm's results.
