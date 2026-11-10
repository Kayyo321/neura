# `Std.Collections.ImmutableList`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.ImmutableList`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.ImmutableList`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ImmutableList** · [`export let ImmutableList = class<T> implements List<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L11)
- **ImmutableList.ImmutableList** · [`export let init = func(values: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L15)
  Copies `values`.
- **ImmutableList.ImmutableList** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L26)
- **ImmutableList.ImmutableList** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L28)
- **ImmutableList.ImmutableList** · [`export let get = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L31)
  A bounds trap for an index outside 0 through size() - 1.
- **ImmutableList.ImmutableList** · [`export let first = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L33)
- **ImmutableList.ImmutableList** · [`export let last = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L40)
- **ImmutableList.ImmutableList** · [`export let first_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L47)
- **ImmutableList.ImmutableList** · [`export let last_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L54)
- **ImmutableList.ImmutableList** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L62)
  By equals, with `value` as the receiver.
- **ImmutableList.ImmutableList** · [`export let index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L64)
- **ImmutableList.ImmutableList** · [`export let last_index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L76)
- **ImmutableList.ImmutableList** · [`export let index_where = func(test: func(T) -> bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L88)
  The first index whose element passes, or -1.
- **ImmutableList.ImmutableList** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L100)
- **ImmutableList.ImmutableList** · [`export let slice = func(start: int, end: int) -> ImmutableList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L112)
  A new ImmutableList with the elements from `start` inclusive to `end` exclusive. Traps with "invalid argument: range" unless 0 <= start <= end <= size().
- **ImmutableList.ImmutableList** · [`export let binary_search = func(value: T, c: func(T, T) -> ordering.Ordering) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L126)
  Sorting.binary_search over the elements, which must be sorted by c.
- **ImmutableList.ImmutableList** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L130)
  A new built-in list in order.
- **ImmutableList.ImmutableList** · [`export let iterator = func -> Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L141)
- **ImmutableList.ImmutableList** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L145)
  Another ImmutableList with the same element type, size, and equal elements at each index.
- **ImmutableList.ImmutableList** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L163)
  The list formula of section 5.2.
- **ImmutableList.ImmutableList** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L167)
  `[a, b, c]`. The elements exist before the list, so it can never contain itself.
- **ImmutableList** · [`export let of = func<T>(items: T[list]) -> ImmutableList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-list.nra#L191)
  Copies `items`.
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
