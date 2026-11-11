# `Std.Core.Float32`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Float32`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Float32`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Float32** · [`export const Max: float32 = float32.from_bits(2139095039)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L13)
  The largest finite value.
- **Float32** · [`export const MinPositive: float32 = float32.from_bits(1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L15)
  The smallest positive (subnormal) value.
- **Float32** · [`export const MinNormal: float32 = float32.from_bits(8388608)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L17)
  The smallest positive normal value.
- **Float32** · [`export const Epsilon: float32 = float32.from_bits(872415232)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L19)
  The difference between 1.0 and the next larger value.
- **Float32** · [`export const Infinity: float32 = float32.from_bits(2139095040)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L21)
  The positive infinity of this format.
- **Float32** · [`export const NegativeInfinity: float32 = float32.from_bits(4286578688)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L23)
  The negative infinity of this format.
- **Float32** · [`export const NaN: float32 = float32.from_bits(2143289344)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L25)
  The canonical quiet NaN.
- **Float32** · [`export let is_nan = func(v: float32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L34)
  Reports whether the value is a NaN. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let is_infinite = func(v: float32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L38)
  Reports whether the value is either signed infinity. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let is_finite = func(v: float32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L42)
  Reports whether the value is finite, including zero and subnormal values. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let is_negative = func(v: float32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L46)
  Reads the sign bit, so it is true for -0.0 and for a NaN with the sign bit set. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let abs = func(v: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L50)
  Clears the sign bit; never rounds. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let copy_sign = func(magnitude: float32, sign: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L54)
  The magnitude of `magnitude` with the sign bit of `sign`. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let min = func(a: float32, b: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L59)
  NaN (canonical) if either operand is NaN; -0.0 is less than 0.0. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let max = func(a: float32, b: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L77)
  Returns the greater value, propagating NaN and preferring positive zero over negative zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let clamp = func(v: float32, low: float32, high: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L96)
  max(v, low), then min with high, so a NaN `v` gives NaN. Traps when either bound is NaN or `low` exceeds `high` (-0.0 is below 0.0). Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let floor = func(v: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L109)
  Rounding to an integral value. All keep the sign of zero (ceil(-0.5) is -0.0); infinities are unchanged and a NaN becomes the canonical NaN. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let ceil = func(v: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L113)
  Rounds toward positive infinity, retaining the sign of zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let trunc = func(v: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L117)
  Rounds toward zero, retaining the sign of zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let round = func(v: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L121)
  Halves round away from zero. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let round_even = func(v: float32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L125)
  Halves round to the even neighbour. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let to_bits = func(v: float32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L129)
  Returns the raw bit pattern as an unsigned integer. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let from_bits = func(bits: uint32) -> float32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L133)
  Interprets an unsigned integer as a raw floating-point bit pattern. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let total_compare = func(a: float32, b: float32) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L138)
  The total order of section 5.3: -Infinity, negative values, -0.0, 0.0, positive values, Infinity, then every NaN (all NaNs equal). Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let equals = func(a: float32, b: float32) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L168)
  Same bit pattern, except that every NaN equals every NaN; 0.0 and -0.0 differ (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let hash = func(v: float32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L172)
  Returns the primitive hash consistent with canonical-NaN and signed-zero equality. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let to_string = func(v: float32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L176)
  The shortest text that parses back to the same value (section 7.5). Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let to_fixed = func(v: float32, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L181)
  Exactly `digits` fraction digits (0 through 100; no point for 0), correctly rounded, ties to even. Other digit counts trap. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let to_exponential = func(v: float32, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L187)
  `d.ddd` with `digits` fraction digits (0 through 100), `e`, and the decimal exponent; correctly rounded, ties to even. Tagged `#Pure`, `#ThreadSafe`.
- **Float32** · [`export let parse = func(text: string) -> Result<float32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/float32.nra#L193)
  The grammar of section 7.5, converted exactly and rounded once, ties to even. Too large becomes an infinity and too small a signed zero. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
