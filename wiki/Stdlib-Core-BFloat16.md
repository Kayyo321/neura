# `Std.Core.BFloat16`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.BFloat16`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.BFloat16`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BFloat16** · [`export const Max: bfloat16 = bfloat16.from_bits(32639)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L13)
  The largest finite value.
- **BFloat16** · [`export const MinPositive: bfloat16 = bfloat16.from_bits(1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L15)
  The smallest positive (subnormal) value.
- **BFloat16** · [`export const MinNormal: bfloat16 = bfloat16.from_bits(128)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L17)
  The smallest positive normal value.
- **BFloat16** · [`export const Epsilon: bfloat16 = bfloat16.from_bits(15360)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L19)
  The difference between 1.0 and the next larger value.
- **BFloat16** · [`export const Infinity: bfloat16 = bfloat16.from_bits(32640)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L21)
  The positive infinity of this format.
- **BFloat16** · [`export const NegativeInfinity: bfloat16 = bfloat16.from_bits(65408)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L23)
  The negative infinity of this format.
- **BFloat16** · [`export const NaN: bfloat16 = bfloat16.from_bits(32704)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L25)
  The canonical quiet NaN.
- **BFloat16** · [`export let is_nan = func(v: bfloat16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L34)
  Reports whether the value is a NaN. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let is_infinite = func(v: bfloat16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L38)
  Reports whether the value is either signed infinity. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let is_finite = func(v: bfloat16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L42)
  Reports whether the value is finite, including zero and subnormal values. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let is_negative = func(v: bfloat16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L46)
  Reads the sign bit, so it is true for -0.0 and for a NaN with the sign bit set. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let abs = func(v: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L50)
  Clears the sign bit; never rounds. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let copy_sign = func(magnitude: bfloat16, sign: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L54)
  The magnitude of `magnitude` with the sign bit of `sign`. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let min = func(a: bfloat16, b: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L59)
  NaN (canonical) if either operand is NaN; -0.0 is less than 0.0. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let max = func(a: bfloat16, b: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L77)
  Returns the greater value, propagating NaN and preferring positive zero over negative zero. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let clamp = func(v: bfloat16, low: bfloat16, high: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L96)
  max(v, low), then min with high, so a NaN `v` gives NaN. Traps when either bound is NaN or `low` exceeds `high` (-0.0 is below 0.0). Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let floor = func(v: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L109)
  Rounding to an integral value. All keep the sign of zero (ceil(-0.5) is -0.0); infinities are unchanged and a NaN becomes the canonical NaN. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let ceil = func(v: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L113)
  Rounds toward positive infinity, retaining the sign of zero. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let trunc = func(v: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L117)
  Rounds toward zero, retaining the sign of zero. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let round = func(v: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L121)
  Halves round away from zero. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let round_even = func(v: bfloat16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L125)
  Halves round to the even neighbour. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let to_bits = func(v: bfloat16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L129)
  Returns the raw bit pattern as an unsigned integer. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let from_bits = func(bits: uint16) -> bfloat16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L133)
  Interprets an unsigned integer as a raw floating-point bit pattern. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let total_compare = func(a: bfloat16, b: bfloat16) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L138)
  The total order of section 5.3: -Infinity, negative values, -0.0, 0.0, positive values, Infinity, then every NaN (all NaNs equal). Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let equals = func(a: bfloat16, b: bfloat16) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L168)
  Same bit pattern, except that every NaN equals every NaN; 0.0 and -0.0 differ (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let hash = func(v: bfloat16) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L172)
  Returns the primitive hash consistent with canonical-NaN and signed-zero equality. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let to_string = func(v: bfloat16) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L176)
  The shortest text that parses back to the same value (section 7.5). Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let to_fixed = func(v: bfloat16, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L181)
  Exactly `digits` fraction digits (0 through 100; no point for 0), correctly rounded, ties to even. Other digit counts trap. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let to_exponential = func(v: bfloat16, digits: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L187)
  `d.ddd` with `digits` fraction digits (0 through 100), `e`, and the decimal exponent; correctly rounded, ties to even. Tagged `#Pure`, `#ThreadSafe`.
- **BFloat16** · [`export let parse = func(text: string) -> Result<bfloat16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/b-float16.nra#L193)
  The grammar of section 7.5, converted exactly and rounded once, ties to even. Too large becomes an infinity and too small a signed zero. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
