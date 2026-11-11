# `Std.Core.Float16`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Float16`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Float16`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Float16** · [`export const Max: float16 = float16.from_bits(31743)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L13)
  The largest finite value.
- **Float16** · [`export const MinPositive: float16 = float16.from_bits(1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L15)
  The smallest positive (subnormal) value.
- **Float16** · [`export const MinNormal: float16 = float16.from_bits(1024)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L17)
  The smallest positive normal value.
- **Float16** · [`export const Epsilon: float16 = float16.from_bits(5120)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L19)
  The difference between 1.0 and the next larger value.
- **Float16** · [`export const Infinity: float16 = float16.from_bits(31744)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L21)
  The positive infinity of this format.
- **Float16** · [`export const NegativeInfinity: float16 = float16.from_bits(64512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L23)
  The negative infinity of this format.
- **Float16** · [`export const NaN: float16 = float16.from_bits(32256)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L25)
  The canonical quiet NaN.
- **Float16** · [`export let is_nan = func(v: float16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L33)
  Reports whether the value is a NaN.
- **Float16** · [`export let is_infinite = func(v: float16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L36)
  Reports whether the value is either signed infinity.
- **Float16** · [`export let is_finite = func(v: float16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L39)
  Reports whether the value is finite, including zero and subnormal values.
- **Float16** · [`export let is_negative = func(v: float16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L42)
  Reads the sign bit, so it is true for -0.0 and for a NaN with the sign bit set.
- **Float16** · [`export let abs = func(v: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L45)
  Clears the sign bit; never rounds.
- **Float16** · [`export let copy_sign = func(magnitude: float16, sign: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L48)
  The magnitude of `magnitude` with the sign bit of `sign`.
- **Float16** · [`export let min = func(a: float16, b: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L52)
  NaN (canonical) if either operand is NaN; -0.0 is less than 0.0.
- **Float16** · [`export let max = func(a: float16, b: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L69)
  Returns the greater value, propagating NaN and preferring positive zero over negative zero.
- **Float16** · [`export let clamp = func(v: float16, low: float16, high: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L87)
  max(v, low), then min with high, so a NaN `v` gives NaN. Traps when either bound is NaN or `low` exceeds `high` (-0.0 is below 0.0).
- **Float16** · [`export let floor = func(v: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L99)
  Rounding to an integral value. All keep the sign of zero (ceil(-0.5) is -0.0); infinities are unchanged and a NaN becomes the canonical NaN.
- **Float16** · [`export let ceil = func(v: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L102)
  Rounds toward positive infinity, retaining the sign of zero.
- **Float16** · [`export let trunc = func(v: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L105)
  Rounds toward zero, retaining the sign of zero.
- **Float16** · [`export let round = func(v: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L108)
  Halves round away from zero.
- **Float16** · [`export let round_even = func(v: float16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L111)
  Halves round to the even neighbour.
- **Float16** · [`export let to_bits = func(v: float16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L114)
  Returns the raw bit pattern as an unsigned integer.
- **Float16** · [`export let from_bits = func(bits: uint16) -> float16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L117)
  Interprets an unsigned integer as a raw floating-point bit pattern.
- **Float16** · [`export let total_compare = func(a: float16, b: float16) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L121)
  The total order of section 5.3: -Infinity, negative values, -0.0, 0.0, positive values, Infinity, then every NaN (all NaNs equal).
- **Float16** · [`export let equals = func(a: float16, b: float16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L150)
  Same bit pattern, except that every NaN equals every NaN; 0.0 and -0.0 differ (section 5.2).
- **Float16** · [`export let hash = func(v: float16) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L153)
  Returns the primitive hash consistent with canonical-NaN and signed-zero equality.
- **Float16** · [`export let to_string = func(v: float16) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L156)
  The shortest text that parses back to the same value (section 7.5).
- **Float16** · [`export let to_fixed = func(v: float16, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L160)
  Exactly `digits` fraction digits (0 through 100; no point for 0), correctly rounded, ties to even. Other digit counts trap.
- **Float16** · [`export let to_exponential = func(v: float16, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L165)
  `d.ddd` with `digits` fraction digits (0 through 100), `e`, and the decimal exponent; correctly rounded, ties to even.
- **Float16** · [`export let parse = func(text: string) -> Result<float16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float16.nra#L171)
  The grammar of section 7.5, converted exactly and rounded once, ties to even. Too large becomes an infinity and too small a signed zero. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
