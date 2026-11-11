# `Std.Collections.ImmutableSet`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.ImmutableSet`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.ImmutableSet`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ImmutableSet** · [`export let ImmutableSet = class<T> implements Set<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L15)
  A read-only hash set with elements in first-occurrence order. Tagged `#Final`, `#NoDiscard`.
- **ImmutableSet.ImmutableSet** · [`export let init = func(seal: Seal, values: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L20)
  Copies the supplied elements into a private hash set, dropping later duplicates.
- **ImmutableSet.ImmutableSet** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L26)
  Returns the number of distinct elements.
- **ImmutableSet.ImmutableSet** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L29)
  Returns true when there are no distinct elements.
- **ImmutableSet.ImmutableSet** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L32)
  Tests membership using the element's hash and equality.
- **ImmutableSet.ImmutableSet** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L35)
  A new built-in list in iteration order.
- **ImmutableSet.ImmutableSet** · [`export let iterator = func -> Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L38)
  Returns an iterator in first-occurrence order.
- **ImmutableSet.ImmutableSet** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L41)
  Calls `action` with each distinct element in first-occurrence order.
- **ImmutableSet.ImmutableSet** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L47)
  Another ImmutableSet of the same size whose elements this set all contains.
- **ImmutableSet.ImmutableSet** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L68)
  The wrapping sum of the element hashes.
- **ImmutableSet.ImmutableSet** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L72)
  `{a, b, c}` in iteration order. The elements exist before the set, so it can never contain itself.
- **ImmutableSet** · [`export let of = func<T>(items: T[list]) -> ImmutableSet<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/immutable-set.nra#L76)
  Creates a set from a shallow copy of `items`, dropping later duplicates.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

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

- Read-only and mutable interfaces are separate. A function that only reads takes the read-only interface, and no member simulates immutability by trapping.
- Members that need only other members (`is_empty`, `first`, `contains_all`, `for_each`) become default methods written once in the interface.
- `ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are classes that copy their contents at construction and implement only the read-only interfaces. Their module functions `of` take a built-in list (for the map, a list of `[K, V]`; a duplicate key traps).
- Members that an interface declares, such as `size()` and `is_empty()`, stay methods in every class, because an interface cannot require an [accessor](Types#accessors); `ArrayList.items()` stays a method with them. The immutable classes and `Sequence` are `#Final` and `#NoDiscard`, and every query that returns an `Optional` (`get`, `first_or_none`, `peek`, `floor`, and the like) is `#NoDiscard` ([Tags](Tags)).
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.
