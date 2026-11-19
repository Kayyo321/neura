# `Std.Net.Dns.DnsTransport`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.DnsTransport`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.DnsTransport`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DnsTransport** · [`export let DnsTransport = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L19)
  Sends a query and returns the response.
- **DnsTransport** · [`export let MemoryTransport = class implements DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L38)
  An authoritative in-memory server over a list of records, for tests. For a question it answers the records whose owner and type match; otherwise a CNAME at the owner (without following it, so the resolver's chasing is exercised); otherwise NODATA when the owner has other records and NXDOMAIN when it has none, each with the zone SOA (if set) in the authority section. Every response passes through `Message.encode` and `decode`. As a datagram transport it truncates (TC set, sections emptied) a response longer than 512 bytes, or the query's EDNS payload size. It can be told to fail the next exchanges with TimedOut, to answer with a wrong ID, or to answer with a fixed RCODE. Tagged `#Final`, `#NotThreadSafe`.
- **DnsTransport.MemoryTransport** · [`export last label: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L44)
  The name used in `to_string`.
- **DnsTransport.MemoryTransport** · [`export last received: messages.Message[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L48)
  The queries received, in order.
- **DnsTransport.MemoryTransport** · [`export let fail_next: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L50)
  Exchanges still to fail with TimedOut.
- **DnsTransport.MemoryTransport** · [`export let wrong_id_next: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L52)
  Exchanges still to answer with the query's ID plus one.
- **DnsTransport.MemoryTransport** · [`export let forced_rcode: int = -1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L54)
  An RCODE to answer every query with instead of the zone data; -1 for none.
- **DnsTransport.MemoryTransport** · [`export let init = func(name: string, is_stream_transport: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L57)
  A transport named `name`; a stream when `is_stream_transport` is true.
- **DnsTransport.MemoryTransport** · [`export let add = func(record: messages.ResourceRecord)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L63)
  Adds a record to the zone.
- **DnsTransport.MemoryTransport** · [`export let set_soa = func(record: messages.ResourceRecord)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L68)
  Sets the SOA record for negative answers (it must hold SOA data).
- **DnsTransport.MemoryTransport** · [`export let query_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L74)
  The number of queries received. Tagged `#NoDiscard`.
- **DnsTransport.MemoryTransport** · [`export let is_stream = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L77)
  Whether this behaves as a stream.
- **DnsTransport.MemoryTransport** · [`export let exchange = func(query: messages.Message) -> Result<messages.Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L80)
  Answers from the zone.
- **DnsTransport.MemoryTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/dns-transport.nra#L119)
  Text such as "MemoryTransport(primary, 3 records)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
