# `Std.Collections.Frozen`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.Frozen`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.Frozen`](#exported-declarations)
- [FrozenList in the collections reference](Collections#frozenlist)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Frozen** · [`export let FrozenList = class<T> implements Iterable<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L15)
  An ordered list of deeply immutable elements in storage that no other code holds. Indexes are zero-based `int` positions; an index out of range traps as a list index does. Tagged `#Final`, `#Immutable`, `#NoDiscard`.
- **Frozen.FrozenList** · [`export let init = func(values: T[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L22)
  Copies `values` into new storage.
- **Frozen.FrozenList** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L34)
  Returns the number of elements.
- **Frozen.FrozenList** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L37)
  Returns true when there are no elements.
- **Frozen.FrozenList** · [`export let get = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L40)
  Returns the element at zero-based `index`. An index out of range traps as a list index does.
- **Frozen.FrozenList** · [`export let first_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L43)
  Returns the first element as `Some`, or `None` when empty.
- **Frozen.FrozenList** · [`export let last_or_none = func -> optional.Optional<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L51)
  Returns the last element as `Some`, or `None` when empty.
- **Frozen.FrozenList** · [`export let contains = func(value: T) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L59)
  Tests membership by value equality (`===`).
- **Frozen.FrozenList** · [`export let index_of = func(value: T) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L62)
  Returns the lowest index of an element equal to `value` by `===`, or -1 when absent.
- **Frozen.FrozenList** · [`export let for_each = func(action: func(T) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L75)
  Calls `action` once for each element in index order. `action` is not retained.
- **Frozen.FrozenList** · [`export let slice = func(start: int, end: int) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L86)
  A new FrozenList with the elements from `start` inclusive to `end` exclusive. Traps with "invalid argument: range" unless 0 <= start <= end <= length().
- **Frozen.FrozenList** · [`export let to_list = func -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L100)
  A fresh built-in list with the elements in order; changing it changes nothing here.
- **Frozen.FrozenList** · [`export let appended = func(value: T) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L112)
  A new FrozenList with `value` after the last element. This list is unchanged.
- **Frozen.FrozenList** · [`export let replaced = func(index: int, value: T) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L119)
  A new FrozenList with `value` at `index`. An index out of range traps as a list index does.
- **Frozen.FrozenList** · [`export let removed = func(index: int) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L126)
  A new FrozenList without the element at `index`. An index out of range traps as a list index does.
- **Frozen.FrozenList** · [`export let concat = func(other: FrozenList<T>) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L133)
  A new FrozenList with this list's elements followed by `other`'s.
- **Frozen.FrozenList** · [`export let iterator = func -> Iterator<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L145)
  Returns an iterator over the elements in index order.
- **Frozen.FrozenList** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L149)
  Another FrozenList of the same element type with equal elements (`===`) at each index. `==` stays reference identity.
- **Frozen.FrozenList** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L168)
  Folds the element hashes in index order, as a built-in list's `hash_code` does.
- **Frozen.FrozenList** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L171)
  `[a, b, c]`.
- **Frozen** · [`export let copy_of = func<T>(source: T[]) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L205)
  Copies the elements of `source` into a new FrozenList.
- **Frozen** · [`export let copy_of_list = func<T>(source: T[list]) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L217)
  Copies the elements of the list `source` into a new FrozenList; later changes to `source` are not seen.
- **Frozen** · [`export let empty = func<T>() -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L220)
  Returns a FrozenList with no elements.
- **Frozen** · [`export let of = func<T>(items: ...T) -> FrozenList<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/frozen.nra#L226)
  Returns a FrozenList of the arguments in order.
<!-- END SOURCE API -->

## Detailed contracts

- The element type is deeply immutable: `bool`, a number, `string`, a tuple or enum of such parts, an `#Immutable` class whose fields are too, or a `FrozenList` of them. Any other type argument is [E0580](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0580.md).
- The constructors copy. Nothing freezes a list in place, and a later change to the source array or list is not seen.
- `appended`, `replaced`, `removed`, `concat` and `slice` return new lists and leave the receiver unchanged. They copy; no storage is shared.
- `get` with an index out of range traps as a list index does. `contains`, `index_of` and `equals` compare elements with `===`; `==` stays reference identity.
- In 0.3.0 a `FrozenList` cannot be sent to another task: the image format admits no list in a transferable layout, so a send or a spawn argument is [E0602](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0602.md).
