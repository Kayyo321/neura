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

- **QuicConnection** · [`export const StackStreamLimit: int64 = 100`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L36)
  The stream limit the pinned stack advertises; any other configured limit is unsupported. Since 0.3.1.
- **QuicConnection** · [`export const StackIdleTimeoutNanos: int64 = 30000000000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L39)
  The idle timeout the connections use, the stack's default; any other configured value is refused. Since 0.3.1.
- **QuicConnection** · [`export const StackDatagramSize: int64 = 1200`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L42)
  The maximum UDP payload the connections send, the stack's default; any other value is refused. Since 0.3.1.
- **QuicConnection** · [`export let QuicConnection = class implements MultiplexedTransport<int64, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L46)
  One QUIC connection. Tagged `#Final`, `#Immutable`.
- **QuicConnection.QuicConnection** · [`export last client: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L52)
  True on the client side.
- **QuicConnection.QuicConnection** · [`export last alpn: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L54)
  The ALPN protocol the handshake selected.
- **QuicConnection.QuicConnection** · [`export let init = func(connection: int64, address: string, is_client: bool, protocol: string, default_timeout: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L59)
  Use `connect` or `QuicEndpoint.accept`; the ID comes from the library.
- **QuicConnection.QuicConnection** · [`export let timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L70)
  The default timeout of waiting operations; zero waits without limit. Since 0.3.1.
- **QuicConnection.QuicConnection** · [`export let open = func(bidirectional: bool) -> Result<QuicStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L75)
  Opens a stream of this side, bidirectional or send-only, without waiting. Fails with `quic.busy` at the peer's stream limit. Tagged `#NoDiscard`. Since 0.3.1.
- **QuicConnection.QuicConnection** · [`export let accept = func -> Result<QuicStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L86)
  Waits for the next stream the peer opens, with the default timeout or `timeout`. Tagged `#NoDiscard`. Since 0.3.1.
- **QuicConnection.QuicConnection** · [`export let open_stream = func(bidirectional: bool) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L91)
  As `open`, returning the stream ID (`MultiplexedTransport`). Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let accept_stream = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L98)
  As `accept`, returning the stream ID (`MultiplexedTransport`). Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let send_on = func(stream: int64, frame: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L106)
  Sends `frame` on `stream`. A stream this side cannot send on (the peer's unidirectional stream) is a `quic.stream_state` error. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let receive_on = func(stream: int64) -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L113)
  The next bytes of `stream`, or `None` once the peer finished it. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let finish_stream = func(stream: int64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L120)
  Finishes this side of `stream` (a STREAM frame with FIN). Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L128)
  Unsupported: the pinned QUIC stack has no RFC 9221 datagrams. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L132)
  Unsupported: the pinned QUIC stack has no RFC 9221 datagrams. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let migrate = func(local: SocketAddr) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L137)
  Unsupported: the pinned QUIC stack has no application-directed migration. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let session_ticket = func -> Result<Optional<SessionTicket>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L142)
  Unsupported until session resumption is offered. Tagged `#NoDiscard`.
- **QuicConnection.QuicConnection** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L146)
  Closes the connection with application error 0 (CONNECTION_CLOSE).
- **QuicConnection.QuicConnection** · [`export let close_with = func(code: int64, reason: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L156)
  Closes the connection with an application error code (0 through 2^62 - 1) and a reason, after the streams' buffered data. Every handle of the connection fails with `Closed` from the start. `true` when the connection terminated within the default timeout (or two seconds without one), `false` when it was released before that. Since 0.3.1.
- **QuicConnection.QuicConnection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L164)
  Text such as "QuicConnection(client, h3, 192.0.2.1:443)".
- **QuicConnection** · [`export let connect = func(address: SocketAddr, config: QuicConfig, ticket: Optional<SessionTicket>, early_bytes: uint8[list]) -> Result<QuicConnection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L189)
  Opens a QUIC connection to `address` with a client configuration (its TLS settings give the server name, CA file and ALPN). The handshake is bounded by the TLS handshake timeout, which is also the default timeout of later waiting operations. A ticket, 0-RTT bytes, or a configuration the pinned stack cannot honour (see the module documentation) fail with `Unsupported` before any host call. Tagged `#NoDiscard`.
- **QuicConnection** · [`export let connection_of = func(opened: [int64, string], address: string, is_client: bool, nanos: int64) -> QuicConnection`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L215)
  A connection handle from the host's [ID, ALPN]. Library use: `QuicEndpoint.accept` builds its connections with it. Since 0.3.1.
- **QuicConnection** · [`export let check_limits = func(config: QuicConfig) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-connection.nra#L224)
  `Ok` when the pinned QUIC stack can honour `config`, else the typed `Unsupported` of the first setting it cannot. Tagged `#NoDiscard`. Since 0.3.1.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
