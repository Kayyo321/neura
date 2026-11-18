# `Std.Net.Dns.DoqTransport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.DoqTransport`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.DoqTransport`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DoqTransport** · [`export const DoqPort: int = 853`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L18)
  The DoQ port (RFC 9250 section 4.1.1, "Port Selection").
- **DoqTransport** · [`export const Alpn = "doq"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L20)
  The ALPN token (RFC 9250 section 4.1).
- **DoqTransport** · [`export let DoqTransport = class implements transports.DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L27)
  A DoQ transport. Tagged `#Final`, `#NotThreadSafe`.
- **DoqTransport.DoqTransport** · [`export last server: SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L29)
  The server.
- **DoqTransport.DoqTransport** · [`export last server_name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L31)
  The TLS server name.
- **DoqTransport.DoqTransport** · [`export let init = func(address: SocketAddr, name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L34)
  A transport to `address`, verifying `name`.
- **DoqTransport.DoqTransport** · [`export let is_stream = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L40)
  A stream transport.
- **DoqTransport.DoqTransport** · [`export let exchange = func(query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L43)
  One exchange on a new QUIC stream.
- **DoqTransport.DoqTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L53)
  Text such as "DoqTransport(192.0.2.53:853)".
- **DoqTransport** · [`export let stream_payload = func(query: messages.Message) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/doq-transport.nra#L66)
  What a DoQ client writes on its stream: the query with ID 0 (RFC 9250 section 4.2.1, "DNS Message IDs") behind its two-byte length (section 4.2). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
