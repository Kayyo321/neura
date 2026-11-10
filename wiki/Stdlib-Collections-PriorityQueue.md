# `Std.Collections.PriorityQueue`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.PriorityQueue`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.PriorityQueue`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PriorityQueue** · [`export let PriorityQueue = class<T> implements Queue<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L14)
- **PriorityQueue.PriorityQueue** · [`export let init = func(items: T[list], c: func(T, T) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L24)
  Copies `items` and heapifies them: sifts down each index from size / 2 - 1 to 0.
- **PriorityQueue.PriorityQueue** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L91)
- **PriorityQueue.PriorityQueue** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L93)
- **PriorityQueue.PriorityQueue** · [`export let enqueue = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L95)
- **PriorityQueue.PriorityQueue** · [`export let dequeue = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L104)
  Removes and returns a least element. Traps with "no such element" when empty.
- **PriorityQueue.PriorityQueue** · [`export let peek = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L121)
  A least element, or None when empty.
- **PriorityQueue.PriorityQueue** · [`export let try_dequeue = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L128)
- **PriorityQueue.PriorityQueue** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L135)
- **PriorityQueue.PriorityQueue** · [`export let drain_sorted = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L142)
  Dequeues everything into a new built-in list, least first.
- **PriorityQueue.PriorityQueue** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L151)
  By equals, with `value` as the receiver.
- **PriorityQueue.PriorityQueue** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L161)
  A new built-in list in heap order, which is not sorted.
- **PriorityQueue.PriorityQueue** · [`export let iterator = func -> iterators.Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L170)
  Heap order; fail-fast.
- **PriorityQueue.PriorityQueue** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L174)
  Heap order. The action must not change the queue structurally.
- **PriorityQueue.PriorityQueue** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L188)
  Traps with "invalid state: heap order" when an element is Less than its parent.
- **PriorityQueue.PriorityQueue** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L201)
  `[a, b, c]` in heap order; the queue itself as a direct element prints "(this collection)".
- **PriorityQueue** · [`export let create = func<T>(c: func(T, T) -> ordering.Ordering) -> PriorityQueue<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L249)
  An empty queue ordered by `c`.
- **PriorityQueue** · [`export let from_list = func<T>(items: T[list], c: func(T, T) -> ordering.Ordering) -> PriorityQueue<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/priority-queue.nra#L255)
  A queue of a copy of `items`, heapified in linear time.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

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

### 9.8 `PriorityQueue<T>`

A binary min-heap following the queue contract: `dequeue` returns a least element under the comparator given at construction. Needs G1.

- **Layout.** A built-in list in heap order (children of index `i` at `2i + 1` and `2i + 2`), the comparator, and the modification count.
- **Sift up:** while the item is not at the root and the comparator says it is `Less` than its parent, move the parent down.
- **Sift down:** pick the left child, or the right child when it exists and is `Less` than the left; if the child is `Less` than the item, move it up and continue; otherwise stop.
- `dequeue` takes the root, moves the last element to the root, removes the last slot, and sifts down.
- The module function `from_list(items, comparator)` appends everything and then sifts down each index from `size / 2 - 1` to 0.
- Iteration is in heap order, which is not sorted. `drain_sorted()` dequeues everything into a built-in list.

These rules fix which of several equal elements comes out first; they are not first-in first-out among equals.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` for a collection that directly contains itself, through a shared depth counter (`Std.Collections.Internal.TextDepth`); deeper cycles still end with the frame-limit fault.
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the probe as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.
