# `Std.Net.Dns.Mdns`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.Mdns`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.Mdns`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Mdns** · [`export const Port: int = 5353`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L23)
  The mDNS port (section 1).
- **Mdns** · [`export const UnicastResponseBit: int = 32768`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L25)
  The unicast-response bit in a question's class (section 5.4).
- **Mdns** · [`export const CacheFlushBit: int = 32768`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L27)
  The cache-flush bit in a record's class (section 10.2).
- **Mdns** · [`export let ipv4_group = func -> Ipv4Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L31)
  The IPv4 group 224.0.0.251. Tagged `#NoDiscard`.
- **Mdns** · [`export let ipv6_group = func -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L35)
  The IPv6 group ff02::fb. Tagged `#NoDiscard`.
- **Mdns** · [`export let is_local_name = func(name: names.DnsName) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L48)
  Whether a name belongs to mDNS: within `local.`, `254.169.in-addr.arpa.`, or the IPv6 link-local reverse zones `8.e.f.ip6.arpa.` through `b.e.f.ip6.arpa.` (sections 3 and 4). Tagged `#NoDiscard`.
- **Mdns** · [`export let question = func(name: names.DnsName, qtype: int, unicast: bool) -> messages.Question`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L66)
  A question for `name` and `qtype` in class IN, asking for a unicast reply when `unicast` is true. Tagged `#NoDiscard`.
- **Mdns** · [`export let wants_unicast = func(asked: messages.Question) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L71)
  Whether a question asks for a unicast reply. Tagged `#NoDiscard`.
- **Mdns** · [`export let question_class = func(asked: messages.Question) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L75)
  A question's class without the unicast-response bit. Tagged `#NoDiscard`.
- **Mdns** · [`export let with_cache_flush = func(record: messages.ResourceRecord, flush: bool) -> messages.ResourceRecord`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L79)
  The record with the cache-flush bit set or cleared. Tagged `#NoDiscard`.
- **Mdns** · [`export let is_cache_flush = func(record: messages.ResourceRecord) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L86)
  Whether a record has the cache-flush bit set. An OPT record's class is its payload size, so it is never a cache-flush record. Tagged `#NoDiscard`.
- **Mdns** · [`export let record_class = func(record: messages.ResourceRecord) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L91)
  A record's class without the cache-flush bit. Tagged `#NoDiscard`.
- **Mdns** · [`export let query = func(name: names.DnsName, qtype: int, unicast: bool) -> messages.Message`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L96)
  A one-shot multicast query: ID 0, opcode 0, RD clear (section 18), one question. Tagged `#NoDiscard`.
- **Mdns** · [`export let response = func(answers: messages.ResourceRecord[list]) -> messages.Message`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L104)
  A multicast response carrying `answers`: ID 0, QR and AA set, no questions (section 18). Tagged `#NoDiscard`.
- **Mdns** · [`export let MdnsQuerier = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L119)
  A querier on the local link. Tagged `#Final`, `#NotThreadSafe`.
- **Mdns.MdnsQuerier** · [`export last window: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L121)
  How long to collect responses.
- **Mdns.MdnsQuerier** · [`export let init = func(wait: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L124)
  A querier collecting responses for `wait`.
- **Mdns.MdnsQuerier** · [`export let query_all = func(name: names.DnsName, qtype: int) -> Result<messages.Message[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L130)
  Sends a query to the group and returns every response received within the window. Tagged `#NoDiscard`.
- **Mdns.MdnsQuerier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L139)
  Text such as "MdnsQuerier(1s)".
- **Mdns** · [`export let responses_in = func(datagrams: uint8[list][list]) -> messages.Message[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/mdns.nra#L144)
  The decodable responses among datagrams; others are dropped. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
