# `Std.Net.Rpc.Codec`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Codec`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Codec`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Codec** · [`export let RpcCodec = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L19)
  Encodes messages of type `T` to bytes and decodes them back.
- **Codec** · [`export let content_type = func<T>(codec: RpcCodec<T>) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L30)
  The gRPC content type of a codec: `application/grpc+<subtype>`. Tagged `#NoDiscard`.
- **Codec** · [`export let RawCodec = class implements RpcCodec<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L34)
  Bytes as they are; subtype `proto` by default, since raw gRPC payloads are usually protobuf. Tagged `#Final`.
- **Codec.RawCodec** · [`export let init = func(subtype_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L39)
  A raw codec reporting `subtype_name`.
- **Codec.RawCodec** · [`export let encode = func(value: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L44)
  The bytes unchanged.
- **Codec.RawCodec** · [`export let decode = func(bytes: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L47)
  The bytes unchanged.
- **Codec.RawCodec** · [`export let subtype = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L50)
  The subtype given.
- **Codec.RawCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L53)
  `RawCodec(subtype)`.
- **Codec** · [`export let JsonCodec = class implements RpcCodec<JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L58)
  JSON values as UTF-8 JSON text (RFC 8259), subtype `json`. Tagged `#Final`.
- **Codec.JsonCodec** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L60)
  A JSON codec.
- **Codec.JsonCodec** · [`export let encode = func(value: JsonValue) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L64)
  The compact JSON text of the value.
- **Codec.JsonCodec** · [`export let decode = func(bytes: uint8[list]) -> Result<JsonValue>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L67)
  Parses UTF-8 JSON text.
- **Codec.JsonCodec** · [`export let subtype = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L76)
  `json`.
- **Codec.JsonCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L79)
  `JsonCodec`.
- **Codec** · [`export let MappedJsonCodec = class<T> implements RpcCodec<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L84)
  A type mapped to JSON through two functions, subtype `json`. Tagged `#Final`.
- **Codec.MappedJsonCodec** · [`export let init = func(write: func(T) -> JsonValue, read: func(JsonValue) -> Result<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L91)
  A codec from the two mappings.
- **Codec.MappedJsonCodec** · [`export let encode = func(value: T) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L97)
  The JSON text of the mapped value.
- **Codec.MappedJsonCodec** · [`export let decode = func(bytes: uint8[list]) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L100)
  Parses JSON text, then maps it.
- **Codec.MappedJsonCodec** · [`export let subtype = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L109)
  `json`.
- **Codec.MappedJsonCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L112)
  `MappedJsonCodec`.
- **Codec** · [`export let ProtoCodec = class<T> implements RpcCodec<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L117)
  A type mapped to protobuf through a writer function and a reader function, subtype `proto`. Tagged `#Final`.
- **Codec.ProtoCodec** · [`export let init = func(write: func(T, proto.ProtoWriter) -> void, read: func(proto.ProtoReader) -> Result<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L124)
  A codec from the two functions.
- **Codec.ProtoCodec** · [`export let encode = func(value: T) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L130)
  The protobuf bytes of the value.
- **Codec.ProtoCodec** · [`export let decode = func(bytes: uint8[list]) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L137)
  Reads the value from protobuf bytes.
- **Codec.ProtoCodec** · [`export let subtype = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L140)
  `proto`.
- **Codec.ProtoCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L143)
  `ProtoCodec`.
- **Codec** · [`export let FlatBuffersCodec = class<T> implements RpcCodec<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L148)
  A FlatBuffers codec for the table type named `table` (subtype `flatbuffers`). Tagged `#Final`.
- **Codec.FlatBuffersCodec** · [`export last table: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L150)
  The fully qualified table type.
- **Codec.FlatBuffersCodec** · [`export let init = func(table_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L153)
  A codec for `table_name`.
- **Codec.FlatBuffersCodec** · [`export let encode = func(value: T) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L159)
  The FlatBuffers bytes of the value.
- **Codec.FlatBuffersCodec** · [`export let decode = func(bytes: uint8[list]) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L165)
  The value of FlatBuffers bytes.
- **Codec.FlatBuffersCodec** · [`export let subtype = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L170)
  `flatbuffers`.
- **Codec.FlatBuffersCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L173)
  `FlatBuffersCodec(table)`.
- **Codec** · [`export let CapnProtoCodec = class<T> implements RpcCodec<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L178)
  A Cap'n Proto codec for the struct type named `struct_name` (subtype `capnp`). Tagged `#Final`.
- **Codec.CapnProtoCodec** · [`export last struct_name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L180)
  The fully qualified struct type.
- **Codec.CapnProtoCodec** · [`export let init = func(name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L183)
  A codec for `name`.
- **Codec.CapnProtoCodec** · [`export let encode = func(value: T) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L189)
  The Cap'n Proto bytes of the value.
- **Codec.CapnProtoCodec** · [`export let decode = func(bytes: uint8[list]) -> Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L195)
  The value of Cap'n Proto bytes.
- **Codec.CapnProtoCodec** · [`export let subtype = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L200)
  `capnp`.
- **Codec.CapnProtoCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/codec.nra#L203)
  `CapnProtoCodec(struct)`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
