# `Std.Net.Rpc.GrpcWeb`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.GrpcWeb`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.GrpcWeb`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **GrpcWeb** · [`export const ContentType: string = "application/grpc-web"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L22)
  The binary content type prefix (`+proto`, `+json` follow).
- **GrpcWeb** · [`export const TextContentType: string = "application/grpc-web-text"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L24)
  The text (base64) content type prefix.
- **GrpcWeb** · [`export const TrailerFrameFlags: int = 128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L26)
  The flags of an uncompressed trailer frame.
- **GrpcWeb** · [`export let trailer_block = func(trailers: metadata.Metadata) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L30)
  The payload of a trailer frame: `name: value\r\n` for each entry, in order. Tagged `#NoDiscard`.
- **GrpcWeb** · [`export let encode_trailers = func(trailers: metadata.Metadata) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L43)
  The length-prefixed trailer frame (flags 0x80) of `trailers`. Tagged `#NoDiscard`.
- **GrpcWeb** · [`export let parse_trailer_block = func(block: uint8[list]) -> Result<metadata.Metadata>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L53)
  Parses a trailer block: lines ending in CRLF (a final line without one is accepted), each `name: value` with optional whitespace around the value. A line without `:` or with an empty name fails with a Parse error. Names are lower-cased. Tagged `#NoDiscard`.
- **GrpcWeb** · [`export let WebBody = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L77)
  A decoded gRPC-Web response body: its messages in order and its trailers. Tagged `#Final`.
- **GrpcWeb.WebBody** · [`export last messages: framing.Frame[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L79)
  The message frames before the trailers.
- **GrpcWeb.WebBody** · [`export last trailers: metadata.Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L81)
  The trailers (empty when the body had no trailer frame).
- **GrpcWeb.WebBody** · [`export last has_trailers: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L83)
  Whether a trailer frame was present.
- **GrpcWeb.WebBody** · [`export let init = func(frames: framing.Frame[list], trailer_metadata: metadata.Metadata, present: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L86)
  A body with every part given.
- **GrpcWeb.WebBody** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L93)
  The message count and trailers.
- **GrpcWeb** · [`export let decode_body = func(body: uint8[list], max_message_size: int) -> Result<WebBody>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L101)
  Splits a complete gRPC-Web response body into messages and trailers. Fails with INTERNAL when a frame follows the trailer frame, when the trailer frame is compressed (no DEFLATE in this library), or when the trailer block is malformed; framing failures as in `Framing.MessageDecoder`. Tagged `#NoDiscard`.
- **GrpcWeb** · [`export let encode_text = func(bytes: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L134)
  The text variant of bytes: padded standard base64. A sender may encode each flush separately and concatenate the texts. Tagged `#NoDiscard`.
- **GrpcWeb** · [`export let TextDecoder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L139)
  Decodes a text-variant stream that arrives in chunks: whole 4-character groups are decoded as they arrive, and padding may end any group, not only the last. Tagged `#Final`, `#NotThreadSafe`.
- **GrpcWeb.TextDecoder** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L144)
  An empty decoder.
- **GrpcWeb.TextDecoder** · [`export let push = func(chunk: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L150)
  Decodes the whole groups of `pending + chunk`; keeps the rest. A non-ASCII byte or a group with characters outside the alphabet fails with an Encoding error. Tagged `#NoDiscard`.
- **GrpcWeb.TextDecoder** · [`export let is_complete = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L171)
  Whether the stream ended on a group boundary. Tagged `#NoDiscard`.
- **GrpcWeb.TextDecoder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L174)
  The pending character count.
- **GrpcWeb** · [`export let decode_text = func(text: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc-web.nra#L180)
  Decodes a complete text-variant body; fails on bad base64 or a body that ends inside a group. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
