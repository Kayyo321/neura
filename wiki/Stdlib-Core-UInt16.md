# `Std.Core.UInt16`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.UInt16`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.UInt16`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UInt16** · [`export const Min: uint16 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L10)
- **UInt16** · [`export const Max: uint16 = 65535`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L11)
- **UInt16** · [`export const Bits: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L12)
- **UInt16** · [`export let sign = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L14)
- **UInt16** · [`export let min = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L16)
- **UInt16** · [`export let max = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L18)
- **UInt16** · [`export let clamp = func(v: uint16, low: uint16, high: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L21)
  Traps with "invalid argument: low exceeds high".
- **UInt16** · [`export let compare = func(a: uint16, b: uint16) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L35)
  The natural order; usable as a comparator.
- **UInt16** · [`export let pow = func(base: uint16, exponent: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L49)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **UInt16** · [`export let gcd = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L69)
  Euclid's algorithm. gcd(0, 0) is 0.
- **UInt16** · [`export let checked_add = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L82)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **UInt16** · [`export let checked_sub = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L89)
- **UInt16** · [`export let checked_mul = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L96)
- **UInt16** · [`export let checked_div = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L103)
- **UInt16** · [`export let checked_rem = func(a: uint16, b: uint16) -> Optional<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L110)
- **UInt16** · [`export let wrapping_add = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L120)
  The result modulo 2^Bits (section 7.2): the operands are widened to uint64, where a sum is below 2^33 and a product below 2^64, and the result is masked to the width.
- **UInt16** · [`export let wrapping_sub = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L125)
  2^Bits is added before subtracting, so the uint64 difference stays non-negative.
- **UInt16** · [`export let wrapping_mul = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L128)
- **UInt16** · [`export let wrapping_neg = func(a: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L131)
- **UInt16** · [`export let saturating_add = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L134)
  Clamp to Min or Max instead of overflowing.
- **UInt16** · [`export let saturating_sub = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L141)
- **UInt16** · [`export let saturating_mul = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L148)
- **UInt16** · [`export let floor_div = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L157)
  For unsigned operands these are the ordinary quotient and remainder. A zero divisor traps (reason 1).
- **UInt16** · [`export let floor_mod = func(a: uint16, b: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L159)
- **UInt16** · [`export let count_ones = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L164)
  Bit counts over Bits bits by masks and shifts. Each step adds neighbouring fields whose sums fit in the wider field, so nothing overflows.
- **UInt16** · [`export let leading_zeros = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L175)
  A binary search for the highest set bit: a left shift only discards bits already known to be zero.
- **UInt16** · [`export let trailing_zeros = func(v: uint16) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L200)
- **UInt16** · [`export let rotate_left = func(v: uint16, n: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L227)
  `n` is reduced to 0 through Bits - 1 first. A rotation by 0 returns `v`; otherwise both shift counts are between 1 and Bits - 1.
- **UInt16** · [`export let rotate_right = func(v: uint16, n: int) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L235)
- **UInt16** · [`export let to_unsigned = func(v: uint16) -> uint16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L244)
  The identity: every integer module has both conversions.
- **UInt16** · [`export let to_signed = func(v: uint16) -> int16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L247)
  The same-width signed type with the same bit pattern (section 7.2).
- **UInt16** · [`export let hash = func(v: uint16) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L257)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **UInt16** · [`export let to_string = func(v: uint16) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L260)
  Decimal, with `-` for negative values (section 5.2).
- **UInt16** · [`export let to_string_radix = func(v: uint16, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L263)
  Lowercase digits. A radix outside 2 through 36 traps.
- **UInt16** · [`export let format = func(v: uint16, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L284)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **UInt16** · [`export let parse = func(text: string) -> Result<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L287)
- **UInt16** · [`export let parse_radix = func(text: string, radix: int) -> Result<uint16>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int16.nra#L294)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A `-` is accepted only for zero. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

This module has no dedicated section in the historical design. Use the declaration reference and source above for its public API; the shared contracts and implementation decisions apply where relevant.
