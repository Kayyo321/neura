# `Std.Collections.BitSet`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.BitSet`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.BitSet`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BitSet** · [`export let BitSet = class implements MutableSet<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L59)
  A mutable set of non-negative integers that iterates in ascending order. Negative element arguments trap with `invalid argument: negative element`. Tagged `#NotThreadSafe`.
- **BitSet.BitSet** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L98)
  Returns the number of distinct elements. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L102)
  Returns true when there are no distinct elements. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let contains = func(value: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L106)
  Tests the bit representing `value`. A negative value traps with `invalid argument: negative element`. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let add = func(value: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L117)
  Adds the value and returns false if it was already present. Appends zero words as needed.
- **BitSet.BitSet** · [`export let add_all = func(items: int[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L132)
  Adds the members of `items`, ignoring duplicates. A negative item traps with `invalid argument: negative element`.
- **BitSet.BitSet** · [`export let remove = func(value: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L142)
  Removes the value and returns false if it was absent.
- **BitSet.BitSet** · [`export let flip = func(value: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L156)
  Adds the value when absent and removes it when present.
- **BitSet.BitSet** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L171)
  Removes all members and releases the stored words.
- **BitSet.BitSet** · [`export let next_set = func(from: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L180)
  The least member at or after `from`, or -1. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let to_list = func -> int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L187)
  A new built-in list of the members in ascending order. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let iterator = func -> iterators.Iterator<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L202)
  Returns a fail-fast iterator over the members in ascending order. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let for_each = func(action: func(int) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L205)
  Ascending. The action must not change the set structurally.
- **BitSet.BitSet** · [`export let remove_if = func(drop: func(int) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L218)
  Calls `drop` once per member in ascending order and removes the matches. The predicate leaves the set structure unchanged. The number of removed members.
- **BitSet.BitSet** · [`export let retain_if = func(keep: func(int) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L223)
  Calls `keep` once per member in ascending order, removing rejected members. The predicate leaves the set structure unchanged. The number of removed members.
- **BitSet.BitSet** · [`export let is_subset_of = func(other: BitSet) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L246)
  Reports whether every member is also present in `other`. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let is_disjoint_from = func(other: BitSet) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L259)
  Reports whether the two sets have no members in common. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let union_with = func(other: BitSet)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L271)
  In place: adds the other's members.
- **BitSet.BitSet** · [`export let intersect_with = func(other: BitSet)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L287)
  In place: keeps only members the other also has.
- **BitSet.BitSet** · [`export let subtract = func(other: BitSet)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L301)
  In place: removes the other's members.
- **BitSet.BitSet** · [`export let symmetric_difference_with = func(other: BitSet)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L315)
  In place: keeps the members in exactly one of the two sets.
- **BitSet.BitSet** · [`export let union = func(other: BitSet) -> BitSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L345)
  A new set of the members of either set. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let intersection = func(other: BitSet) -> BitSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L353)
  A new set of the members of both sets. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let difference = func(other: BitSet) -> BitSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L361)
  A new set of this set's members that the other lacks. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L369)
  Tests for another BitSet with the same members, ignoring trailing zero words. Tagged `#Pure`.
- **BitSet.BitSet** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L393)
  The wrapping sum of the members' hashes (each member's own value). Tagged `#Pure`.
- **BitSet.BitSet** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L403)
  `{a, b, c}` in ascending order. Tagged `#Pure`.
- **BitSet** · [`export let create = func -> BitSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L465)
  Creates an empty set. Tagged `#Pure`, `#ThreadSafe`.
- **BitSet** · [`export let of = func(items: int[list]) -> BitSet`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/bit-set.nra#L469)
  A set of the values in `items`. Tagged `#Pure`, `#ThreadSafe`.
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
- **Self-containment.** Text prints `(this collection)` where a collection's own text reaches it again, directly or through other collections. Each collection keeps the flag for this in the instance, so text conversion reads no module state and is task-safe ([Concurrency](Concurrency#52-task-safe-calls)).
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the searched value as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.
