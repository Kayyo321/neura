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

- **TlsStream** · [`export let TlsStream = class implements StreamSocket<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L30)
  One TLS stream. Tagged `#Final`, `#NoDiscard(reason: "a stream must be closed")`, `#NotThreadSafe`.
- **TlsStream.TlsStream** · [`export last alpn: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L38)
  The ALPN protocol the handshake selected; empty when none.
- **TlsStream.TlsStream** · [`export let init = func(opened: socket.Handle, dialed: Optional<SocketAddr>, selected: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L47)
  Use `TlsConnector` or `TlsAcceptor`; the handle comes from the library.
- **TlsStream.TlsStream** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L57)
  Up to `max` decrypted bytes; an empty list once the peer has closed the stream. A maximum below 1 traps. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L70)
  Encrypts and writes every byte, repeating partial host writes. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L94)
  The next decrypted bytes (at most `ReceiveChunk`), or `None` at the end of the peer's data. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L102)
  Sends every byte; the `Std.Net.Transport` form of `write`. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L105)
  The longest a read waits; zero waits without limit.
- **TlsStream.TlsStream** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L108)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **TlsStream.TlsStream** · [`export let peer_address = func -> Optional<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L113)
  The dialed peer, or `None` for an accepted stream (not reported by the host).
- **TlsStream.TlsStream** · [`export let local_address = func -> Result<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L117)
  The underlying socket's local address. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let shutdown = func(how: Shutdown) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L128)
  Ends reading or sends an authenticated `close_notify` to end writing. The socket stays open. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let enable_kernel_offload = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L149)
  Requires kernel record processing in both directions. Set `TlsConfigBuilder.set_kernel_offload` before the handshake. Returns Unsupported when the build, OS or negotiated cipher cannot offload. Tagged `#NoDiscard`.
- **TlsStream.TlsStream** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L159)
  Sends `close_notify` and releases the stream. Closing again returns Ok; the stream is closed even when the host reports an error.
- **TlsStream.TlsStream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-stream.nra#L171)
  Text such as "TlsStream(to 192.0.2.1:443, alpn h2)".
<!-- END SOURCE API -->

## Detailed contracts

`close` sends TLS `close_notify` and releases the stream. A write half-close sends `close_notify` and leaves the stream open for reading. `enable_kernel_offload` checks both directions after a handshake that requested offload. It returns typed `Unsupported` where the kernel, OpenSSL build or negotiated parameters cannot offload.

See [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) and the source comments for the complete module contract.
