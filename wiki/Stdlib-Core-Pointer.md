# `Std.Core.Pointer`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

This module provides mutable and read-only views of managed cell storage and bounds-checked views of arrays and lists.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Pointer** · [`export let Pointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L30)
  A mutable view of a cell's retained storage. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer.Pointer** · [`export let init = func(storage: cell.Cell<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L34)
  Retains the storage of the supplied live cell.
- **Pointer.Pointer** · [`export let read = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L40)
  Reads the current stored value.
- **Pointer.Pointer** · [`export let write = func(value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L43)
  Replaces the current stored value.
- **Pointer.Pointer** · [`export let as_const = func -> ConstPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L48)
  Shares access to a deeply immutable stored value.
- **Pointer.Pointer** · [`export let clone = func -> Pointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L51)
  Returns another mutable view of the same storage.
- **Pointer** · [`export let ConstPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L57)
  A read-only view that keeps a cell's storage live. A Pointer to the same storage can still change it, so the view stays in the task that holds it. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer.ConstPointer** · [`export let read = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L66)
  Reads the stored value. T must be deeply immutable.
- **Pointer.ConstPointer** · [`export let clone = func -> ConstPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L69)
  Returns another read-only view of the same storage.
- **Pointer** · [`export let ListPointer = class<T>:ManagedPointer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L74)
  A non-copying, bounds-checked view of a contiguous array or list region. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.1.
- **Pointer.ListPointer** · [`export let init = func(values: T[], start: int, end: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L81)
  Creates a view of [start, end) in a fixed-length array.
- **Pointer.ListPointer** · [`export let len = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L117)
  Returns the number of elements in the view.
- **Pointer.ListPointer** · [`export let value_at = func(index: int) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L123)
  Reads the element at a checked index relative to this view.
- **Pointer.ListPointer** · [`export let set = func(index: int, value: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L132)
  Replaces the element at a checked index without changing the list version.
- **Pointer.ListPointer** · [`export let sub = func(start: int, end: int) -> ListPointer<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L145)
  Creates a checked subview of [start, end) relative to this view.
- **Pointer.ListPointer** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/pointer.nra#L155)
  Reports whether the view contains no elements.
<!-- END SOURCE API -->

## Detailed contracts

A pointer keeps its storage reachable. Destroying the original cell invalidates its cell handles and leaves the pointer's storage live. A list view becomes stale when its backing list changes length. The [0.3.1 pointer contract](https://github.com/Kayyo321/neura/blob/main/docs/lang/Neura0.3.1.md#stdcorepointer) specifies construction, bounds, type restrictions, task use, and foreign interoperability.
