# `Std.Net.Socket.TcpConnector`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.TcpConnector`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.TcpConnector`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpConnector** · [`export let TcpConnector = class implements Connector<SocketAddr, TcpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L20)
  Dials `address()` for each `connect`. Tagged `#Final`.
- **TcpConnector.TcpConnector** · [`export last timeout: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L24)
  The connect timeout; zero waits without limit.
- **TcpConnector.TcpConnector** · [`export let init = func(address: SocketAddr, connect_timeout: Duration, options: SocketOptions)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L30)
  A connector for `address`. A negative timeout traps with "invalid argument: negative timeout"; options built for another kind than `SocketKind.TcpStream` trap with "invalid argument".
- **TcpConnector.TcpConnector** · [`export let address = func -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L39)
  The address this connector dials.
- **TcpConnector.TcpConnector** · [`export let connect = func -> Result<TcpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L43)
  Opens a new connection to `address()`. Tagged `#NoDiscard`.
- **TcpConnector.TcpConnector** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L46)
  Text such as "TcpConnector(127.0.0.1:8080)".
- **TcpConnector** · [`export let create = func(address: SocketAddr, timeout: Duration) -> TcpConnector`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-connector.nra#L51)
  A connector for `address` with `timeout` and no options. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
