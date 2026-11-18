# `Std.Net.Rpc.Framing`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Framing`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Framing`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Framing** · [`export const PrefixSize: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L21)
  The size of a gRPC message prefix.
- **Framing** · [`export const CompressedFlag: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L23)
  Flag bit 0: the message is compressed with the call's `grpc-encoding`.
- **Framing** · [`export const TrailerFlag: int = 128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L25)
  Flag bit 7: a gRPC-Web trailer frame.
- **Framing** · [`export const DefaultMaxMessageSize: int = 4194304`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L28)
  The default largest message a decoder accepts: 4 MiB (a decision of this library, matching common gRPC receive limits).
- **Framing** · [`export let Compression = enum (Identity, Gzip, Deflate)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L31)
  The `grpc-encoding` of a message.
- **Framing** · [`export let compression_name = func(compression: Compression) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L35)
  The `grpc-encoding` token: `identity`, `gzip` or `deflate`. Tagged `#NoDiscard`.
- **Framing** · [`export let compression_of = func(token: string) -> Optional<Compression>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L44)
  The compression named by a `grpc-encoding` token, or `None` for an unknown one (a server answers UNIMPLEMENTED). Tagged `#NoDiscard`.
- **Framing** · [`export let compress = func(compression: Compression, payload: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L57)
  Compresses one message; identity returns the bytes unchanged. Tagged `#NoDiscard`.
- **Framing** · [`export let decompress = func(compression: Compression, payload: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L66)
  Decompresses one message; identity returns the bytes unchanged. Tagged `#NoDiscard`.
- **Framing** · [`export let Frame = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L75)
  One length-prefixed frame: its flags byte and its message bytes as sent. Tagged `#Final`.
- **Framing.Frame** · [`export last flags: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L77)
  The flags byte.
- **Framing.Frame** · [`export last payload: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L79)
  The message bytes (compressed when the compressed flag is set).
- **Framing.Frame** · [`export let init = func(bits: int, data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L82)
  A frame with flags `bits` (0 to 255; otherwise traps with "invalid argument: flags").
- **Framing.Frame** · [`export let is_compressed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L92)
  Whether the compressed flag is set. Tagged `#NoDiscard`.
- **Framing.Frame** · [`export let is_trailer = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L96)
  Whether this is a gRPC-Web trailer frame. Tagged `#NoDiscard`.
- **Framing.Frame** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L99)
  Equal when the flags and bytes are.
- **Framing.Frame** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L115)
  Combines the flags and bytes.
- **Framing.Frame** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L124)
  The flags and size.
- **Framing** · [`export let append_frame = func(out: uint8[list], flags: int, payload: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L130)
  Appends a length-prefixed frame with `flags` to `out`. A payload of 2^32 bytes or more traps with "invalid argument: message too large".
- **Framing** · [`export let encode_message = func(payload: uint8[list], compressed: bool) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L147)
  The length-prefixed encoding of one message. Tagged `#NoDiscard`.
- **Framing** · [`export let encode_with = func(compression: Compression, payload: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L155)
  Compresses `payload` with `compression` (unless identity) and frames it with the matching flag. Tagged `#NoDiscard`.
- **Framing** · [`export let message_of = func(frame: Frame, compression: Compression) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L166)
  The message bytes of a frame received on a call whose `grpc-encoding` is `compression`: fails with INTERNAL when the compressed flag is set on an identity call (PROTOCOL-HTTP2.md "Compressed-Flag"). Tagged `#NoDiscard`.
- **Framing** · [`export let MessageDecoder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L180)
  Decodes length-prefixed frames from chunks of any size. With `web` false only flags 0 and 1 are valid; with `web` true flags 128 and 129 (trailer frames) are too. Any other flags byte fails. Tagged `#Final`, `#NotThreadSafe`.
- **Framing.MessageDecoder** · [`export let init = func(max_message_size: int, grpc_web: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L193)
  A decoder; a maximum below 0 traps with "invalid argument: max_size".
- **Framing.MessageDecoder** · [`export let push = func(chunk: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L202)
  Adds received bytes.
- **Framing.MessageDecoder** · [`export let buffered = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L210)
  The number of bytes not yet decoded. Tagged `#NoDiscard`.
- **Framing.MessageDecoder** · [`export let next = func -> Result<Optional<Frame>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L214)
  The next whole frame, `None` when more bytes are needed, or the failure of a bad frame. Tagged `#NoDiscard`.
- **Framing.MessageDecoder** · [`export let finish = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L248)
  Checks the stream ended on a frame boundary: leftover bytes fail with INTERNAL. Tagged `#NoDiscard`.
- **Framing.MessageDecoder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L257)
  The buffered byte count.
- **Framing** · [`export let decode_all = func(body: uint8[list], max_message_size: int, grpc_web: bool) -> Result<Frame[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L282)
  Every frame of a complete body; fails on a bad frame or a body that ends inside a frame. Tagged `#NoDiscard`.
- **Framing** · [`export let encode_delimited = func(payload: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L304)
  The varint-delimited encoding of one message: a base-128 varint length, then the bytes (the protobuf `writeDelimitedTo` form). Tagged `#NoDiscard`.
- **Framing** · [`export let DelimitedDecoder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L315)
  Decodes varint-delimited messages from chunks of any size. Tagged `#Final`, `#NotThreadSafe`.
- **Framing.DelimitedDecoder** · [`export let init = func(max_message_size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L326)
  A decoder; a maximum below 0 traps with "invalid argument: max_size".
- **Framing.DelimitedDecoder** · [`export let push = func(chunk: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L334)
  Adds received bytes.
- **Framing.DelimitedDecoder** · [`export let next = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L343)
  The next whole message, `None` when more bytes are needed, or a failure: a varint over 10 bytes or 64 bits (INTERNAL) or a length above the maximum (RESOURCE_EXHAUSTED). Tagged `#NoDiscard`.
- **Framing.DelimitedDecoder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/framing.nra#L391)
  The buffered byte count.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
