# `Std.Collections.LinkedList`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.LinkedList`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.LinkedList`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **LinkedList** · [`export let LinkedList = class<T> implements MutableList<T>, Deque<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L33)
  A doubly linked list that also supports queue and deque operations.
- **LinkedList.LinkedList** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L123)
  Returns the number of elements.
- **LinkedList.LinkedList** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L126)
  Returns true when there are no elements.
- **LinkedList.LinkedList** · [`export let get = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L130)
  Traps with "invalid argument: index out of range" for an index outside 0 through size() - 1.
- **LinkedList.LinkedList** · [`export let set = func(index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L134)
  Replaces the element at `index` without invalidating iterators. An invalid index traps with `invalid argument: index out of range`.
- **LinkedList.LinkedList** · [`export let first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L139)
  Returns the first element. Traps with `no such element` when empty.
- **LinkedList.LinkedList** · [`export let last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L147)
  Returns the last element. Traps with `no such element` when empty.
- **LinkedList.LinkedList** · [`export let first_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L155)
  Returns the first element as `Some`, or `None` when empty.
- **LinkedList.LinkedList** · [`export let last_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L163)
  Returns the last element as `Some`, or `None` when empty.
- **LinkedList.LinkedList** · [`export let peek = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L171)
  The first element, or None when empty.
- **LinkedList.LinkedList** · [`export let add = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L174)
  Appends `value` and returns true.
- **LinkedList.LinkedList** · [`export let add_last = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L180)
  Appends `value` at the back.
- **LinkedList.LinkedList** · [`export let enqueue = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L185)
  Appends `value` at the back.
- **LinkedList.LinkedList** · [`export let add_first = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L190)
  Inserts `value` at the front.
- **LinkedList.LinkedList** · [`export let add_at = func(index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L196)
  Inserts before `index` or appends if the index equals the size. Traps with "invalid argument: index out of range" otherwise.
- **LinkedList.LinkedList** · [`export let add_all = func(values: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L206)
  Appends each element in order, reading by position up to the starting length.
- **LinkedList.LinkedList** · [`export let remove_at = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L216)
  Unlinks and returns the element at `index`. Traps with `invalid argument: index out of range` for an invalid index.
- **LinkedList.LinkedList** · [`export let remove_first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L219)
  Removes and returns the first element. Traps with `no such element` when empty.
- **LinkedList.LinkedList** · [`export let remove_last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L227)
  Removes and returns the last element. Traps with `no such element` when empty.
- **LinkedList.LinkedList** · [`export let dequeue = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L235)
  Removes and returns the first element. Traps with `no such element` when empty.
- **LinkedList.LinkedList** · [`export let try_dequeue = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L238)
  Removes the first element and returns it as `Some`, or returns `None` when empty.
- **LinkedList.LinkedList** · [`export let swap = func(i: int, j: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L247)
  Exchanges the values in two nodes without invalidating iterators. An invalid index traps with `invalid argument: index out of range`.
- **LinkedList.LinkedList** · [`export let reverse = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L256)
  Reverses the element order in place without invalidating iterators.
- **LinkedList.LinkedList** · [`export let fill = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L271)
  Replaces each element with `value` without invalidating iterators.
- **LinkedList.LinkedList** · [`export let truncate = func(size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L282)
  Removes elements from the end until `size` remain. Traps with "invalid argument: negative size" or "invalid argument: size above current size".
- **LinkedList.LinkedList** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L296)
  Clears every node's links, so iterators that still hold a node keep nothing else reachable.
- **LinkedList.LinkedList** · [`export let slice = func(start: int, end: int) -> LinkedList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L313)
  A new LinkedList with the elements from `start` inclusive to `end` exclusive. Traps with "invalid argument: range" unless 0 <= start <= end <= size().
- **LinkedList.LinkedList** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L332)
  A new built-in list in order.
- **LinkedList.LinkedList** · [`export let iterator = func -> iterators.Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L343)
  Returns a fail-fast iterator from the first node to the last.
- **LinkedList.LinkedList** · [`export let sequence = func -> sequences.Sequence<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L347)
  A lazy pipeline over this list's fail-fast iterator (Stdlib.md section 9.12): a structural change while it runs traps.
- **LinkedList.LinkedList** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L351)
  Calls `action` in list order. The action leaves the list structure unchanged.
- **LinkedList.LinkedList** · [`export let remove_if = func(drop: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L366)
  Calls `drop` once per element in list order and unlinks the matches. The predicate leaves the list structure unchanged. The number of removed elements.
- **LinkedList.LinkedList** · [`export let retain_if = func(keep: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L371)
  Removes elements rejected by `keep` and returns the removal count. Calls the predicate once per element in list order. The predicate leaves the list structure unchanged.
- **LinkedList.LinkedList** · [`export let index_where = func(test: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L397)
  Returns the first index accepted by `test`, or -1 if no element matches. The predicate leaves the list structure unchanged.
- **LinkedList.LinkedList** · [`export let sort = func(c: func(T, T) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L417)
  Stably sorts a copy of the values and writes them back into the existing nodes. The comparator defines a total order and leaves the list structure unchanged.
- **LinkedList.LinkedList** · [`export let binary_search = func(value: T, c: func(T, T) -> ordering.Ordering) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L438)
  Searches a copy and returns the first comparator-equal index, or `-insertion_point - 1` when absent. The list is already sorted by `c`, which leaves the list structure unchanged.
- **LinkedList.LinkedList** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L450)
  Tests membership with `value.equals(element)`.
- **LinkedList.LinkedList** · [`export let index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L453)
  Returns the lowest index where `value.equals(element)` is true, or -1 when absent.
- **LinkedList.LinkedList** · [`export let last_index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L467)
  Returns the highest index where `value.equals(element)` is true, or -1 when absent.
- **LinkedList.LinkedList** · [`export let remove = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L481)
  Removes the first element equal to `value`.
- **LinkedList.LinkedList** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L495)
  Traps with "invalid state: ..." on a broken endpoint, reciprocal link, or count.
- **LinkedList.LinkedList** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L523)
  Another LinkedList with the same element type, size, and equal elements at each index.
- **LinkedList.LinkedList** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L543)
  Folds the element hashes in list order from 1, multiplying by 31 before each addition with 32-bit wraparound.
- **LinkedList.LinkedList** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L555)
  Formats the list as `[a, b, c]`. The list prints "(this collection)" where its own text reaches it again.
- **LinkedList** · [`export let create = func<T> -> LinkedList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L606)
  Creates an empty linked list.
- **LinkedList** · [`export let of = func<T>(items: T[list]) -> LinkedList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/linked-list.nra#L609)
  Copies `items`.
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

### 9.2 Equality of collections

All of this needs G3.

| Kind | Equal when | Hash | Text |
| --- | --- | --- | --- |
| List | Same class and size, with equal elements at each index | The list formula of [section 5.2](Stdlib-Design#52-equality-hashing-and-text) | `[a, b, c]` |
| Set | Same class and size, and this set contains each of the other's elements | The wrapping sum of the element hashes | `{a, b, c}` in iteration order |
| Map | Same class and size, and for each of the other's entries this map holds an equal value for the key | The wrapping sum over entries of key hash exclusive-or value hash | `{k: v, k2: v2}` in iteration order |
| `ArrayDeque`, `PriorityQueue`, `Sequence` | Identity | Identity | `[a, b, c]` in iteration order; `Sequence` prints `Sequence` |

`equals(other: class)` first tests whether `other` is an instance of the same collection type. It then downcasts and compares contents. Comparing an `ArrayList` with a `LinkedList` by content is the separate `Lists.same_elements` function over the list interface.

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
- **Equality direction.** Search members call `value.equals(element)` with the searched value as the receiver.
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
