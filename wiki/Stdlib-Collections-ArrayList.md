# `Std.Collections.ArrayList`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.ArrayList`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.ArrayList`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ArrayList** · [`export let ArrayList = class<T> implements MutableList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L13)
- **ArrayList.ArrayList** · [`export let init = func(values: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L20)
  Adopts `values` without copying, as `wrap` does.
- **ArrayList.ArrayList** · [`export let items = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L26)
  The live backing list. A built-in for-each over it traps with reason 5 on a structural change.
- **ArrayList.ArrayList** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L28)
- **ArrayList.ArrayList** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L30)
- **ArrayList.ArrayList** · [`export let get = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L33)
  A bounds trap for an index outside 0 through size() - 1.
- **ArrayList.ArrayList** · [`export let set = func(index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L36)
  Not structural.
- **ArrayList.ArrayList** · [`export let first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L40)
- **ArrayList.ArrayList** · [`export let last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L47)
- **ArrayList.ArrayList** · [`export let first_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L54)
- **ArrayList.ArrayList** · [`export let last_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L61)
- **ArrayList.ArrayList** · [`export let add = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L69)
  Appends; always true.
- **ArrayList.ArrayList** · [`export let add_at = func(index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L75)
  Inserts before `index`; an index equal to the size appends.
- **ArrayList.ArrayList** · [`export let add_all = func(values: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L95)
  Appends each element in order. Reads by position up to the starting length, so adding the list's own items() doubles it.
- **ArrayList.ArrayList** · [`export let remove_at = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L104)
- **ArrayList.ArrayList** · [`export let remove_first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L110)
- **ArrayList.ArrayList** · [`export let remove_last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L119)
- **ArrayList.ArrayList** · [`export let swap = func(i: int, j: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L130)
  Not structural.
- **ArrayList.ArrayList** · [`export let reverse = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L137)
  Not structural.
- **ArrayList.ArrayList** · [`export let fill = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L150)
  Not structural.
- **ArrayList.ArrayList** · [`export let truncate = func(size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L162)
  Removes elements from the end of the backing list until `size` remain. Traps with "invalid argument: negative size" or "invalid argument: size above current size".
- **ArrayList.ArrayList** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L175)
  Removes every element from the end of the same backing list.
- **ArrayList.ArrayList** · [`export let slice = func(start: int, end: int) -> ArrayList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L184)
  A new ArrayList with the elements from `start` inclusive to `end` exclusive. Traps with "invalid argument: range" unless 0 <= start <= end <= size().
- **ArrayList.ArrayList** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L198)
  A new built-in list in order.
- **ArrayList.ArrayList** · [`export let iterator = func -> Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L210)
  A fail-fast iterator over the backing list.
- **ArrayList.ArrayList** · [`export let sequence = func -> sequences.Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L214)
  A lazy pipeline over this list's fail-fast iterator (Stdlib.md section 9.12): a structural change while it runs traps.
- **ArrayList.ArrayList** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L217)
  In order. The action must not change the list structurally.
- **ArrayList.ArrayList** · [`export let remove_if = func(drop: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L232)
  Calls `drop` once per element in order, compacts the kept elements by indexed writes, then removes the tail from the end. Returns how many were removed.
- **ArrayList.ArrayList** · [`export let retain_if = func(keep: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L234)
- **ArrayList.ArrayList** · [`export let index_where = func(test: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L262)
  The first index whose element passes, or -1.
- **ArrayList.ArrayList** · [`export let sort = func(c: func(T, T) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L279)
  Stable; Sorting.sort on the backing list.
- **ArrayList.ArrayList** · [`export let binary_search = func(value: T, c: func(T, T) -> ordering.Ordering) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L284)
  Sorting.binary_search on the backing list, which must be sorted by c.
- **ArrayList.ArrayList** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L288)
  By equals, with `value` as the receiver.
- **ArrayList.ArrayList** · [`export let index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L290)
- **ArrayList.ArrayList** · [`export let last_index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L302)
- **ArrayList.ArrayList** · [`export let remove = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L314)
  Removes the first element equal to `value`.
- **ArrayList.ArrayList** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L325)
  Another ArrayList with the same element type, size, and equal elements at each index.
- **ArrayList.ArrayList** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L333)
  The list formula of section 5.2.
- **ArrayList.ArrayList** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L337)
  `[a, b, c]`; the list itself as a direct element prints "(this collection)".
- **ArrayList** · [`export let create = func<T> -> ArrayList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L381)
- **ArrayList** · [`export let of = func<T>(items: T[list]) -> ArrayList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L387)
  Copies `items`.
- **ArrayList** · [`export let filled = func<T>(count: int, value: T) -> ArrayList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L399)
  `count` copies of `value`. Traps with "invalid argument: negative count".
- **ArrayList** · [`export let wrap = func<T>(items: T[list]) -> ArrayList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-list.nra#L414)
  Adopts `items` without copying; the list and the wrapper share storage.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.1 Failure model

There are two kinds of failure and they never mix.

**Returned errors.** A function whose failure depends on input text, input bytes, or the outside world returns `Result<T>` ([section 6.2](Stdlib-Design#62-resultt)). The caller reads it with `match`. There is no propagation shorthand and no exception; passing an error up is an explicit `match` arm that returns it.

**Traps.** A function whose precondition is violated ends the run. The run stops at once with process status 70; no library or user code runs afterwards; nothing can observe or recover it ([Errors](https://github.com/Kayyo321/neura/wiki/Errors)). The module `Failure` in `Std.Core` has one function:

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

**Decision: the library does not re-check what the language already checks.** An index outside a built-in list, an overflow, a zero divisor, a shift count out of range, and an out-of-range conversion trap in the language with their own reasons (1 and 2) and locations. A library member that would fail the same way lets the built-in operation trap and documents that it does. `ArrayList.get` with a bad index is a bounds trap from the list read, not a library trap.

**What the absence of unwinding changes.** The first plan assumed panics that unwind, deferred actions, and recovery. None exists, and this plan asks for none.

| First plan's promise | Now |
| --- | --- |
| `Closeables.use` closes the resource even when the body panics | Removed. A body cannot fail and return; it either returns or the run ends. Callers call `close` on each return path. When a run ends, by return or by trap, the host releases every file and socket. |
| "If the predicate of `remove_if` panics, the collection is left valid" | Removed. A trap inside a callback ends the run, so no state can be observed afterwards. No collection keeps recovery code. |
| `Iterator.next` panics with `NoSuchElementError`; modification panics with `ConcurrentModificationError` | Both are traps with the messages above. The error classes for programmer mistakes are gone. |
| `assert_panics` recovers a panic and returns its error | Removed. A test that expects a trap is a program with an expected-diagnostic file, run by the test harness as the existing trap tests under `tests/backend/traps` are ([section 17](Stdlib-Design#17-verification)). |
| A fallible callback | A callback that can fail returns a `Result`, and the member that calls it documents how it stops. |

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

### 5.6 Callbacks

- A callback parameter is written as a native function type. The library declares no named function shapes: a type overlay can be neither generic nor a function type, and the native spelling says everything a name would.
- **Concrete callbacks work today.** `Logger` takes a sink of type `(string) → void`, and any module can use function values whose types name no type parameter.
- **Generic callbacks specialize with their caller.** A member of `ArrayList<T>` can take `(T) → bool`. The G1 label in API tables identifies members that depend on this compiler facility.
- **Capture is by value.** A closure copies the locals it uses when it is created. A closure cannot count or accumulate in a captured local. It captures an object and mutates that. `Std.Core` provides `Cell<T>` ([section 6.5](Stdlib-Design#65-small-types)) for the purpose: a callback that sums captures a `Cell<int64>`.
- **Capture of a reference is shared.** A closure that captures a collection sees later changes to it, and changes it makes are visible outside. A callback passed to a collection's own member must not structurally modify that collection; the member compares the modification count after each call and traps on a change.
- Unless a member's description says otherwise, a callback may be called any number of times, in the order described.

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

| Type | Definition | Status |
| --- | --- | --- |
| `Unit` | A fieldless enum with the single variant `Value`. | Today |
| `Cell<T>` | A class with one mutable field `value`, and the module function `of<T>(value)`. It exists so that closures, which capture by value, can share mutable state. | Today |
| `IntRange` | An immutable class: `start` (inclusive), `end` (exclusive), `step` (nonzero). Module functions `of(start, end)` and `stepped(start, end, step)`; a zero step traps. A negative step counts down while the value is greater than `end`. Members `size()`, `is_empty()`, `contains(value)`, `get(index)`, and `to_list()`. `size` is computed in `int64` so that no intermediate overflows. | Today |

Removed from the first plan:

- **`Pair<A, B>`** is the native tuple `[A, B]`. `partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`.
- **`Entry<K, V>`** is the tuple `[K, V]`. Its contract was "an immutable snapshot of one association", which a tuple already is. Its text (`key: value`) is produced by the map's `to_string`.
- **`Boxed<T>`** existed to pass non-class values as `Object`. No API on this page takes an `Object`.

Named types are kept where the name carries a contract a tuple cannot: `RegexMatch` (group lookup by name), `FileMetadata` (three fields of unrelated types that will grow), and the value classes of `Std.Time`.

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

### 9.2 Equality of collections

All of this needs G3.

| Kind | Equal when | Hash | Text |
| --- | --- | --- | --- |
| List | Same class and size, with equal elements at each index | The list formula of [section 5.2](Stdlib-Design#52-equality-hashing-and-text) | `[a, b, c]` |
| Set | Same class and size, and this set contains each of the other's elements | The wrapping sum of the element hashes | `{a, b, c}` in iteration order |
| Map | Same class and size, and for each of the other's entries this map holds an equal value for the key | The wrapping sum over entries of key hash exclusive-or value hash | `{k: v, k2: v2}` in iteration order |
| `ArrayDeque`, `PriorityQueue`, `Sequence` | Identity | Identity | `[a, b, c]` in iteration order; `Sequence` prints `Sequence` |

`equals(other: class)` first tests whether `other` is an instance of the same collection type. It then downcasts and compares contents. Comparing an `ArrayList` with a `LinkedList` by content is the separate `Lists.same_elements` function over the list interface.

### 9.3 `ArrayList<T>`

**Decision:** `ArrayList` is a thin class over one built-in list. Each built-in list operation is a single NVM storage operation; a library-managed backing array would spend graph evaluations on capacity checks for every append and would need runtime-length arrays, which do not exist.

- **Layout.** One built-in list `storage`. Its `version` is the modification counter.
- **Module functions.** `create<T>()`, `of<T>(items: T[list])` (copies), `filled<T>(count, value)`, and `wrap<T>(items: T[list])` (adopts the list without copying).
- **`items() → T[list]`** returns the live built-in list. A built-in for-each over it traps with reason 5 on structural modification. For-each over the wrapper calls its iterator through `Iterable<T>`.
- **Aliasing.** `wrap` and `items` share the same backing list. The wrapper preserves the identity of that list for its lifetime. Its iterators compare the backing version, so modifications through a direct alias are detected as well.

| Operation | Cost | How |
| --- | --- | --- |
| `get`, `set`, `size`, `add`, `remove_last` | One storage operation | Built-in index, length, `append`, and `remove_at` of the last index. |
| `remove_at` | One storage operation | Built-in `remove_at`. |
| `add_at` | One operation per element after the index | At the end, append the new value directly. Otherwise append the last element again, shift the suffix right by one with indexed writes, and store the new value. A2 would make this one operation. |
| `truncate`, `clear` | One operation per removed element | Repeatedly remove the last index from the same backing list. A2 would shorten this to one in-place operation. |
| `remove_if` | Linear | Check the version after each callback, compact the kept elements by indexed writes, then remove the tail from the end. The backing version advances on each actual removal. |
| `reverse`, `fill`, `slice`, `to_list` | Linear | Element reads and writes. |

No removal leaves a stale reference: the built-in list drops the slot, and the probes showed the removed element is reclaimed.

### 9.5 `LinkedList<T>`

Nodes are separately allocated and reclaimed by the NVM. Missing neighbors are ordinary nil references.

- **Layout.** An unexported generic class `Node<T>` holds `value`, `next`, and `previous`. The list holds `head`, `tail`, `count`, and the modification count. An empty list has two nil endpoints. The head has no previous node, and the tail has no next node. Every allocated node has an initialized value.
- **Insertion** sets both neighbors of a new node and updates each adjacent link or the corresponding endpoint. **Removal** reconnects each existing neighbor or updates the endpoint. Clear the removed node's links so that an iterator holding the removed node does not retain the remaining chain.
- **Positional access** walks from the first element when the index is in the first half and from the last otherwise.
- **Why nodes and not an arena.** Each element costs one small object either way. The arena saved allocations only by reusing slots through a free list, and it paid for that with array growth copies, a free list to maintain, and vacant slots the language does not have.

It follows both the list and the deque contracts. Prefer `ArrayList` or `ArrayDeque` unless elements are inserted or removed in the middle while iterating by position.

### 9.11 Sorting and searching

The module `Sorting`. Generic sorting needs G1 for the comparator.

**`sort<T>(items: T[list], c: (T, T) → Ordering)`** is stable:

1. Sort each consecutive block of 16 elements (the last may be shorter) in place with binary insertion sort: for each element, binary-search the sorted prefix for the position after the last element not `Greater` than it, and shift the tail right by one.
2. Create a buffer: a second built-in list of the same length, filled by appending the elements themselves. They are references the list already holds, so the buffer keeps nothing extra alive, and no "empty" value is needed.
3. Merge bottom-up. For widths 16, 32, 64, and so on while the width is less than the length, merge each adjacent pair of runs from the source list into the destination, then swap the two lists' roles. If the last element of the left run is not `Greater` than the first of the right run, the pair is copied without comparisons. When merging, take from the right run only when its head is `Less` than the left run's head; this makes the sort stable.
4. If the result ended in the buffer, write it back to `items` by index. Element writes do not change the list's structural version.

It makes at most about `n log2 n` comparator calls. `ArrayList.sort` calls it on `storage`. `sort_range(items, start, end, c)` sorts part of a list.

**`binary_search<T>(items: T[list], value: T, c) → int`** requires `items` sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index, the first of any equal run. Otherwise it returns `-(insertion point) - 1`.

The module **`Lists`** has helpers for built-in lists that need no callback and work today: `copy<T>(items)`, `concat<T>(a, b)`, `reverse<T>(items)`, `swap<T>(items, i, j)`, `fill<T>(items, value)`, `insert_at<T>(items, index, value)`, `truncate<T>(items, length)`, `clear<T>(items)`, `filled<T>(count, value)`, and `from_array<T>(items: T[])`. There is no conversion from a list to an array, because an array's length must come from a literal.

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

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` for a collection that directly contains itself, through a shared depth counter (`Std.Collections.Internal.TextDepth`); deeper cycles still end with the frame-limit fault.
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the probe as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.

### 18.9 Hash collections, trees, and sequences

- **Compaction rule 1** also compacts when live entries plus tombstones fill `E`. Trailing releases leave tombstones behind, so without this addition the index could fill and a lookup for a missing key would not terminate.
- **Modification counts are `uint64`**, as [section 5.4](Stdlib-Design#54-iteration) states; compaction and `trim()` advance them, so an iterator traps after either. `clear()` returns to the table size chosen at construction.
- **Bulk removal.** `remove_if` and `retain_if` call the callback on every entry, then release the chosen entries and compact at most once. `compute` returns the result of its callback. A `with_keys` collection hashes with its stored function, so `hash_code` agrees with its equality.
- **IntMap and StringMap** are generated from `hash-map.nra` with the key operations called directly (`tests/stdlib/hash-map/generators/gen_maps.py`); `StringMap` uses the string's built-in `hash_code` and `==`, which equal `Strings.hash` and `Strings.equals`, because `Std.Text` is above `Std.Collections` in the dependency order.
- **Extra members.** `with_keys_and_capacity`; `trim` and `validate` on every hash collection; `TreeMap.key_iterator`, `key_range`, and `descending_keys`, which `TreeSet` uses to avoid a tuple per element; `Sequence.from_iterator`, a public `Sequence` constructor from two functions, and `Sequence.iterator()` so that a sequence works once in for-each. Constructors of these collections take a module-private token, so only their module functions build them.
- **Sequence choices.** `range(from, to)` on a sorted collection is empty when `to` is not above `from`. `windowed` emits only full windows. `zip` and `chain` mark the other sequence used. `from_list` fails fast on a structural change to its list. `sum_float64` starts from the first element, so `[-0.0]` sums to `-0.0`, and an empty sequence sums to `0.0`. Maps and sets have no `sequence()` member; `ArrayList` and `LinkedList` build theirs with `from_iterator`.
- **Trap details.** `duplicate key`, `negative count`, `chunk size`, and `window size` after `invalid argument: `; `invalid state: hash table …` and `invalid state: tree …` for a failed `validate()`.

### 18.11 Verification as built

- **Layout.** Each area of `tests/stdlib` holds test files, `support/` modules, `traps/` programs with `// status:` and `// stderr:` headers, and `programs/` with expected output (`tests/stdlib/area.cmake`). Every area runs against a fresh installation (`stdlib-install`), its tests optimized twice and unoptimized, its programs and traps in both modes, and its program images through `disassemble`, `assemble`, and `decompile`.
- **Reference evaluator.** `backend-reference` runs every library test function under the NeuraIR reference evaluator and NVM and compares the outcome, the console output, and any trap message. Tests that need host responses (files, sockets, console input) or exceed the evaluator's budget are counted and not compared.
- **Host services** are tested with the capability denied, with real providers in temporary directories and on localhost, and by replaying checked-in traces.
- **Cost baseline.** `tests/benchmarks` holds the workloads of this section's cost baseline and a few more (ArrayList, TreeMap, strings, JSON, dates); `benchmarks` compares their steps, peak heap cells, neurons, and image bytes with `baseline.txt` and checks that steps grow as expected between two sizes.
- **Not established.** Agreement of `Math` with a reference fdlibm 5.3 build (no reference build was available); `with_capacity(2^29)` and other limits that need billions of elements were only inspected.
