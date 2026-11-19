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
- **Int8** · [`export let abs = func(v: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L25)
  Traps (reason 1) for Min, from the negation. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let sign = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L29)
  Returns minus one, zero, or one according to the sign of the value. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let min = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L38)
  Returns the smaller of the two operands. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let max = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L42)
  Returns the larger of the two operands. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let clamp = func(v: int8, low: int8, high: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L46)
  Traps with "invalid argument: low exceeds high". Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let compare = func(a: int8, b: int8) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L58)
  The natural order; usable as a comparator. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let pow = func(base: int8, exponent: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L70)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let gcd = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L92)
  Euclid's algorithm in the negative range, where every magnitude fits. gcd(0, 0) is 0. Traps (reason 1) when the result, 2^(Bits - 1), does not fit. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let checked_add = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L111)
  None where the operator would trap. Each test runs before the operation, so nothing overflows. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let checked_sub = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L120)
  Returns the difference in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let checked_mul = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L129)
  Returns the product in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let checked_div = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L135)
  None for a zero divisor and for Min / -1. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let checked_rem = func(a: int8, b: int8) -> Optional<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L141)
  None for a zero divisor. Min % -1 is 0, as for the operator. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let wrapping_add = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L148)
  The result modulo 2^Bits: the operation runs on the unsigned pattern and converts back (section 7.2). Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let wrapping_sub = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L153)
  Subtracts modulo two raised to the type's bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let wrapping_mul = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L158)
  Multiplies modulo two raised to the type's bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let wrapping_neg = func(a: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L163)
  Min wraps to itself. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let saturating_add = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L168)
  Clamp to Min or Max instead of overflowing. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let saturating_sub = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L177)
  Subtracts and clamps an out-of-range result to the nearest representable bound. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let saturating_mul = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L186)
  Multiplies and clamps an out-of-range result to the nearest representable bound. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let floor_div = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L196)
  The quotient rounded toward negative infinity. A zero divisor and Min / -1 trap (reason 1) in the division. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let floor_mod = func(a: int8, b: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L203)
  The remainder with the divisor's sign. A zero divisor traps (reason 1). Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let count_ones = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L210)
  Bit counts and rotations over the two's complement pattern. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let leading_zeros = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L214)
  Counts zero bits from the most significant end. Zero returns the full bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let trailing_zeros = func(v: int8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L218)
  Counts zero bits from the least significant end. Zero returns the full bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let rotate_left = func(v: int8, n: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L222)
  Rotates the bits left, reducing the distance modulo the bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let rotate_right = func(v: int8, n: int) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L227)
  Rotates the bits right, reducing the distance modulo the bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let to_unsigned = func(v: int8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L232)
  The same-width unsigned type with the same bit pattern (section 7.2). Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let to_signed = func(v: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L243)
  The identity: every integer module has both conversions. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let hash = func(v: int8) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L248)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let to_string = func(v: int8) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L252)
  Decimal, with `-` for negative values (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let to_string_radix = func(v: int8, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L257)
  Lowercase digits, with `-` for a negative value. A radix outside 2 through 36 traps. The neura.integer.format_i64 kernel writes the digits. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let format = func(v: int8, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L263)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value. Tagged `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let parse = func(text: string) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L269)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let parse_radix = func(text: string, radix: int) -> Result<int8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L280)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A radix outside 2 through 36 traps. The neura.integer.parse_i64 kernel parses the text as int64, and the value is then checked against the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int8** · [`export let zero = func() -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L300)
  The additive identity, 0 (Std.Core.Arithmetic.Additive). Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Int8** · [`export let add = func(left: int8, right: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L304)
  The sum `left + right`, at the type's width; traps (reason 1) on overflow. Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Int8** · [`export let one = func() -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L308)
  The multiplicative identity, 1 (Std.Core.Arithmetic.Multiplicative). Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Int8** · [`export let multiply = func(left: int8, right: int8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int8.nra#L312)
  The product `left * right`, at the type's width; traps (reason 1) on overflow. Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
