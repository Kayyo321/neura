# `Std.Collections.Sequence`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.Sequence`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.Sequence`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Sequence** · [`export let Sequence = class<T> implements Iterable<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L18)
  A lazy traversal that can be handed to a downstream operation only once.
- **Sequence.Sequence** · [`export let init = func(more: func() -> bool, pull: func() -> T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L29)
  Constructs a lazy sequence from an availability check and an element pull.
- **Sequence.Sequence** · [`export let iterator = func -> Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L36)
  Uses the sequence up and returns an iterator over its elements, so that for-each can visit a sequence once.
- **Sequence.Sequence** · [`export let map = func<U>(f: func(T) -> U) -> Sequence<U>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L42)
  Returns a lazy sequence applying `f` once to each element as it is pulled.
- **Sequence.Sequence** · [`export let filter = func(keep: func(T) -> bool) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L51)
  The elements that pass `keep`.
- **Sequence.Sequence** · [`export let flat_map = func<U>(f: func(T) -> U[list]) -> Sequence<U>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L69)
  The elements of each list `f` returns, in order.
- **Sequence.Sequence** · [`export let take = func(n: int) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L93)
  Returns a sequence of at most the first `n` elements and stops pulling after the last. Traps with "invalid argument: negative count" for a negative `n`.
- **Sequence.Sequence** · [`export let skip = func(n: int) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L114)
  All but the first `n` elements. Traps with "invalid argument: negative count" for a negative `n`.
- **Sequence.Sequence** · [`export let take_while = func(test: func(T) -> bool) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L142)
  Returns a sequence of leading elements accepted by `test` up to the first rejection.
- **Sequence.Sequence** · [`export let skip_while = func(test: func(T) -> bool) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L165)
  The elements from the first that fails `test` on.
- **Sequence.Sequence** · [`export let enumerate = func -> Sequence<[int, T]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L192)
  Each element with its position from 0.
- **Sequence.Sequence** · [`export let zip = func<U>(other: Sequence<U>) -> Sequence<[T, U]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L206)
  Returns pairs of corresponding elements until either sequence ends. Uses `other` up as well.
- **Sequence.Sequence** · [`export let chain = func(other: Sequence<T>) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L223)
  This sequence's elements, then those of `other`. Uses `other` up as well.
- **Sequence.Sequence** · [`export let peek = func(action: func(T) -> void) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L240)
  Calls `action` with each element as it passes.
- **Sequence.Sequence** · [`export let chunked = func(n: int) -> Sequence<T[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L252)
  Returns successive new lists of `n` elements, with a shorter final list when needed. Traps with "invalid argument: chunk size" when `n` is below 1.
- **Sequence.Sequence** · [`export let windowed = func(n: int) -> Sequence<T[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L276)
  Every run of `n` consecutive elements, each a new list, advancing by one. There are no windows when there are fewer than `n` elements. Traps with "invalid argument: window size" when `n` is below 1.
- **Sequence.Sequence** · [`export let sorted = func(c: func(T, T) -> ordering.Ordering) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L313)
  Returns a sequence that buffers and stably sorts the entire input on first demand. The comparator defines a total order.
- **Sequence.Sequence** · [`export let reversed = func -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L333)
  Returns a sequence that buffers the entire input on first demand, then emits it in reverse order.
- **Sequence.Sequence** · [`export let distinct = func -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L361)
  Returns a sequence keeping only the first occurrence under the elements' equality and hash.
- **Sequence.Sequence** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L380)
  Consumes the sequence into a new built-in list in traversal order.
- **Sequence.Sequence** · [`export let to_array_list = func -> array_list.ArrayList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L390)
  Consumes the sequence into a new ArrayList in traversal order.
- **Sequence.Sequence** · [`export let partition = func(test: func(T) -> bool) -> [array_list.ArrayList<T>, array_list.ArrayList<T>]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L393)
  Consumes the sequence into two ArrayLists in input order. The first holds matches and the second holds nonmatches.
- **Sequence.Sequence** · [`export let fold = func<A>(initial: A, f: func(A, T) -> A) -> A`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L410)
  Consumes the sequence by applying `f` to the accumulator and each successive element. Returns `initial` when empty.
- **Sequence.Sequence** · [`export let count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L420)
  Consumes the sequence and returns the number of elements. Count overflow traps with reason 1.
- **Sequence.Sequence** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L431)
  Consumes the sequence and calls `action` on each element in order.
- **Sequence.Sequence** · [`export let any = func(test: func(T) -> bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L439)
  Returns true at the first element accepted by `test`, or false at exhaustion.
- **Sequence.Sequence** · [`export let all = func(test: func(T) -> bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L450)
  Returns false at the first element rejected by `test`, or true at exhaustion.
- **Sequence.Sequence** · [`export let none = func(test: func(T) -> bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L461)
  Returns false at the first element accepted by `test`, or true at exhaustion.
- **Sequence.Sequence** · [`export let reduce = func(f: func(T, T) -> T) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L472)
  Combines elements from left to right, starting with the first. Returns `None` when empty.
- **Sequence.Sequence** · [`export let first = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L485)
  Returns the first element as `Some`, or `None` when empty. Pulls at most one element.
- **Sequence.Sequence** · [`export let last = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L494)
  Consumes all elements and returns the last as `Some`, or `None` when empty.
- **Sequence.Sequence** · [`export let find = func(test: func(T) -> bool) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L507)
  Returns the first element accepted by `test` as `Some`, or `None` at exhaustion.
- **Sequence.Sequence** · [`export let min = func(c: func(T, T) -> ordering.Ordering) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L519)
  Returns the first of the least elements according to `c`, or `None` when empty.
- **Sequence.Sequence** · [`export let max = func(c: func(T, T) -> ordering.Ordering) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L523)
  Returns the first of the greatest elements according to `c`, or `None` when empty.
- **Sequence.Sequence** · [`export let to_set = func -> hash_set.HashSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L528)
  A HashSet of the elements in first-occurrence order, by equals and hash_code.
- **Sequence.Sequence** · [`export let to_map = func<K, V>(key: func(T) -> K, value: func(T) -> V) -> hash_map.HashMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L540)
  A HashMap from key(element) to value(element), by the keys' equals and hash_code. Traps with "invalid argument: duplicate key" when two elements give equal keys.
- **Sequence.Sequence** · [`export let group_by = func<K>(key: func(T) -> K) -> hash_map.HashMap<K, array_list.ArrayList<T>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L556)
  The elements grouped by key(element), groups in first-seen key order and each group in sequence order.
- **Sequence.Sequence** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L568)
  Returns `Sequence` without claiming or evaluating the pipeline.
- **Sequence** · [`export let from_list = func<T>(items: T[list]) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L658)
  The list's elements in order. It fails fast with "collection modified during iteration" when the list changes structurally, as the built-in for-each does.
- **Sequence** · [`export let from_iterator = func<T>(items: Iterator<T>) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L679)
  The remaining elements of an iterator, which keeps its own modification checks.
- **Sequence** · [`export let empty = func<T> -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L688)
  Creates a sequence with no elements.
- **Sequence** · [`export let generate = func<T>(make: func() -> T) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L697)
  An infinite sequence of make(), make(), ...
- **Sequence** · [`export let iterate = func<T>(seed: T, step: func(T) -> T) -> Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L705)
  Returns an infinite sequence starting with `seed` and repeatedly applying `step`. The step runs only when the next element is pulled.
- **Sequence** · [`export let range = func(start: int, end: int) -> Sequence<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L722)
  Returns the integers in ascending order from `start` inclusive to `end` exclusive. The sequence is empty when `end` is at or below `start`.
- **Sequence** · [`export let join = func(s: Sequence<string>, separator: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L736)
  The strings with `separator` between consecutive ones.
- **Sequence** · [`export let sum_int32 = func(s: Sequence<int>) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L752)
  Consumes the sequence by adding from zero in order. Overflow traps with reason 1.
- **Sequence** · [`export let sum_int64 = func(s: Sequence<int64>) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L762)
  Consumes the sequence by adding from zero in order. Overflow traps with reason 1.
- **Sequence** · [`export let sum_float64 = func(s: Sequence<float64>) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sequence.nra#L772)
  Consumes the sequence by adding left to right starting from the first element. Returns 0.0 when empty.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

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

**Decision: the library does not re-check what the language already checks.** An index outside a built-in list, an overflow, a zero divisor, a shift count out of range, and an out-of-range conversion trap in the language with their own reasons (1 and 2) and locations. A library member that would fail the same way lets the built-in operation trap and documents that it does. `ArrayList.get` with a bad index is a bounds trap from the list read, not a library trap.

**What the absence of unwinding changes.** The first plan assumed panics that unwind, deferred actions, and recovery. None exists, and this plan asks for none.

| First plan's promise | Now |
| --- | --- |
| `Closeables.use` closes the resource even when the body panics | Replaced by the scoped helpers of [section 5.5](Stdlib-Design#55-resources), which close the resource on every ordinary completion. A body cannot fail and return; it either returns or the run ends, and when a run ends, by return or by trap, the host releases every file and socket. |
| "If the predicate of `remove_if` panics, the collection is left valid" | Removed. A trap inside a callback ends the run, so no state can be observed afterwards. No collection keeps recovery code. |
| `Iterator.next` panics with `NoSuchElementError`; modification panics with `ConcurrentModificationError` | Both are traps with the messages above. The error classes for programmer mistakes are gone. |
| `assert_panics` recovers a panic and returns its error | Removed. A test that expects a trap is a program with an expected-diagnostic file, run by the test harness as the existing trap tests under `tests/backend/traps` are ([section 17](Stdlib-Design#17-verification)). |
| A fallible callback | A callback that can fail returns a `Result`, and the member that calls it documents how it stops. |

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

### 18.9 Hash collections, trees, and sequences

- **Compaction rule 1** also compacts when live entries plus tombstones fill `E`. Trailing releases leave tombstones behind, so without this addition the index could fill and a lookup for a missing key would not terminate.
- **Modification counts are `uint64`**, as [section 5.4](Stdlib-Design#54-iteration) states; compaction and `trim()` advance them, so an iterator traps after either. `clear()` returns to the table size chosen at construction.
- **Bulk removal.** `remove_if` and `retain_if` call the callback on every entry, then release the chosen entries and compact at most once. `compute` returns the result of its callback. A `with_keys` collection hashes with its stored function, so `hash_code` agrees with its equality.
- **IntMap and StringMap** are generated from `hash-map.nra` with the key operations called directly (`tests/stdlib/hash-map/generators/gen_maps.py`); `StringMap` uses the string's built-in `hash_code` and `==`, which equal `Strings.hash` and `Strings.equals`, because `Std.Text` is above `Std.Collections` in the dependency order.
- **Extra members.** `with_keys_and_capacity`; `trim` and `validate` on every hash collection; `TreeMap.key_iterator`, `key_range`, and `descending_keys`, which `TreeSet` uses to avoid a tuple per element; `Sequence.from_iterator`, a public `Sequence` constructor from two functions, and `Sequence.iterator()` so that a sequence works once in for-each. Constructors of these collections take a module-private token, so only their module functions build them.
- **Sequence choices.** `range(from, to)` on a sorted collection is empty when `to` is not above `from`. `windowed` emits only full windows. `zip` and `chain` mark the other sequence used. `from_list` fails fast on a structural change to its list. `sum_float64` starts from the first element, so `[-0.0]` sums to `-0.0`, and an empty sequence sums to `0.0`. Maps and sets have no `sequence()` member; `ArrayList` and `LinkedList` build theirs with `from_iterator`.
- **Trap details.** `duplicate key`, `negative count`, `chunk size`, and `window size` after `invalid argument: `; `invalid state: hash table …` and `invalid state: tree …` for a failed `validate()`.
