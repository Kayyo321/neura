# `Std.Collections.Lists`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.Lists`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.Lists`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Lists** · [`export let copy = func<T>(items: T[list]) -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L9)
  A new list with the same elements.
- **Lists** · [`export let concat = func<T>(a: T[list], b: T[list]) -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L21)
  A new list with the elements of `a` followed by those of `b`.
- **Lists** · [`export let reverse = func<T>(items: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L33)
  Reverses the list in place. Not structural.
- **Lists** · [`export let swap = func<T>(items: T[list], i: int, j: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L46)
  Exchanges two elements. Not structural.
- **Lists** · [`export let fill = func<T>(items: T[list], value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L53)
  Replaces every element with `value`. Not structural.
- **Lists** · [`export let insert_at = func<T>(items: T[list], index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L64)
  Inserts `value` before position `index`, or appends when the index equals the length. Appends the last element again and shifts the suffix right by indexed writes.
- **Lists** · [`export let truncate = func<T>(items: T[list], length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L85)
  Removes elements from the end until `length` remain. Traps with "invalid argument: negative size" or "invalid argument: size above current size".
- **Lists** · [`export let clear = func<T>(items: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L98)
  Removes every element from the end of the same list.
- **Lists** · [`export let filled = func<T>(count: int, value: T) -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L106)
  A new list of `count` copies of `value`. Traps with "invalid argument: negative count".
- **Lists** · [`export let from_array = func<T>(items: T[]) -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L120)
  A new list with the array's elements.
- **Lists** · [`export let same_elements = func<T>(a: list.List<T>, b: list.List<T>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/lists.nra#L133)
  Whether two lists of any classes have the same size and equal elements at each position, by equals with the element of `a` first.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 9.2 Equality of collections

All of this needs G3.

| Kind | Equal when | Hash | Text |
| --- | --- | --- | --- |
| List | Same class and size, with equal elements at each index | The list formula of [section 5.2](Stdlib-Design#52-equality-hashing-and-text) | `[a, b, c]` |
| Set | Same class and size, and this set contains each of the other's elements | The wrapping sum of the element hashes | `{a, b, c}` in iteration order |
| Map | Same class and size, and for each of the other's entries this map holds an equal value for the key | The wrapping sum over entries of key hash exclusive-or value hash | `{k: v, k2: v2}` in iteration order |
| `ArrayDeque`, `PriorityQueue`, `Sequence` | Identity | Identity | `[a, b, c]` in iteration order; `Sequence` prints `Sequence` |

`equals(other: class)` first tests whether `other` is an instance of the same collection type. It then downcasts and compares contents. Comparing an `ArrayList` with a `LinkedList` by content is the separate `Lists.same_elements` function over the list interface.

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
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.

### 9.11 Sorting and searching

The module `Sorting`. Generic sorting needs G1 for the comparator.

**`sort<T>(items: T[list], c: (T, T) → Ordering)`** is stable:

1. Sort each consecutive block of 16 elements (the last may be shorter) in place with binary insertion sort: for each element, binary-search the sorted prefix for the position after the last element not `Greater` than it, and shift the tail right by one.
2. Create a buffer: a second built-in list of the same length, filled by appending the elements themselves. They are references the list already holds, so the buffer keeps nothing extra alive, and no "empty" value is needed.
3. Merge bottom-up. For widths 16, 32, 64, and so on while the width is less than the length, merge each adjacent pair of runs from the source list into the destination, then swap the two lists' roles. If the last element of the left run is not `Greater` than the first of the right run, the pair is copied without comparisons. When merging, take from the right run only when its head is `Less` than the left run's head; this makes the sort stable.
4. If the result ended in the buffer, write it back to `items` by index. Element writes do not change the list's structural version.

It makes at most about `n log2 n` comparator calls. `ArrayList.sort` calls it on `storage`. `sort_range(items, start, end, c)` sorts part of a list.

**`binary_search<T>(items: T[list], value: T, c) → int`** requires `items` sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index, the first of any equal run. Otherwise it returns `-(insertion point) - 1`.

The module **`Lists`** has helpers for built-in lists that need no callback: `copy<T>(items)`, `concat<T>(a, b)`, `reverse<T>(items)`, `swap<T>(items, i, j)`, `fill<T>(items, value)`, `insert_at<T>(items, index, value)`, `truncate<T>(items, length)`, `clear<T>(items)`, `filled<T>(count, value)`, and `from_array<T>(items: T[])`. There is no conversion from a list to an array, because an array's length must come from a literal.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` where a collection's own text reaches it again, directly or through other collections. Each collection keeps the flag for this in the instance, so text conversion reads no module state and is task-safe ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)).
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the searched value as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.
