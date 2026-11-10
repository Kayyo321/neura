# `Std.Collections.HashMap`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.HashMap`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.HashMap`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **HashMap** · [`export let HashMap = class<K, V> implements MutableMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L20)
- **HashMap.HashMap** · [`export let init = func(seal: Seal, table: hash_core.HashCore, hash: func(K) -> uint32, equal: func(K, K) -> bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L29)
- **HashMap.HashMap** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L35)
- **HashMap.HashMap** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L37)
- **HashMap.HashMap** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L40)
  Replaces every list with a new one of the initial table size.
- **HashMap.HashMap** · [`export let contains_key = func(key: K) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L46)
- **HashMap.HashMap** · [`export let get = func(key: K) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L48)
- **HashMap.HashMap** · [`export let get_or = func(key: K, fallback: V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L56)
- **HashMap.HashMap** · [`export let require = func(key: K) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L65)
  Traps with "no such element" when the key is absent.
- **HashMap.HashMap** · [`export let set = func(key: K, value: V)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L73)
- **HashMap.HashMap** · [`export let put = func(key: K, value: V) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L84)
  The previous value.
- **HashMap.HashMap** · [`export let delete = func(key: K) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L98)
  Removes the key; returns whether it was present.
- **HashMap.HashMap** · [`export let remove = func(key: K) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L108)
  The removed value.
- **HashMap.HashMap** · [`export let keys = func -> K[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L118)
- **HashMap.HashMap** · [`export let values = func -> V[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L132)
- **HashMap.HashMap** · [`export let entries = func -> [K, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L146)
- **HashMap.HashMap** · [`export let iterator = func -> Iterator<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L162)
  Yields [K, V] in insertion order.
- **HashMap.HashMap** · [`export let put_all = func(other: HashMap<K, V>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L166)
  Sets each entry of `other` in its iteration order.
- **HashMap.HashMap** · [`export let for_each = func(action: func(K, V) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L179)
  In insertion order. The action must not change the map structurally.
- **HashMap.HashMap** · [`export let remove_if = func(drop: func(K, V) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L195)
  Calls `drop` once per entry in order, then removes the entries it chose. Returns how many were removed.
- **HashMap.HashMap** · [`export let get_or_put = func(key: K, make: func() -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L227)
  The existing value, or the result of `make`, stored. `make` must not change the map structurally.
- **HashMap.HashMap** · [`export let merge = func(key: K, value: V, combine: func(V, V) -> V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L244)
  Stores `value` when the key is absent, otherwise combine(old, value). Returns the stored value.
- **HashMap.HashMap** · [`export let compute = func(key: K, f: func(K, optional.Optional<V>) -> optional.Optional<V>) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L263)
  Stores a Some result of `f`, or removes the key for None. Returns the result.
- **HashMap.HashMap** · [`export let contains_value = func(value: V) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L293)
  By the values' equals, with `value` as the receiver.
- **HashMap.HashMap** · [`export let trim = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L307)
  Compacts at the smallest table whose entry capacity holds size().
- **HashMap.HashMap** · [`export let validate = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L313)
  Traps with "invalid state: ..." on a broken table invariant or when a live key's hash differs from its cached hash.
- **HashMap.HashMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L330)
  Another HashMap with the same size in which, for each of its entries, this map holds an equal value for the key.
- **HashMap.HashMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L354)
  The wrapping sum over entries of the key hash exclusive-or the value hash.
- **HashMap.HashMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L371)
  `{k: v, k2: v2}` in iteration order; the map itself as a direct key or value prints "(this collection)".
- **HashMap** · [`export let create = func<K, V> -> HashMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L493)
  Keys compared with equals and hashed with hash_code.
- **HashMap** · [`export let with_capacity = func<K, V>(capacity: int) -> HashMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L498)
  A table sized for `capacity` entries. Traps with "invalid argument: capacity" for a negative capacity or one above 2^29.
- **HashMap** · [`export let with_keys = func<K, V>(hash: func(K) -> uint32, same: func(K, K) -> bool) -> HashMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L503)
  Keys hashed and compared with the given functions. Equal keys must have equal hashes.
- **HashMap** · [`export let with_keys_and_capacity = func<K, V>(hash: func(K) -> uint32, same: func(K, K) -> bool, capacity: int) -> HashMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/hash-map.nra#L506)
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

### 9.6 Hash collections

**Decisions kept from the first plan:** hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including the ones that can be written today.

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
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing: it can be written today |
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
