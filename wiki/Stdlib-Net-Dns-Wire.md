# `Std.Net.Dns.Wire`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.Wire`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.Wire`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Wire** · [`export const MaxPointerOffset: int = 16383`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L20)
  The largest offset a compression pointer can hold.
- **Wire** · [`export let WireReader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L24)
  A bounds-checked reader over one DNS message. Tagged `#Final`, `#NotThreadSafe`.
- **Wire.WireReader** · [`export let init = func(message: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L31)
  A reader at offset 0 of `message`; or at `start`.
- **Wire.WireReader** · [`export let offset = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L40)
  The offset of the next byte. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let seek = func(target: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L43)
  Moves to `target` (0 to the message length).
- **Wire.WireReader** · [`export let remaining = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L52)
  The bytes left. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L56)
  The message length. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_u8 = func -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L60)
  One byte. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_u16 = func -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L71)
  A big-endian 16-bit value. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_u32 = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L82)
  A big-endian 32-bit unsigned value. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_bytes = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L96)
  The next `count` bytes. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_character_string = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L110)
  A character-string: a length byte and that many bytes (RFC 1035 section 3.3). Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_name = func -> Result<names.DnsName>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L118)
  A domain name, following compression pointers; the reader moves past the name as written here (past the first pointer when there is one). Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L173)
  The text "WireReader(offset N of M)".
- **Wire** · [`export let WireWriter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L179)
  A writer that builds one DNS message and remembers name offsets for compression. Tagged `#Final`, `#NotThreadSafe`.
- **Wire.WireWriter** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L186)
  An empty writer.
- **Wire.WireWriter** · [`export let bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L191)
  The bytes written so far (the writer's own list). Tagged `#NoDiscard`.
- **Wire.WireWriter** · [`export let length = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L195)
  The number of bytes written. Tagged `#NoDiscard`.
- **Wire.WireWriter** · [`export let write_u8 = func(value: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L198)
  Appends one byte; a value outside 0-255 traps with "invalid argument: u8".
- **Wire.WireWriter** · [`export let write_u16 = func(value: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L206)
  Appends a big-endian 16-bit value; outside 0-65535 traps with "invalid argument: u16".
- **Wire.WireWriter** · [`export let write_u32 = func(value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L215)
  Appends a big-endian 32-bit value; outside 0 to 2^32-1 traps with "invalid argument: u32".
- **Wire.WireWriter** · [`export let write_bytes = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L226)
  Appends bytes.
- **Wire.WireWriter** · [`export let write_character_string = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L233)
  Appends a character-string; more than 255 bytes traps with "invalid argument: character-string".
- **Wire.WireWriter** · [`export let patch_u16 = func(at: int, value: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L242)
  Overwrites a 16-bit value at `at` (for a length written after its data).
- **Wire.WireWriter** · [`export let write_name = func(name: names.DnsName, compress: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L253)
  Appends a name, ending in a pointer to an earlier copy of its longest known suffix when `compress` is true. Every suffix written at an offset below 16384 becomes a target for later names, whether or not this name was compressed.
- **Wire.WireWriter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/wire.nra#L278)
  The text "WireWriter(N bytes)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
