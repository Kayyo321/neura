# `Std.Core.Int8`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Int8`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Int8`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Int8** · [`export const Min: int8 = -128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L17)
  The smallest value representable by this integer type.
- **Int8** · [`export const Max: int8 = 127`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L19)
  The largest value representable by this integer type.
- **Int8** · [`export const Bits: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L21)
  The number of bits in the integer representation.
- **Int8** · [`export let abs = func(v: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L24)
  Traps (reason 1) for Min, from the negation.
- **Int8** · [`export let sign = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L27)
  Returns minus one, zero, or one according to the sign of the value.
- **Int8** · [`export let min = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L38)
  Returns the smaller of the two operands.
- **Int8** · [`export let max = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L41)
  Returns the larger of the two operands.
- **Int8** · [`export let clamp = func(v: int8, low: int8, high: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L44)
  Traps with "invalid argument: low exceeds high".
- **Int8** · [`export let compare = func(a: int8, b: int8) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L58)
  The natural order; usable as a comparator.
- **Int8** · [`export let pow = func(base: int8, exponent: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L72)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **Int8** · [`export let gcd = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L93)
  Euclid's algorithm in the negative range, where every magnitude fits. gcd(0, 0) is 0. Traps (reason 1) when the result, 2^(Bits - 1), does not fit.
- **Int8** · [`export let checked_add = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L112)
  None where the operator would trap. Each test runs before the operation, so nothing overflows. Tagged `#NoDiscard`.
- **Int8** · [`export let checked_sub = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L121)
  Returns the difference in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`.
- **Int8** · [`export let checked_mul = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L130)
  Returns the product in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`.
- **Int8** · [`export let checked_div = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L139)
  None for a zero divisor and for Min / -1. Tagged `#NoDiscard`.
- **Int8** · [`export let checked_rem = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L148)
  None for a zero divisor. Min % -1 is 0, as for the operator. Tagged `#NoDiscard`.
- **Int8** · [`export let wrapping_add = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L157)
  The result modulo 2^Bits: the operation runs on the unsigned pattern and converts back (section 7.2).
- **Int8** · [`export let wrapping_sub = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L161)
  Subtracts modulo two raised to the type's bit width.
- **Int8** · [`export let wrapping_mul = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L165)
  Multiplies modulo two raised to the type's bit width.
- **Int8** · [`export let wrapping_neg = func(a: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L169)
  Min wraps to itself.
- **Int8** · [`export let saturating_add = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L173)
  Clamp to Min or Max instead of overflowing.
- **Int8** · [`export let saturating_sub = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L184)
  Subtracts and clamps an out-of-range result to the nearest representable bound.
- **Int8** · [`export let saturating_mul = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L195)
  Multiplies and clamps an out-of-range result to the nearest representable bound.
- **Int8** · [`export let floor_div = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L207)
  The quotient rounded toward negative infinity. A zero divisor and Min / -1 trap (reason 1) in the division.
- **Int8** · [`export let floor_mod = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L216)
  The remainder with the divisor's sign. A zero divisor traps (reason 1).
- **Int8** · [`export let count_ones = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L225)
  Bit counts and rotations over the two's complement pattern.
- **Int8** · [`export let leading_zeros = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L228)
  Counts zero bits from the most significant end. Zero returns the full bit width.
- **Int8** · [`export let trailing_zeros = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L231)
  Counts zero bits from the least significant end. Zero returns the full bit width.
- **Int8** · [`export let rotate_left = func(v: int8, n: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L234)
  Rotates the bits left, reducing the distance modulo the bit width.
- **Int8** · [`export let rotate_right = func(v: int8, n: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L238)
  Rotates the bits right, reducing the distance modulo the bit width.
- **Int8** · [`export let to_unsigned = func(v: int8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L242)
  The same-width unsigned type with the same bit pattern (section 7.2).
- **Int8** · [`export let to_signed = func(v: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L252)
  The identity: every integer module has both conversions.
- **Int8** · [`export let hash = func(v: int8) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L256)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **Int8** · [`export let to_string = func(v: int8) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L259)
  Decimal, with `-` for negative values (section 5.2).
- **Int8** · [`export let to_string_radix = func(v: int8, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L263)
  Lowercase digits, with `-` for a negative value. A radix outside 2 through 36 traps. The neura.integer.format_i64 kernel writes the digits.
- **Int8** · [`export let format = func(v: int8, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L268)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **Int8** · [`export let parse = func(text: string) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L274)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range. Tagged `#NoDiscard`.
- **Int8** · [`export let parse_radix = func(text: string, radix: int) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L285)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A radix outside 2 through 36 traps. The neura.integer.parse_i64 kernel parses the text as int64, and the value is then checked against the type's range. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
