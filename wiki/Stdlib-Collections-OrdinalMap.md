# `Std.Collections.OrdinalMap`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.OrdinalMap`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.OrdinalMap`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **OrdinalMap** · [`export let OrdinalMap = class<V> implements MutableMap<int, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L14)
  A mutable map for a fixed range of integer keys. A key outside that range causes a bounds trap.
- **OrdinalMap.OrdinalMap** · [`export let init = func(limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L28)
  An empty map for the keys 0 through `limit` - 1. Traps with "invalid argument: negative size".
- **OrdinalMap.OrdinalMap** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L78)
  Returns the number of entries.
- **OrdinalMap.OrdinalMap** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L81)
  Returns true when there are no entries.
- **OrdinalMap.OrdinalMap** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L84)
  Removes all entries while preserving the permitted key range. Invalidates active iterators.
- **OrdinalMap.OrdinalMap** · [`export let contains_key = func(key: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L96)
  Reports whether an entry exists for `key`.
- **OrdinalMap.OrdinalMap** · [`export let get = func(key: int) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L99)
  Returns the associated value as `Some`, or `None` when the key is absent.
- **OrdinalMap.OrdinalMap** · [`export let get_or = func(key: int, fallback: V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L108)
  Returns the associated value, or `fallback` when the key is absent.
- **OrdinalMap.OrdinalMap** · [`export let require = func(key: int) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L117)
  Returns the associated value. Traps with `no such element` when the key is absent.
- **OrdinalMap.OrdinalMap** · [`export let set = func(key: int, value: V)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L126)
  Stores the value for `key`. Replacing an existing value preserves the key and iterator validity.
- **OrdinalMap.OrdinalMap** · [`export let delete = func(key: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L136)
  Removes the entry for `key`. Returns true if an entry was present.
- **OrdinalMap.OrdinalMap** · [`export let put = func(key: int, value: V) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L146)
  Stores the value for `key` and returns its previous value as `Some`, or `None` for a new key.
- **OrdinalMap.OrdinalMap** · [`export let remove = func(key: int) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L158)
  Removes the entry for `key` and returns its value as `Some`, or `None` when absent.
- **OrdinalMap.OrdinalMap** · [`export let keys = func -> int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L169)
  Copies the keys to a new built-in list in ascending key order.
- **OrdinalMap.OrdinalMap** · [`export let values = func -> V[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L183)
  Copies the values to a new built-in list in ascending key order.
- **OrdinalMap.OrdinalMap** · [`export let entries = func -> [int, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L198)
  Copies the `[key, value]` tuples to a new built-in list in ascending key order.
- **OrdinalMap.OrdinalMap** · [`export let iterator = func -> iterators.Iterator<[int, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L213)
  Returns a fail-fast iterator over `[key, value]` tuples in ascending key order.
- **OrdinalMap.OrdinalMap** · [`export let put_all = func(other: OrdinalMap<V>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L217)
  Sets every entry from `other` in ascending key order. A source key at or beyond this map's construction limit causes a bounds trap.
- **OrdinalMap.OrdinalMap** · [`export let contains_value = func(value: V) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L224)
  Tests whether `value.equals(held_value)` is true for any entry.
- **OrdinalMap.OrdinalMap** · [`export let for_each = func(action: func(int, V) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L235)
  Calls `action` with the key and value of each entry in ascending key order. The action leaves the map structure unchanged.
- **OrdinalMap.OrdinalMap** · [`export let remove_if = func(drop: func(int, V) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L252)
  Calls `drop` once per entry in ascending key order and removes each match as it goes. The predicate leaves the map structure unchanged. The number of removed entries.
- **OrdinalMap.OrdinalMap** · [`export let get_or_put = func(key: int, make: func() -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L275)
  Returns the associated value, or calls `make` once, stores its result, and returns it when absent. The factory leaves the map structure unchanged.
- **OrdinalMap.OrdinalMap** · [`export let merge = func(key: int, value: V, combine: func(V, V) -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L289)
  Stores and returns `value` when the key is absent. Otherwise stores and returns `combine(old, value)`. The combiner leaves the map structure unchanged.
- **OrdinalMap.OrdinalMap** · [`export let compute = func(key: int, f: func(int, optional.Optional<V>) -> optional.Optional<V>) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L305)
  Calls `f` with the key and its optional current value. Stores a `Some` result or removes the entry for `None`. The callback leaves the map structure unchanged. The result of the callback.
- **OrdinalMap.OrdinalMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L321)
  Reports whether `other` is an OrdinalMap with the same value type and equal associations. The permitted key limits may differ.
- **OrdinalMap.OrdinalMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L344)
  The wrapping sum over entries of the key's hash exclusive-or the value's hash.
- **OrdinalMap.OrdinalMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L358)
  Formats entries as `{k: v, k2: v2}` in key order. The map prints "(this collection)" where its own text reaches it again.
- **OrdinalMap** · [`export let create = func<V>(size: int) -> OrdinalMap<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/ordinal-map.nra#L437)
  An empty map for the keys 0 through `size` - 1. Traps with "invalid argument: negative size".
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

### 9.1 Contracts

This section fixes the member sets that the concrete classes and the interfaces in [section 9.10](Stdlib-Design#910-interfaces-and-read-only-views) share.

The **Needs** column names the compiler facilities of [section 3](Stdlib-Design#3-language-facilities) that a member uses.

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

### 9.9 `BitSet` and `OrdinalMap<V>`


- **`BitSet`** is a set of non-negative `int` values following the set contract. Layout: a built-in `uint32` list of words (bit `i` is in word `i / 32` at position `i % 32`) and a cached `count`. A negative element traps. Setting a bit beyond the last word appends zero words. Besides the contract it has `flip(i)`, the in-place `union_with`, `intersect_with`, `subtract`, and `symmetric_difference_with`, and `next_set(from) → int` (the next member at or after `from`, or -1). Every operation is a mask, a shift by 0 through 31, or a comparison on `uint32`; nothing wraps. Iteration is ascending. Equality is set equality, so trailing zero words do not matter.
- **`OrdinalMap<V>`** is a map from the integers 0 through `n - 1` to values, with `n` fixed at construction. Layout: an `int` list `position` of length `n` (the key's place in the dense lists, or -1), a dense `int` list of keys, and a dense list of values. `set` appends to the dense lists; `delete` moves the last dense entry into the removed place and removes the last slot, so nothing is retained. Every operation is constant time. Iteration is in key order, by scanning `position`.

**Enum keys.** There is no `EnumSet<E>` or `EnumMap<E, V>`. A fieldless enum already converts to its ordinal with `as`, so a set of variants is a `BitSet` of ordinals and a map keyed by variants is an `OrdinalMap<V>` sized by the caller with the number of variants. The caller converts at the call. Going back from an ordinal to a variant is a `match`-style function the enum's author writes. This applies to fieldless enums only: a payload enum has no ordinal conversion, and nothing here constructs one.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` where a collection's own text reaches it again, directly or through other collections. Each collection keeps the flag for this in the instance, so text conversion reads no module state and is task-safe ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)).
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the searched value as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.
