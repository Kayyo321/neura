# `Std.Core.Int64`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Int64`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Int64`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Int64** · [`export const Min: int64 = -9223372036854775808`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L11)
- **Int64** · [`export const Max: int64 = 9223372036854775807`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L12)
- **Int64** · [`export const Bits: int = 64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L13)
- **Int64** · [`export let abs = func(v: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L16)
  Traps (reason 1) for Min, from the negation.
- **Int64** · [`export let sign = func(v: int64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L18)
- **Int64** · [`export let min = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L28)
- **Int64** · [`export let max = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L30)
- **Int64** · [`export let clamp = func(v: int64, low: int64, high: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L33)
  Traps with "invalid argument: low exceeds high".
- **Int64** · [`export let compare = func(a: int64, b: int64) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L47)
  The natural order; usable as a comparator.
- **Int64** · [`export let pow = func(base: int64, exponent: int) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L61)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1.
- **Int64** · [`export let gcd = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L83)
  Euclid's algorithm in the negative range, where every magnitude fits. gcd(0, 0) is 0. Traps (reason 1) when the result, 2^(Bits - 1), does not fit.
- **Int64** · [`export let checked_add = func(a: int64, b: int64) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L102)
  None where the operator would trap. Each test runs before the operation, so nothing overflows.
- **Int64** · [`export let checked_sub = func(a: int64, b: int64) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L109)
- **Int64** · [`export let checked_mul = func(a: int64, b: int64) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L116)
- **Int64** · [`export let checked_div = func(a: int64, b: int64) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L124)
  None for a zero divisor and for Min / -1.
- **Int64** · [`export let checked_rem = func(a: int64, b: int64) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L132)
  None for a zero divisor. Min % -1 is 0, as for the operator.
- **Int64** · [`export let wrapping_add = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L141)
  The result modulo 2^Bits: the operation runs on the unsigned pattern and converts back (section 7.2).
- **Int64** · [`export let wrapping_sub = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L144)
- **Int64** · [`export let wrapping_mul = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L147)
- **Int64** · [`export let wrapping_neg = func(a: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L151)
  Min wraps to itself.
- **Int64** · [`export let saturating_add = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L155)
  Clamp to Min or Max instead of overflowing.
- **Int64** · [`export let saturating_sub = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L165)
- **Int64** · [`export let saturating_mul = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L175)
- **Int64** · [`export let floor_div = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L190)
  The quotient rounded toward negative infinity. A zero divisor and Min / -1 trap (reason 1) in the division. The remainder is recovered by multiplication, which cannot overflow because |quotient * b| <= |a|; a multiplication circuit is several times smaller than a second divider.
- **Int64** · [`export let floor_mod = func(a: int64, b: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L202)
  The remainder with the divisor's sign. A zero divisor traps (reason 1). Computed from the quotient, so floor_div and floor_mod share one divider circuit. A divisor of -1 always leaves 0 (Min / -1 would trap, while Min % -1 is 0).
- **Int64** · [`export let count_ones = func(v: int64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L214)
  Bit counts and rotations over the two's complement pattern.
- **Int64** · [`export let leading_zeros = func(v: int64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L216)
- **Int64** · [`export let trailing_zeros = func(v: int64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L218)
- **Int64** · [`export let rotate_left = func(v: int64, n: int) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L220)
- **Int64** · [`export let rotate_right = func(v: int64, n: int) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L223)
- **Int64** · [`export let to_unsigned = func(v: int64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L227)
  The same-width unsigned type with the same bit pattern (section 7.2).
- **Int64** · [`export let to_signed = func(v: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L237)
  The identity: every integer module has both conversions.
- **Int64** · [`export let hash = func(v: int64) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L241)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half.
- **Int64** · [`export let to_string = func(v: int64) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L244)
  Decimal, with `-` for negative values (section 5.2).
- **Int64** · [`export let to_string_radix = func(v: int64, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L248)
  Lowercase digits. The digits are produced in the negative range, so Min needs no negation. A radix outside 2 through 36 traps.
- **Int64** · [`export let format = func(v: int64, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L272)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value.
- **Int64** · [`export let parse = func(text: string) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L275)
- **Int64** · [`export let parse_radix = func(text: string, radix: int) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int64.nra#L283)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. The digits accumulate in the negative range, so Min parses without overflow. A radix outside 2 through 36 traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 7.2 Arithmetic without wrapping intrinsics

The first plan asked for wrapping arithmetic and sign reinterpretation as intrinsics. The language's checked operators, shifts that discard high bits, and masks are enough. Each recipe below says why no intermediate value leaves its type. The `uint32` and `uint64` multiplication recipes were probed in both build modes.

| Operation | Recipe | Why it fits |
| --- | --- | --- |
| Wrapping add or subtract, widths up to 32 | Convert both operands to the 64-bit type of the same signedness, operate, mask to the width, convert back. For signed types go through the unsigned pattern first. | The sum of two values below 2^32 is below 2^33. |
| Wrapping multiply, widths up to 32 | Convert to `uint64`, multiply, mask, convert back. | The product of two values below 2^32 is below 2^64. |
| Wrapping add, `uint64` | Let `room` be `Max - a`. If `b` exceeds `room`, the result is `b - room - 1`; otherwise `a + b`. | `Max - a` cannot underflow; `b - room - 1` is at least 0 because `b > room`; the other branch is at most `Max`. |
| Wrapping subtract, `uint64` | If `a` is at least `b`, `a - b`; otherwise `Max - (b - a) + 1`. | `b - a` is between 1 and `Max`, so `Max - (b - a)` is at most `Max - 1`. |
| Wrapping multiply, `uint64` | Split each operand into low and high 32-bit halves. The result is `low × low`, plus the low 32 bits of (`high_a × low_b` + `low_a × high_b`) shifted left by 32, with wrapping adds. | Each partial product has two factors below 2^32. The two cross products are each masked to 32 bits before they are added, so their sum is below 2^33. The shift discards the bits above 64 by definition. |
| Multiply by 5, 9, or 31 | A shift and a wrapping add or subtract. | Shifts discard high bits. |
| Signed to unsigned, same width up to 32 | A non-negative value converts directly. A negative value is converted to `int64`, 2^width is added, and the sum is converted. | A negative value is at least -2^(width-1), so the sum is between 2^(width-1) and 2^width - 1. |
| Unsigned to signed, same width up to 32 | A value below 2^(width-1) converts directly. Otherwise convert to `int64`, subtract 2^width, convert. | The difference is between -2^(width-1) and -1. |
| `int64` to `uint64` | A non-negative value converts directly. For a negative value, add `Int64.Max`, add 1, convert, and add 2^63. | Adding `Max` to a negative value gives at least -1; adding 1 gives a value from 0 through 2^63 - 1; adding 2^63 as `uint64` stays below 2^64. |
| `uint64` to `int64` | A value below 2^63 converts directly. Otherwise subtract 2^63, convert, add `Int64.Min`. | The difference is below 2^63, and adding `Min` to it gives a value from `Min` through -1. |
| Limb arithmetic for big numbers | Limbs are `uint32`; every intermediate is `uint64`. | A limb product plus an incoming carry plus an existing limb is at most (2^32 - 1)^2 + 2(2^32 - 1), which is 2^64 - 1. |

Signed wrapping operations convert to the unsigned pattern, operate, and convert back.

**What the recipes do not cover.** A float's bit pattern (N1). Everything else that the first plan routed through intrinsics is on this table: hashing uses the 32-bit rows, `Random` and `Sha256` use the `uint64` and `uint32` rows, `BitSet` uses plain masks and shifts on `uint32`, and `BigInteger` uses the limb row.
