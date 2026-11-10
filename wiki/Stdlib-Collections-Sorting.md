# `Std.Collections.Sorting`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.Sorting`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sorting.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.Sorting`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Sorting** · [`export let sort_range = func<T>(items: T[list], start: int, end: int, c: func(T, T) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sorting.nra#L151)
  Sorts items[start, end) stably. Traps with "invalid argument: range" unless 0 <= start <= end <= items.length.
- **Sorting** · [`export let sort = func<T>(items: T[list], c: func(T, T) -> ordering.Ordering)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sorting.nra#L172)
  Sorts the whole list stably with at most about n log2 n comparator calls.
- **Sorting** · [`export let binary_search = func<T>(items: T[list], value: T, c: func(T, T) -> ordering.Ordering) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/sorting.nra#L179)
  `items` must be sorted by `c`. Returns the lowest index whose element is not Less than `value` when that element compares Equal, and otherwise -(insertion point) - 1.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 9.11 Sorting and searching

The module `Sorting`. Generic sorting needs G1 for the comparator.

**`sort<T>(items: T[list], c: (T, T) → Ordering)`** is stable:

1. Sort each consecutive block of 16 elements (the last may be shorter) in place with binary insertion sort: for each element, binary-search the sorted prefix for the position after the last element not `Greater` than it, and shift the tail right by one.
2. Create a buffer: a second built-in list of the same length, filled by appending the elements themselves. They are references the list already holds, so the buffer keeps nothing extra alive, and no "empty" value is needed.
3. Merge bottom-up. For widths 16, 32, 64, and so on while the width is less than the length, merge each adjacent pair of runs from the source list into the destination, then swap the two lists' roles. If the last element of the left run is not `Greater` than the first of the right run, the pair is copied without comparisons. When merging, take from the right run only when its head is `Less` than the left run's head; this makes the sort stable.
4. If the result ended in the buffer, write it back to `items` by index. Element writes do not change the list's structural version.

It makes at most about `n log2 n` comparator calls. `ArrayList.sort` calls it on `storage`. `sort_range(items, start, end, c)` sorts part of a list.

**`binary_search<T>(items: T[list], value: T, c) → int`** requires `items` sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index, the first of any equal run. Otherwise it returns `-(insertion point) - 1`.

The module **`Lists`** has helpers for built-in lists that need no callback and work today: `copy<T>(items)`, `concat<T>(a, b)`, `reverse<T>(items)`, `swap<T>(items, i, j)`, `fill<T>(items, value)`, `insert_at<T>(items, index, value)`, `truncate<T>(items, length)`, `clear<T>(items)`, `filled<T>(count, value)`, and `from_array<T>(items: T[])`. There is no conversion from a list to an array, because an array's length must come from a literal.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` for a collection that directly contains itself, through a shared depth counter (`Std.Collections.Internal.TextDepth`); deeper cycles still end with the frame-limit fault.
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the probe as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.
