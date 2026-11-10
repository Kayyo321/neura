# `Std.Collections.TreeSet`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.TreeSet`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.TreeSet`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TreeSet** · [`export let TreeSet = class<T> implements MutableSet<T>, SortedSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L20)
  A mutable set that identifies and orders elements using its comparator.
- **TreeSet.TreeSet** · [`export let init = func(seal: Seal, comparator: func(T, T) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L29)
  Creates an empty backing tree with the comparator supplied by the factory.
- **TreeSet.TreeSet** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L35)
  Returns the number of distinct elements.
- **TreeSet.TreeSet** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L38)
  Returns true when there are no distinct elements.
- **TreeSet.TreeSet** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L41)
  Removes all elements and invalidates iterators.
- **TreeSet.TreeSet** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L46)
  Reports whether an element compares equal to `value`.
- **TreeSet.TreeSet** · [`export let add = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L50)
  False when the set already holds an element the comparator calls Equal. The set keeps the element it holds.
- **TreeSet.TreeSet** · [`export let add_all = func(items: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L57)
  Adds each element in order.
- **TreeSet.TreeSet** · [`export let remove = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L67)
  Removes an element that compares equal to `value`. Returns false when absent.
- **TreeSet.TreeSet** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L70)
  A new built-in list in ascending order.
- **TreeSet.TreeSet** · [`export let iterator = func -> Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L73)
  Returns a fail-fast iterator over the elements in ascending order.
- **TreeSet.TreeSet** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L77)
  Calls `action` in ascending comparator order. The action leaves the set structure unchanged.
- **TreeSet.TreeSet** · [`export let remove_if = func(drop: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L86)
  Calls `drop` once per element in ascending order, then removes the matches. The predicate leaves the set structure unchanged. The number of removed elements.
- **TreeSet.TreeSet** · [`export let retain_if = func(keep: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L95)
  Calls `keep` once per element in ascending order, then removes rejected elements. The predicate leaves the set structure unchanged. The number of removed elements.
- **TreeSet.TreeSet** · [`export let first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L102)
  Returns the least element. Traps with `no such element` when empty.
- **TreeSet.TreeSet** · [`export let last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L108)
  Returns the greatest element. Traps with `no such element` when empty.
- **TreeSet.TreeSet** · [`export let remove_first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L115)
  Removes and returns the least element. Traps with "no such element" when empty.
- **TreeSet.TreeSet** · [`export let remove_last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L121)
  Removes and returns the greatest element. Traps with `no such element` when empty.
- **TreeSet.TreeSet** · [`export let range = func(from: T, to: T) -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L128)
  Returns a new list in ascending order from `from` inclusive to `to` exclusive. Returns an empty list when `to` is at or below `from`.
- **TreeSet.TreeSet** · [`export let descending = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L131)
  Copies the elements into a new list in descending order.
- **TreeSet.TreeSet** · [`export let floor = func(value: T) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L134)
  Returns the greatest element at most `value` as `Some`, or `None` when absent.
- **TreeSet.TreeSet** · [`export let ceiling = func(value: T) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L137)
  Returns the least element at least `value` as `Some`, or `None` when absent.
- **TreeSet.TreeSet** · [`export let lower = func(value: T) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L140)
  Returns the greatest element strictly below `value` as `Some`, or `None` when absent.
- **TreeSet.TreeSet** · [`export let higher = func(value: T) -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L143)
  Returns the least element strictly above `value` as `Some`, or `None` when absent.
- **TreeSet.TreeSet** · [`export let is_subset_of = func(other: TreeSet<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L147)
  Whether every element of this set is in `other`, by the other set's comparator.
- **TreeSet.TreeSet** · [`export let is_disjoint_from = func(other: TreeSet<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L164)
  Reports whether none of this set's elements pass `other`'s membership test.
- **TreeSet.TreeSet** · [`export let union = func(other: TreeSet<T>) -> TreeSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L178)
  A new set with this set's comparator holding the elements of both.
- **TreeSet.TreeSet** · [`export let intersection = func(other: TreeSet<T>) -> TreeSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L186)
  This set's elements that are in `other`.
- **TreeSet.TreeSet** · [`export let difference = func(other: TreeSet<T>) -> TreeSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L189)
  This set's elements that are not in `other`.
- **TreeSet.TreeSet** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L192)
  Traps with "invalid state: tree ..." on a broken AVL invariant.
- **TreeSet.TreeSet** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L198)
  Another TreeSet of the same size whose elements this set all contains.
- **TreeSet.TreeSet** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L219)
  The wrapping sum of the element hashes.
- **TreeSet.TreeSet** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L233)
  Formats elements as `{a, b, c}` in ascending order. The set prints "(this collection)" where its own text reaches it again.
- **TreeSet** · [`export let create = func<T>(comparator: func(T, T) -> ordering.Ordering) -> TreeSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-set.nra#L260)
  An empty set ordered by `comparator`, which must define a total order.
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

### 9.7 `TreeMap<K, V>` and `TreeSet<T>`

Sorted collections ordered by a comparator given at construction. They need G1, because the comparator's type mentions `K`. **Decision:** a comparator is always required; for a built-in type pass its `compare` function. Two keys are the same key when the comparator returns `Equal`; `equals` is not consulted.

**Decision kept:** the balanced tree is an **AVL tree**. Lookups dominate, each comparison is a call through a function value, and an AVL tree is at most about 1.44 log2 of the size tall against about 2 log2 for a red-black tree.

Nodes are separately allocated. Missing child and parent links use `nil`.

- **Layout.** An unexported class `Node<K, V>` has `key`, `value`, `left`, `right`, `parent`, and `height` (`int`). Every node has a key and value. The tree stores a nullable `root`, `count`, its comparator, and the modification count. The root's parent is nil. A height helper returns 0 for nil without dereferencing it; a leaf has height 1.
- **Lookup** descends from the root calling the comparator with the probe key first: `Less` goes left, `Greater` goes right, `Equal` stops, and reaching `nil` means absent.
- **Insertion** descends the same way. On `Equal` it overwrites the value (not structural). Otherwise it attaches a new node, then walks up through `parent`: recompute each ancestor's height; if its children's heights differ by 2, rebalance with a single rotation when the taller child leans the same way and a double rotation when it leans the other way; stop when an ancestor's height is unchanged.
- **Removal.** A node with two children takes the key and value of its in-order successor, and the successor is unlinked instead. A node with at most one child is replaced by that child. Then walk up, recomputing heights and rebalancing, until a height is unchanged. The unlinked node becomes unreachable and is reclaimed; there is no free list and no slot to clear.
- **Rotations** update the child links and the parent link of each moved non-nil node. If the rotated subtree had no parent, update `root`. Never read or write a field on nil.
- **Iteration** is ascending: start at the leftmost node and step to the in-order successor through `right` and `parent`, with no stack.
- `TreeSet<T>` is a class holding a `TreeMap<T, Unit>`. A separate node class without a value would save one cell per element and duplicate the balancing code.

Lookup, insertion, and removal take logarithmic time.

### 18.9 Hash collections, trees, and sequences

- **Compaction rule 1** also compacts when live entries plus tombstones fill `E`. Trailing releases leave tombstones behind, so without this addition the index could fill and a lookup for a missing key would not terminate.
- **Modification counts are `uint64`**, as [section 5.4](Stdlib-Design#54-iteration) states; compaction and `trim()` advance them, so an iterator traps after either. `clear()` returns to the table size chosen at construction.
- **Bulk removal.** `remove_if` and `retain_if` call the callback on every entry, then release the chosen entries and compact at most once. `compute` returns the result of its callback. A `with_keys` collection hashes with its stored function, so `hash_code` agrees with its equality.
- **IntMap and StringMap** are generated from `hash-map.nra` with the key operations called directly (`tests/stdlib/hash-map/generators/gen_maps.py`); `StringMap` uses the string's built-in `hash_code` and `==`, which equal `Strings.hash` and `Strings.equals`, because `Std.Text` is above `Std.Collections` in the dependency order.
- **Extra members.** `with_keys_and_capacity`; `trim` and `validate` on every hash collection; `TreeMap.key_iterator`, `key_range`, and `descending_keys`, which `TreeSet` uses to avoid a tuple per element; `Sequence.from_iterator`, a public `Sequence` constructor from two functions, and `Sequence.iterator()` so that a sequence works once in for-each. Constructors of these collections take a module-private token, so only their module functions build them.
- **Sequence choices.** `range(from, to)` on a sorted collection is empty when `to` is not above `from`. `windowed` emits only full windows. `zip` and `chain` mark the other sequence used. `from_list` fails fast on a structural change to its list. `sum_float64` starts from the first element, so `[-0.0]` sums to `-0.0`, and an empty sequence sums to `0.0`. Maps and sets have no `sequence()` member; `ArrayList` and `LinkedList` build theirs with `from_iterator`.
- **Trap details.** `duplicate key`, `negative count`, `chunk size`, and `window size` after `invalid argument: `; `invalid state: hash table …` and `invalid state: tree …` for a failed `validate()`.
