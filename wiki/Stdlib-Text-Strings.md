# `Std.Text.Strings`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Text.Strings`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Text.Strings`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Strings** · [`export let byte_length = func(s: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L18)
  Constant time.
- **Strings** · [`export let scalar_count = func(s: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L21)
  The number of bytes that are not continuation bytes. Linear.
- **Strings** · [`export let is_empty = func(s: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L35)
  Tests whether the string has zero bytes.
- **Strings** · [`export let byte_at = func(s: string, offset: int) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L38)
  The byte at the offset; the built-in read traps outside the string.
- **Strings** · [`export let scalar_at = func(s: string, offset: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L44)
  The scalar value of the character starting at the offset. An offset outside the string is a built-in bounds trap; a continuation byte traps with "invalid argument: offset is not a character boundary".
- **Strings** · [`export let is_boundary = func(s: string, offset: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L53)
  True for 0, for the byte length, and for every offset whose byte is not a continuation byte; false outside 0 through the byte length.
- **Strings** · [`export let scalars = func(s: string) -> uint32[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L61)
  The scalar values in order.
- **Strings** · [`export let scalar_offsets = func(s: string) -> [int, uint32][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L74)
  Each character's starting offset with its scalar value.
- **Strings** · [`export let slice = func(s: string, start: int, end: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L87)
  The built-in slice: both ends on boundaries and start not above end.
- **Strings** · [`export let slice_from = func(s: string, start: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L90)
  Returns the suffix beginning at a UTF-8 byte boundary.
- **Strings** · [`export let equals = func(a: string, b: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L93)
  Same bytes (Stdlib.md section 5.2).
- **Strings** · [`export let hash = func(s: string) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L96)
  Start with 0; for each byte, 31 times the hash plus the byte, wrapping.
- **Strings** · [`export let compare = func(a: string, b: string) -> ordering.Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L100)
  Byte-wise lexicographic order; a proper prefix sorts first. This is also the order of the scalar values.
- **Strings** · [`export let starts_with = func(s: string, prefix: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L129)
  Tests whether the string starts with the literal prefix.
- **Strings** · [`export let ends_with = func(s: string, suffix: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L132)
  Tests whether the string ends with the literal suffix.
- **Strings** · [`export let contains = func(s: string, needle: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L141)
  Tests whether the literal needle occurs anywhere in the string.
- **Strings** · [`export let index_of = func(s: string, needle: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L144)
  The offset of the first occurrence, or -1. An empty needle matches at 0.
- **Strings** · [`export let index_of_from = func(s: string, needle: string, from: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L148)
  The offset of the first occurrence at or after `from`, or -1. An empty needle matches at `from`. `from` must be a boundary from 0 through the byte length.
- **Strings** · [`export let last_index_of = func(s: string, needle: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L154)
  The offset of the last occurrence, or -1. An empty needle matches at the byte length.
- **Strings** · [`export let split = func(s: string, separator: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L174)
  Every piece between non-overlapping occurrences of the separator, left to right. Empty pieces are kept; the empty string gives one piece. An empty separator traps with "invalid argument: empty separator".
- **Strings** · [`export let split_limit = func(s: string, separator: string, limit: int) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L179)
  As split, with at most `limit` pieces; the last piece holds the rest. A limit below 1 traps with "invalid argument: limit below 1".
- **Strings** · [`export let lines = func(s: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L188)
  Splits at LF. A CR immediately before an LF is removed, and the empty piece after a final LF is dropped. The empty string gives one empty line, as split does.
- **Strings** · [`export let join = func(parts: string[list], separator: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L211)
  The parts with the separator between each pair.
- **Strings** · [`export let repeat = func(s: string, count: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L234)
  `count` copies of the string. A negative count traps with "invalid argument: negative count".
- **Strings** · [`export let trim = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L254)
  Removes space, tab, LF, vertical tab, form feed, and CR from both ends.
- **Strings** · [`export let trim_start = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L264)
  Removes ASCII space and bytes 9 through 13 from the start.
- **Strings** · [`export let trim_end = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L274)
  Removes ASCII space and bytes 9 through 13 from the end.
- **Strings** · [`export let strip_prefix = func(s: string, prefix: string) -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L277)
  The rest of the string when it starts with the prefix.
- **Strings** · [`export let strip_suffix = func(s: string, suffix: string) -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L285)
  The start of the string when it ends with the suffix.
- **Strings** · [`export let replace = func(s: string, target: string, replacement: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L295)
  Replaces every non-overlapping occurrence, left to right. An empty target traps with "invalid argument: empty target".
- **Strings** · [`export let replace_first = func(s: string, target: string, replacement: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L299)
  Replaces the first occurrence. An empty target traps as in replace.
- **Strings** · [`export let to_ascii_upper = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L303)
  a through z become A through Z; every other byte is unchanged.
- **Strings** · [`export let to_ascii_lower = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L306)
  A through Z become a through z; every other byte is unchanged.
- **Strings** · [`export let equals_ignore_ascii_case = func(a: string, b: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L309)
  Equal after mapping A through Z to a through z in both strings.
- **Strings** · [`export let pad_start = func(s: string, width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L331)
  Copies of `pad` before the string until it has `width` scalar values. `pad` must be exactly one scalar value; otherwise the call traps with "invalid argument: pad is not one scalar value".
- **Strings** · [`export let pad_end = func(s: string, width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L343)
  Copies of `pad` after the string, with the rules of pad_start.
- **Strings** · [`export let reverse = func(s: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L355)
  The scalar values in reverse order.
- **Strings** · [`export let to_bytes = func(s: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L371)
  A copy of the UTF-8 bytes.
- **Strings** · [`export let from_bytes = func(bytes: uint8[list]) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L381)
  The text the bytes encode, or an Encoding error whose offset is the start of the first ill-formed sequence (the length of the longest valid prefix). Its message is "invalid start byte", "invalid continuation byte", or "unexpected end of data".
- **Strings** · [`export let from_bytes_lossy = func(bytes: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L403)
  The text the bytes encode, with each maximal ill-formed subsequence (Unicode's "maximal subpart" practice) replaced by one U+FFFD.
- **Strings** · [`export let from_scalars = func(values: uint32[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/text/strings.nra#L431)
  The text of the scalar values. A surrogate or a value above 0x10FFFF traps with "invalid argument: not a Unicode scalar value".
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.2 Equality, hashing, and text

The compiler supplies these results for supported value members. Companion functions such as `Int32.hash` and `Strings.equals` use the same contracts. For types with custom equality, equal values must have the same hash.

| Type | Equal when | Hash | Text |
| --- | --- | --- | --- |
| `bool` | Same value | 1231 for true, 1237 for false | `true` or `false` |
| Integers up to 32 bits | Same value | The value's 32-bit two's complement pattern as a `uint32` | Decimal, `-` for negatives, no leading zeros |
| `int64`, `uint64` | Same value | Low 32 bits exclusive-or high 32 bits | Decimal, as above |
| Floats | Same bit pattern, except that every NaN equals every NaN. `0.0` and `-0.0` are **not** equal. | The bit pattern with every NaN replaced by the canonical NaN; 16-bit patterns zero-extended, the 64-bit pattern folded like `uint64` | [Section 7.5](Stdlib-Design#75-float-text-conversion) |
| Fieldless enum | Same variant | The ordinal | The variant name |
| `string` | Same bytes | Start with 0; for each byte, 31 times the hash plus the byte, wrapping | The string itself |
| Tuple | Same element types and each pair of elements equal, in order | Start with 1; for each element, 31 times the hash plus the element's hash, wrapping | `[`, the elements' text separated by `, `, `]` |
| Array, list | Same kind, same length, and each pair of elements equal in index order | As for a tuple | As for a tuple |
| Class (default, G3b) | Identity | The identity number times 2,654,435,761, wrapping | The qualified class name, `#`, and the identity number, for example `Acme.Shapes.Circle#3` |
| Function value | Not comparable | None | None |
| Payload enum | Not provided | Not provided | Not provided |

Float equality here deliberately differs from `==`: it is an equivalence relation consistent with the hash, which hash tables need, while `==` stays IEEE 754. The multiplier 31 is chosen because multiplying by 31 is a shift and a subtraction.

The module **`Hash`** has the building blocks, all ordinary functions that work today:

| Member | Behavior |
| --- | --- |
| `combine(seed: uint32, value: uint32) → uint32` | 31 times `seed` plus `value`, wrapping. Computed in `uint64`: the product is below 2^37, so nothing overflows before the mask. |
| `spread(hash: uint32) → uint32` | `hash` exclusive-or `hash` shifted right by 16. Hash tables apply this before masking. |
| `of_bool`, `of_int64`, `of_uint64` | The table's results for those types. |

A class's hash starts from 17 and combines each field's hash in declaration order.

**The contract for a class that overrides the G3b members:** `equals` is an equivalence relation; equal values have equal hashes; neither traps nor modifies the value; and while an object is a key in a hash collection its hash and equality do not change. The library does not detect violations.

**Self-containing collections.** A collection's text prints `(this collection)` for itself as a direct element. Deeper cycles end with the frame-limit fault.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`. All of this works today.
- **A comparator is a native function value** of type `(T, T) → Ordering`. There is no comparator interface or wrapper class. A named function, a bound method, or a closure all serve. A comparator must define a total order; if it does not, the library stays memory safe and terminates, and the resulting order or lookup is unspecified.
- **Natural orders** are named functions in the companion modules, usable as comparators directly: `Int32.compare`, `Float64.total_compare`, `Strings.compare`, and so on.

| Type | Natural order |
| --- | --- |
| Integers | Numeric value |
| `bool` | `false` before `true` |
| Floats | Negative infinity, negative finite values, `-0.0`, `0.0`, positive finite values, positive infinity, then NaN; all NaNs equal |
| `string` | Byte-wise lexicographic; a proper prefix sorts first. This equals ordering by Unicode scalar value. |
| Fieldless enum | Ordinal, obtained by the caller with `as` |

- The module **`Comparators`** uses generic function values (G1) for `reversed<T>(c)`, `by_key<T, K>(key: (T) → K, c: (K, K) → Ordering)`, and `then<T>(first, second)`.
- **`Comparable<T>`**, an interface with `compare_to(other: T) → Ordering`, arrives with G4. Nothing in the library requires it: sorted structures always take a comparator.

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

### 8.1 `Strings`

All of `Strings` needs T1. String operations are functions that take the string first. **Decision:** all positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

| Member | Behavior |
| --- | --- |
| `byte_length(s) → int` | Constant time. |
| `scalar_count(s) → int` | Counts bytes that are not continuation bytes. Linear. |
| `is_empty(s) → bool` | |
| `byte_at(s, offset) → uint8` | |
| `scalar_at(s, offset) → uint32` | Decodes the character starting at `offset`. |
| `is_boundary(s, offset) → bool` | True for 0, for the byte length, and for any offset whose byte is not a continuation byte. False out of range. |
| `scalars(s)` | A built-in list of `uint32`: the scalar values in order. For-each works on it directly. |
| `scalar_offsets(s)` | A built-in list of `[int, uint32]`: each starting offset with its scalar value. |
| `slice(s, start, end) → string`, `slice_from(s, start) → string` | Both ends on boundaries, `start` not above `end`. |
| `equals(a, b) → bool`, `hash(s) → uint32`, `compare(a, b) → Ordering` | [Sections 5.2](Stdlib-Design#52-equality-hashing-and-text) and [5.3](Stdlib-Design#53-ordering). |
| `starts_with(s, prefix)`, `ends_with(s, suffix)`, `contains(s, needle)` `→ bool` | The empty string is a prefix, suffix, and substring of every string. |
| `index_of(s, needle) → int`, `index_of_from(s, needle, from) → int` | Offset of the first occurrence at or after `from`, or -1. An empty needle matches at `from`. |
| `last_index_of(s, needle) → int` | Offset of the last occurrence, or -1. |
| `split(s, separator)` | A built-in list of strings: every piece between occurrences, left to right without overlap. Empty pieces are kept, so `a,,b,` gives four pieces and the empty string gives one. An empty separator traps. |
| `split_limit(s, separator, limit)` | At most `limit` pieces; the last holds the remainder. `limit` below 1 traps. |
| `lines(s)` | Splits at LF; a CR immediately before an LF is removed; a final empty piece after a trailing LF is dropped. |
| `join(parts: string[list], separator) → string` | |
| `repeat(s, count) → string` | A negative count traps. |
| `trim(s)`, `trim_start(s)`, `trim_end(s)` | Remove space, tab, LF, vertical tab, form feed, and CR. |
| `strip_prefix(s, prefix)`, `strip_suffix(s, suffix)` `→ Optional<string>` | The remainder when the affix is present. |
| `replace(s, target, replacement)`, `replace_first(...)` | Every non-overlapping occurrence, left to right. An empty target traps. |
| `to_ascii_upper(s)`, `to_ascii_lower(s)`, `equals_ignore_ascii_case(a, b)` | Change or ignore only the letters A through Z and a through z. |
| `pad_start(s, width, pad: string)`, `pad_end(...)` | Pads to `width` scalar values with copies of `pad`, which must be one scalar value. |
| `reverse(s) → string` | Reverses the order of scalar values. |
| `to_bytes(s)` | A built-in list of `uint8`, a copy. |
| `from_bytes(bytes: uint8[list]) → Result<string>` | `Encoding` error with the offset of the first invalid byte. |
| `from_bytes_lossy(bytes: uint8[list]) → string` | Each maximal invalid sequence becomes one U+FFFD. |
| `from_scalars(scalars: uint32[list]) → string` | Traps for a value that is not a Unicode scalar value. |

Functions that return several strings return a built-in list, not an `ArrayList`: the built-in for-each works on it, and `Std.Text` then needs nothing from `Std.Collections`.

**Search algorithm.** `index_of` scans for the needle's first byte and then compares the remaining bytes in place. Its worst case is the product of the two lengths. The simple scan has the smallest circuit and a predictable step count.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator is Neura code in `Std.Text` and must agree with the validation NVM performs when constructing text.

**Unicode scope. Decision:** `Std.Text.Strings` handles UTF-8 encoding, decoding, and iteration for all Unicode scalar values. Its original ASCII case and classification methods keep their scope. The [`String` companion](https://github.com/Kayyo321/neura/wiki/StandardLibrary#string) adds a fixed whitespace set for strip and indentation, Unicode 17.0.0 case conversion and folding, and standard charset conversion from `Std.Text.Charsets`. These are ordinary Neura routines with embedded Unicode data and use no host locale or charset services.

### 8.3 `Scalars`

**Decision: there is no `char` type.** A code point is a `uint32` holding a Unicode scalar value. The first plan made `char` a language blocker so that a character would print as itself; `StringBuilder.append_scalar` and `Strings.from_scalars` do that explicitly, and the library needs no new primitive type or literal form.

`Scalars` is a module of functions on `uint32` that work today: `is_valid` (at most 0x10FFFF and not a surrogate), `utf8_length` (1 through 4), `is_ascii`, `is_ascii_digit`, `is_ascii_hex_digit`, `is_ascii_alphabetic`, `is_ascii_alphanumeric`, `is_ascii_uppercase`, `is_ascii_lowercase`, `is_ascii_whitespace`, `is_ascii_punctuation`, `is_ascii_control`, `to_ascii_upper`, `to_ascii_lower`, `digit_value(scalar, radix: int) → int` (-1 when it is not a digit of the radix), and `from_digit(value: int, radix: int) → uint32` (lowercase; traps when out of range). Each is a range comparison; none uses a table. The constant `Replacement` is 0xFFFD.

### 8.4 `Regex`

A class holding a compiled regular expression. Needs T1 only: its types are all concrete. Matching runs in time proportional to the pattern size times the input length and never backtracks.

**Syntax.** Literals; `.`; character classes with ranges and negation; `\d`, `\w`, `\s` and their negations (ASCII definitions); `\t`, `\n`, `\r`, `\\`, escaped metacharacters, and `\u{...}`; anchors `^` and `$`; `\b` and `\B`; grouping, non-capturing groups, and named groups; alternation; the quantifiers `*`, `+`, `?`, `{n}`, `{n,}`, `{n,m}`, each with a lazy form. **Not supported:** backreferences, lookahead, lookbehind, possessive quantifiers, and Unicode property classes; a pattern that uses them fails to compile.

**Compilation.** The module functions `compile(pattern) → Result<Regex>` and `compile_with(pattern, flags: RegexFlags) → Result<Regex>`. `RegexFlags` is a class with three `bool` fields: `ignore_ascii_case`, `multi_line`, `dot_all`. A recursive-descent parser builds a syntax tree, which is a concrete recursive payload enum (literal, class, concatenation, alternation, repetition, group, assertion). A compiler turns it into a program whose instructions are another payload enum: scalar, any, class, split (two targets in priority order), jump, save, assert, and match. Limits: a repetition count of at most 1,000, at most 10,000 instructions, and group nesting of at most 250. Exceeding a limit, or a syntax error, returns a `Parse` error with the pattern offset.

**Matching.** A Pike machine: it advances a list of threads over the input one scalar value at a time, in priority order, with at most one thread per instruction per position. Each thread carries its capture positions. The result is the leftmost match, and among matches at that start the one a backtracking matcher would find first. It reports byte offsets.

| Member | Behavior |
| --- | --- |
| `is_match(input) → bool`, `matches_fully(input) → bool` | |
| `find(input) → RegexMatch`, `find_from(input, offset) → RegexMatch` | The first match at or after the offset, or `nil` when none exists. A successful match is always a real object, including an empty match. |
| `find_all(input)` | A built-in list of `RegexMatch`: successive non-overlapping matches. After an empty match the search resumes one scalar value later. |
| `replace_all(input, replacement) → string`, `replace_first(...)` | `$0` through `$99` and `${name}` insert groups; `$$` is a dollar sign. A reference to a group that does not exist traps; a group that did not participate inserts nothing. |
| `split(input)` | A built-in list of strings, with the rules of `Strings.split`. |
| `group_count() → int`, `pattern() → string` | |

`RegexMatch` is an immutable class: `start()`, `end()`, `text()`, `group(index) → Optional<string>`, `group_start(index) → int` (-1 when the group did not participate), and `named_group(name) → Optional<string>`. Group 0 is the whole match.

### 9.6 Hash collections

**Decisions kept from the first plan:** hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including the ones that can be written today.

| Field | Type | Meaning |
| --- | --- | --- |
| `index` | `int` list of length `T` | Open-addressed table. Each slot holds an entry number, -1 (`Empty`), or -2 (`Tombstone`). `T` is a power of two, at least 8. |
| `hashes` | `uint32` list | Each entry's spread hash, in insertion order. Its length is `used`. |
| `live` | `bool` list | False for a removed entry. Same length. |
| `count` | `int` | Live entries. |
| `modifications` | `uint64` | The modification count ([section 5.4](Stdlib-Design#54-iteration)). |

`E`, the entry capacity, is `T / 2`. At most half of the index is ever non-empty, because each entry occupies one slot. One half is chosen because lookups use linear probing: at half load an unsuccessful search inspects about 2.5 slots on average and a successful one about 1.5, and every inspected entry costs a comparison circuit. The index holds only integers, so a sparse table is cheap.

The core's operations, as the owner uses them:

- **Lookup.** The owner computes the spread hash, asks for the home slot (the hash masked with `T - 1`), and walks slots in increasing order, wrapping. `Empty` ends the search: absent. `Tombstone` continues. An entry number whose cached hash equals the probe's hash is a candidate: the owner compares its stored key with the probe key, and stops on a match. The probe key is the first argument to the equality. The loop terminates because at least half the slots are `Empty`.
- **Claim.** For a new key the owner remembers the first `Tombstone` it passed. The core appends the hash and `true` to its lists, writes the new entry number into the remembered tombstone or the final `Empty` slot, and advances `count` and the modification count. The owner appends the key and value to its lists in the same step, so entry numbers agree.
- **Release.** The slot becomes `Tombstone`, the entry's `live` flag becomes false, `count` decreases, and the modification count advances. If the released entry is the last one, it and any dead entries before it are removed from the ends of all the dense lists at once, so stack-like use retains nothing.
- **Compaction.** The core returns the old entry numbers of the live entries in order. The owner builds new key and value lists in that order; the core rebuilds `hashes`, `live`, and `index` by probing from each cached hash for the first `Empty` slot. No hash or equality function is called. It runs in a helper function that returns the new lists ([section 5.5](Stdlib-Design#55-resources)).

When compaction happens:

1. A new key arrives and `used` equals `E`, or the live entries plus the tombstones in the index fill `E` (trailing releases leave tombstones behind). If `count` is greater than `E / 2`, `T` doubles first. A `T` above 2^30 traps with `invalid argument: capacity`.
2. A release leaves more dead entries than live ones and at least 8 dead entries. `T` is unchanged.

**Retention bound.** A removed key and value stay in the owner's dense lists, and so stay reachable, until the next compaction. Rule 2 bounds this: a hash collection never holds more dead entries than `max(count, 7)`. Compaction costs time proportional to `count` and happens after more than `count` releases, so removal stays amortized constant time. Nothing observable depends on the retained references, since reclamation runs no code; only heap use does.

`clear` replaces every list with a new one of the initial size. The table never shrinks on its own; `trim()` compacts at the smallest `T` whose `E` holds `count`.

**Costs.** Lookup, insertion, and removal are expected constant time with well-distributed hashes and linear in `used` in the worst case. There is no conversion of long probe sequences into trees. Code that hashes adversarial keys uses `TreeMap`.

#### The collections

| Class | Keys | Key operations | Needs |
| --- | --- | --- | --- |
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing: it can be written today |
| `StringMap<V>` | `string` | `Strings.hash` and `Strings.equals` | T1 |
| `HashMap<K, V>`, `HashSet<T>` created with `with_keys<K, V>(hash: (K) → uint32, same: (K, K) → bool)` | Any | The two function values, stored in the collection | G1 |
| `HashMap<K, V>`, `HashSet<T>` created with `create<K, V>()` | Any | `hash_code` and `equals` | G3 |

- `IntMap` and `StringMap` are permanent, not stopgaps. They follow the map contract with their key type fixed, call their key operations directly without a function value, and are the right choice for those key types after `HashMap` exists.
- `HashMap` stores its two key operations as fields either way. `create` fills them with the universal members, so the rest of the class is written once.
- `HashSet` owns a `HashCore` and a keys list. `HashMap` owns a `HashCore`, a keys list, and a values list.
- `with_capacity` variants of each constructor choose the smallest power-of-two `T` that is at least `max(8, 2n)`; a negative `n` or one above 2^29 traps.
- Setting a value for an existing key overwrites the value only: the entry keeps its position and the modification count does not change. A key removed and added again goes to the end of the order.
- Iteration visits entry numbers in order and skips dead entries.
- A callback passed to `get_or_put`, `merge`, or `compute` must not modify the map ([section 5.6](Stdlib-Design#56-callbacks)).

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone and can be written today.
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero.
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**Recommendation: `JsonValue` is a recursive payload enum.** The first plan had an interface, six implementing classes, a kind enum, and five accessors returning `Optional`. All of that is one declaration and `match`, and it compiles today: the probe declared this shape and walked it with mutually recursive functions in both build modes.

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
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | Today |
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

### 18.4 Text

- **Offsets.** Library offset checks trap with `invalid argument: offset out of range` or `invalid argument: offset is not a character boundary`; `byte_at`, `slice`, and `slice_from` use the language's own traps.
- **Strings.** `lines("")` is `[""]`. `last_index_of(s, "")` is the byte length. `pad_start` and `pad_end` return the text unchanged for a width at or below its scalar count. `from_bytes` reports the offset of the first ill-formed sequence with the message `invalid start byte`, `invalid continuation byte`, or `unexpected end of data`.
- **Scalars.** `utf8_length` traps for a value that is not a scalar value. `is_ascii_whitespace` includes vertical tab, matching `trim` and `\s`.
- **Regex syntax.** `(?<name>…)` and `(?P<name>…)` both name a group. Any ASCII punctuation may be escaped; other escapes are errors. A literal `{` must be escaped. A quantified assertion is `nothing to repeat`, a second quantifier `repeated quantifier`, and `+` after a quantifier `possessive quantifiers are not supported`.
- **Regex matching.** `$` without `multi_line` matches only at the end, and `.` excludes only LF. An iteration beyond a quantifier's minimum that matches empty fails, and a capture inside a repeated group keeps its last value. The Pike machine keeps one thread per instruction per position, so a loop whose body can match empty can merge iterations and differ from every backtracking order; for example `(?:a*?)*` on `aa` matches `[0, 1)`. Replacements validate before matching, accept `${number}`, and trap on a malformed `$`. `group`, `group_start`, and `named_group` trap for an unknown index or name.

### 18.7 Encodings and digests

- **Additions.** `Crc32.create()` and `Sha256.create()` make fresh digests; `Base64.decode_unpadded` and `decode_url_unpadded` read the unpadded forms, which reject padding.
- **Utf8** delegates to `Strings`, so its errors match `Strings.from_bytes`; `validate` returns the length of the longest valid prefix, the offset `decode` reports.
- **Hex errors.** `invalid hex digit` at the byte; `odd number of hex digits` at the text length, reported only when every digit is valid.
- **Base64 errors.** `invalid character`, `invalid padding`, and `unexpected padding` at the byte; `unexpected end of data` at the text length; `nonzero trailing bits` at the last character. The first structural error wins.
- **Csv.** A CR that does not start a CRLF is data. An empty line is a record with one empty field, and a record with no fields is written as an empty line. Errors: `quote in unquoted field` and `text after closing quote` (Syntax), `unterminated quoted field` (UnexpectedEnd).
- **Json parse errors.** `Empty` for empty or all-whitespace text; `UnexpectedEnd` at the text length; `Syntax` for structure; `InvalidCharacter` inside a token, with escapes reported at the backslash; `OutOfRange` at the bracket that opens level 513. Top-level scalars are accepted.
- **Json values.** Numbers compare by a canonical decimal text, including exponents beyond `int64`. `hash` starts each variant from its position (Null 1 through Object 6), and an object's hash combines its member count with the wrapping sum of its distinct member hashes, so it agrees with order-insensitive equality even when names repeat. `number_to_int64` is `None` for anything but a plain integer. `index` keeps the last value of a repeated name. A `StringMap<JsonValue>` supports lookups; its `equals` and `to_string` are rejected at build time because `JsonValue` has no built-in value members.
- **Json traps.** `number is not finite`, `negative indent`, `number text`, and `number exponent out of range`, after `invalid argument: `.
- **Sha256** keeps rolled loops: unrolling cut steps by about four but grew the image by 1.5 to 3.6 MB.

### 18.9 Hash collections, trees, and sequences

- **Compaction rule 1** also compacts when live entries plus tombstones fill `E`. Trailing releases leave tombstones behind, so without this addition the index could fill and a lookup for a missing key would not terminate.
- **Modification counts are `uint64`**, as [section 5.4](Stdlib-Design#54-iteration) states; compaction and `trim()` advance them, so an iterator traps after either. `clear()` returns to the table size chosen at construction.
- **Bulk removal.** `remove_if` and `retain_if` call the callback on every entry, then release the chosen entries and compact at most once. `compute` returns the result of its callback. A `with_keys` collection hashes with its stored function, so `hash_code` agrees with its equality.
- **IntMap and StringMap** are generated from `hash-map.nra` with the key operations called directly (`tests/stdlib/hash-map/generators/gen_maps.py`); `StringMap` uses the string's built-in `hash_code` and `==`, which equal `Strings.hash` and `Strings.equals`, because `Std.Text` is above `Std.Collections` in the dependency order.
- **Extra members.** `with_keys_and_capacity`; `trim` and `validate` on every hash collection; `TreeMap.key_iterator`, `key_range`, and `descending_keys`, which `TreeSet` uses to avoid a tuple per element; `Sequence.from_iterator`, a public `Sequence` constructor from two functions, and `Sequence.iterator()` so that a sequence works once in for-each. Constructors of these collections take a module-private token, so only their module functions build them.
- **Sequence choices.** `range(from, to)` on a sorted collection is empty when `to` is not above `from`. `windowed` emits only full windows. `zip` and `chain` mark the other sequence used. `from_list` fails fast on a structural change to its list. `sum_float64` starts from the first element, so `[-0.0]` sums to `-0.0`, and an empty sequence sums to `0.0`. Maps and sets have no `sequence()` member; `ArrayList` and `LinkedList` build theirs with `from_iterator`.
- **Trap details.** `duplicate key`, `negative count`, `chunk size`, and `window size` after `invalid argument: `; `invalid state: hash table …` and `invalid state: tree …` for a failed `validate()`.
