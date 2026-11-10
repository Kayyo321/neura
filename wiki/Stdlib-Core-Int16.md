# `Std.Core.Int16`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Int16`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Int16`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Int16** · [`export const Min: int16 = -32768`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L14)
  The smallest value representable by this integer type.
- **Int16** · [`export const Max: int16 = 32767`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L16)
  The largest value representable by this integer type.
- **Int16** · [`export const Bits: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L18)
  The number of bits in the integer representation.
- **Int16** · [`export let abs = func(v: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L21)
  Traps (reason 1) for Min, from the negation.
- **Int16** · [`export let sign = func(v: int16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L24)
  Returns minus one, zero, or one according to the sign of the value.
- **Int16** · [`export let min = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L35)
  Returns the smaller of the two operands.
- **Int16** · [`export let max = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L38)
  Returns the larger of the two operands.
- **Int16** · [`export let clamp = func(v: int16, low: int16, high: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L41)
  Traps with "invalid argument: low exceeds high".
- **Int16** · [`export let compare = func(a: int16, b: int16) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L55)
  The natural order; usable as a comparator.
- **Int16** · [`export let pow = func(base: int16, exponent: int) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L69)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **Int16** · [`export let gcd = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L90)
  Euclid's algorithm in the negative range, where every magnitude fits. gcd(0, 0) is 0. Traps (reason 1) when the result, 2^(Bits - 1), does not fit.
- **Int16** · [`export let checked_add = func(a: int16, b: int16) -> Optional<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L108)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **Int16** · [`export let checked_sub = func(a: int16, b: int16) -> Optional<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L116)
  Returns the difference in Some, or None when it falls outside the type's range.
- **Int16** · [`export let checked_mul = func(a: int16, b: int16) -> Optional<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L124)
  Returns the product in Some, or None when it falls outside the type's range.
- **Int16** · [`export let checked_div = func(a: int16, b: int16) -> Optional<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L132)
  None for a zero divisor and for Min / -1.
- **Int16** · [`export let checked_rem = func(a: int16, b: int16) -> Optional<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L140)
  None for a zero divisor. Min % -1 is 0, as for the operator.
- **Int16** · [`export let wrapping_add = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L149)
  The result modulo 2^Bits: the operation runs on the unsigned pattern and converts back (section 7.2).
- **Int16** · [`export let wrapping_sub = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L153)
  Subtracts modulo two raised to the type's bit width.
- **Int16** · [`export let wrapping_mul = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L157)
  Multiplies modulo two raised to the type's bit width.
- **Int16** · [`export let wrapping_neg = func(a: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L161)
  Min wraps to itself.
- **Int16** · [`export let saturating_add = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L165)
  Clamp to Min or Max instead of overflowing.
- **Int16** · [`export let saturating_sub = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L176)
  Subtracts and clamps an out-of-range result to the nearest representable bound.
- **Int16** · [`export let saturating_mul = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L187)
  Multiplies and clamps an out-of-range result to the nearest representable bound.
- **Int16** · [`export let floor_div = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L199)
  The quotient rounded toward negative infinity. A zero divisor and Min / -1 trap (reason 1) in the division.
- **Int16** · [`export let floor_mod = func(a: int16, b: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L208)
  The remainder with the divisor's sign. A zero divisor traps (reason 1).
- **Int16** · [`export let count_ones = func(v: int16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L217)
  Bit counts and rotations over the two's complement pattern.
- **Int16** · [`export let leading_zeros = func(v: int16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L220)
  Counts zero bits from the most significant end. Zero returns the full bit width.
- **Int16** · [`export let trailing_zeros = func(v: int16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L223)
  Counts zero bits from the least significant end. Zero returns the full bit width.
- **Int16** · [`export let rotate_left = func(v: int16, n: int) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L226)
  Rotates the bits left, reducing the distance modulo the bit width.
- **Int16** · [`export let rotate_right = func(v: int16, n: int) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L230)
  Rotates the bits right, reducing the distance modulo the bit width.
- **Int16** · [`export let to_unsigned = func(v: int16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L234)
  The same-width unsigned type with the same bit pattern (section 7.2).
- **Int16** · [`export let to_signed = func(v: int16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L244)
  The identity: every integer module has both conversions.
- **Int16** · [`export let hash = func(v: int16) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L248)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **Int16** · [`export let to_string = func(v: int16) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L251)
  Decimal, with `-` for negative values (section 5.2).
- **Int16** · [`export let to_string_radix = func(v: int16, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L255)
  Lowercase digits. The digits are produced in the negative range, so Min needs no negation. A radix outside 2 through 36 traps.
- **Int16** · [`export let format = func(v: int16, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L278)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **Int16** · [`export let parse = func(text: string) -> Result<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L283)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range.
- **Int16** · [`export let parse_radix = func(text: string, radix: int) -> Result<int16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int16.nra#L290)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. The digits accumulate in the negative range, so Min parses without overflow. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

This module has no dedicated section in the historical design. Use the declaration reference and source above for its public API; the shared contracts and implementation decisions apply where relevant.
