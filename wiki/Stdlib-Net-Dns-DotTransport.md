# `Std.Net.Dns.DotTransport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.DotTransport`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.DotTransport`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DotTransport** · [`export const DotPort: int = 853`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L20)
  The DoT port (RFC 7858 section 3.1).
- **DotTransport** · [`export let DotTransport = class implements transports.DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L24)
  A DoT transport: one TLS connection per exchange. Tagged `#Final`, `#NotThreadSafe`.
- **DotTransport.DotTransport** · [`export last server: SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L26)
  The server.
- **DotTransport.DotTransport** · [`export last config: TlsConfig`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L28)
  The TLS client configuration.
- **DotTransport.DotTransport** · [`export let init = func(address: SocketAddr, client: TlsConfig)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L31)
  A transport to `address` with a client configuration.
- **DotTransport.DotTransport** · [`export let is_stream = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L37)
  A stream transport.
- **DotTransport.DotTransport** · [`export let exchange = func(query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L40)
  Connects with TLS, sends the framed query, reads the framed response and closes.
- **DotTransport.DotTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dot-transport.nra#L48)
  Text such as "DotTransport(192.0.2.53:853)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
