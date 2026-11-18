# `Std.Net.Dns.TcpDnsTransport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.TcpDnsTransport`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.TcpDnsTransport`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpDnsTransport** · [`export const DnsPort: int = 53`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L24)
  The DNS port.
- **TcpDnsTransport** · [`export let TcpDnsTransport = class implements transports.DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L28)
  A DNS transport over one TCP connection per exchange. Tagged `#Final`, `#NotThreadSafe`.
- **TcpDnsTransport.TcpDnsTransport** · [`export last server: SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L30)
  The server.
- **TcpDnsTransport.TcpDnsTransport** · [`export last timeout: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L32)
  The longest the connection and each read may take (zero: no limit).
- **TcpDnsTransport.TcpDnsTransport** · [`export let init = func(address: SocketAddr, limit: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L35)
  A transport to `address` with a timeout.
- **TcpDnsTransport.TcpDnsTransport** · [`export let is_stream = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L41)
  A stream transport.
- **TcpDnsTransport.TcpDnsTransport** · [`export let exchange = func(query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L44)
  Connects, sends the framed query, reads the framed response and closes.
- **TcpDnsTransport.TcpDnsTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L59)
  Text such as "TcpDnsTransport(192.0.2.53:53)".
- **TcpDnsTransport** · [`export let exchange_on = func(read: func(int) -> Result<uint8[list]>, write: func(uint8[list]) -> Result<Unit>, query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L64)
  Encodes `query`, writes it framed, and reads and decodes one framed response. Tagged `#NoDiscard`.
- **TcpDnsTransport** · [`export let write_framed = func(write: func(uint8[list]) -> Result<Unit>, message: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L72)
  Writes the two-byte length and the message in one write. A message above 65,535 bytes traps with "invalid argument: message length".
- **TcpDnsTransport** · [`export let frame = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L77)
  The two-byte length followed by the message. Tagged `#NoDiscard`.
- **TcpDnsTransport** · [`export let read_framed = func(read: func(int) -> Result<uint8[list]>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/tcp-dns-transport.nra#L93)
  Reads one framed message, however the bytes are split across reads. Err (Io UnexpectedEnd) when the stream ends inside the length or the message. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
