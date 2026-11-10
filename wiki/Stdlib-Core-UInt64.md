# `Std.Core.UInt64`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.UInt64`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.UInt64`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UInt64** · [`export const Min: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L10)
- **UInt64** · [`export const Max: uint64 = 18446744073709551615`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L11)
- **UInt64** · [`export const Bits: int = 64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L12)
- **UInt64** · [`export let sign = func(v: uint64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L14)
- **UInt64** · [`export let min = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L16)
- **UInt64** · [`export let max = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L18)
- **UInt64** · [`export let clamp = func(v: uint64, low: uint64, high: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L21)
  Traps with "invalid argument: low exceeds high".
- **UInt64** · [`export let compare = func(a: uint64, b: uint64) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L35)
  The natural order; usable as a comparator.
- **UInt64** · [`export let pow = func(base: uint64, exponent: int) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L49)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **UInt64** · [`export let gcd = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L69)
  Euclid's algorithm. gcd(0, 0) is 0.
- **UInt64** · [`export let checked_add = func(a: uint64, b: uint64) -> Optional<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L82)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **UInt64** · [`export let checked_sub = func(a: uint64, b: uint64) -> Optional<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L89)
- **UInt64** · [`export let checked_mul = func(a: uint64, b: uint64) -> Optional<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L96)
- **UInt64** · [`export let checked_div = func(a: uint64, b: uint64) -> Optional<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L103)
- **UInt64** · [`export let checked_rem = func(a: uint64, b: uint64) -> Optional<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L110)
- **UInt64** · [`export let wrapping_add = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L119)
  The result modulo 2^64 (section 7.2). `room` is Max - a, which cannot underflow; when b exceeds it, b - room - 1 is the wrapped sum.
- **UInt64** · [`export let wrapping_sub = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L128)
  When a < b, b - a is between 1 and Max, so Max - (b - a) + 1 fits.
- **UInt64** · [`export let wrapping_mul = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L139)
  Low and high 32-bit halves: every partial product has two factors below 2^32, the cross products are masked to 32 bits before they are added (their sum is below 2^33), and the shift discards the bits above 64.
- **UInt64** · [`export let wrapping_neg = func(a: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L148)
- **UInt64** · [`export let saturating_add = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L151)
  Clamp to Min or Max instead of overflowing.
- **UInt64** · [`export let saturating_sub = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L158)
- **UInt64** · [`export let saturating_mul = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L165)
- **UInt64** · [`export let floor_div = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L174)
  For unsigned operands these are the ordinary quotient and remainder. A zero divisor traps (reason 1).
- **UInt64** · [`export let floor_mod = func(a: uint64, b: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L176)
- **UInt64** · [`export let count_ones = func(v: uint64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L181)
  Bit counts over Bits bits by masks and shifts. Each step adds neighbouring fields whose sums fit in the wider field, so nothing overflows.
- **UInt64** · [`export let leading_zeros = func(v: uint64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L194)
  A binary search for the highest set bit: a left shift only discards bits already known to be zero.
- **UInt64** · [`export let trailing_zeros = func(v: uint64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L227)
- **UInt64** · [`export let rotate_left = func(v: uint64, n: int) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L262)
  `n` is reduced to 0 through Bits - 1 first. A rotation by 0 returns `v`; otherwise both shift counts are between 1 and Bits - 1.
- **UInt64** · [`export let rotate_right = func(v: uint64, n: int) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L270)
- **UInt64** · [`export let to_unsigned = func(v: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L279)
  The identity: every integer module has both conversions.
- **UInt64** · [`export let to_signed = func(v: uint64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L282)
  The same-width signed type with the same bit pattern (section 7.2).
- **UInt64** · [`export let hash = func(v: uint64) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L292)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **UInt64** · [`export let to_string = func(v: uint64) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L295)
  Decimal, with `-` for negative values (section 5.2).
- **UInt64** · [`export let to_string_radix = func(v: uint64, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L298)
  Lowercase digits. A radix outside 2 through 36 traps.
- **UInt64** · [`export let format = func(v: uint64, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L319)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **UInt64** · [`export let parse = func(text: string) -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L322)
- **UInt64** · [`export let parse_radix = func(text: string, radix: int) -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int64.nra#L329)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A `-` is accepted only for zero. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

This module has no dedicated section in the historical design. Use the declaration reference and source above for its public API; the shared contracts and implementation decisions apply where relevant.
