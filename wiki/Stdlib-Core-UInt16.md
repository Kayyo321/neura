# `Std.Core.UInt16`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.UInt16`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.UInt16`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UInt16** · [`export const Min: uint16 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L16)
  The smallest value representable by this integer type.
- **UInt16** · [`export const Max: uint16 = 65535`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L18)
  The largest value representable by this integer type.
- **UInt16** · [`export const Bits: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L20)
  The number of bits in the integer representation.
- **UInt16** · [`export let sign = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L24)
  Returns zero for zero and one for every other value. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let min = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L28)
  Returns the smaller of the two operands. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let max = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L32)
  Returns the larger of the two operands. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let clamp = func(v: uint16, low: uint16, high: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L36)
  Traps with "invalid argument: low exceeds high". Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let compare = func(a: uint16, b: uint16) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L51)
  The natural order; usable as a comparator. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let pow = func(base: uint16, exponent: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L66)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let gcd = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L87)
  Euclid's algorithm. gcd(0, 0) is 0. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let checked_add = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L100)
  None where the operator would trap. Each test runs before the operation, so nothing overflows. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let checked_sub = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L109)
  Returns the difference in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let checked_mul = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L118)
  Returns the product in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let checked_div = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L127)
  Returns the quotient in Some, or None when the divisor is zero. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let checked_rem = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L136)
  Returns the remainder in Some, or None when the divisor is zero. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let wrapping_add = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L146)
  The result modulo 2^Bits (section 7.2): the operands are widened to uint64, where a sum is below 2^33 and a product below 2^64, and the result is masked to the width. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let wrapping_sub = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L151)
  2^Bits is added before subtracting, so the uint64 difference stays non-negative. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let wrapping_mul = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L156)
  Multiplies modulo two raised to the type's bit width. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let wrapping_neg = func(a: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L161)
  Negates modulo two raised to the type's bit width. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let saturating_add = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L165)
  Clamp to Min or Max instead of overflowing. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let saturating_sub = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L174)
  Subtracts and clamps an out-of-range result to the nearest representable bound. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let saturating_mul = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L183)
  Multiplies and clamps an out-of-range result to the nearest representable bound. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let floor_div = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L193)
  For unsigned operands these are the ordinary quotient and remainder. A zero divisor traps (reason 1). Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let floor_mod = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L197)
  Returns the unsigned remainder. A zero divisor traps. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let count_ones = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L202)
  Bit counts over Bits bits by masks and shifts. Each step adds neighbouring fields whose sums fit in the wider field, so nothing overflows. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let leading_zeros = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L214)
  A binary search for the highest set bit: a left shift only discards bits already known to be zero. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let trailing_zeros = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L241)
  Counts zero bits from the least significant end. Zero returns the full bit width. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let rotate_left = func(v: uint16, n: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L269)
  `n` is reduced to 0 through Bits - 1 first. A rotation by 0 returns `v`; otherwise both shift counts are between 1 and Bits - 1. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let rotate_right = func(v: uint16, n: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L279)
  Rotates the bits right, reducing the distance modulo the bit width. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let to_unsigned = func(v: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L289)
  The identity: every integer module has both conversions. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let to_signed = func(v: uint16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L293)
  The same-width signed type with the same bit pattern (section 7.2). Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let hash = func(v: uint16) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L304)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let to_string = func(v: uint16) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L308)
  Decimal, with `-` for negative values (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let to_string_radix = func(v: uint16, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L313)
  Lowercase digits. A radix outside 2 through 36 traps. The neura.integer.format_u64 kernel writes the digits. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let format = func(v: uint16, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L319)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value. Tagged `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let parse = func(text: string) -> Result<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L325)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **UInt16** · [`export let parse_radix = func(text: string, radix: int) -> Result<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L336)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A `-` is accepted only for zero. A radix outside 2 through 36 traps. The neura.integer.parse_u64 kernel parses the text as uint64, and the value is then checked against the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
