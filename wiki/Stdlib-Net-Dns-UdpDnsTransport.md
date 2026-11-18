# `Std.Net.Dns.UdpDnsTransport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.UdpDnsTransport`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.UdpDnsTransport`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UdpDnsTransport** · [`export const MaxDatagram: int = 65535`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L25)
  The largest datagram read.
- **UdpDnsTransport** · [`export let UdpDnsTransport = class implements transports.DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L29)
  A DNS transport over UDP datagrams. Tagged `#Final`, `#NotThreadSafe`.
- **UdpDnsTransport.UdpDnsTransport** · [`export last server: SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L31)
  The server.
- **UdpDnsTransport.UdpDnsTransport** · [`export last timeout: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L33)
  The longest to wait for the response (zero: no limit).
- **UdpDnsTransport.UdpDnsTransport** · [`export let init = func(address: SocketAddr, limit: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L36)
  A transport to `address` with a timeout.
- **UdpDnsTransport.UdpDnsTransport** · [`export let is_stream = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L42)
  A datagram transport.
- **UdpDnsTransport.UdpDnsTransport** · [`export let exchange = func(query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L45)
  Sends the query and waits for the matching datagram.
- **UdpDnsTransport.UdpDnsTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/udp-dns-transport.nra#L56)
  Text such as "UdpDnsTransport(192.0.2.53:53)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
