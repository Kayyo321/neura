# `Std.Core.UInt32`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.UInt32`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.UInt32`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UInt32** · [`export const Min: uint32 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L13)
  The smallest value representable by this integer type.
- **UInt32** · [`export const Max: uint32 = 4294967295`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L15)
  The largest value representable by this integer type.
- **UInt32** · [`export const Bits: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L17)
  The number of bits in the integer representation.
- **UInt32** · [`export let sign = func(v: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L20)
  Returns zero for zero and one for every other value.
- **UInt32** · [`export let min = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L23)
  Returns the smaller of the two operands.
- **UInt32** · [`export let max = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L26)
  Returns the larger of the two operands.
- **UInt32** · [`export let clamp = func(v: uint32, low: uint32, high: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L29)
  Traps with "invalid argument: low exceeds high".
- **UInt32** · [`export let compare = func(a: uint32, b: uint32) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L43)
  The natural order; usable as a comparator.
- **UInt32** · [`export let pow = func(base: uint32, exponent: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L57)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **UInt32** · [`export let gcd = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L77)
  Euclid's algorithm. gcd(0, 0) is 0.
- **UInt32** · [`export let checked_add = func(a: uint32, b: uint32) -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L89)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **UInt32** · [`export let checked_sub = func(a: uint32, b: uint32) -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L97)
  Returns the difference in Some, or None when it falls outside the type's range.
- **UInt32** · [`export let checked_mul = func(a: uint32, b: uint32) -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L105)
  Returns the product in Some, or None when it falls outside the type's range.
- **UInt32** · [`export let checked_div = func(a: uint32, b: uint32) -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L113)
  Returns the quotient in Some, or None when the divisor is zero.
- **UInt32** · [`export let checked_rem = func(a: uint32, b: uint32) -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L121)
  Returns the remainder in Some, or None when the divisor is zero.
- **UInt32** · [`export let wrapping_add = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L130)
  The result modulo 2^Bits (section 7.2): the operands are widened to uint64, where a sum is below 2^33 and a product below 2^64, and the result is masked to the width.
- **UInt32** · [`export let wrapping_sub = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L134)
  2^Bits is added before subtracting, so the uint64 difference stays non-negative.
- **UInt32** · [`export let wrapping_mul = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L138)
  Multiplies modulo two raised to the type's bit width.
- **UInt32** · [`export let wrapping_neg = func(a: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L142)
  Negates modulo two raised to the type's bit width.
- **UInt32** · [`export let saturating_add = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L145)
  Clamp to Min or Max instead of overflowing.
- **UInt32** · [`export let saturating_sub = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L153)
  Subtracts and clamps an out-of-range result to the nearest representable bound.
- **UInt32** · [`export let saturating_mul = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L161)
  Multiplies and clamps an out-of-range result to the nearest representable bound.
- **UInt32** · [`export let floor_div = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L170)
  For unsigned operands these are the ordinary quotient and remainder. A zero divisor traps (reason 1).
- **UInt32** · [`export let floor_mod = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L173)
  Returns the unsigned remainder. A zero divisor traps.
- **UInt32** · [`export let count_ones = func(v: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L177)
  Bit counts over Bits bits by masks and shifts. Each step adds neighbouring fields whose sums fit in the wider field, so nothing overflows.
- **UInt32** · [`export let leading_zeros = func(v: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L189)
  A binary search for the highest set bit: a left shift only discards bits already known to be zero.
- **UInt32** · [`export let trailing_zeros = func(v: uint32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L219)
  Counts zero bits from the least significant end. Zero returns the full bit width.
- **UInt32** · [`export let rotate_left = func(v: uint32, n: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L250)
  `n` is reduced to 0 through Bits - 1 first. A rotation by 0 returns `v`; otherwise both shift counts are between 1 and Bits - 1.
- **UInt32** · [`export let rotate_right = func(v: uint32, n: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L259)
  Rotates the bits right, reducing the distance modulo the bit width.
- **UInt32** · [`export let to_unsigned = func(v: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L268)
  The identity: every integer module has both conversions.
- **UInt32** · [`export let to_signed = func(v: uint32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L271)
  The same-width signed type with the same bit pattern (section 7.2).
- **UInt32** · [`export let hash = func(v: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L281)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **UInt32** · [`export let to_string = func(v: uint32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L284)
  Decimal, with `-` for negative values (section 5.2).
- **UInt32** · [`export let to_string_radix = func(v: uint32, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L287)
  Lowercase digits. A radix outside 2 through 36 traps.
- **UInt32** · [`export let format = func(v: uint32, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L307)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **UInt32** · [`export let parse = func(text: string) -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L312)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range.
- **UInt32** · [`export let parse_radix = func(text: string, radix: int) -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int32.nra#L319)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A `-` is accepted only for zero. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
