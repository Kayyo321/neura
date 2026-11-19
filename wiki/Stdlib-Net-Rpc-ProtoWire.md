# `Std.Net.Rpc.ProtoWire`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.ProtoWire`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.ProtoWire`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ProtoWire** · [`export const Varint: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L19)
  Wire type 0: a base-128 varint.
- **ProtoWire** · [`export const Fixed64: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L21)
  Wire type 1: 8 little-endian bytes.
- **ProtoWire** · [`export const LengthDelimited: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L23)
  Wire type 2: a varint length and that many bytes.
- **ProtoWire** · [`export const StartGroup: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L25)
  Wire type 3: start of a group (deprecated; rejected by the reader).
- **ProtoWire** · [`export const EndGroup: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L27)
  Wire type 4: end of a group (deprecated; rejected by the reader).
- **ProtoWire** · [`export const Fixed32: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L29)
  Wire type 5: 4 little-endian bytes.
- **ProtoWire** · [`export const MaxFieldNumber: int = 536870911`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L31)
  The largest field number (2^29 - 1).
- **ProtoWire** · [`export const MaxVarintBytes: int = 10`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L33)
  The longest varint in bytes.
- **ProtoWire** · [`export let append_varint = func(out: uint8[list], value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L43)
  Appends the varint encoding of `value` to `out`.
- **ProtoWire** · [`export let varint = func(value: uint64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L54)
  The varint encoding of `value`. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let varint_size = func(value: uint64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L62)
  The number of bytes the varint encoding of `value` takes. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let int64_bits = func(value: int64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L74)
  The two's-complement bit pattern of `value`. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let bits_int64 = func(bits: uint64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L81)
  The `int64` whose two's-complement bit pattern is `bits`. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let low_int32 = func(bits: uint64) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L88)
  The `int32` held in the low 32 bits of `bits`, as protobuf reads an `int32` field. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let zigzag64 = func(value: int64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L96)
  The zigzag encoding of a 64-bit signed value: 0, -1, 1, -2 become 0, 1, 2, 3. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let unzigzag64 = func(encoded: uint64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L103)
  The 64-bit signed value of a zigzag encoding. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let zigzag32 = func(value: int32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L110)
  The zigzag encoding of a 32-bit signed value. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let unzigzag32 = func(encoded: uint32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L114)
  The 32-bit signed value of a zigzag encoding. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let tag = func(field: int, wire_type: int) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L119)
  The tag of field `field` with wire type `wire_type`. A field outside 1 to 536870911 traps with "invalid argument: field number"; a wire type outside 0 to 5 traps with "invalid argument: wire type". Tagged `#NoDiscard`.
- **ProtoWire** · [`export let ProtoWriter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L131)
  Builds one protobuf message by appending fields. Tagged `#Final`, `#NotThreadSafe`.
- **ProtoWire.ProtoWriter** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L136)
  An empty writer.
- **ProtoWire.ProtoWriter** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L141)
  The bytes written so far (the writer's own list). Tagged `#NoDiscard`.
- **ProtoWire.ProtoWriter** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L145)
  The number of bytes written. Tagged `#NoDiscard`.
- **ProtoWire.ProtoWriter** · [`export let write_tag = func(field: int, wire_type: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L148)
  Writes a tag.
- **ProtoWire.ProtoWriter** · [`export let write_uint64 = func(field: int, value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L153)
  Writes a varint field holding `value` (`uint64`, or `uint32` widened).
- **ProtoWire.ProtoWriter** · [`export let write_uint32 = func(field: int, value: uint32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L159)
  Writes a `uint32` field.
- **ProtoWire.ProtoWriter** · [`export let write_int64 = func(field: int, value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L162)
  Writes an `int64` field; a negative value takes 10 bytes.
- **ProtoWire.ProtoWriter** · [`export let write_int32 = func(field: int, value: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L165)
  Writes an `int32` field; a negative value is sign-extended to 10 bytes.
- **ProtoWire.ProtoWriter** · [`export let write_sint64 = func(field: int, value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L168)
  Writes a `sint64` field (zigzag).
- **ProtoWire.ProtoWriter** · [`export let write_sint32 = func(field: int, value: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L171)
  Writes a `sint32` field (zigzag).
- **ProtoWire.ProtoWriter** · [`export let write_bool = func(field: int, value: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L174)
  Writes a `bool` field as 0 or 1.
- **ProtoWire.ProtoWriter** · [`export let write_enum = func(field: int, value: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L177)
  Writes an enum field by its number.
- **ProtoWire.ProtoWriter** · [`export let write_fixed32 = func(field: int, value: uint32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L180)
  Writes a `fixed32` field.
- **ProtoWire.ProtoWriter** · [`export let write_fixed64 = func(field: int, value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L186)
  Writes a `fixed64` field.
- **ProtoWire.ProtoWriter** · [`export let write_sfixed32 = func(field: int, value: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L192)
  Writes an `sfixed32` field.
- **ProtoWire.ProtoWriter** · [`export let write_sfixed64 = func(field: int, value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L196)
  Writes an `sfixed64` field.
- **ProtoWire.ProtoWriter** · [`export let write_float = func(field: int, value: float32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L199)
  Writes a `float` field (IEEE 754 binary32).
- **ProtoWire.ProtoWriter** · [`export let write_double = func(field: int, value: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L202)
  Writes a `double` field (IEEE 754 binary64).
- **ProtoWire.ProtoWriter** · [`export let write_bytes = func(field: int, value: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L205)
  Writes a length-delimited `bytes` field.
- **ProtoWire.ProtoWriter** · [`export let write_string = func(field: int, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L214)
  Writes a `string` field as its UTF-8 bytes.
- **ProtoWire.ProtoWriter** · [`export let write_message = func(field: int, message: ProtoWriter)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L223)
  Writes an embedded message field from another writer's bytes.
- **ProtoWire.ProtoWriter** · [`export let write_packed_uint64 = func(field: int, values: uint64[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L226)
  Writes a packed repeated varint field (`repeated uint64 [packed = true]`).
- **ProtoWire.ProtoWriter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L235)
  The number of bytes written.
- **ProtoWire** · [`export let FieldKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L247)
  A field's number and wire type, as read from its tag. Tagged `#Final`.
- **ProtoWire.FieldKey** · [`export last field: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L249)
  The field number, 1 to 536870911.
- **ProtoWire.FieldKey** · [`export last wire_type: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L251)
  The wire type, 0, 1, 2 or 5.
- **ProtoWire.FieldKey** · [`export let init = func(number: int, kind: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L254)
  A key with both parts.
- **ProtoWire.FieldKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L260)
  Equal when the number and wire type are.
- **ProtoWire.FieldKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L268)
  Combines the number and wire type.
- **ProtoWire.FieldKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L272)
  `field N, wire type W`.
- **ProtoWire** · [`export let ProtoReader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L281)
  Reads the fields of one protobuf message in order. Call `has_next`, then `read_key`, then the reader that fits the key's wire type, or `skip` for a field the program does not know. Offsets in errors are relative to the start of the bytes given to `of`. Tagged `#Final`, `#NotThreadSafe`.
- **ProtoWire.ProtoReader** · [`export let init = func(bytes: uint8[list], start: int, stop: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L290)
  A reader over `data[start, stop)`; a range outside the list traps with "invalid argument: range".
- **ProtoWire.ProtoReader** · [`export let has_next = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L301)
  Whether bytes remain. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let position = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L305)
  The offset of the next unread byte. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_key = func -> Result<FieldKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L309)
  Reads a tag and checks its field number and wire type. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_varint = func -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L326)
  Reads a varint of at most 10 bytes whose value fits 64 bits. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_uint64 = func -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L349)
  Reads a `uint64` varint. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_uint32 = func -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L353)
  Reads a `uint32` varint (the low 32 bits, as protobuf parsers do). Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_int64 = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L357)
  Reads an `int64` varint. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_int32 = func -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L361)
  Reads an `int32` varint (the low 32 bits as a signed value). Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_sint64 = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L365)
  Reads a `sint64` (zigzag) varint. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_sint32 = func -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L369)
  Reads a `sint32` (zigzag) varint. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_bool = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L374)
  Reads a `bool`: any nonzero varint is true. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_fixed32 = func -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L378)
  Reads a `fixed32`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_fixed64 = func -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L382)
  Reads a `fixed64`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_sfixed32 = func -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L386)
  Reads an `sfixed32`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_sfixed64 = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L390)
  Reads an `sfixed64`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_float = func -> Result<float32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L394)
  Reads a `float`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_double = func -> Result<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L398)
  Reads a `double`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_bytes = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L402)
  Reads a length-delimited field's bytes. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_string = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L413)
  Reads a length-delimited field as UTF-8 text; invalid UTF-8 is an Encoding error. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_message = func -> Result<ProtoReader>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L425)
  A reader over an embedded message field, without copying its bytes. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let read_packed_uint64 = func -> Result<uint64[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L432)
  Reads a packed repeated varint field. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let skip = func(wire_type: int) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L443)
  Skips the value of a field with wire type `wire_type`. Tagged `#NoDiscard`.
- **ProtoWire.ProtoReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L459)
  The position and the end.
- **ProtoWire** · [`export let reader = func(bytes: uint8[list]) -> ProtoReader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L493)
  A reader over all of `bytes`. Tagged `#NoDiscard`.
- **ProtoWire** · [`export let writer = func -> ProtoWriter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/proto-wire.nra#L497)
  A new empty writer. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
