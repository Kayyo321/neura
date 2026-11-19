# `Std.Net.Core.IoSlice`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.IoSlice`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.IoSlice`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **IoSlice** · [`export let IoSlice = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L21)
  A read-only window over a backing byte list. Tagged `#Final`, `#NoDiscard`. Since 0.3.0.
- **IoSlice.IoSlice** · [`export let init = func(bytes: uint8[list], offset: int, length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L31)
  Constructs the view of `bytes[offset]` to `bytes[offset + length - 1]`; traps when that range is not inside the list.
- **IoSlice.IoSlice** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L41)
  The number of bytes in the view.
- **IoSlice.IoSlice** · [`export let byte_at = func(index: int) -> uint8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L44)
  Byte `index` of the view (0 is the first); traps when the index is outside the view.
- **IoSlice.IoSlice** · [`export let slice = func(start: int, end: int) -> IoSlice`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L53)
  The view of bytes `start` to `end - 1` of this view, over the same backing list; traps unless 0 <= start <= end <= length.
- **IoSlice.IoSlice** · [`export let to_list = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L61)
  A new list holding a copy of the view's bytes.
- **IoSlice.IoSlice** · [`export let append_to = func(target: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L68)
  Appends a copy of the view's bytes to `target`.
- **IoSlice.IoSlice** · [`export let index_of = func(value: uint8) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L75)
  The index in the view of the first byte equal to `value`, or -1.
- **IoSlice.IoSlice** · [`export let equals_bytes = func(bytes: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L85)
  Tests whether the view holds exactly the bytes of `bytes`.
- **IoSlice.IoSlice** · [`export let content_equals = func(other: IoSlice) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L91)
  Tests whether two views hold the same bytes (wherever they are stored).
- **IoSlice.IoSlice** · [`export let starts_with = func(prefix: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L104)
  Tests whether the view begins with the bytes of `prefix`.
- **IoSlice.IoSlice** · [`export let backing = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L117)
  The backing list itself (not a copy), for a host call that takes a list and a range.
- **IoSlice.IoSlice** · [`export let offset = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L120)
  The index in the backing list of the view's first byte.
- **IoSlice.IoSlice** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L123)
  `IoSlice(offset N, length M)`; the bytes are not shown.
- **IoSlice** · [`export let IoSliceList = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L129)
  An ordered list of views to be written as one gather (writev-style) vector. Tagged `#Final`, `#NoDiscard`. Since 0.3.0.
- **IoSlice.IoSliceList** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L134)
  Creates an empty list.
- **IoSlice.IoSliceList** · [`export let add = func(slice: IoSlice)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L139)
  Appends a view.
- **IoSlice.IoSliceList** · [`export let count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L144)
  The number of views.
- **IoSlice.IoSliceList** · [`export let get = func(index: int) -> IoSlice`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L147)
  View `index`; traps when it is outside the list.
- **IoSlice.IoSliceList** · [`export let slices = func -> IoSlice[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L155)
  A new list of the views (the views themselves are shared).
- **IoSlice.IoSliceList** · [`export let total_length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L164)
  The sum of the views' lengths.
- **IoSlice.IoSliceList** · [`export let gather = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L167)
  A new list holding the views' bytes in order.
- **IoSlice.IoSliceList** · [`export let write_to = func(writer: func(IoSlice) -> Result<int>) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L170)
  Writes the views in order through `writer`, as `write_gather` does.
- **IoSlice.IoSliceList** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L174)
  `IoSliceList(N slices, M bytes)`.
- **IoSlice** · [`export let of = func(bytes: uint8[list]) -> IoSlice`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L180)
  The view of the whole list. Tagged `#NoDiscard`, `#ThreadSafe`.
- **IoSlice** · [`export let view = func(bytes: uint8[list], offset: int, length: int) -> IoSlice`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L184)
  The view of `length` bytes from `offset`; traps when that range is not inside the list. Tagged `#NoDiscard`, `#ThreadSafe`.
- **IoSlice** · [`export let total_length = func(slices: IoSlice[list]) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L189)
  The sum of the views' lengths. Tagged `#ThreadSafe`.
- **IoSlice** · [`export let gather = func(slices: IoSlice[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L193)
  A new list holding the views' bytes in order (one copy of each byte). Tagged `#NoDiscard`, `#ThreadSafe`.
- **IoSlice** · [`export let write_gather = func(slices: IoSlice[list], writer: func(IoSlice) -> Result<int>) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/io-slice.nra#L203)
  Writes the views in order through `writer` and returns the number of bytes written. `writer` is offered the unwritten rest of one view at a time and returns how many of its bytes it took, 1 to the view's length; after a short write it is offered the rest of the same view. Empty views are skipped. An error from `writer` is returned as is (the bytes written before it are not reported); a count of 0 or more than offered is an `Io` error of kind `Other`. This is the writev-style entry point: a runtime with a vectored host write would take the whole list in one call. Tagged `#NoDiscard`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
