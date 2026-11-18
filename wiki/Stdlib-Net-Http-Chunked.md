# `Std.Net.Http.Chunked`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Chunked`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Chunked`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Chunked** · [`export let ChunkedDecoder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L26)
  An incremental decoder of one chunked body. After the last chunk and the trailer section, the decoder is done and further bytes, including those of the feed that finished it, are kept as the remainder (the next message on the connection). After an error every `feed` returns the same error. Tagged `#Final`, `#NotThreadSafe`.
- **Chunked.ChunkedDecoder** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L53)
  A decoder with the default limits.
- **Chunked.ChunkedDecoder** · [`export let feed = func(bytes: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L63)
  Decodes the next bytes of the body; Ok holds the data they completed, possibly empty. Tagged `#NoDiscard`.
- **Chunked.ChunkedDecoder** · [`export let is_done = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L152)
  Whether the last chunk and the trailer section have been read.
- **Chunked.ChunkedDecoder** · [`export let trailers = func -> http_headers.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L155)
  The trailer fields; empty until done.
- **Chunked.ChunkedDecoder** · [`export let data_length = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L158)
  The data bytes decoded so far.
- **Chunked.ChunkedDecoder** · [`export let remainder = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L162)
  The bytes received after the body, as a new list. Tagged `#NoDiscard`.
- **Chunked.ChunkedDecoder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L171)
  The decoder's progress as text.
- **Chunked** · [`export let encode_chunk = func(data: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L252)
  One chunk holding `data`: its size in lowercase hex, CRLF, the data, CRLF. Empty data gives an empty list, since a zero-size chunk would end the body. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Chunked** · [`export let last_chunk = func(trailer_fields: http_headers.Headers) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L270)
  The last chunk "0" CRLF, the trailer fields as "Name: value" CRLF lines, and the final CRLF. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Chunked** · [`export let encode = func(data: uint8[list], chunk_size: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L283)
  The whole chunked body of `data` in chunks of `chunk_size` bytes (the last may be shorter), then the last chunk with no trailer. Traps with "invalid argument: chunk_size" unless it is positive. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Chunked** · [`export let decode = func(body: uint8[list], limits: wire.Limits) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/chunked.nra#L309)
  The data of a complete chunked body. 400 when the body ends early or bytes follow it; other errors as `ChunkedDecoder.feed`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
