# `Std.Net.Core.ByteRing`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.ByteRing`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.ByteRing`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ByteRing** · [`export let ByteRing = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L18)
  A mutable ring buffer of bytes with a fixed capacity. Tagged `#Final`, `#NoDiscard`. Since 0.3.0.
- **ByteRing.ByteRing** · [`export let init = func(capacity_bytes: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L27)
  Creates an empty ring of `capacity_bytes` bytes; traps below 1.
- **ByteRing.ByteRing** · [`export let capacity = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L38)
  The fixed capacity in bytes.
- **ByteRing.ByteRing** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L41)
  The number of readable bytes.
- **ByteRing.ByteRing** · [`export let writable_space = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L44)
  The number of bytes `write` can take now.
- **ByteRing.ByteRing** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L47)
  Tests whether no byte is readable.
- **ByteRing.ByteRing** · [`export let is_full = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L50)
  Tests whether no byte can be written.
- **ByteRing.ByteRing** · [`export let write = func(bytes: uint8[list]) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L53)
  Copies as many bytes from the front of `bytes` as fit and returns that count.
- **ByteRing.ByteRing** · [`export let write_slice = func(slice: slices.IoSlice) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L56)
  Copies as many bytes from the front of the view as fit and returns that count.
- **ByteRing.ByteRing** · [`export let read = func(max: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L72)
  Removes and returns up to `max` bytes from the head; traps when `max` is negative.
- **ByteRing.ByteRing** · [`export let peek = func(max: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L80)
  Returns a copy of up to `max` bytes from the head without removing them; traps when `max` is negative.
- **ByteRing.ByteRing** · [`export let skip = func(count: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L99)
  Discards up to `count` bytes from the head and returns how many were discarded; traps when `count` is negative.
- **ByteRing.ByteRing** · [`export let readable_slices = func -> slices.IoSlice[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L110)
  Views of the readable bytes in order: none when empty, one, or two when they wrap. Valid until the ring next changes.
- **ByteRing.ByteRing** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L126)
  Discards every readable byte.
- **ByteRing.ByteRing** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/byte-ring.nra#L132)
  `ByteRing(length N, capacity M)`; the bytes are not shown.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
