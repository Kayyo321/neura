# `Std.Net.Tls.TlsStream`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.TlsStream`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.TlsStream`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TlsStream** · [`export let TlsStream = class implements StreamSocket<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L26)
  One TLS stream. Tagged `#Final`, `#NoDiscard(reason: "a stream must be closed")`, `#NotThreadSafe`.
- **TlsStream.TlsStream** · [`export last alpn: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L34)
  The ALPN protocol the handshake selected; empty when none.
- **TlsStream.TlsStream** · [`export let init = func(opened: socket.Handle, dialed: Optional<SocketAddr>, selected: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L41)
  Use `TlsConnector` or `TlsAcceptor`; the handle comes from the library.
- **TlsStream.TlsStream** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L51)
  Up to `max` decrypted bytes; an empty list once the peer has closed the stream. A maximum below 1 traps. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L62)
  Encrypts and writes every byte, repeating partial host writes. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L84)
  The next decrypted bytes (at most `ReceiveChunk`), or `None` at the end of the peer's data. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L94)
  Sends every byte; the `Std.Net.Transport` form of `write`. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L97)
  The longest a read waits; zero waits without limit.
- **TlsStream.TlsStream** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L100)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **TlsStream.TlsStream** · [`export let peer_address = func -> Optional<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L105)
  The dialed peer, or `None` for an accepted stream (not reported by the host).
- **TlsStream.TlsStream** · [`export let local_address = func -> Result<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L109)
  The local address, from the host; traps in Neura 0.3.0 (no local-address service). Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let shutdown = func(how: Shutdown) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L121)
  Ends reading, writing or both of the underlying TCP connection. Traps in Neura 0.3.0: the runtime has no shutdown service. `close` sends `close_notify`. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let enable_kernel_offload = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L133)
  Moves symmetric record encryption into the kernel (kTLS, and the NIC where it supports it). Traps in Neura 0.3.0: no runtime offers kTLS. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L143)
  Sends `close_notify` and releases the stream. Closing again returns Ok; the stream is closed even when the host reports an error.
- **TlsStream.TlsStream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L153)
  Text such as "TlsStream(to 192.0.2.1:443, alpn h2)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
