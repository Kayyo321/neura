# `Std.Core.Result`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Result`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Result`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Result** · [`export let Result = enum<T> (Ok(T), Err(Error))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L10)
  Represents a successful payload (Ok) or an expected failure (Err).
- **Result** · [`export let is_ok = func<T>(r: Result<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L13)
  Reports whether the result holds a success payload.
- **Result** · [`export let is_err = func<T>(r: Result<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L19)
  Reports whether the result holds an error.
- **Result** · [`export let value_or = func<T>(r: Result<T>, fallback: T) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L25)
  Returns the success payload or the already-evaluated fallback.
- **Result** · [`export let expect = func<T>(r: Result<T>, detail: string) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L31)
  The value, or a trap whose message is the detail, ": ", and the error's message.
- **Result** · [`export let to_optional = func<T>(r: Result<T>) -> Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L43)
  Returns Some for success and discards an error by returning None.
- **Result** · [`export let with_context = func<T>(r: Result<T>, message: string) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L49)
  Wraps an Err in a Context error; returns an Ok unchanged.
- **Result** · [`export let map = func<T, U>(r: Result<T>, f: func(T) -> U) -> Result<U>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L55)
  Transforms only a successful payload, preserving an error unchanged.
- **Result** · [`export let and_then = func<T, U>(r: Result<T>, f: func(T) -> Result<U>) -> Result<U>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L61)
  Calls the function on success and returns its result, or propagates the original error.
- **Result** · [`export let map_error = func<T>(r: Result<T>, f: func(Error) -> Error) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L67)
  Transforms only the error payload, preserving success unchanged.
- **Result** · [`export let equals = func<T>(a: Result<T>, b: Result<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L73)
  Both Ok with equal values, or both Err with structurally equal errors.
- **Result** · [`export let hash = func<T>(r: Result<T>) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L91)
  Hash.combine(1, the value's hash) for Ok and Hash.combine(2, the error's hash) for Err.
- **Result** · [`export let to_string = func<T>(r: Result<T>) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/result.nra#L97)
  Formats the success value or the error with its variant name.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.1 Failure model

There are two kinds of failure and they never mix.

**Returned errors.** A function whose failure depends on input text, input bytes, or the outside world returns `Result<T>` ([section 6.2](Stdlib-Design#62-resultt)). The caller reads it with `match`. There is no propagation shorthand and no exception; passing an error up is an explicit `match` arm that returns it.

**Traps.** A function whose precondition is violated ends the run. The run stops at once with the process status of the trap's class, 134 (`Abort`) for a message trap; no library or user code runs afterwards; nothing can observe or recover it ([Errors](https://github.com/Kayyo321/neura/wiki/Errors)). The module `Failure` in `Std.Core` has one function:

| Member | Behavior |
| --- | --- |
| `trap(message: string)` | Calls `trap_with_message`. The run ends with reason 3, the message, and a source location when available. |

Library trap conditions and their messages. The message text is part of the contract. `<detail>` is fixed text given where the trap is documented.

| Condition | Message | Examples |
| --- | --- | --- |
| An element was required and none exists | `no such element` | `first()` of an empty list, `next()` after the last element, `expect` on `None` |
| An argument violates a documented precondition | `invalid argument: <detail>` | A negative count, a radix outside 2 through 36, a zero step |
| The receiver's state forbids the call | `invalid state: <detail>` | A consumed `Sequence`, `update` after `finish` on a digest |
| A collection changed structurally during iteration | `collection modified during iteration` | Any library iterator; the built-in for-each reports the same words with reason 5 |
| An assertion failed | The assertion's message | `Std.Test` |
| A host write that has no error result failed | `console write failed` | `Console.print_line` |

**The library does not re-check what the language already checks.** An index outside a built-in list, an overflow, a zero divisor, a shift count out of range, and an out-of-range conversion trap in the language with their own reasons (1 and 2) and locations. A library member that would fail the same way lets the built-in operation trap and documents that it does. `ArrayList.get` with a bad index is a bounds trap from the list read, not a library trap.

**What the absence of unwinding means.** The language has no panics that unwind, no deferred actions, and no recovery, and the library has none.

| Not provided | Instead |
| --- | --- |
| `Closeables.use` closes the resource even when the body panics | Replaced by the scoped helpers of [section 5.5](Stdlib-Design#55-resources), which close the resource on every ordinary completion. A body cannot fail and return; it either returns or the run ends, and when a run ends, by return or by trap, the host releases every file and socket. |
| "If the predicate of `remove_if` panics, the collection is left valid" | Removed. A trap inside a callback ends the run, so no state can be observed afterwards. No collection keeps recovery code. |
| `Iterator.next` panics with `NoSuchElementError`; modification panics with `ConcurrentModificationError` | Both are traps with the messages above. The error classes for programmer mistakes are gone. |
| `assert_panics` recovers a panic and returns its error | Removed. A test that expects a trap is a program with an expected-diagnostic file, run by the test harness as the existing trap tests under `tests/backend/traps` are ([section 17](Stdlib-Design#17-verification)). |
| A fallible callback | A callback that can fail returns a `Result`, and the member that calls it documents how it stops. |

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- **Scoped helpers.** `File.with_open<T>(path, mode, body: (File) → Result<T>) → Result<T>` and `TcpStream.with_connection<T>(host, port, body: (TcpStream) → Result<T>) → Result<T>` open the resource, run the body, and close the resource exactly once on every ordinary completion: a returned value, a returned error, and a return after the body's scope was cancelled. With tasks a body can end early by cancellation, and the close must still happen ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#7-cancellation-deadlines-and-resources)).
  - If the open fails, the helper returns that error and does not call the body.
  - The body runs in a `scope` the helper owns, so every task started in that scope has finished before the resource is closed.
  - If the close succeeds, the helper returns the body's result unchanged. If the close fails after a body `Ok`, it returns the close error. If the close fails after a body `Err`, it returns a `Context` error whose cause is the body's error and whose message carries the close error's message: `closing the resource also failed: ` and that message from `with_open`, `close failed after the body failed: ` and that message from `with_connection`.
  - A file is closed even when the body closed it already, which reports no error. `with_connection` connects without a time limit.
  - A trap in the body ends the run without cleanup; the runner then closes what the run left open.
- **`Closeable.release<T>(resource: Closeable, outcome: Result<T>) → Result<T>`** is the closing rule on its own: it closes the resource and combines the result with the outcome of the work, as `with_open` does. A program that owns another kind of closeable resource calls it exactly once, after the work, on every ordinary completion.
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

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

**`Optional<T>` is a generic payload enum with the variants `None` and `Some(T)`, read with `match`.** The enum cannot be read without handling both cases, has no trapping accessor to misuse, and needs no late field.

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

**`Result<T>` is a generic payload enum with the variants `Ok(T)` and `Err(Error)`.** It is the one representation of a fallible outcome. An operation with no value returns `Result<Unit>`. The error type is fixed to `Error` so that every fallible function composes with every other.

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

### 7.3 Float modules

The float companions use N1 for bit-level operations. `is_nan` can also use ordinary float comparison because a NaN differs from itself.

| Member | Behavior |
| --- | --- |
| Constants `Max`, `MinPositive`, `MinNormal`, `Epsilon`, `Infinity`, `NegativeInfinity`, `NaN` | Construct exact boundary and special encodings with `from_bits`. Use the format's canonical quiet NaN. |
| `is_nan`, `is_infinite`, `is_finite`, `is_negative` | `is_negative` reads the sign bit, so it is true for `-0.0`. |
| `abs`, `copy_sign(magnitude, sign)` | Sign-bit operations; never round. |
| `min(a, b)`, `max(a, b)` | NaN if either operand is NaN. `-0.0` is less than `0.0`. |
| `clamp(v, low, high)` | Traps when `low` exceeds `high` or either is NaN. |
| `floor`, `ceil`, `trunc`, `round`, `round_even` | `round` rounds halves away from zero. All preserve the sign of zero. |
| `to_bits(v)`, `from_bits(bits)` | N1 itself. |
| `total_compare(a, b) → Ordering` | The total order of [section 5.3](Stdlib-Design#53-ordering). |
| `equals(a, b)`, `hash(v)` | [Section 5.2](Stdlib-Design#52-equality-hashing-and-text). |
| `to_string(v)`, `to_fixed(v, digits: int)`, `to_exponential(v, digits: int)` | [Section 7.5](Stdlib-Design#75-float-text-conversion). `to_fixed` gives exactly `digits` fraction digits (0 through 100), correctly rounded, ties to even. |
| `parse(text) → Result<X>` | Also T1. |

### 7.4 `Bool`

`to_int(v) → int` gives 1 or 0; `xor(a, b)`; `compare(a, b) → Ordering`; `hash(v)`; `to_string(v)`. `parse(text) → Result<bool>` accepts exactly `true` and `false` and needs T1.

### 8.1 `Strings`

All of `Strings` needs T1. String operations are functions that take the string first. All positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

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

**Unicode scope.** `Std.Text.Strings` handles UTF-8 encoding, decoding, and iteration for all Unicode scalar values. Its original ASCII case and classification methods keep their scope. The [`String` companion](https://github.com/Kayyo321/neura/wiki/StandardLibrary#string) adds a fixed whitespace set for strip and indentation, Unicode 17.0.0 case conversion and folding, and standard charset conversion from `Std.Text.Charsets`. These are ordinary Neura routines with embedded Unicode data and use no host locale or charset services.

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

An immutable decimal number stores an unscaled `BigInteger` and an `int` `scale`, representing unscaled × 10^(-scale). Parsing uses T1 and float conversion uses N1.

- `equals` and `compare_to` are numeric, so `2.0` equals `2.00`. `hash_code` is computed on the form with trailing zeros stripped.
- `add` and `subtract` give the larger scale; `multiply` gives the sum of the scales; all are exact.
- Division always takes an explicit result scale and `RoundingMode`: `divide(other, scale, mode)`. There is no exact division that could fail on a non-terminating quotient.
- `RoundingMode` is a fieldless enum, in order: `Up`, `Down`, `Ceiling`, `Floor`, `HalfUp`, `HalfDown`, `HalfEven`, with Java's meanings.
- Other members: `set_scale(scale, mode)`, `round(precision, mode)`, `strip_trailing_zeros`, `negate`, `abs`, `sign`, `pow(exponent: int)`, `to_big_integer(mode)`, `to_float64()`, and the module functions `of_int64`, `of_big_integer(value, scale)`, `of_float64(v)` (the exact binary value; NaN and infinities trap), and `parse(text) → Result<BigDecimal>`.
- `to_string` always uses plain notation with exactly `scale` fraction digits when the scale is positive, and appends zeros for a negative scale (zero with a negative scale prints as `0`).
- A scale that leaves the `int` range traps (reason 1).

### 11.1 `Std.Io`

**`Console`** is the module that replaces `SysIo.println`. It needs P2.

| Member | Behavior | Also needs |
| --- | --- | --- |
| `print_line(s: string)` | Writes the text and one LF through `neura.console.write_line`. A failed write traps with `console write failed`. | F1 for the message |
| `try_print_line(s: string) → Result<Unit>` | The same write, returning the failure. | |
| `write(s)`, `error_line(s)`, and their `try_` forms | Write raw text through `neura.console.write`, to output or to the error stream. | T1 (text to bytes) |
| `format(pattern, args...)`, `vformat(pattern, args)`, and their `try_` forms | Render a [format string](https://github.com/Kayyo321/neura/wiki/StandardLibrary#console-formatting) to UTF-8 without calling a host service. | |
| `print`, `println`, `eprint`, `eprintln`, and their `try_` forms | Format first and then write to output or to the error stream. The newline forms append one LF. | T1, P2 |
| `read_line() → Result<Optional<string>>` | A line without its terminator; `None` at the end of input. | T1 |
| `output() → ByteSink`, `error() → ByteSink`, `input() → ByteSource` | The console as streams. | |

Console output is unbuffered in the library. Each completed format is sent in program order through the existing raw write paths. The byte paths repeat partial writes until they finish or return an error.

**Streams are function values.** A reader or writer is one operation with a concrete signature, which is what a native function value is, and a bound method supplies one from any object. The library declares no `ByteReader`, `ByteWriter`, `TextReader`, or `TextWriter` interface.

| Shape | Type | Contract |
| --- | --- | --- |
| `ByteSource` | `(int) → Result<uint8[list]>` | Called with a maximum count of at least 1. Returns a new list of 1 through that many bytes, or an empty list at the end of the stream. |
| `ByteSink` | `(uint8[list]) → Result<Unit>` | Writes every byte, repeating partial host writes. Does not keep the list. |
| `TextSink` | `(string) → Result<Unit>` | Writes the string. |

The three names are this page's shorthand. A type overlay cannot name a function type, so library source spells each type out. Reading the method `read` of a `File` as a value binds the file and gives a `ByteSource`; a closure over anything else gives one too. Closing is not part of a stream: the code that opened a resource closes it.

| Class | Behavior | Needs |
| --- | --- | --- |
| `ByteBuffer` | An in-memory accumulator over a built-in `uint8` list. `write` (a `ByteSink` when bound), `to_list()`, `size()`, `clear()`, and `reader()`, a `ByteSource` over a copy. Equality, hashing, and text use the current ordered bytes. A reader's position is independent of its buffer, and mutating a buffer used as a hash key invalidates that key's stored hash. | None |
| `BufferedReader` | Built from a `ByteSource`. Holds up to 8,192 bytes (configurable, at least 16). `read` is a `ByteSource`; `read_exact(count) → Result<uint8[list]>` fails with kind `UnexpectedEnd`; `read_all()`. | None |
| `BufferedWriter` | Built from a `ByteSink`. Collects up to 8,192 bytes and passes them on when full and on `flush() → Result<Unit>`. It does not close anything. | None |
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

**`File`** is a class wrapping a host file ID. The module functions `open(path, mode: OpenMode) → Result<File>` and `with_open<T>(path, mode, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](Stdlib-Design#55-resources)). A `File` is unbuffered.

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

This module replaces `SysIo.Entry` ([section 4.6](Stdlib-Design#46-retired-built-ins)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, as every program does. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, `Status(int)`, and the named statuses `SeriousFailure` (2), `CommandNotFound` (127), `IllegalInstruction` (132), `Breakpoint` (133), `Abort` (134), `ArithmeticError` (136), `OutOfMemory` (137), `BusError` (138), `SegmentationFault` (139), `CpuTimeExceeded` (152), and `FileSizeExceeded` (153). `Ok`, `Failure`, and `Status` map to 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. A payload enum lets a program return any status without a host call. Neura enums cannot carry explicit integer values, so the compiler and `status_of(code) → int` hold the table; the named variants follow declaration order after `Status`, so the original three keep their ordinals.
- **`status_of(code: ExitCode) → int`** returns the status a variant stands for, trapping for an invalid `Status`.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. The overload `Process.exit(code: ExitCode)` exits with `status_of(code)`. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 11.4 `Std.Net`

TCP only. A call waits for its result; in a program with tasks the waiting task parks and the others run, and cancelling its scope ends the wait with kind `Cancelled` ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#104-host-io)). Needs P2 and T1.

- `TcpStream`: the module functions `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>` and `with_connection<T>(host, port, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `set_read_timeout(timeout: Duration)` (zero for none; an expired timeout fails with kind `TimedOut`), and `close()`. A write that is cancelled while it waits fails with kind `Cancelled` and the message `stream write cancelled after N bytes`, where N is the number of bytes already sent.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, `local_port() → int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

The library has no UDP, TLS, HTTP, or name-resolution APIs.

### 12.1 `Std.Time`

The value types are immutable classes with `compare_to`, `equals`, `hash_code`, and `to_string`. Their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Module functions: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; the constant `Zero`; `parse(text) → Result<Duration>`. Overflow traps (reason 1), here and in every member below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- Module functions `of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`, and `parse`. Members `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `epoch_seconds()`, `nanos()`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface with `now() → Result<Instant>`, `monotonic() → Result<Duration>` (elapsed time from an arbitrary fixed origin, never decreasing), and `sleep(duration) → Result<Unit>`.

- `SystemClock` uses the clock host services (P2) and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock`, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, and `elapsed() → Result<Duration>`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate` stores `year` (-999,999 through 999,999), `month` (1 through 12), and `day`. The module function `of(year, month, day)` traps for an invalid date. `try_of(year, month, day) → LocalDate` returns `nil` for invalid components. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days. Shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle using floor division so that negative years work. Members include `plus_days`, `plus_months`, `plus_years`, `days_until(other) → int64`, `day_of_week() → DayOfWeek`, `day_of_year()`, `is_leap_year()`, and `length_of_month()`. Month and year changes clamp the day to the end of the target month. `DayOfWeek` runs from `Monday` through `Sunday`, with epoch day 0 on Thursday. Text uses `YYYY-MM-DD`. Years outside 0000 through 9999 have a sign and at least four digits.
- `LocalTime`: `hour`, `minute`, `second`, `nano`, stored as nanoseconds of the day. `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800. The constant `Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.

There is no time zone database, no named zones, and no daylight-saving rules. Conversions between instants and local times take an explicit `ZoneOffset`. The ISO forms above are the only text forms.

### 12.2 `Std.Random`

**`Random`** implements the xoshiro256** generator using shifts, rotations, exclusive ors, and multiplication by 5 and 9. Use the `uint64` recipes in [section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics). `next_gaussian` depends on the library's `Math` module, and `from_entropy` requires a registered entropy provider.

- **State.** Four `uint64` words `s0` through `s3`.
- **Step.** The output is `rotate_left(s1 × 5, 7) × 9`, wrapping. Then, with `t` = `s1` shifted left by 17: `s2 ^= s0`; `s3 ^= s1`; `s1 ^= s2`; `s0 ^= s3`; `s2 ^= t`; `s3 = rotate_left(s3, 45)`.
- **Seeding.** The module function `seeded(seed: uint64)` fills the four words with successive outputs of SplitMix64 started at the seed: add 0x9E3779B97F4A7C15 to the state; take `z` as the new state; `z = (z ^ (z >> 30)) × 0xBF58476D1CE4E5B9`; `z = (z ^ (z >> 27)) × 0x94D049BB133111EB`; output `z ^ (z >> 31)`; all wrapping. The source writes these constants in decimal (A5). `from_entropy() → Result<Random>` requests 8 bytes from `neura.entropy.fill`, reads them as a little-endian `uint64`, and seeds with that.

| Member | Behavior |
| --- | --- |
| `next_uint64() → uint64` | One step. |
| `next_uint32() → uint32` | The high 32 bits of one step. |
| `next_bool() → bool` | The top bit of one step. |
| `next_int_below(bound: int) → int` | Uniform in 0 up to but excluding `bound`. Take the smallest all-ones mask covering `bound - 1`; draw `next_uint32` masked until the result is below `bound`. A bound below 1 traps. |
| `next_int_between(low: int, high: int) → int` | Uniform from `low` up to but excluding `high`. The span is computed in `int64`, sampled with the same mask-and-reject loop on `uint32`, and added to `low` in `int64`. `low` not below `high` traps. |
| `next_float64() → float64` | The top 53 bits of one step, converted to `float64` (exact) and multiplied by 2^-53: uniform in 0 up to but excluding 1. |
| `next_gaussian() → float64` | Marsaglia's polar method. Each accepted pair yields two values; the second is kept in the generator and returned by the next call. |
| `fill_bytes(count: int)` | A new built-in `uint8` list. Each step supplies 8 bytes, least significant first; a final partial group uses the low bytes. |
| `shuffle<T>(items: T[list])` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. Generic, with no callback. |
| `choose<T>(items: T[list]) → T` | Traps with `no such element` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets, and the library offers no cryptographic generator.

**`Uuid`** is an immutable value of 16 bytes. The module function `random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `to_string` writes the 36-character hyphenated form in lowercase. `parse(text) → Result<Uuid>` accepts either case and needs T1. It has `compare_to`, `equals`, and `hash_code` over its bytes.

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone.
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero.
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

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.

### 18.5 Time, randomness, and logging

- **Duration text.** `parse` accepts an optional sign, `P`, an optional day count, and `T` with hours, minutes, and seconds (seconds with up to nine fraction digits); days are read but never written.
- **Instant** normalizes the nanoseconds of `of_epoch_seconds` by floor division and writes every `int64` second count; `parse` accepts years -999,999 through 999,999.
- **ZoneOffset** holds whole minutes; hours and minutes share a sign, and `compare_to` orders by total seconds.
- **Clocks.** `Clock.sleep` returns at once for a zero or negative duration. `FixedClock.of(instant)` makes a fixed clock whose monotonic reading is the total of its advances and sleeps; `advance` traps for a negative duration. `Stopwatch.create(clock)`; `start` and `stop` return `Result<Unit>` and trap when repeated.
- **Calendar arithmetic** splits off 400-year cycles with one 64-bit division and works within a cycle in 32-bit arithmetic, which keeps images that format dates several megabytes smaller.
- **Random.** `seeded` and `from_entropy` are the only constructors; `from_entropy` fails with `InvalidData` when the host returns other than 8 bytes. **Uuid** stores two big-endian `uint64` halves and has `to_bytes`.
- **Trap details.** `invalid date`, `date out of range`, `invalid time`, `invalid zone offset`, `day of week`, `negative duration`, `bound must be positive`, `low must be below high`, and `negative count` after `invalid argument: `; `stopwatch is running` and `stopwatch is stopped` after `invalid state: `.
- **Logger.** `is_enabled(Off)` is false.
