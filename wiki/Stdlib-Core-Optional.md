# `Std.Core.Optional`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Optional`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Optional`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Optional** · [`export let Optional = enum<T> (None, Some(T))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L10)
  Represents an absent value (None) or a present payload (Some).
- **Optional** · [`export let is_some = func<T>(o: Optional<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L13)
  Reports whether the optional contains a payload.
- **Optional** · [`export let is_none = func<T>(o: Optional<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L19)
  Reports whether the optional is None.
- **Optional** · [`export let value_or = func<T>(o: Optional<T>, fallback: T) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L25)
  Returns the payload or the already-evaluated fallback.
- **Optional** · [`export let expect = func<T>(o: Optional<T>, detail: string) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L31)
  The value, or a trap with "no such element: " and the detail.
- **Optional** · [`export let to_result = func<T>(o: Optional<T>, failure: Error) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L43)
  Wraps a present payload in Ok or uses the supplied error for None.
- **Optional** · [`export let map = func<T, U>(o: Optional<T>, f: func(T) -> U) -> Optional<U>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L49)
  Applies the function only to a present payload and wraps its output in Some.
- **Optional** · [`export let and_then = func<T, U>(o: Optional<T>, f: func(T) -> Optional<U>) -> Optional<U>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L55)
  Applies the function only to a present payload and returns its optional result.
- **Optional** · [`export let filter = func<T>(o: Optional<T>, keep: func(T) -> bool) -> Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L61)
  Keeps the present payload only when it passes the predicate.
- **Optional** · [`export let value_or_else = func<T>(o: Optional<T>, make: func() -> T) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L76)
  Calls `make` only for None.
- **Optional** · [`export let or_else = func<T>(o: Optional<T>, other: func() -> Optional<T>) -> Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L82)
  Calls the fallback only for None and returns its optional result.
- **Optional** · [`export let equals = func<T>(a: Optional<T>, b: Optional<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L88)
  Both None, or both Some with equal values.
- **Optional** · [`export let hash = func<T>(o: Optional<T>) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L103)
  0 for None, otherwise Hash.combine(1, the value's hash).
- **Optional** · [`export let to_string = func<T>(o: Optional<T>) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/optional.nra#L109)
  Formats None or the text of a present payload wrapped in Some.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.4 Iteration

The loop form depends on the source value.

| Way | Available | Notes |
| --- | --- | --- |
| For-each over the built-in list from `ArrayList.items()`, or from any collection's `to_list()` | Today | The built-in loop compares the list's structural version before each advance and traps with reason 5 on a change. `items()` is live; `to_list()` is a copy. |
| An index loop over `size()` and `get(index)` | Today | For lists and deques. No modification check. |
| The collection's iterator object: `iterator()` returns a class with `has_next() → bool` and `next() → T` | Today, as one concrete iterator class per collection | `has_next` has no effect and can be repeated. `next` returns the next element and advances, and traps with `no such element` at the end. An iterator is used once. |
| For-each directly over the collection | G4 and G5 | Lowers to the iterator protocol when the source implements `Iterable<T>`. |

- **Decision: iteration does not allocate per element.** `has_next` and `next` stay two calls. A single `next` returning `Optional<T>` would read well with `match`, but it would allocate an enum value for every element.
- **Map iteration yields tuples.** A map's iterator returns `[K, V]`. The tuple is an immutable snapshot of the entry, which is the contract the first plan's `Entry` class existed to state.
- **Decision: no removal through iterators.** Collections remove in bulk with `remove_if` and `retain_if`.
- **Decision: fail-fast iterator objects.** A wrapper over a single built-in list uses that list's `uint64` structural version. Other mutable collections use a `uint64` modification count and increment it after each structural change. An iterator records its version when created. `has_next` and `next` compare first and trap on a difference. Replacing an element by index or replacing the value for an existing key is not structural.
- The iteration order of every collection is specified in section 9.
- `Iterator<T>` and `Iterable<T>` are the interfaces for this protocol. Concrete iterator classes and collections implement these member signatures.

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

### 6.1 `Optional<T>`

**Recommendation: a generic payload enum with the variants `None` and `Some(T)`, read with `match`.** The first plan's class (a `present` flag and a late field) is dropped. The enum cannot be read without handling both cases, has no trapping accessor to misuse, and needs no late field.

Requires G2. The module `Optional` exports the enum and these functions; the last column says what each needs beyond G2.

| Function | Behavior | Also needs |
| --- | --- | --- |
| `is_some<T>(o) → bool`, `is_none<T>(o) → bool` | | |
| `value_or<T>(o, fallback: T) → T` | The value, or `fallback`. | |
| `expect<T>(o, detail: string) → T` | The value, or a trap with `no such element` followed by `: ` and `detail`. | |
| `to_result<T>(o, error: Error) → Result<T>` | `Ok` of the value, or `Err` of `error`. | |
| `map<T, U>(o, f: (T) → U) → Optional<U>` | | G1 |
| `and_then<T, U>(o, f: (T) → Optional<U>) → Optional<U>` | | G1 |
| `filter<T>(o, keep: (T) → bool) → Optional<T>` | | G1 |
| `value_or_else<T>(o, make: () → T) → T` | Calls `make` only for `None`. | G1 |
| `or_else<T>(o, other: () → Optional<T>) → Optional<T>` | `o` when it is `Some`, otherwise the result of `other`. | G1 |
| `equals<T>(a, b) → bool`, `hash<T>(o) → uint32`, `to_string<T>(o) → string` | Both `None`, or both `Some` with equal values. Hash 0 for `None`, otherwise `Hash.combine(1, value hash)`. Text `None` or `Some(` value `)`. | G3a |

The mapping functions are conveniences. A `match` is useful when the cases require different control flow or the caller wants to avoid a callback.

### 6.2 `Result<T>`

**Recommendation: a generic payload enum with the variants `Ok(T)` and `Err(Error)`.** It is the one representation of a fallible outcome. An operation with no value returns `Result<Unit>`. The error type is fixed to `Error` so that every fallible function composes with every other.

Requires G2. The module `Result` exports the enum and:

| Function | Behavior | Also needs |
| --- | --- | --- |
| `is_ok<T>(r) → bool`, `is_err<T>(r) → bool` | | |
| `value_or<T>(r, fallback: T) → T` | | |
| `expect<T>(r, detail: string) → T` | The value, or a trap whose message is `detail`, `: `, and the error's message. For failures the caller has decided are impossible. | |
| `to_optional<T>(r) → Optional<T>` | Discards the error. | |
| `with_context<T>(r, message: string) → Result<T>` | An `Err` becomes an `Err` of a `Context` error wrapping the original; an `Ok` is returned unchanged. | |
| `map<T, U>(r, f: (T) → U) → Result<U>` | | G1 |
| `and_then<T, U>(r, f: (T) → Result<U>) → Result<U>` | | G1 |
| `map_error<T>(r, f: (Error) → Error) → Result<T>` | | G1 |

There is no accessor for the error alone: `match` reads it.

### 6.4 Concrete aliases and absence

Define `Optional<T>` and `Result<T>` directly as generic payload enums. A concrete public name such as `File.Result` can be a type overlay of `Result<File>`. Reuse the same `Ok` and `Err` variants and payload types through the overlay.

Use `nil` for an internal absent reference when it cannot be confused with a real value, such as a missing parent link or an unused iterator reference. Do not replace `Optional<T>` with a nil convention on a generic return, or on an API where a present nil reference must be distinguishable from absence. JSON `Null` remains its own payload enum variant.

### 7.1 Integer modules

`X` denotes the module's integer type. Parsing uses T1; the other operations use checked integer arithmetic and the modular recipes below.

| Member | Behavior |
| --- | --- |
| Constants `Min`, `Max`, `Bits` | The type's range and width. |
| `abs(v) → X` | Signed types only. Traps (reason 1) for `Min`, from the negation. |
| `sign(v) → int` | -1, 0, or 1. |
| `min(a, b)`, `max(a, b)`, `clamp(v, low, high)` | `clamp` traps with `invalid argument: low exceeds high`. |
| `compare(a, b) → Ordering` | The natural order; a comparator. |
| `pow(base, exponent: int) → X` | Square-and-multiply with checked multiplication. A negative exponent traps. `pow(0, 0)` is 1. |
| `gcd(a, b) → X` | Euclid's algorithm on magnitudes. `gcd(0, 0)` is 0. Traps (reason 1) when the result does not fit. |
| `checked_add`, `checked_sub`, `checked_mul`, `checked_div`, `checked_rem` `→ Optional<X>` | `None` where the operator would trap. Each tests its operands before operating: for addition, `b > Max - a` when both are positive, and the mirror image when both are negative. |
| `wrapping_add`, `wrapping_sub`, `wrapping_mul`, `wrapping_neg` | The result modulo 2^`Bits`, written with checked operators ([section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics)). |
| `saturating_add`, `saturating_sub`, `saturating_mul` | Clamp to `Min` or `Max`. |
| `floor_div(a, b)`, `floor_mod(a, b)` | Quotient rounded toward negative infinity, and the remainder with the divisor's sign. |
| `count_ones`, `leading_zeros`, `trailing_zeros` `→ int` | Bit counts over `Bits` bits, by masks and shifts. |
| `rotate_left(v, n: int)`, `rotate_right(v, n: int)` | `n` is first reduced to 0 through `Bits - 1`. A rotation by 0 returns `v`; otherwise the two shifts have counts between 1 and `Bits - 1`, so neither traps. |
| `to_unsigned(v)`, `to_signed(v)` | The same-width type with the same bit pattern ([section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics)). |
| `hash(v) → uint32` | [Section 5.2](Stdlib-Design#52-equality-hashing-and-text). |
| `to_string(v) → string` | Decimal. Built by concatenating one-character literals, most significant digit first; negative values are handled in the negative range so `Min` needs no negation. |
| `to_string_radix(v, radix: int) → string` | Lowercase digits. |
| `format(v, radix: int, min_width: int, pad: string) → string` | As `to_string_radix`, then left-padded with copies of `pad` to `min_width` digits and sign. When `pad` is `0`, padding goes after the sign. |
| `parse(text) → Result<X>`, `parse_radix(text, radix: int) → Result<X>` | **T1.** Grammar below. |

**Parsing grammar.** An optional `+` or `-`, then one or more digits of the radix, letters in either case. No whitespace, separators, or radix prefix. An empty input fails with kind `Empty`; a sign with no digits with `UnexpectedEnd`; any other byte with `InvalidCharacter` at its offset; a value outside the type with `OutOfRange` at offset 0. A `-` on an unsigned type is accepted only for zero. The parser accumulates in the negative range for signed types, so `Min` parses without overflow.

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

### 9.1 Contracts

This section fixes the member sets that the concrete classes and the interfaces in [section 9.10](Stdlib-Design#910-interfaces-and-read-only-views) share.

The **Needs** column names the compiler facilities an implementation relies on.

**List contract** (`ArrayList`, `LinkedList`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size() → int`, `is_empty() → bool` | | |
| `get(index) → T`, `set(index, value)` | `set` is not structural. | |
| `first() → T`, `last() → T` | Trap with `no such element` when empty. | |
| `add(value)`, `add_at(index, value)`, `add_all(items: T[list])` | `add` appends. `add_at` accepts an index equal to the size. | |
| `remove_at(index) → T`, `remove_first() → T`, `remove_last() → T` | The last two trap when empty. | |
| `swap(i, j)`, `reverse()`, `fill(value)`, `truncate(size)`, `clear()` | `truncate` traps for a size above the current size. | |
| `slice(start, end)` | A new collection of the same class. It copies; there are no sublist views. | |
| `to_list()` | A new built-in list in order. | |
| `iterator()` | [Section 5.4](Stdlib-Design#54-iteration). | |
| `first_or_none()`, `last_or_none()` `→ Optional<T>` | | G2 |
| `for_each(action: (T) → void)` | In order. | G1 |
| `remove_if(drop: (T) → bool) → int`, `retain_if(keep: (T) → bool) → int` | The callback runs exactly once per element, in order. Returns how many were removed. | G1 |
| `index_where(test: (T) → bool) → int` | The first matching index, or -1. | G1 |
| `sort(c: (T, T) → Ordering)`, `binary_search(value, c) → int` | [Section 9.11](Stdlib-Design#911-sorting-and-searching). | G1 |
| `sequence() → Sequence<T>` | | G1 |
| `contains(value) → bool`, `index_of(value) → int`, `last_index_of(value) → int`, `remove(value) → bool` | By `equals`. | G3 |
| `equals(other)`, `hash_code()`, `to_string()` | [Section 9.2](Stdlib-Design#92-equality-of-collections). | G3 |

**Queue and deque contracts** (`ArrayDeque`, `LinkedList`; the queue part also `PriorityQueue`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size()`, `is_empty()`, `clear()`, `to_list()`, `iterator()` | | |
| `enqueue(value)`, `dequeue() → T` | `dequeue` traps when empty. For a deque, `enqueue` is `add_last` and `dequeue` is `remove_first`. | |
| `add_first(value)`, `add_last(value)`, `remove_first() → T`, `remove_last() → T`, `first() → T`, `last() → T` | Deques only. | |
| `peek() → Optional<T>`, `try_dequeue() → Optional<T>` | | G2 |

**Map contract** (`HashMap`, `TreeMap`, `IntMap`, `StringMap`, `OrdinalMap`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size()`, `is_empty()`, `clear()` | | |
| `contains_key(key) → bool` | | |
| `get_or(key, fallback: V) → V` | | |
| `require(key) → V` | Traps with `no such element` when the key is absent. | |
| `set(key, value)` | Associates the value, replacing any previous one. | |
| `delete(key) → bool` | Removes the key; returns whether it was present. | |
| `keys()`, `values()`, `entries()` | New built-in lists of `K`, of `V`, and of `[K, V]`, in iteration order. They are copies. | |
| `iterator()` | Yields `[K, V]`. | |
| `put_all(other)` | From a map of the same class, in its iteration order. | |
| `get(key) → Optional<V>`, `put(key, value) → Optional<V>`, `remove(key) → Optional<V>` | `put` and `remove` return the previous value. | G2 |
| `for_each(action: (K, V) → void)`, `remove_if(drop: (K, V) → bool) → int` | | G1 |
| `get_or_put(key, make: () → V) → V` | Returns the existing value, or stores and returns the new one. | G1 |
| `merge(key, value, combine: (V, V) → V) → V` | Stores `value` when absent, otherwise the combination of old and new. | G1 |
| `compute(key, f: (K, Optional<V>) → Optional<V>) → Optional<V>` | Stores a `Some` result and removes the key for `None`. | G1, G2 |
| `contains_value(value) → bool`, `equals`, `hash_code`, `to_string` | | G3 |

`set` and `delete` avoid constructing an `Optional` when the caller does not need the previous value.

**Set contract** (`HashSet`, `TreeSet`, `BitSet`): `size`, `is_empty`, `clear`, `contains(value)`, `add(value) → bool` (false for a duplicate), `remove(value) → bool`, `to_list`, `iterator`, `add_all(items: T[list])`, and `is_subset_of`, `is_disjoint_from`, `union`, `intersection`, `difference` with a set of the same class (each of the last three returns a new set of that class: this set's elements first, then for `union` the other's new elements). With G1: `for_each`, `remove_if`, `retain_if`. With G3: `equals`, `hash_code`, `to_string`.

**Sorted contract** (`TreeSet`, `TreeMap`): `first`, `last` (trap when empty), `range(from, to)` (a built-in list of the elements or entries from `from` inclusive to `to` exclusive, ascending), `descending()` (a built-in list in descending order), and `remove_first`, `remove_last`. With G2: `floor`, `ceiling`, `lower`, `higher`, and for maps the `_entry` forms, each returning `Optional`.

### 9.12 `Sequence<T>`

A class for lazy pipelines, the counterpart of Java's streams. Needs G1; the members that return `Optional` also need G2.

**Redesign.** The first plan wrapped an `Iterator<T>` interface and gave each operation an adapter class implementing it. A sequence is now two native function values, and each operation is a closure over the previous two.

- **Layout.** `has_next: () → bool`, `next: () → T`, and a `bool` `consumed`.
- **Why closures fit.** An adapter's state is exactly what a closure captures. `map` captures the upstream pair and the mapping function and needs no state of its own. Operations that do keep state (`take`'s remaining count, `filter`'s buffered element, `enumerate`'s index) hold it in a captured `Cell`, because captured locals are copies ([section 5.6](Stdlib-Design#56-callbacks)).
- **Single use.** Every operation marks the sequence consumed; a second operation on the same sequence traps with `invalid state: sequence already used`.
- **Evaluation.** Pull-based and sequential. Nothing runs until a terminal operation, and each element passes through the whole pipeline before the next is requested.
- **Sources.** Module functions `from_list<T>(items: T[list])`, `empty<T>()`, `generate<T>(make: () → T)` (infinite), `iterate<T>(seed, step: (T) → T)` (infinite), and `range(start, end)` for `int`. Each collection's `sequence()` member closes over one of its iterator objects, so the pipeline is fail-fast.

| Intermediate | Behavior |
| --- | --- |
| `map<U>(f)`, `filter(keep)`, `flat_map<U>(f: (T) → U[list])` | |
| `take(n)`, `skip(n)`, `take_while(test)`, `skip_while(test)` | A negative `n` traps. `take` stops pulling after `n` elements. |
| `enumerate() → Sequence<[int, T]>` | Each element with its position from 0. |
| `zip<U>(other: Sequence<U>) → Sequence<[T, U]>` | Ends with the shorter input. |
| `chain(other: Sequence<T>)`, `peek(action)` | |
| `chunked(n)`, `windowed(n)` | Sequences of built-in lists, each list new. `n` below 1 traps. |
| `sorted(c)`, `reversed()` | Buffer the whole input when first pulled. `sorted` is stable. |
| `distinct()` | Keeps the first of each group of equal elements. Needs G3. |

| Terminal | Behavior |
| --- | --- |
| `to_list()`, `to_array_list()` | A built-in list; an `ArrayList<T>`. |
| `partition(test) → [ArrayList<T>, ArrayList<T>]` | Matching elements first. |
| `fold<A>(initial: A, f: (A, T) → A) → A` | Left to right. |
| `count() → int`, `for_each(action)` | `count` traps (reason 1) beyond the `int` range. |
| `any(test)`, `all(test)`, `none(test)` `→ bool` | Stop at the first deciding element. |
| `reduce(f) → Optional<T>`, `first()`, `last()`, `find(test)`, `min(c)`, `max(c)` | G2. `min` and `max` return the first of several equal extremes. |
| `to_set()`, `to_map<K, V>(key, value)`, `group_by<K>(key)` | G3, for the default key operations. A duplicate key in `to_map` traps. `group_by` returns a `HashMap<K, ArrayList<T>>` in first-seen key order. |

The module also has functions for sequences of one element type, which a member of a generic class cannot express: `join(s: Sequence<string>, separator: string) → string`, `sum_int32`, `sum_int64` (checked; overflow traps), and `sum_float64` (left-to-right addition).

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

Bitwise and, or, and exclusive or on big integers are not in the first version.

### 11.1 `Std.Io`

**`Console`** is the module that replaces `SysIo.println`. It needs P2.

| Member | Behavior | Also needs |
| --- | --- | --- |
| `print_line(s: string)` | Writes the text and one LF through `neura.console.write_line`. A failed write traps with `console write failed`, as `SysIo.println` does today. | F1 for the message |
| `try_print_line(s: string) → Result<Unit>` | The same write, returning the failure. | |
| `write(s)`, `error_line(s)`, and their `try_` forms | Write raw text through `neura.console.write`, to output or to the error stream. | T1 (text to bytes) |
| `format(pattern, args...)`, `vformat(pattern, args)`, and their `try_` forms | Render a [format string](https://github.com/Kayyo321/neura/wiki/StandardLibrary#console-formatting) to UTF-8 without calling a host service. | |
| `print`, `println`, `eprint`, `eprintln`, and their `try_` forms | Format first and then write to output or to the error stream. The newline forms append one LF. | T1, P2 |
| `read_line() → Result<Optional<string>>` | A line without its terminator; `None` at the end of input. | T1 |
| `output() → ByteSink`, `error() → ByteSink`, `input() → ByteSource` | The console as streams. | |

**Decision:** console output is unbuffered in the library. Each completed format is sent in program order through the existing raw write paths. The byte paths repeat partial writes until they finish or return an error.

**Streams are function values.** The first plan declared interfaces (`ByteReader`, `ByteWriter`, `TextReader`, `TextWriter`) so that a buffer could wrap any reader. Each of those is one operation with a concrete signature, which is what a native function value is, and a bound method supplies one from any object. No interface is needed and nothing here waits for G4.

| Shape | Type | Contract |
| --- | --- | --- |
| `ByteSource` | `(int) → Result<uint8[list]>` | Called with a maximum count of at least 1. Returns a new list of 1 through that many bytes, or an empty list at the end of the stream. |
| `ByteSink` | `(uint8[list]) → Result<Unit>` | Writes every byte, repeating partial host writes. Does not keep the list. |
| `TextSink` | `(string) → Result<Unit>` | Writes the string. |

The three names are this page's shorthand. A type overlay cannot name a function type today (probed), so library source spells each type out. Reading the method `read` of a `File` as a value binds the file and gives a `ByteSource`; a closure over anything else gives one too. The probe passed a bound method of a class to a function expecting a byte source and drained it, in both build modes. Closing is not part of a stream: the code that opened a resource closes it.

| Class | Behavior | Needs |
| --- | --- | --- |
| `ByteBuffer` | An in-memory accumulator over a built-in `uint8` list. `write` (a `ByteSink` when bound), `to_list()`, `size()`, `clear()`, and `reader()`, a `ByteSource` over a copy. Equality, hashing, and text use the current ordered bytes. A reader's position is independent of its buffer, and mutating a buffer used as a hash key invalidates that key's stored hash. | Today |
| `BufferedReader` | Built from a `ByteSource`. Holds up to 8,192 bytes (configurable, at least 16). `read` is a `ByteSource`; `read_exact(count) → Result<uint8[list]>` fails with kind `UnexpectedEnd`; `read_all()`. | Today |
| `BufferedWriter` | Built from a `ByteSink`. Collects up to 8,192 bytes and passes them on when full and on `flush() → Result<Unit>`. It does not close anything. | Today |
| `Utf8Reader` | Built from a `ByteSource`. `read_scalar() → Result<Optional<uint32>>`, `read_line() → Result<Optional<string>>`, `read_to_end() → Result<string>`. Invalid UTF-8 fails with kind `InvalidData`. | T1 |
| `Utf8Writer` | Built from a `ByteSink`. `write` is a `TextSink`; `write_line(s)` adds one LF. | T1 |

`read_line` ends a line at LF and removes it, along with a CR immediately before it. A final line without an LF is returned; after that the result is `None`.

### 11.2 `Std.Fs`

Needs P2 for every host operation and T1 for `Path`.

**`Path`** is an immutable class holding normalized text.

- The separator is `/` on every host. The host service translates.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- `is_absolute()` is true exactly when the text starts with `/`. Platform prefixes such as drive letters are passed to the host unchanged and are not interpreted.
- Module function `of(text)`. Members include `join(other: string) → Path`, `parent() → Path`, `file_name()`, `stem()`, `extension()`, `components()`, `resolve_lexically() → Path`, `starts_with(other: Path) → bool`, and `relative_to(base: Path) → Path`. An absolute argument to `join` replaces the path. `parent` returns `nil` when the path has no parent. `relative_to` returns `nil` when it cannot express the path relative to the base. `file_name`, `stem`, and `extension` return `Optional<string>`. The extension is the text after the last `.` of the file name when that dot is not the first character. `components` returns a built-in list of strings. Equality, hash, ordering, and text use the normalized path.

**`File`** is a class wrapping a host file ID. The module function `open(path, mode: OpenMode) → Result<File>`. `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](Stdlib-Design#55-resources)). A `File` is unbuffered.

**`Fs`** module:

| Member | Behavior |
| --- | --- |
| `read_bytes(path) → Result<uint8[list]>`, `read_text(path) → Result<string>` | `read_text` fails with `InvalidData` for invalid UTF-8. Both close the file on every path, including a failed read. |
| `write_bytes(path, bytes)`, `write_text(path, text)`, `append_text(path, text)` `→ Result<Unit>` | Create the file if needed; the `write_` forms truncate. A failed write is reported after the file is closed. |
| `exists(path) → Result<bool>` | `Ok(false)` for `NotFound`; other failures are errors. |
| `metadata(path) → Result<FileMetadata>` | `FileMetadata` is a class: `kind` (`FileKind`: `File`, `Directory`, `Other`), `size: int64`, `modified: Instant`. |
| `list(path)` | A `Result` of a built-in list of `Path`: the directory's entries joined to `path`, sorted by name bytes. |
| `create_directory(path)`, `create_directories(path)` | The second creates missing parents and succeeds when the directory exists. |
| `remove_file(path)`, `remove_directory(path)` | `remove_directory` requires an empty directory. |
| `remove_tree(path)` | Removes depth first, in sorted order, without descending into an entry of kind `Other`. Stops at the first error. |
| `rename(from, to)`, `copy_file(from, to)` | `copy_file` streams through a 65,536-byte buffer, replaces an existing target, and closes both files on every path. |

"On every path" is written out in each function as explicit `match` arms that close before returning the error. There is nothing to defer to.

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](Stdlib-Design#46-retiring-sysio-and-test)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, exactly as programs inherit `SysIo.Entry` today. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, and `Status(int)`. They map to process statuses 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. The first plan kept the old one-variant enum; a payload enum lets a program return any status without a host call.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

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

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
