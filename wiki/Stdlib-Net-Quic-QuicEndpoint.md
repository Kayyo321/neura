# `Std.Net.Quic.QuicEndpoint`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.QuicEndpoint`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.QuicEndpoint`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **QuicEndpoint** · [`export let QuicEndpoint = class implements Listener<SocketAddr, QuicConnection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L34)
  A listening QUIC endpoint. Tagged `#Final`, `#NoDiscard(reason: "an endpoint must be closed")`, `#NotThreadSafe`.
- **QuicEndpoint.QuicEndpoint** · [`export last config: QuicConfig`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L40)
  The server configuration.
- **QuicEndpoint.QuicEndpoint** · [`export let init = func(opened: socket.Handle, address: SocketAddr, settings: QuicConfig)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L45)
  Use `bind`; the handle comes from the library.
- **QuicEndpoint.QuicEndpoint** · [`export let address = func -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L52)
  The bound address.
- **QuicEndpoint.QuicEndpoint** · [`export let accept = func -> Result<QuicConnection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L56)
  Waits for the next connection and completes its handshake. Tagged `#NoDiscard`.
- **QuicEndpoint.QuicEndpoint** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L66)
  Closes every connection and releases the port. Closing again returns Ok.
- **QuicEndpoint.QuicEndpoint** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L76)
  Text such as "QuicEndpoint(0.0.0.0:443)".
- **QuicEndpoint** · [`export let bind = func(address: SocketAddr, config: QuicConfig) -> Result<QuicEndpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-endpoint.nra#L82)
  Listens for QUIC on `address` (port 0 asks for a free port) with a server configuration. Traps in Neura 0.3.0 (no QUIC host service). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
