# `Std.Core.UInt8`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.UInt8`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.UInt8`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UInt8** · [`export const Min: uint8 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L13)
  The smallest value representable by this integer type.
- **UInt8** · [`export const Max: uint8 = 255`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L15)
  The largest value representable by this integer type.
- **UInt8** · [`export const Bits: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L17)
  The number of bits in the integer representation.
- **UInt8** · [`export let sign = func(v: uint8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L20)
  Returns zero for zero and one for every other value.
- **UInt8** · [`export let min = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L23)
  Returns the smaller of the two operands.
- **UInt8** · [`export let max = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L26)
  Returns the larger of the two operands.
- **UInt8** · [`export let clamp = func(v: uint8, low: uint8, high: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L29)
  Traps with "invalid argument: low exceeds high".
- **UInt8** · [`export let compare = func(a: uint8, b: uint8) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L43)
  The natural order; usable as a comparator.
- **UInt8** · [`export let pow = func(base: uint8, exponent: int) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L57)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **UInt8** · [`export let gcd = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L77)
  Euclid's algorithm. gcd(0, 0) is 0.
- **UInt8** · [`export let checked_add = func(a: uint8, b: uint8) -> Optional<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L89)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **UInt8** · [`export let checked_sub = func(a: uint8, b: uint8) -> Optional<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L97)
  Returns the difference in Some, or None when it falls outside the type's range.
- **UInt8** · [`export let checked_mul = func(a: uint8, b: uint8) -> Optional<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L105)
  Returns the product in Some, or None when it falls outside the type's range.
- **UInt8** · [`export let checked_div = func(a: uint8, b: uint8) -> Optional<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L113)
  Returns the quotient in Some, or None when the divisor is zero.
- **UInt8** · [`export let checked_rem = func(a: uint8, b: uint8) -> Optional<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L121)
  Returns the remainder in Some, or None when the divisor is zero.
- **UInt8** · [`export let wrapping_add = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L130)
  The result modulo 2^Bits (section 7.2): the operands are widened to uint64, where a sum is below 2^33 and a product below 2^64, and the result is masked to the width.
- **UInt8** · [`export let wrapping_sub = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L134)
  2^Bits is added before subtracting, so the uint64 difference stays non-negative.
- **UInt8** · [`export let wrapping_mul = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L138)
  Multiplies modulo two raised to the type's bit width.
- **UInt8** · [`export let wrapping_neg = func(a: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L142)
  Negates modulo two raised to the type's bit width.
- **UInt8** · [`export let saturating_add = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L145)
  Clamp to Min or Max instead of overflowing.
- **UInt8** · [`export let saturating_sub = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L153)
  Subtracts and clamps an out-of-range result to the nearest representable bound.
- **UInt8** · [`export let saturating_mul = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L161)
  Multiplies and clamps an out-of-range result to the nearest representable bound.
- **UInt8** · [`export let floor_div = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L170)
  For unsigned operands these are the ordinary quotient and remainder. A zero divisor traps (reason 1).
- **UInt8** · [`export let floor_mod = func(a: uint8, b: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L173)
  Returns the unsigned remainder. A zero divisor traps.
- **UInt8** · [`export let count_ones = func(v: uint8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L177)
  Bit counts over Bits bits by masks and shifts. Each step adds neighbouring fields whose sums fit in the wider field, so nothing overflows.
- **UInt8** · [`export let leading_zeros = func(v: uint8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L187)
  A binary search for the highest set bit: a left shift only discards bits already known to be zero.
- **UInt8** · [`export let trailing_zeros = func(v: uint8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L209)
  Counts zero bits from the least significant end. Zero returns the full bit width.
- **UInt8** · [`export let rotate_left = func(v: uint8, n: int) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L232)
  `n` is reduced to 0 through Bits - 1 first. A rotation by 0 returns `v`; otherwise both shift counts are between 1 and Bits - 1.
- **UInt8** · [`export let rotate_right = func(v: uint8, n: int) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L241)
  Rotates the bits right, reducing the distance modulo the bit width.
- **UInt8** · [`export let to_unsigned = func(v: uint8) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L250)
  The identity: every integer module has both conversions.
- **UInt8** · [`export let to_signed = func(v: uint8) -> int8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L253)
  The same-width signed type with the same bit pattern (section 7.2).
- **UInt8** · [`export let hash = func(v: uint8) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L263)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **UInt8** · [`export let to_string = func(v: uint8) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L266)
  Decimal, with `-` for negative values (section 5.2).
- **UInt8** · [`export let to_string_radix = func(v: uint8, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L269)
  Lowercase digits. A radix outside 2 through 36 traps.
- **UInt8** · [`export let format = func(v: uint8, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L289)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **UInt8** · [`export let parse = func(text: string) -> Result<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L294)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range.
- **UInt8** · [`export let parse_radix = func(text: string, radix: int) -> Result<uint8>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/u-int8.nra#L301)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A `-` is accepted only for zero. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
