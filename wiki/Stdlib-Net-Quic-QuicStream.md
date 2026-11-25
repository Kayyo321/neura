# `Std.Net.Quic.QuicStream`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.QuicStream`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.QuicStream`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **QuicStream** · [`export let QuicStream = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L27)
  A stream of a connection. Tagged `#Final`, `#Immutable`.
- **QuicStream.QuicStream** · [`export last id: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L31)
  The QUIC stream ID (RFC 9000 section 2.1).
- **QuicStream.QuicStream** · [`export last local: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L33)
  True when this side opened the stream.
- **QuicStream.QuicStream** · [`export let init = func(owner: int64, stream: int64, opened_here: bool, default_timeout: int64, address: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L40)
  Use `QuicConnection.open` or `accept`; the IDs come from the library.
- **QuicStream.QuicStream** · [`export let bidirectional = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L50)
  Whether data can flow both ways on this stream.
- **QuicStream.QuicStream** · [`export let read = func(maximum: int) -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L55)
  Up to `maximum` (1 or more) bytes, or `None` once the peer finished the stream. Uses the connection's default timeout, or `timeout` (zero: none). Tagged `#NoDiscard`.
- **QuicStream.QuicStream** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L62)
  Sends all of `data`; complete once the provider has accepted every byte for sending. Uses the connection's default timeout, or `timeout`. Tagged `#NoDiscard`.
- **QuicStream.QuicStream** · [`export let finish = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L70)
  Finishes this side of the stream (a STREAM frame with FIN) and waits, within the default timeout or `timeout`, until the peer has acknowledged every byte written to it. `true` when it did, `false` when the timeout passed first (the FIN is still sent). Without a timeout it waits for the acknowledgment. Tagged `#NoDiscard`.
- **QuicStream.QuicStream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-stream.nra#L74)
  Text such as "QuicStream(4, 127.0.0.1:4433)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.1. The source and the [language status](Status) identify shipped behavior.
