# `Std.Math.BigInteger`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Math.BigInteger`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Math.BigInteger`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BigInteger** · [`export let BigInteger = class implements Comparable<BigInteger>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L30)
  An immutable signed integer with a normalized little-endian magnitude. Tagged `#Final`, `#NoDiscard`.
- **BigInteger.BigInteger** · [`export let init = func(sign: int, limbs: uint32[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L41)
  The value sign * limbs, limbs least significant first. Zero limbs at the top are dropped, and a zero magnitude has sign 0 whatever `sign` says; otherwise only the sign of `sign` matters. The new value owns `limbs`: the caller must not change the list afterwards.
- **BigInteger.BigInteger** · [`export let sign = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L61)
  Returns minus one, zero, or one according to the sign. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let is_zero = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L65)
  Reports whether this integer is zero. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let is_even = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L69)
  Tests divisibility by two, including zero. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let negate = func -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L72)
  Returns the value with its sign reversed.
- **BigInteger.BigInteger** · [`export let abs = func -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L75)
  Returns the nonnegative magnitude.
- **BigInteger.BigInteger** · [`export let add = func(other: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L78)
  Returns the exact sum, subject to the limb limit.
- **BigInteger.BigInteger** · [`export let subtract = func(other: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L82)
  Returns the exact difference, subject to the limb limit.
- **BigInteger.BigInteger** · [`export let multiply = func(other: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L87)
  Schoolbook with uint64 partial products; Karatsuba when both operands have at least 48 limbs. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let divide_and_remainder = func(other: BigInteger) -> [BigInteger, BigInteger]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L96)
  The quotient truncated toward zero and the remainder with the dividend's sign, from one division. A zero divisor traps (reason 1) in the division by the divisor's sign.
- **BigInteger.BigInteger** · [`export let divide = func(other: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L103)
  Returns the quotient truncated toward zero. A zero divisor traps.
- **BigInteger.BigInteger** · [`export let remainder = func(other: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L109)
  Returns the remainder from truncating division. A zero divisor traps.
- **BigInteger.BigInteger** · [`export let floor_mod = func(m: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L116)
  The remainder with the modulus's sign: non-negative for a positive modulus. A zero modulus traps (reason 1).
- **BigInteger.BigInteger** · [`export let pow = func(exponent: int) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L126)
  Square and multiply. pow(0) is 1; a negative exponent traps. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let gcd = func(other: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L161)
  Euclid's algorithm with remainders; the result is non-negative and gcd(0, 0) is 0.
- **BigInteger.BigInteger** · [`export let mod_pow = func(exponent: BigInteger, modulus: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L174)
  this^exponent mod modulus, in 0 through modulus - 1: left-to-right binary exponentiation reducing after each step. A negative exponent or a modulus that is not positive traps.
- **BigInteger.BigInteger** · [`export let mod_inverse = func(modulus: BigInteger) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L197)
  The x in 0 through modulus - 1 with this * x = 1 (mod modulus), by extended Euclid, or nil when gcd(this, modulus) is not 1. A modulus that is not positive traps.
- **BigInteger.BigInteger** · [`export let shift_left = func(n: int) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L221)
  this * 2^n. A negative n traps. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let shift_right = func(n: int) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L233)
  floor(this / 2^n): rounds toward negative infinity. A negative n traps. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let bit_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L246)
  The number of bits of the magnitude; 0 for zero. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let test_bit = func(n: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L256)
  Bit n of the magnitude. A negative n traps. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let compare_to = func(other: BigInteger) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L269)
  Compares signed values by sign and then magnitude. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L288)
  Tests whether another BigInteger has the same sign and magnitude. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L299)
  Starts at 0, combines each limb from most to least significant, then negates (wrapping) for a negative value. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let to_int64 = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L312)
  Returns Some when the value fits in int64, or None otherwise. Tagged `#NoDiscard`, `#Pure`.
- **BigInteger.BigInteger** · [`export let to_float64 = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L340)
  The nearest float64, ties to even; an infinity beyond the range. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L360)
  Decimal, by repeated division by 10^9 and nine-digit groups. Tagged `#Pure`.
- **BigInteger.BigInteger** · [`export let to_string_radix = func(radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L364)
  Lowercase digits. A radix outside 2 through 36 traps. Tagged `#Pure`.
- **BigInteger** · [`export const Zero: BigInteger = new BigInteger(0, [])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L373)
  The methods above build their own zero and one instead of reading these constants: a constant object is module state, which a spawned task may not read (Concurrency.md section 5.2). The integer zero.
- **BigInteger** · [`export const One: BigInteger = new BigInteger(1, [1])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L375)
  The integer one.
- **BigInteger** · [`export const Ten: BigInteger = new BigInteger(1, [10])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L377)
  The integer ten.
- **BigInteger** · [`export let of_int64 = func(value: int64) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L381)
  Constructs a BigInteger from any int64, including the minimum signed value. Tagged `#Pure`, `#ThreadSafe`.
- **BigInteger** · [`export let of_uint64 = func(value: uint64) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L392)
  Constructs a nonnegative BigInteger from an unsigned 64-bit value. Tagged `#Pure`, `#ThreadSafe`.
- **BigInteger** · [`export let parse = func(text: string) -> Result<BigInteger>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L396)
  Parses base-10 integer text or returns a parse error. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **BigInteger** · [`export let parse_radix = func(text: string, radix: int) -> Result<BigInteger>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-integer.nra#L401)
  An optional `+` or `-`, then one or more digits of the radix in either case (Stdlib.md section 7.1), with no range failure. A radix outside 2 through 36 traps. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.7 Conformance summary

These are the intended source-module contracts. Compiler support for the operations does not imply that a collection module or adapter is already packaged.

| Type | Equality and hash | Natural order | Iterable | Interfaces (G4) |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, fieldless enums, `string`, tuples | Built in (G3a) | Companion `compare` functions | No (`Strings.scalars` gives a list) | |
| Array, list | Built in, structural (G3a) | No | Built-in for-each | |
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Module functions; payload enums have no built-in equality | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `RegexFlags`, `RegexMatch`, `Regex` | By value | No | No | |
| `StringBuilder`, `ByteBuffer` | Current byte sequence | No | No | |
| `Sequence`, `Random`, `Logger`, stream adapters, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](Stdlib-Design#111-stdio) |

### 6.5 Small types

| Type | Definition | Available |
| --- | --- | --- |
| `Unit` | A fieldless enum with the single variant `Value`. | Yes |
| `Cell<T>` | A class with one mutable field `value`, and the module function `of<T>(value)`. It exists so that closures, which capture by value, can share mutable state. | Yes |
| `IntRange` | An immutable class with the get-only fields `start` (inclusive), `end` (exclusive), and `step` (nonzero). Module functions `of(start, end)` and `stepped(start, end, step)`; a zero step traps. A negative step counts down while the value is greater than `end`. Members `size()`, `is_empty()`, `contains(value)`, `get(index)`, `to_list()`, and `iterator()`; it implements `Iterable<int>`, so `for (let i in range)` visits the values in order. `size` and iteration are computed in `int64` so that no intermediate overflows. | Yes |

`Pair`, `Entry`, and `Boxed` do not exist: a pair is the native tuple `[A, B]` (`partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`), and a map entry is the tuple `[K, V]`. No API on this page takes an `Object`.

Named types are kept where the name carries a contract a tuple cannot: `RegexMatch` (group lookup by name), `FileMetadata` (three fields of unrelated types that will grow), and the value classes of `Std.Time`.

### 7.2 Arithmetic without wrapping intrinsics

The language has no wrapping arithmetic or sign reinterpretation. The checked operators, shifts that discard high bits, and masks are enough. Each recipe below says why no intermediate value leaves its type. The `uint32` and `uint64` multiplication recipes are tested in both build modes.

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

**What the recipes do not cover.** A float's bit pattern (N1). Everything else is on this table: hashing uses the 32-bit rows, `Random` and `Sha256` use the `uint64` and `uint32` rows, `BitSet` uses plain masks and shifts on `uint32`, and `BigInteger` uses the limb row.

### 10.2 `BigInteger`

An immutable arbitrary-precision integer. Parsing uses T1 and conversion to `float64` uses N1.

- **Layout.** `sign` (`int`: -1, 0, or 1) and `magnitude`, a built-in `uint32` list of limbs, least significant first, with no zero limb at the top. Zero has sign 0 and an empty magnitude. The form is unique, so equality compares the fields. No method changes the list after construction.
- **Limits.** At most 2^26 limbs; a larger result traps with `invalid argument: magnitude too large`.
- **Module functions.** The constants `Zero`, `One`, `Ten`; `of_int64`, `of_uint64`; `parse(text)` and `parse_radix(text, radix)` returning `Result<BigInteger>`, with the grammar of [section 7.1](Stdlib-Design#71-integer-modules) and no range failure.

| Operation | Algorithm |
| --- | --- |
| `add`, `subtract` | Limb-wise with carry or borrow held in `uint64`; signs handled by comparing magnitudes. |
| `multiply` | Schoolbook with `uint64` partial products, which fit by the limb row of [section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics). When both operands have at least 48 limbs, Karatsuba: split at half of the larger length and recurse. |
| `divide`, `remainder` | Knuth's Algorithm D in base 2^32. The quotient truncates toward zero and the remainder takes the dividend's sign, matching the integer operators. A single-limb divisor uses the short division loop. A zero divisor traps (reason 1), from the division it performs. |
| `divide_and_remainder(other) → [BigInteger, BigInteger]` | Both results from one division, as a tuple. |
| `floor_mod(m)` | Non-negative result for a positive modulus. |
| `pow(exponent: int)` | Square and multiply. A negative exponent traps. |
| `gcd(other)` | Euclid's algorithm with `remainder`; the result is non-negative. |
| `mod_pow(exponent, modulus)` | Left-to-right binary exponentiation, reducing after each step. A negative exponent or a non-positive modulus traps. |
| `mod_inverse(modulus) → BigInteger` | Extended Euclid. Returns `nil` when no inverse exists. |
| `negate`, `abs`, `sign`, `is_zero`, `is_even` | |
| `shift_left(n)`, `shift_right(n)` | `shift_right` rounds toward negative infinity. A negative `n` traps. Limb shifts use counts from 1 through 31, so the built-in shifts never trap. |
| `bit_length() → int`, `test_bit(n) → bool` | Of the magnitude. |
| `compare_to(other) → Ordering`, `equals(other)`, `hash_code()` | Hash: start at 0, combine each limb from most to least significant, then negate (wrapping) for a negative value. |
| `to_int64() → Optional<int64>`, `to_float64() → float64` | `to_float64` rounds to nearest, ties to even, and gives an infinity beyond the range. |
| `to_string()`, `to_string_radix(radix)` | Radix 10 divides repeatedly by 10^9 and joins nine-digit groups. |

`BigInteger` has no bitwise and, or, or exclusive or.

### 10.3 `BigDecimal`

An immutable decimal number stores an unscaled `BigInteger` and an `int` `scale`, representing unscaled × 10^(-scale), in the read-only fields `unscaled` and `scale`. Parsing uses T1 and float conversion uses N1.

- `equals` and `compare_to` are numeric, so `2.0` equals `2.00`. `hash_code` is computed on the form with trailing zeros stripped.
- `add` and `subtract` give the larger scale; `multiply` gives the sum of the scales; all are exact.
- Division always takes an explicit result scale and `RoundingMode`: `divide(other, scale, mode)`. There is no exact division that could fail on a non-terminating quotient.
- `RoundingMode` is a fieldless enum, in order: `Up`, `Down`, `Ceiling`, `Floor`, `HalfUp`, `HalfDown`, `HalfEven`, with Java's meanings.
- Other members: `set_scale(scale, mode)`, `round(precision, mode)`, `strip_trailing_zeros`, `negate`, `abs`, `sign`, `pow(exponent: int)`, `to_big_integer(mode)`, `to_float64()`, and the module functions `of_int64`, `of_big_integer(value, scale)`, `of_float64(v)` (the exact binary value; NaN and infinities trap), and `parse(text) → Result<BigDecimal>`.
- `to_string` always uses plain notation with exactly `scale` fraction digits when the scale is positive, and appends zeros for a negative scale (zero with a negative scale prints as `0`).
- A scale that leaves the `int` range traps (reason 1).

### 18.10 Math

- **fdlibm port.** `Math` is a port of fdlibm 5.3 written without a reference copy of the sources; Sun's notice is kept in the Neura port (`tests/stdlib/math/support/reference-math.nra` and `reference-math-trig.nra`, the kernels' oracle since `Math` binds the kernels), in `src/kernels/`, and in the test transliteration. The Neura functions agree bit for bit with a C transliteration of the same port on 9,148 vector cases (`tests/stdlib/math/vectors`), and that transliteration stays within 1 ulp of a 70-digit reference for the functions fdlibm documents as such, with the same larger errors as glibc for `sinh`, `cosh`, `tanh`, and `log10`. Agreement with a reference fdlibm 5.3 build has not been checked. Expressions fdlibm evaluates only to raise the inexact flag are omitted. `sqrt` is the correctly rounded integer method of [section 10.1](Stdlib-Design#101-math). `log2`, which fdlibm 5.3 lacks, follows `e_log10.c` and returns exact results for powers of two. `pow(±1, ±∞)` and `pow(1, NaN)` are NaN, as in fdlibm.
- **BigInteger** keeps its fields private (`signum`, `magnitude`); its constructor normalizes the value and adopts the list. `mod_inverse` traps for a modulus that is not positive. `floor_mod` takes the modulus's sign.
- **BigDecimal** adds the read-only fields `unscaled` and `scale`. `round(0, mode)` returns the value unchanged; a rounding carry drops one zero, as Java does. `of_float64` gives the exact value at the smallest scale. `-0.00` parses as zero. `parse` accepts an optional sign, digits with an optional point, and an optional `e` or `E` exponent; a scale outside `int` fails with `OutOfRange` at offset 0. Zero with a negative scale prints as `0`.
- **Trap details.** `negative exponent`, `negative shift count`, `negative bit index`, `modulus not positive`, `not a finite value`, `negative precision`, `magnitude too large`, and `zero sign with a nonzero magnitude` after `invalid argument: `.
- **Cost.** A `Math` call is one kernel call of one to four steps. While `Math` was Neura code, every float64 operation in it compiled to a large circuit: the `elementary` test program, which calls every function, had an image of 14,719,526 bytes, and has one of 15,480 bytes since it binds the kernels. `Math` is not in the cost baseline.

### Task safety

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). Every function of this module is. The constants `Zero`, `One`, and `Ten` are objects, and a constant that is an object is module state a spawned task may not read; a task builds the value with `of_int64`. The module's own methods do not read the constants. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.
