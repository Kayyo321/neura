# `Std.Math.BigDecimal`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Math.BigDecimal`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Math.BigDecimal`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BigDecimal** · [`export let RoundingMode = enum (Up, Down, Ceiling, Floor, HalfUp, HalfDown, HalfEven)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L22)
  Java's rounding modes: Up and Down round away from and toward zero, Ceiling and Floor toward positive and negative infinity, and the Half modes round to the nearest neighbour with ties away from zero, toward zero, or to the even neighbour.
- **BigDecimal** · [`export let BigDecimal = class implements Comparable<BigDecimal>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L26)
  An immutable decimal represented by an unscaled integer times ten raised to the negative scale. Tagged `#Final`, `#NoDiscard`.
- **BigDecimal.BigDecimal** · [`export last unscaled: BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L28)
  The integer coefficient, the unscaled value.
- **BigDecimal.BigDecimal** · [`export last scale: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L30)
  The number of digits after the decimal point; negative for a multiple of a power of ten.
- **BigDecimal.BigDecimal** · [`export let init = func(unscaled_part: BigInteger, scale_part: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L33)
  The value unscaled * 10^(-scale). A nil unscaled value traps.
- **BigDecimal.BigDecimal** · [`export let sign = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L42)
  Returns minus one, zero, or one according to the coefficient's sign.
- **BigDecimal.BigDecimal** · [`export let negate = func -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L45)
  Reverses the sign and preserves the scale.
- **BigDecimal.BigDecimal** · [`export let abs = func -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L48)
  Returns the absolute value and preserves the scale.
- **BigDecimal.BigDecimal** · [`export let add = func(other: BigDecimal) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L51)
  Exact; the result has the larger scale.
- **BigDecimal.BigDecimal** · [`export let subtract = func(other: BigDecimal) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L64)
  Exact; the result has the larger scale.
- **BigDecimal.BigDecimal** · [`export let multiply = func(other: BigDecimal) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L67)
  Exact; the result's scale is the sum of the scales.
- **BigDecimal.BigDecimal** · [`export let divide = func(other: BigDecimal, result_scale: int, mode: RoundingMode) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L72)
  this / other rounded to `result_scale` digits after the point with `mode`. A zero divisor traps (reason 1) in the BigInteger division.
- **BigDecimal.BigDecimal** · [`export let set_scale = func(new_scale: int, mode: RoundingMode) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L91)
  The same value with `new_scale` digits after the point, rounded with `mode` when digits are dropped.
- **BigDecimal.BigDecimal** · [`export let round = func(precision: int, mode: RoundingMode) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L101)
  The value rounded to `precision` significant digits with `mode`. A precision of 0 returns the value unchanged; a negative precision traps.
- **BigDecimal.BigDecimal** · [`export let strip_trailing_zeros = func -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L121)
  The numerically equal value with no trailing zeros in the unscaled value; zero has scale 0.
- **BigDecimal.BigDecimal** · [`export let pow = func(exponent: int) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L149)
  this^exponent exactly, with scale * exponent. pow(0) is 1 with scale 0; a negative exponent traps.
- **BigDecimal.BigDecimal** · [`export let to_big_integer = func(mode: RoundingMode) -> BigInteger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L157)
  The integer part rounded with `mode`.
- **BigDecimal.BigDecimal** · [`export let to_float64 = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L161)
  The nearest float64, ties to even; infinities beyond the range and signed zeros below it.
- **BigDecimal.BigDecimal** · [`export let compare_to = func(other: BigDecimal) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L194)
  Compares numerical values independently of scale without aligning widely separated scales.
- **BigDecimal.BigDecimal** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L226)
  Numeric equality: 2.0 equals 2.00.
- **BigDecimal.BigDecimal** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L234)
  From 17, combines the hashes of the stripped form's unscaled value and scale.
- **BigDecimal.BigDecimal** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L242)
  Plain notation: exactly `scale` digits after the point when the scale is positive, and -scale appended zeros when it is negative. Zero with a negative scale prints as 0.
- **BigDecimal** · [`export let of_int64 = func(value: int64) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L263)
  Constructs an exact decimal from an int64 at scale zero.
- **BigDecimal** · [`export let of_big_integer = func(value: BigInteger, scale: int) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L267)
  Constructs a decimal from its unscaled integer and scale.
- **BigDecimal** · [`export let of_float64 = func(v: float64) -> BigDecimal`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L272)
  The exact binary value of v with the smallest scale that holds it: 0.5 has scale 1 and 2.0 has scale 0. NaN and the infinities trap.
- **BigDecimal** · [`export let parse = func(text: string) -> Result<BigDecimal>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/math/big-decimal.nra#L310)
  An optional sign, digits with an optional point (at least one digit overall), and an optional exponent: `e` or `E`, an optional sign, and digits. The scale is the number of fraction digits minus the exponent; one outside the int range fails with OutOfRange. Tagged `#NoDiscard`.
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

### 10.3 `BigDecimal`

An immutable decimal number stores an unscaled `BigInteger` and an `int` `scale`, representing unscaled × 10^(-scale), in the read-only fields `unscaled` and `scale`. Parsing uses T1 and float conversion uses N1.

- `equals` and `compare_to` are numeric, so `2.0` equals `2.00`. `hash_code` is computed on the form with trailing zeros stripped.
- `add` and `subtract` give the larger scale; `multiply` gives the sum of the scales; all are exact.
- Division always takes an explicit result scale and `RoundingMode`: `divide(other, scale, mode)`. There is no exact division that could fail on a non-terminating quotient.
- `RoundingMode` is a fieldless enum, in order: `Up`, `Down`, `Ceiling`, `Floor`, `HalfUp`, `HalfDown`, `HalfEven`, with Java's meanings.
- Other members: `set_scale(scale, mode)`, `round(precision, mode)`, `strip_trailing_zeros`, `negate`, `abs`, `sign`, `pow(exponent: int)`, `to_big_integer(mode)`, `to_float64()`, and the module functions `of_int64`, `of_big_integer(value, scale)`, `of_float64(v)` (the exact binary value; NaN and infinities trap), and `parse(text) → Result<BigDecimal>`.
- `to_string` always uses plain notation with exactly `scale` fraction digits when the scale is positive, and appends zeros for a negative scale (zero with a negative scale prints as `0`).
- A scale that leaves the `int` range traps (reason 1).

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone. The kernel `neura.encoding.utf8_validate` computes it (P2a).
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error). The kernels `neura.encoding.hex_encode` and `hex_decode` compute it (P2a).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero. The kernels `neura.encoding.base64_encode` and `base64_decode` compute every form (P2a).
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**`JsonValue` is a recursive payload enum.** It is read with `match` and walked with mutually recursive functions.

| Variant | Payload |
| --- | --- |
| `Null` | None |
| `Boolean` | `bool` |
| `Number` | `string`: the number's JSON text |
| `Text` | `string` |
| `Array` | A built-in list of `JsonValue` |
| `Object` | A built-in list of `[string, JsonValue]`, in member order |

- **Objects are member lists.** They keep insertion order in the serialized representation. Lookup is a linear scan. `Json.index(members)` builds a `StringMap<JsonValue>` when callers need repeated lookups.
- **Numbers keep their text**, so no precision is lost in a round trip. The payload must be valid JSON number text; the writer checks it and traps with `invalid argument: number text` otherwise.
- **Aliasing.** The payloads of `Array` and `Object` are built-in lists: they are mutable, and the same list can be reached from more than one value. A `JsonValue` is therefore not a frozen tree. Changing a list changes every value that holds it, and a list can be made to contain a value that holds the same list. `write`, `equals`, and `hash` do not detect such a cycle and end with the frame-limit fault. The parser always produces a tree with no sharing.

The module `Json`:

| Function | Behavior | Needs |
| --- | --- | --- |
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | None |
| `number_of_float64(v: float64) → JsonValue` | A `Number` with the float's text. NaN and infinities trap. | N1 |
| `member(members, name: string) → Optional<JsonValue>` | The first member with that name. | T1 |
| `number_to_int64(text: string) → Optional<int64>` | `Some` when the text has no fraction or exponent and fits. | T1 |
| `number_to_float64(text: string) → float64` | The nearest value. | T1, N1 |
| `number_to_big_decimal(text: string) → BigDecimal` | Exact. | T1 |
| `parse(text: string) → Result<JsonValue>` | Below. | T1 |
| `write(value) → string`, `write_pretty(value, indent: int) → string` | Below. | T1 |
| `equals(a, b) → bool`, `hash(value) → uint32` | Structural. Numbers are equal when their exact decimal values are. Objects are equal when they have the same number of members and each member of one has an equal value under the same name in the other, regardless of order. | T1 |

- **Parsing** is strict recursive descent: no comments, no trailing commas, no leading zeros, only the four JSON whitespace characters. String escapes include `\uXXXX`, with surrogate pairs combined; an unpaired surrogate fails. Nesting deeper than 512 fails. For a repeated member name, the last value wins and the name keeps its first position. Anything but whitespace after the value fails. Errors are `Parse` errors with the byte offset.
- **Writing.** `write` produces no whitespace. `write_pretty` puts each element and member on its own line, indented by `indent` spaces per level, with `: ` after names; empty arrays and objects are `[]` and `{}`. Strings escape `"`, `\`, and characters below 0x20 (using `\b`, `\f`, `\n`, `\r`, `\t`, and otherwise `\u00XX` with lowercase hex). Nothing else is escaped. Every member of an object is written, including repeated names.

### 18.10 Math

- **fdlibm port.** `Math` is a port of fdlibm 5.3 written without a reference copy of the sources; Sun's notice is kept in the Neura port (`tests/stdlib/math/support/reference-math.nra` and `reference-math-trig.nra`, the kernels' oracle since `Math` binds the kernels), in `src/kernels/`, and in the test transliteration. The Neura functions agree bit for bit with a C transliteration of the same port on 9,148 vector cases (`tests/stdlib/math/vectors`), and that transliteration stays within 1 ulp of a 70-digit reference for the functions fdlibm documents as such, with the same larger errors as glibc for `sinh`, `cosh`, `tanh`, and `log10`. Agreement with a reference fdlibm 5.3 build has not been checked. Expressions fdlibm evaluates only to raise the inexact flag are omitted. `sqrt` is the correctly rounded integer method of [section 10.1](Stdlib-Design#101-math). `log2`, which fdlibm 5.3 lacks, follows `e_log10.c` and returns exact results for powers of two. `pow(±1, ±∞)` and `pow(1, NaN)` are NaN, as in fdlibm.
- **BigInteger** keeps its fields private (`signum`, `magnitude`); its constructor normalizes the value and adopts the list. `mod_inverse` traps for a modulus that is not positive. `floor_mod` takes the modulus's sign.
- **BigDecimal** adds the read-only fields `unscaled` and `scale`. `round(0, mode)` returns the value unchanged; a rounding carry drops one zero, as Java does. `of_float64` gives the exact value at the smallest scale. `-0.00` parses as zero. `parse` accepts an optional sign, digits with an optional point, and an optional `e` or `E` exponent; a scale outside `int` fails with `OutOfRange` at offset 0. Zero with a negative scale prints as `0`.
- **Trap details.** `negative exponent`, `negative shift count`, `negative bit index`, `modulus not positive`, `not a finite value`, `negative precision`, `magnitude too large`, and `zero sign with a nonzero magnitude` after `invalid argument: `.
- **Cost.** A `Math` call is one kernel call of one to four steps. While `Math` was Neura code, every float64 operation in it compiled to a large circuit: the `elementary` test program, which calls every function, had an image of 14,719,526 bytes, and has one of 15,480 bytes since it binds the kernels. `Math` is not in the cost baseline.
