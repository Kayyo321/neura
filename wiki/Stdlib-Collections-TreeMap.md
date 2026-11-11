# `Std.Collections.TreeMap`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.TreeMap`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.TreeMap`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TreeMap** · [`export let TreeMap = class<K, V> implements MutableMap<K, V>, SortedMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L43)
  A mutable AVL tree map that identifies keys by comparator equality. Tagged `#NotThreadSafe`.
- **TreeMap.TreeMap** · [`export let init = func(seal: Seal, comparator: func(K, K) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L56)
  Starts an empty tree with the comparator supplied by the factory.
- **TreeMap.TreeMap** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L62)
  Returns the number of entries.
- **TreeMap.TreeMap** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L65)
  Returns true when there are no entries.
- **TreeMap.TreeMap** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L68)
  Removes all entries and invalidates iterators.
- **TreeMap.TreeMap** · [`export let contains_key = func(key: K) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L75)
  Tests for an existing key that compares equal to the probe.
- **TreeMap.TreeMap** · [`export let get = func(key: K) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L79)
  Returns the associated value as `Some`, or `None` when the key is absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let get_or = func(key: K, fallback: V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L88)
  Returns the associated value, or `fallback` when the key is absent.
- **TreeMap.TreeMap** · [`export let require = func(key: K) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L97)
  Returns the associated value. Traps with `no such element` when the key is absent.
- **TreeMap.TreeMap** · [`export let set = func(key: K, value: V)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L106)
  Stores the value for `key`. A comparator-equal entry keeps its original key and iterator validity.
- **TreeMap.TreeMap** · [`export let put = func(key: K, value: V) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L114)
  Stores the value for `key` and returns its previous value as `Some`, or `None` for a new key.
- **TreeMap.TreeMap** · [`export let delete = func(key: K) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L125)
  Removes the entry for `key`. Returns true if an entry was present.
- **TreeMap.TreeMap** · [`export let remove = func(key: K) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L135)
  Removes the entry for `key` and returns its value as `Some`, or `None` when absent.
- **TreeMap.TreeMap** · [`export let keys = func -> K[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L146)
  Copies the keys to a new built-in list in ascending key order.
- **TreeMap.TreeMap** · [`export let values = func -> V[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L157)
  Copies the values to a new built-in list in ascending key order.
- **TreeMap.TreeMap** · [`export let entries = func -> [K, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L168)
  Copies the `[key, value]` tuples to a new built-in list in ascending key order.
- **TreeMap.TreeMap** · [`export let iterator = func -> iterators.Iterator<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L180)
  Returns a fail-fast iterator over `[key, value]` tuples in ascending key order.
- **TreeMap.TreeMap** · [`export let key_iterator = func -> iterators.Iterator<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L184)
  Returns a fail-fast iterator over keys in ascending order.
- **TreeMap.TreeMap** · [`export let put_all = func(other: TreeMap<K, V>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L188)
  Sets each entry of `other` in its ascending order.
- **TreeMap.TreeMap** · [`export let for_each = func(action: func(K, V) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L201)
  Calls `action` with the key and value of each entry in ascending key order. The action leaves the map structure unchanged.
- **TreeMap.TreeMap** · [`export let remove_if = func(drop: func(K, V) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L216)
  Calls `drop` once per entry in ascending key order, then removes the matches. The predicate leaves the map structure unchanged. The number of removed entries.
- **TreeMap.TreeMap** · [`export let get_or_put = func(key: K, make: func() -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L241)
  Returns the associated value, or calls `make` once, stores its result, and returns it when absent. The factory leaves the map structure unchanged.
- **TreeMap.TreeMap** · [`export let merge = func(key: K, value: V, combine: func(V, V) -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L257)
  Stores and returns `value` when the key is absent. Otherwise stores and returns `combine(old, value)`. The combiner leaves the map structure unchanged.
- **TreeMap.TreeMap** · [`export let compute = func(key: K, f: func(K, optional.Optional<V>) -> optional.Optional<V>) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L275)
  Calls `f` with the key and its optional current value. Stores a `Some` result or removes the entry for `None`. The callback leaves the map structure unchanged. The result of the callback.
- **TreeMap.TreeMap** · [`export let contains_value = func(value: V) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L304)
  Tests whether `value.equals(held_value)` is true for any entry.
- **TreeMap.TreeMap** · [`export let first = func -> [K, V]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L316)
  Returns the entry with the least key. Traps with `no such element` when empty.
- **TreeMap.TreeMap** · [`export let last = func -> [K, V]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L326)
  Returns the entry with the greatest key. Traps with `no such element` when empty.
- **TreeMap.TreeMap** · [`export let remove_first = func -> [K, V]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L337)
  Removes and returns the entry with the least key. Traps with "no such element" when empty.
- **TreeMap.TreeMap** · [`export let remove_last = func -> [K, V]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L349)
  Removes and returns the entry with the greatest key. Traps with "no such element" when empty.
- **TreeMap.TreeMap** · [`export let range = func(from: K, to: K) -> [K, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L361)
  Returns a new list of entries in ascending key order from `from` inclusive to `to` exclusive. Returns an empty list when `to` is at or below `from`.
- **TreeMap.TreeMap** · [`export let key_range = func(from: K, to: K) -> K[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L373)
  Copies the keys from `from` inclusive to `to` exclusive in ascending order.
- **TreeMap.TreeMap** · [`export let descending = func -> [K, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L384)
  Copies entries into a new list in descending key order.
- **TreeMap.TreeMap** · [`export let descending_keys = func -> K[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L396)
  Copies the keys into a new list in descending order.
- **TreeMap.TreeMap** · [`export let floor = func(key: K) -> optional.Optional<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L408)
  Returns the greatest key at most `key` as `Some`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let ceiling = func(key: K) -> optional.Optional<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L412)
  Returns the least key at least `key` as `Some`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let lower = func(key: K) -> optional.Optional<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L416)
  Returns the greatest key strictly below `key` as `Some`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let higher = func(key: K) -> optional.Optional<K>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L420)
  Returns the least key strictly above `key` as `Some`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let floor_entry = func(key: K) -> optional.Optional<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L424)
  Returns the entry with the greatest key at most `key`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let ceiling_entry = func(key: K) -> optional.Optional<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L428)
  Returns the entry with the least key at least `key`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let lower_entry = func(key: K) -> optional.Optional<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L433)
  Returns the entry with the greatest key strictly below `key`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let higher_entry = func(key: K) -> optional.Optional<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L437)
  Returns the entry with the least key strictly above `key`, or `None` when absent. Tagged `#NoDiscard`.
- **TreeMap.TreeMap** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L443)
  Traps with "invalid state: tree ..." on a broken invariant: AVL balance, stored heights, ascending key order, reciprocal parent links, a nil parent at the root, and the count.
- **TreeMap.TreeMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L490)
  Another TreeMap of the same size in which, for each of its entries, this map holds an equal value for the key.
- **TreeMap.TreeMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L514)
  The wrapping sum over entries of the key hash exclusive-or the value hash.
- **TreeMap.TreeMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L527)
  Formats entries as `{k: v, k2: v2}` in ascending order. The map prints "(this collection)" where its own text reaches it again.
- **TreeMap** · [`export let create = func<K, V>(comparator: func(K, K) -> ordering.Ordering) -> TreeMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/tree-map.nra#L869)
  An empty map ordered by `comparator`, which must define a total order.
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

### 9.6 Hash collections

Hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including `IntMap` and `StringMap`.

| Field | Type | Meaning |
| --- | --- | --- |
| `index` | `int` list of length `T` | Open-addressed table. Each slot holds an entry number, -1 (`Empty`), or -2 (`Tombstone`). `T` is a power of two, at least 8. |
| `hashes` | `uint32` list | Each entry's spread hash, in insertion order. Its length is `used`. |
| `live` | `bool` list | False for a removed entry. Same length. |
| `count` | `int` | Live entries. |
| `modifications` | `uint64` | The modification count ([section 5.4](Stdlib-Design#54-iteration)). |

`E`, the entry capacity, is `T / 2`. At most half of the index is ever non-empty, because each entry occupies one slot. One half is chosen because lookups use linear probing: at half load an unsuccessful search inspects about 2.5 slots on average and a successful one about 1.5, and every inspected entry costs a comparison circuit. The index holds only integers, so a sparse table is cheap.

The core's operations, as the owner uses them:

- **Lookup.** The owner computes the spread hash, asks for the home slot (the hash masked with `T - 1`), and walks slots in increasing order, wrapping. `Empty` ends the search: absent. `Tombstone` continues. An entry number whose cached hash equals the probe's hash is a candidate: the owner compares its stored key with the probe key, and stops on a match. The probe key is the first argument to the equality. The loop terminates because at least half the slots are `Empty`.
- **Claim.** For a new key the owner remembers the first `Tombstone` it passed. The core appends the hash and `true` to its lists, writes the new entry number into the remembered tombstone or the final `Empty` slot, and advances `count` and the modification count. The owner appends the key and value to its lists in the same step, so entry numbers agree.
- **Release.** The slot becomes `Tombstone`, the entry's `live` flag becomes false, `count` decreases, and the modification count advances. If the released entry is the last one, it and any dead entries before it are removed from the ends of all the dense lists at once, so stack-like use retains nothing.
- **Compaction.** The core returns the old entry numbers of the live entries in order. The owner builds new key and value lists in that order; the core rebuilds `hashes`, `live`, and `index` by probing from each cached hash for the first `Empty` slot. No hash or equality function is called. It runs in a helper function that returns the new lists ([section 5.5](Stdlib-Design#55-resources)).

When compaction happens:

1. A new key arrives and `used` equals `E`, or the live entries plus the tombstones in the index fill `E` (trailing releases leave tombstones behind). If `count` is greater than `E / 2`, `T` doubles first. A `T` above 2^30 traps with `invalid argument: capacity`.
2. A release leaves more dead entries than live ones and at least 8 dead entries. `T` is unchanged.

**Retention bound.** A removed key and value stay in the owner's dense lists, and so stay reachable, until the next compaction. Rule 2 bounds this: a hash collection never holds more dead entries than `max(count, 7)`. Compaction costs time proportional to `count` and happens after more than `count` releases, so removal stays amortized constant time. Nothing observable depends on the retained references, since reclamation runs no code; only heap use does.

`clear` replaces every list with a new one of the initial size. The table never shrinks on its own; `trim()` compacts at the smallest `T` whose `E` holds `count`.

**Costs.** Lookup, insertion, and removal are expected constant time with well-distributed hashes and linear in `used` in the worst case. There is no conversion of long probe sequences into trees. Code that hashes adversarial keys uses `TreeMap`.

#### The collections

| Class | Keys | Key operations | Needs |
| --- | --- | --- | --- |
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing |
| `StringMap<V>` | `string` | `Strings.hash` and `Strings.equals` | T1 |
| `HashMap<K, V>`, `HashSet<T>` created with `with_keys<K, V>(hash: (K) → uint32, same: (K, K) → bool)` | Any | The two function values, stored in the collection | G1 |
| `HashMap<K, V>`, `HashSet<T>` created with `create<K, V>()` | Any | `hash_code` and `equals` | G3 |

- `IntMap` and `StringMap` are permanent, not stopgaps. They follow the map contract with their key type fixed, call their key operations directly without a function value, and are the right choice for those key types after `HashMap` exists.
- `HashMap` stores its two key operations as fields either way. `create` fills them with the universal members, so the rest of the class is written once.
- `HashSet` owns a `HashCore` and a keys list. `HashMap` owns a `HashCore`, a keys list, and a values list.
- `with_capacity` variants of each constructor choose the smallest power-of-two `T` that is at least `max(8, 2n)`; a negative `n` or one above 2^29 traps.
- Setting a value for an existing key overwrites the value only: the entry keeps its position and the modification count does not change. A key removed and added again goes to the end of the order.
- Iteration visits entry numbers in order and skips dead entries.
- A callback passed to `get_or_put`, `merge`, or `compute` must not modify the map ([section 5.6](Stdlib-Design#56-callbacks)).

### 9.7 `TreeMap<K, V>` and `TreeSet<T>`

Sorted collections ordered by a comparator given at construction. They need G1, because the comparator's type mentions `K`. A comparator is always required; for a built-in type pass its `compare` function. Two keys are the same key when the comparator returns `Equal`; `equals` is not consulted.

The balanced tree is an **AVL tree**. Lookups dominate, each comparison is a call through a function value, and an AVL tree is at most about 1.44 log2 of the size tall against about 2 log2 for a red-black tree.

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

### 18.11 Test layout

- **Layout.** Each area of `tests/stdlib` holds test files, `support/` modules, `traps/` programs with `// status:` and `// stderr:` headers, and `programs/` with expected output (`tests/stdlib/area.cmake`). Every area runs against a fresh installation (`stdlib-install`), its tests optimized twice and unoptimized, its programs and traps in both modes, and its program images through `disassemble`, `assemble`, and `decompile`.
- **Reference evaluator.** `backend-reference` runs every library test function under the NeuraIR reference evaluator and NVM and compares the outcome, the console output, and any trap message. A call of a kernel-bound function runs the runtime's own kernel through the kernel table, so those tests are compared. Tests that need host responses (files, sockets, console input), open a channel, or exceed the evaluator's budget or call depth are counted and not compared.
- **Host services** are tested with the capability denied, with real providers in temporary directories and on localhost, and by replaying checked-in traces.
- **Cost baseline.** `tests/benchmarks` holds the workloads of this section's cost baseline and a few more (ArrayList, TreeMap, strings, JSON, dates); `benchmarks` compares their steps, peak heap cells, neurons, and image bytes with `baseline.txt` and checks that steps grow as expected between two sizes.
- **Not established.** Agreement of `Math` with a reference fdlibm 5.3 build (no reference build was available); `with_capacity(2^29)` and other limits that need billions of elements were only inspected.
