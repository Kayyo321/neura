# `Std.Net.Tls.TlsConnector`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.TlsConnector`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.TlsConnector`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TlsConnector** · [`export let TlsConnector = class implements Connector<SocketAddr, TlsStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L28)
  Dials `address()` and completes a TLS client handshake for each `connect`. Tagged `#Final`.
- **TlsConnector.TlsConnector** · [`export last config: TlsConfig`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L32)
  The client configuration.
- **TlsConnector.TlsConnector** · [`export let init = func(address: SocketAddr, client: TlsConfig)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L36)
  A connector for `address`. A server configuration traps with "invalid argument: a TLS connector needs a client configuration".
- **TlsConnector.TlsConnector** · [`export let address = func -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L45)
  The address this connector dials.
- **TlsConnector.TlsConnector** · [`export let connect = func -> Result<TlsStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L49)
  Opens a TLS stream to `address()`; fails with Unsupported in a build without TLS. Tagged `#NoDiscard`.
- **TlsConnector.TlsConnector** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L67)
  Text such as "TlsConnector(192.0.2.1:443)".
- **TlsConnector** · [`export let version_code = func(version: TlsVersion) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-connector.nra#L72)
  The host's version number: 12 or 13. Tagged `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
