# `Std.Net.Socket.TcpServer`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.TcpServer`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.TcpServer`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpServer** · [`export let TcpServer = class implements Listener<SocketAddr, TcpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L32)
  A listening TCP socket that yields `TcpSocket`s. Tagged `#Final`, `#NoDiscard(reason: "a server must be closed")`, `#NotThreadSafe`.
- **TcpServer.TcpServer** · [`export last multipath: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L40)
  True when the listener uses Multipath TCP.
- **TcpServer.TcpServer** · [`export let init = func(opened: socket.Handle, address: SocketAddr, uses_multipath: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L45)
  Use `bind`; the handle comes from the library.
- **TcpServer.TcpServer** · [`export let address = func -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L54)
  The bound address; its port is the one the host chose for a bind to port 0. Still available after close.
- **TcpServer.TcpServer** · [`export let local_port = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L57)
  The bound port.
- **TcpServer.TcpServer** · [`export let accept = func -> Result<TcpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L62)
  Waits for the next connection. Fails with Closed after close, and with Cancelled when the task's scope is cancelled first. Tagged `#NoDiscard`.
- **TcpServer.TcpServer** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L74)
  Releases the listener, as `TcpListener.close`. Closing again returns Ok.
- **TcpServer.TcpServer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L80)
  Text such as "TcpServer(127.0.0.1:8080)".
- **TcpServer** · [`export let bind = func(address: SocketAddr) -> Result<TcpServer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L85)
  Listens on `address` with no options. Port 0 asks the host for a free port. Tagged `#NoDiscard`.
- **TcpServer** · [`export let bind_with = func(address: SocketAddr, options: SocketOptions) -> Result<TcpServer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-server.nra#L92)
  Listens on `address` with an option set built for `SocketKind.TcpListener` (another kind traps with "invalid argument"). A non-empty set or multipath traps in Neura 0.3.0 (see the module documentation); with multipath, every option is applied after the listener is created. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
