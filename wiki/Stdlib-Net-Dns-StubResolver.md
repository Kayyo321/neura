# `Std.Net.Dns.StubResolver`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.StubResolver`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.StubResolver`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **StubResolver** · [`export const MaxCnameChain: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L50)
  The most CNAME links followed for one question.
- **StubResolver** · [`export const EdnsPayloadSize: int = 1232`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L52)
  The EDNS UDP payload size offered (the DNS flag day 2020 value).
- **StubResolver** · [`export let Upstream = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L57)
  One upstream server: a datagram (or stream) transport and an optional stream transport for truncated answers. Tagged `#Final`.
- **StubResolver.Upstream** · [`export last primary: transports.DnsTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L59)
  The first transport tried.
- **StubResolver.Upstream** · [`export last fallback: Optional<transports.DnsTransport>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L61)
  The stream transport for truncated answers.
- **StubResolver.Upstream** · [`export let init = func(first: transports.DnsTransport)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L64)
  A server reached through `first` only; or with a stream fallback.
- **StubResolver.Upstream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L73)
  Text such as "Upstream(MemoryTransport(a, 2 records))".
- **StubResolver** · [`export let Source = enum (Hosts, Cache, Network)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L77)
  Where an answer came from.
- **StubResolver** · [`export let Answer = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L81)
  The outcome of a lookup. Tagged `#Final`.
- **StubResolver.Answer** · [`export last question: messages.Question`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L83)
  The question finally answered (the last search candidate tried).
- **StubResolver.Answer** · [`export last canonical_name: names.DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L85)
  The name the records belong to after following CNAMEs.
- **StubResolver.Answer** · [`export last aliases: names.DnsName[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L87)
  The aliases followed, in order.
- **StubResolver.Answer** · [`export last records: messages.ResourceRecord[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L89)
  The records of the type asked (empty for NXDOMAIN or NODATA).
- **StubResolver.Answer** · [`export last rcode: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L91)
  NOERROR or NXDOMAIN.
- **StubResolver.Answer** · [`export last source: Source`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L93)
  Where it came from.
- **StubResolver.Answer** · [`export let init = func(asked: messages.Question, canonical: names.DnsName, alias_list: names.DnsName[list], found: messages.ResourceRecord[list], code: int, origin: Source)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L96)
  An answer.
- **StubResolver.Answer** · [`export let is_name_error = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L108)
  Whether the name does not exist. Tagged `#NoDiscard`.
- **StubResolver.Answer** · [`export let is_no_data = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L112)
  Whether the name exists without records of the type. Tagged `#NoDiscard`.
- **StubResolver.Answer** · [`export let addresses = func -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L116)
  The addresses among the records (A and AAAA). Tagged `#NoDiscard`.
- **StubResolver.Answer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L134)
  Text such as "Answer(www.example.com. IN A, 2 records, Network)".
- **StubResolver** · [`export let StubResolver = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L158)
  The resolver. Tagged `#Final`, `#NotThreadSafe`.
- **StubResolver.StubResolver** · [`export last config: resolv_confs.ResolvConf`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L160)
  Search list, ndots, attempts, rotate and edns0.
- **StubResolver.StubResolver** · [`export last hosts: hosts_files.HostsFile`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L162)
  The hosts table.
- **StubResolver.StubResolver** · [`export last cache: caches.DnsCache`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L164)
  The answer cache.
- **StubResolver.StubResolver** · [`export let init = func(settings: resolv_confs.ResolvConf, table: hosts_files.HostsFile, store: caches.DnsCache, upstreams: Upstream[list], random: Random)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L175)
  A resolver. With no servers every network lookup fails with `dns.no_servers`.
- **StubResolver.StubResolver** · [`export let add_route = func(suffix: string, upstreams: Upstream[list]) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L185)
  Sends names within `suffix` to `upstreams` (split horizon). Err when the suffix does not parse.
- **StubResolver.StubResolver** · [`export let servers_for = func(name: names.DnsName) -> Upstream[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L193)
  The servers for a name: the longest matching route, else the defaults. Tagged `#NoDiscard`.
- **StubResolver.StubResolver** · [`export let candidates = func(name: string) -> Result<names.DnsName[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L208)
  The candidate names for `name` after search-list expansion. Tagged `#NoDiscard`.
- **StubResolver.StubResolver** · [`export let lookup = func(name: string, qtype: int) -> Result<Answer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L249)
  Looks up records of `qtype` for `name`: the hosts table for the name as given (A and AAAA), then each search-list candidate. Tagged `#NoDiscard`.
- **StubResolver.StubResolver** · [`export let resolve = func(host: string) -> Result<ips.IpAddr[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L287)
  The addresses of `host`: an address literal as itself; else the hosts table; else A and AAAA lookups (A first). Err (Io NotFound) when there are none. Tagged `#NoDiscard`.
- **StubResolver.StubResolver** · [`export let lookup_name = func(name: names.DnsName, qtype: int) -> Result<Answer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L317)
  Looks up one absolute name (no search list): hosts, cache, then the network with CNAME chasing. Tagged `#NoDiscard`.
- **StubResolver.StubResolver** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L343)
  Text such as "StubResolver(2 servers, 1 routes)".
- **StubResolver** · [`export let upstreams_for = func(settings: resolv_confs.ResolvConf) -> Upstream[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L516)
  One `Upstream` per name server of `settings` (`effective_nameservers`, port 53): UDP first, TCP for truncated answers, each waiting `timeout_seconds`. The UDP half needs the `neura.net.udp.*` services. Tagged `#NoDiscard`.
- **StubResolver** · [`export let from_config = func(settings: resolv_confs.ResolvConf, table: hosts_files.HostsFile, store: caches.DnsCache, random: Random) -> StubResolver`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L528)
  A resolver whose servers come from `settings` through `upstreams_for`. Tagged `#NoDiscard`.
- **StubResolver** · [`export let check_match = func(query: messages.Message, response: messages.Message) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/stub-resolver.nra#L534)
  Checks that a response answers a query: QR set, the same ID and the same single question. Err (Custom `dns.mismatch`) otherwise. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
