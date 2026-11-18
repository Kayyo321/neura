# `Std.Net.Mesh.ProxyProtocolListener`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.ProxyProtocolListener`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.ProxyProtocolListener`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ProxyProtocolListener** · [`export const InvalidCode = "mesh.proxy_header_invalid"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L27)
  The `Custom` error code for a connection whose PROXY header is invalid or incomplete.
- **ProxyProtocolListener** · [`export let ProxiedConnection = class<C>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L31)
  An accepted connection with the PROXY header already read. Tagged `#Final`, `#NotThreadSafe`.
- **ProxyProtocolListener.ProxiedConnection** · [`export last connection: C`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L33)
  The underlying connection, positioned after the header (and after `prefix`).
- **ProxyProtocolListener.ProxiedConnection** · [`export last header: pp.ProxyHeader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L35)
  The header the peer sent.
- **ProxyProtocolListener.ProxiedConnection** · [`export last prefix: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L37)
  Bytes read past the header, which belong to the application stream before any further read.
- **ProxyProtocolListener.ProxiedConnection** · [`export let init = func(conn: C, received: pp.ProxyHeader, extra: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L40)
  Pairs a connection with its header and the bytes read past it.
- **ProxyProtocolListener.ProxiedConnection** · [`export let source = func -> Optional<sockets.SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L49)
  The real client address, or None for LOCAL, UNKNOWN, Unix and UNSPEC headers, where the connection's own peer address applies. Tagged `#NoDiscard`.
- **ProxyProtocolListener.ProxiedConnection** · [`export let destination = func -> Optional<sockets.SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L53)
  The address the client originally connected to, when the header carries one. Tagged `#NoDiscard`.
- **ProxyProtocolListener.ProxiedConnection** · [`export let client_or = func(peer: sockets.SocketAddr) -> sockets.SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L57)
  The client address if the header gives one, else `peer` (the connection's own peer address). Tagged `#NoDiscard`.
- **ProxyProtocolListener.ProxiedConnection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L63)
  Text such as "ProxiedConnection(ProxyHeader(...), 0 prefix bytes)".
- **ProxyProtocolListener** · [`export let ProxyProtocolListener = class<A, C> implements Listener<A, ProxiedConnection<C>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L69)
  A listener whose connections start with a PROXY header (version 1 or 2). Tagged `#Final`, `#NotThreadSafe`.
- **ProxyProtocolListener.ProxyProtocolListener** · [`export let init = func(listener: Listener<A, C>, reader: func(C, int) -> Result<uint8[list]>, closer: func(C) -> Result<Unit>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L78)
  Wraps `listener`, reading through `reader` and closing rejected connections with `closer`.
- **ProxyProtocolListener.ProxyProtocolListener** · [`export let address = func -> A`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L86)
  The wrapped listener's address.
- **ProxyProtocolListener.ProxyProtocolListener** · [`export let accept = func -> Result<ProxiedConnection<C>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L89)
  Accepts the next connection and reads its PROXY header, as the module describes.
- **ProxyProtocolListener.ProxyProtocolListener** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L126)
  Closes the wrapped listener.
- **ProxyProtocolListener.ProxyProtocolListener** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L129)
  The fixed text "ProxyProtocolListener".
- **ProxyProtocolListener** · [`export let ProxyProtocolConnector = class<A, C> implements Connector<A, C>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L140)
  A connector that writes a PROXY header on each new connection before returning it. Tagged `#Final`, `#NotThreadSafe`.
- **ProxyProtocolListener.ProxyProtocolConnector** · [`export let init = func(connector: Connector<A, C>, header: func(C) -> Result<uint8[list]>, writer: func(C, uint8[list]) -> Result<Unit>, closer: func(C) -> Result<Unit>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L151)
  Wraps `connector`; `header` gives each connection's header bytes.
- **ProxyProtocolListener.ProxyProtocolConnector** · [`export let address = func -> A`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L160)
  The wrapped connector's address.
- **ProxyProtocolListener.ProxyProtocolConnector** · [`export let connect = func -> Result<C>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L164)
  Connects and writes the header first. If building or writing the header fails, the connection is closed and the failure returned.
- **ProxyProtocolListener.ProxyProtocolConnector** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L186)
  The fixed text "ProxyProtocolConnector".
- **ProxyProtocolListener** · [`export let fixed_header = func<C>(header: pp.ProxyHeader, with_crc: bool) -> func(C) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol-listener.nra#L192)
  The header bytes of a fixed `header` (in its own version, version 2 with a CRC32C when `with_crc`), as a `header_for` function that ignores the connection. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
