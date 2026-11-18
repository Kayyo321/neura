# `Std.Net.Quic.QuicConnection`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.QuicConnection`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.QuicConnection`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **QuicConnection** · [`export let QuicConnection = class implements MultiplexedTransport<int64, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L50)
  One QUIC connection. Tagged `#Final`, `#NoDiscard(reason: "a connection must be closed")`, `#NotThreadSafe`.
- **QuicConnection.QuicConnection** · [`export last client: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L58)
  True on the client side.
- **QuicConnection.QuicConnection** · [`export last alpn: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L60)
  The ALPN protocol the handshake selected.
- **QuicConnection.QuicConnection** · [`export last early_data_accepted: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L62)
  True when the server accepted this client's 0-RTT data.
- **QuicConnection.QuicConnection** · [`export let init = func(opened: socket.Handle, address: Optional<SocketAddr>, is_client: bool, protocol: string, early_accepted: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L69)
  Use `connect` or `QuicEndpoint.accept`; the handle comes from the library.
- **QuicConnection.QuicConnection** · [`export let peer_address = func -> Optional<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L80)
  The peer's address, or `None` when the host did not report it.
- **QuicConnection.QuicConnection** · [`export let open_stream = func(bidirectional: bool) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L84)
  Opens a stream of this side, bidirectional or send-only. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let accept_stream = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L91)
  Waits for the next stream the peer opens. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let send_on = func(stream: int64, frame: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L99)
  Sends `frame` on `stream`. A stream this side cannot send on (the peer's unidirectional stream) is a `quic.stream_state` error. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let receive_on = func(stream: int64) -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L108)
  The next bytes of `stream`, or `None` once the peer finished it. A stream this side cannot receive on (its own unidirectional stream) is a `quic.stream_state` error. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let finish_stream = func(stream: int64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L121)
  Finishes this side of `stream` (a STREAM frame with FIN). Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L129)
  Sends one unreliable datagram (an RFC 9221 DATAGRAM frame). Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L139)
  The next datagram, or `None` once the connection is closed. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let migrate = func(local: SocketAddr) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L151)
  Moves the connection to a new local address (client migration, RFC 9000 section 9), for example from Wi-Fi to cellular; the peer validates the new path. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let session_ticket = func(clock: Clock, server_name: string) -> Result<Optional<SessionTicket>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L161)
  The newest session ticket the server sent, stamped with the clock's time, or `None`. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L172)
  The longest a read or accept waits; zero waits without limit.
- **QuicConnection.QuicConnection** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L175)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **QuicConnection.QuicConnection** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L180)
  Closes the connection with application error 0 (CONNECTION_CLOSE). Closing again returns Ok.
- **QuicConnection.QuicConnection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L190)
  Text such as "QuicConnection(client, h3, 192.0.2.1:443)".
- **QuicConnection** · [`export let connect = func(address: SocketAddr, config: QuicConfig, ticket: Optional<SessionTicket>, early_bytes: uint8[list]) -> Result<QuicConnection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L218)
  Opens a QUIC connection to `address` with a client configuration (its TLS settings give the server name, CA file and ALPN). With a usable `ticket` and `early_data` enabled in the configuration, the ticket resumes the session and `early_bytes` go as 0-RTT data. Traps in Neura 0.3.0 (no QUIC host service). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
