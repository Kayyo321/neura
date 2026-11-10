# `Std.Core.Int8`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Int8`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Int8`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Int8** · [`export const Min: int8 = -128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L11)
- **Int8** · [`export const Max: int8 = 127`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L12)
- **Int8** · [`export const Bits: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L13)
- **Int8** · [`export let abs = func(v: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L16)
  Traps (reason 1) for Min, from the negation.
- **Int8** · [`export let sign = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L18)
- **Int8** · [`export let min = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L28)
- **Int8** · [`export let max = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L30)
- **Int8** · [`export let clamp = func(v: int8, low: int8, high: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L33)
  Traps with "invalid argument: low exceeds high".
- **Int8** · [`export let compare = func(a: int8, b: int8) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L47)
  The natural order; usable as a comparator.
- **Int8** · [`export let pow = func(base: int8, exponent: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L61)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **Int8** · [`export let gcd = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L83)
  Euclid's algorithm in the negative range, where every magnitude fits. gcd(0, 0) is 0. Traps (reason 1) when the result, 2^(Bits - 1), does not fit.
- **Int8** · [`export let checked_add = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L102)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **Int8** · [`export let checked_sub = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L109)
- **Int8** · [`export let checked_mul = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L116)
- **Int8** · [`export let checked_div = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L124)
  None for a zero divisor and for Min / -1.
- **Int8** · [`export let checked_rem = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L132)
  None for a zero divisor. Min % -1 is 0, as for the operator.
- **Int8** · [`export let wrapping_add = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L141)
  The result modulo 2^Bits: the operation runs on the unsigned pattern and converts back (section 7.2).
- **Int8** · [`export let wrapping_sub = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L144)
- **Int8** · [`export let wrapping_mul = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L147)
- **Int8** · [`export let wrapping_neg = func(a: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L151)
  Min wraps to itself.
- **Int8** · [`export let saturating_add = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L155)
  Clamp to Min or Max instead of overflowing.
- **Int8** · [`export let saturating_sub = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L165)
- **Int8** · [`export let saturating_mul = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L175)
- **Int8** · [`export let floor_div = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L187)
  The quotient rounded toward negative infinity. A zero divisor and Min / -1 trap (reason 1) in the division.
- **Int8** · [`export let floor_mod = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L196)
  The remainder with the divisor's sign. A zero divisor traps (reason 1).
- **Int8** · [`export let count_ones = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L205)
  Bit counts and rotations over the two's complement pattern.
- **Int8** · [`export let leading_zeros = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L207)
- **Int8** · [`export let trailing_zeros = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L209)
- **Int8** · [`export let rotate_left = func(v: int8, n: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L211)
- **Int8** · [`export let rotate_right = func(v: int8, n: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L214)
- **Int8** · [`export let to_unsigned = func(v: int8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L218)
  The same-width unsigned type with the same bit pattern (section 7.2).
- **Int8** · [`export let to_signed = func(v: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L228)
  The identity: every integer module has both conversions.
- **Int8** · [`export let hash = func(v: int8) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L232)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **Int8** · [`export let to_string = func(v: int8) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L235)
  Decimal, with `-` for negative values (section 5.2).
- **Int8** · [`export let to_string_radix = func(v: int8, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L239)
  Lowercase digits. The digits are produced in the negative range, so Min needs no negation. A radix outside 2 through 36 traps.
- **Int8** · [`export let format = func(v: int8, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L263)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **Int8** · [`export let parse = func(text: string) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L266)
- **Int8** · [`export let parse_radix = func(text: string, radix: int) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L274)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. The digits accumulate in the negative range, so Min parses without overflow. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

This module has no dedicated section in the historical design. Use the declaration reference and source above for its public API; the shared contracts and implementation decisions apply where relevant.
