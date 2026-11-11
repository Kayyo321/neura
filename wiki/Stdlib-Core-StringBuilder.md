# `Std.Core.StringBuilder`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.StringBuilder`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.StringBuilder`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **StringBuilder** · [`export let StringBuilder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L20)
  Accumulates mutable UTF-8 bytes and produces immutable string snapshots. Tagged `#Final`. Since 0.1.0.
- **StringBuilder.StringBuilder** · [`export let init = func(seal: Seal, owner: Cell<StringBuilder>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L27)
  Initializes a builder with the supplied backing bytes and construction token.
- **StringBuilder.StringBuilder** · [`export let append = func(s: string) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L32)
  Appends the bytes of `s` and returns this builder.
- **StringBuilder.StringBuilder** · [`export let append_scalar = func(scalar: uint32) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L42)
  Appends the UTF-8 encoding of a Unicode scalar value. Traps for a surrogate or a value above 0x10FFFF.
- **StringBuilder.StringBuilder** · [`export let append_line = func(s: string) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L68)
  Appends `s` and one LF.
- **StringBuilder.StringBuilder** · [`export let append_repeated = func(s: string, count: int) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L75)
  Appends `count` copies of `s`. A negative count traps.
- **StringBuilder.StringBuilder** · [`export let append_int = func(v: int64) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L86)
  The scalar appends use the built-in decimal and float text (Stdlib.md sections 5.2 and 7.5).
- **StringBuilder.StringBuilder** · [`export let append_uint = func(v: uint64) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L89)
  Appends the decimal text of an unsigned integer.
- **StringBuilder.StringBuilder** · [`export let append_float = func(v: float64) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L92)
  Appends the primitive text representation of a floating-point value.
- **StringBuilder.StringBuilder** · [`export let append_bool = func(v: bool) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L95)
  Appends lowercase Boolean text.
- **StringBuilder.StringBuilder** · [`export let append_value = func<T>(value: T) -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L103)
  Appends `value.to_string()`.
- **StringBuilder.StringBuilder** · [`export let byte_length: int { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L108)
  The number of accumulated UTF-8 bytes. The getter reads the byte list, so the field itself holds nothing. Since 0.2.0.
- **StringBuilder.StringBuilder** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L112)
  Reports whether there are any accumulated bytes.
- **StringBuilder.StringBuilder** · [`export let truncate = func(new_length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L116)
  Keeps the first `new_length` bytes. The length must be between 0 and the current byte length and fall on a character boundary.
- **StringBuilder.StringBuilder** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L137)
  Removes all accumulated bytes.
- **StringBuilder.StringBuilder** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L142)
  The current UTF-8 bytes determine equality and hashing.
- **StringBuilder.StringBuilder** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L151)
  Hashes the current text contents.
- **StringBuilder.StringBuilder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L154)
  The accumulated text. The builder stays usable.
- **StringBuilder** · [`export let create = func -> StringBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/string-builder.nra#L174)
  A new, empty builder. Tagged `#NoDiscard`. Since 0.1.0.
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

### 8.2 `StringBuilder`

A class in `Std.Core` that accumulates text. Needs T1. The module function `create()` makes a builder: a method cannot name its own receiver, so the builder keeps a reference to itself that the factory sets, and `new StringBuilder()` is not available outside the module. Every `append` member returns that same builder.

- **Layout.** One built-in `uint8` list, always valid UTF-8 because every append adds whole characters.

| Member | Behavior |
| --- | --- |
| `append(s: string) → StringBuilder` | Returns the builder for chaining. |
| `append_scalar(scalar: uint32)`, `append_line(s)`, `append_repeated(s, count: int)` | `append_line` appends `s` and one LF. |
| `append_int(v: int64)`, `append_uint(v: uint64)`, `append_float(v: float64)`, `append_bool(v: bool)` | Append the value's text without building an intermediate string. |
| `append_value<T>(value: T)` | Appends `value.to_string()`. **G3.** |
| `byte_length: int` | A get-only field ([accessor](Types#accessors)): the number of bytes, read as `builder.byte_length` like a string's. |
| `is_empty() → bool` | |
| `truncate(byte_length: int)`, `clear()` | `truncate` requires a character boundary. |
| `to_string() → string` | Constructs a string from the bytes. The builder stays usable. |
| `equals(other: class) → bool`, `hash_code() → uint32` | Compare and hash the current UTF-8 bytes. The hash changes with the contents, so a builder used as a hash key must stay unchanged while stored. |

### 8.3 `Scalars`

**There is no `char` type.** A code point is a `uint32` holding a Unicode scalar value. `StringBuilder.append_scalar` and `Strings.from_scalars` print a code point as its character explicitly.

`Scalars` is a module of functions on `uint32`: `is_valid` (at most 0x10FFFF and not a surrogate), `utf8_length` (1 through 4), `is_ascii`, `is_ascii_digit`, `is_ascii_hex_digit`, `is_ascii_alphabetic`, `is_ascii_alphanumeric`, `is_ascii_uppercase`, `is_ascii_lowercase`, `is_ascii_whitespace`, `is_ascii_punctuation`, `is_ascii_control`, `to_ascii_upper`, `to_ascii_lower`, `digit_value(scalar, radix: int) → int` (-1 when it is not a digit of the radix), and `from_digit(value: int, radix: int) → uint32` (lowercase; traps when out of range). Each is a range comparison; none uses a table. The constant `Replacement` is 0xFFFD.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.

### 18.3 Numbers and text building

- **Text of numbers.** Integer and float `to_string` use the NVM's scalar text, which implements [section 7.5](Stdlib-Design#75-float-text-conversion); `StringBuilder.append_int` and the other number appends append that text.
- **Integer modules.** Every module has `to_unsigned` and `to_signed` (the same-signedness one is the identity). `format` counts the sign in the width; a width at or below the text length pads nothing. `parse` reports a syntax error before a range error and accepts leading zeros. `floor_div` and `floor_mod` share one divider circuit.
- **Trap details.** `low exceeds high`, `negative exponent`, `radix outside 2 through 36`, `pad is not one scalar value`, `negative count`, `not a Unicode scalar value`, `byte length out of range`, `not a character boundary`, `zero step`, `index out of range`, `NaN bound`, and `digits outside 0 through 100`, each after `invalid argument: `.
- **Floats.** `min`, `max`, and the rounding functions return the canonical NaN for a NaN operand; `clamp` orders `-0.0` below `0.0`. `to_fixed` and `to_exponential` print `NaN` and the infinities as `to_string` does, keep the sign of a negative value that rounds to zero, and take 0 through 100 digits. `parse` accepts `e` and `E`, rejects `+NaN` and `-NaN`, and treats digits beyond 800 significant digits as a sticky bit. Fast paths (Clinger's for `float64`, a guarded `float64` path for narrower formats) are used only where they are exact; exhaustive tests check every `float16` and `bfloat16` pattern.
- **IntRange** exposes `start()`, `end()`, and `step()` as methods. Equality and hashing use those three values in that order. The text is `start..end:step`. `size()` traps with reason 1 beyond `Int32.Max`.
