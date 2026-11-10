# `Std.Collections.ImmutableMap`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.ImmutableMap`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.ImmutableMap`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ImmutableMap** · [`export let ImmutableMap = class<K, V> implements Map<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L14)
  A read-only hash map with entries in construction order.
- **ImmutableMap.ImmutableMap** · [`export let init = func(seal: Seal, contents: hash_map.HashMap<K, V>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L19)
  Adopts the privately built table without exposing it for mutation.
- **ImmutableMap.ImmutableMap** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L24)
  Returns the number of entries.
- **ImmutableMap.ImmutableMap** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L27)
  Returns true when there are no entries.
- **ImmutableMap.ImmutableMap** · [`export let contains_key = func(key: K) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L30)
  Reports whether an entry exists for `key`.
- **ImmutableMap.ImmutableMap** · [`export let get = func(key: K) -> optional.Optional<V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L33)
  Returns the associated value as `Some`, or `None` when the key is absent.
- **ImmutableMap.ImmutableMap** · [`export let get_or = func(key: K, fallback: V) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L36)
  Returns the associated value, or `fallback` when the key is absent.
- **ImmutableMap.ImmutableMap** · [`export let require = func(key: K) -> V`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L39)
  Returns the associated value. Traps with `no such element` when the key is absent.
- **ImmutableMap.ImmutableMap** · [`export let keys = func -> K[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L42)
  Copies the keys to a new built-in list in construction order.
- **ImmutableMap.ImmutableMap** · [`export let values = func -> V[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L45)
  Copies the values to a new built-in list in construction order.
- **ImmutableMap.ImmutableMap** · [`export let entries = func -> [K, V][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L48)
  Copies the `[key, value]` tuples to a new built-in list in construction order.
- **ImmutableMap.ImmutableMap** · [`export let contains_value = func(value: V) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L51)
  Tests whether `value.equals(held_value)` is true for any entry.
- **ImmutableMap.ImmutableMap** · [`export let for_each = func(action: func(K, V) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L54)
  Calls `action` with the key and value of each entry in construction order.
- **ImmutableMap.ImmutableMap** · [`export let iterator = func -> Iterator<[K, V]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L59)
  Returns an iterator over `[key, value]` tuples in construction order.
- **ImmutableMap.ImmutableMap** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L63)
  Another ImmutableMap of the same size in which, for each of its entries, this map holds an equal value for the key.
- **ImmutableMap.ImmutableMap** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L93)
  The wrapping sum over entries of the key hash exclusive-or the value hash.
- **ImmutableMap.ImmutableMap** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L97)
  `{k: v, k2: v2}` in iteration order. The entries exist before the map, so it can never contain itself.
- **ImmutableMap** · [`export let of = func<K, V>(entries: [K, V][list]) -> ImmutableMap<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-map.nra#L102)
  Copies `entries`. Traps with "invalid argument: duplicate key" when two entries have equal keys.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 9.10 Interfaces and read-only views

All of this needs G4. The interfaces are declared in `Std.Collections` and the concrete classes are marked as implementing them; no member changes.

```text
Iterable<T>
└── Collection<T>            size, is_empty, contains, to_list, iterator
    ├── MutableCollection<T> add, remove, clear, ...
    ├── List<T> ──────────── MutableList<T>
    ├── Set<T> ───────────── MutableSet<T>
    │   └── SortedSet<T>
    └── Queue<T> ─────────── Deque<T>

Map<K, V> ── MutableMap<K, V>
└── SortedMap<K, V>
```

- **Decision kept:** read-only and mutable interfaces are separate. A function that only reads takes the read-only interface, and no member simulates immutability by trapping.
- Members that need only other members (`is_empty`, `first`, `contains_all`, `for_each`) become default methods written once in the interface.
- `ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are classes that copy their contents at construction and implement only the read-only interfaces. Their module functions `of` take a built-in list (for the map, a list of `[K, V]`; a duplicate key traps).
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.
