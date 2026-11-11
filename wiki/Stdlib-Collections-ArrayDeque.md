# `Std.Collections.ArrayDeque`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.ArrayDeque`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.ArrayDeque`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ArrayDeque** · [`export let ArrayDeque = class<T> implements Deque<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L11)
  A double-ended queue with constant-time indexing and amortized constant-time end operations.
- **ArrayDeque.ArrayDeque** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L28)
  Returns the number of elements.
- **ArrayDeque.ArrayDeque** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L31)
  Returns true when there are no elements.
- **ArrayDeque.ArrayDeque** · [`export let get = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L34)
  Returns the element at zero-based `index` in constant time. An invalid index causes a bounds trap.
- **ArrayDeque.ArrayDeque** · [`export let add_first = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L40)
  Inserts `value` at the front.
- **ArrayDeque.ArrayDeque** · [`export let add_last = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L46)
  Appends `value` at the back.
- **ArrayDeque.ArrayDeque** · [`export let enqueue = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L52)
  Appends `value` at the back.
- **ArrayDeque.ArrayDeque** · [`export let remove_first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L97)
  Removes and returns the first element. Traps with `no such element` when empty.
- **ArrayDeque.ArrayDeque** · [`export let remove_last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L112)
  Removes and returns the last element. Traps with `no such element` when empty.
- **ArrayDeque.ArrayDeque** · [`export let dequeue = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L127)
  Removes and returns the first element. Traps with `no such element` when empty.
- **ArrayDeque.ArrayDeque** · [`export let first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L130)
  Returns the first element. Traps with `no such element` when empty.
- **ArrayDeque.ArrayDeque** · [`export let last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L141)
  Returns the last element. Traps with `no such element` when empty.
- **ArrayDeque.ArrayDeque** · [`export let peek = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L152)
  The first element, or None when empty.
- **ArrayDeque.ArrayDeque** · [`export let try_dequeue = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L163)
  Removes the first element and returns it as `Some`, or returns `None` when empty.
- **ArrayDeque.ArrayDeque** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L171)
  Removes all elements from both halves of the deque.
- **ArrayDeque.ArrayDeque** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L180)
  Tests membership with `value.equals(element)`.
- **ArrayDeque.ArrayDeque** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L195)
  A new built-in list from first to last.
- **ArrayDeque.ArrayDeque** · [`export let iterator = func -> iterators.Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L209)
  Returns a fail-fast iterator from the front to the back.
- **ArrayDeque.ArrayDeque** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L214)
  Calls `action` from the front to the back. The action leaves the deque structure unchanged.
- **ArrayDeque.ArrayDeque** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L228)
  Traps with "invalid state: ..." when iteration and indexing disagree or the size is inconsistent.
- **ArrayDeque.ArrayDeque** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L248)
  Formats elements as `[a, b, c]` from first to last. The deque prints "(this collection)" where its own text reaches it again.
- **ArrayDeque** · [`export let create = func<T> -> ArrayDeque<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L308)
  Creates an empty deque.
- **ArrayDeque** · [`export let of = func<T>(items: T[list]) -> ArrayDeque<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/array-deque.nra#L311)
  Creates a deque from a shallow copy of `items`, in list order.
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

### 9.4 `ArrayDeque<T>`

**Redesign.** The first plan used a ring buffer in an array with vacant slots. Without vacant slots a ring would keep a reference to every removed element until its slot was reused. The deque is instead two built-in lists used as stacks.

- **Layout.** `front` holds the first part of the deque in reverse order, so that its last element is the deque's first. `back` holds the rest in order. Plus the modification count.
- `add_first` appends to `front`; `add_last` appends to `back`. `remove_first` removes the last element of `front`; `remove_last` removes the last element of `back`. Each is one storage operation, and the removed slot is gone, so nothing is retained.
- **Rebalancing.** When `remove_first` finds `front` empty and `back` not empty, it moves the first half of `back` (rounded up) into `front` in reverse order and shifts the rest of `back` down, in a helper function. `remove_last` on an empty `back` mirrors it. A rebalance of `n` elements is followed by at least `n / 2` cheap removals from that end, so every end operation is amortized constant time. A3 would make the move two bulk operations.
- **Indexing.** `get(i)` reads `front[front.length - 1 - i]` when `i` is less than `front.length`, and `back[i - front.length]` otherwise. Constant time.
- **Iteration** is first to last: `front` from its end to its start, then `back`.

### 9.5 `LinkedList<T>`

Nodes are separately allocated and reclaimed by the NVM. Missing neighbors are ordinary nil references.

- **Layout.** An unexported generic class `Node<T>` holds `value`, `next`, and `previous`. The list holds `head`, `tail`, `count`, and the modification count. An empty list has two nil endpoints. The head has no previous node, and the tail has no next node. Every allocated node has an initialized value.
- **Insertion** sets both neighbors of a new node and updates each adjacent link or the corresponding endpoint. **Removal** reconnects each existing neighbor or updates the endpoint. Clear the removed node's links so that an iterator holding the removed node does not retain the remaining chain.
- **Positional access** walks from the first element when the index is in the first half and from the last otherwise.
- **Why nodes and not an arena.** Each element costs one small object either way. The arena saved allocations only by reusing slots through a free list, and it paid for that with array growth copies, a free list to maintain, and vacant slots the language does not have.

It follows both the list and the deque contracts. Prefer `ArrayList` or `ArrayDeque` unless elements are inserted or removed in the middle while iterating by position.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` where a collection's own text reaches it again, directly or through other collections. Each collection keeps the flag for this in the instance, so text conversion reads no module state and is task-safe ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)).
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the probe as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.
