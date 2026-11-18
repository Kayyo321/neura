# `Std.Net.Dns.SrvLookup`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.SrvLookup`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.SrvLookup`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SrvLookup** · [`export let SrvTarget = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L25)
  One SRV target. Tagged `#Final`.
- **SrvLookup.SrvTarget** · [`export last priority: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L27)
  The priority (lower first).
- **SrvLookup.SrvTarget** · [`export last weight: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L29)
  The relative weight within the priority.
- **SrvLookup.SrvTarget** · [`export last port: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L31)
  The port.
- **SrvLookup.SrvTarget** · [`export last target: names.DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L33)
  The target host.
- **SrvLookup.SrvTarget** · [`export let init = func(rank: int, share: int, service_port: int, host: names.DnsName)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L36)
  A target.
- **SrvLookup.SrvTarget** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L44)
  Equal when every field is.
- **SrvLookup.SrvTarget** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L53)
  Combines the fields.
- **SrvLookup.SrvTarget** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L57)
  Text such as "10 60 5060 sip.example.com.".
- **SrvLookup** · [`export let SrvLookup = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L63)
  SRV lookups through a resolver. Tagged `#Final`, `#NotThreadSafe`.
- **SrvLookup.SrvLookup** · [`export let init = func(stub: resolvers.StubResolver, source: Random)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L70)
  A lookup over `stub` choosing with `source`.
- **SrvLookup.SrvLookup** · [`export let lookup = func(service: string, proto: string, domain: string) -> Result<SrvTarget[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L78)
  The ordered targets of `_service._proto.domain`; empty when the service is not available (no records, a name error, or the single target "."). Tagged `#NoDiscard`.
- **SrvLookup.SrvLookup** · [`export let socket_addresses = func(service: string, proto: string, domain: string) -> Result<SocketAddr[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L90)
  The socket addresses of every target, in target order, each target's addresses in resolver order. Targets that do not resolve are skipped; Err (Io NotFound) when none resolves. Tagged `#NoDiscard`.
- **SrvLookup.SrvLookup** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L111)
  Text "SrvLookup".
- **SrvLookup** · [`export let service_name = func(service: string, proto: string, domain: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L116)
  The owner name "_service._proto.domain." (underscores added when missing). Tagged `#NoDiscard`.
- **SrvLookup** · [`export let targets_from = func(records: messages.ResourceRecord[list]) -> SrvTarget[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L125)
  The SRV targets among records. Tagged `#NoDiscard`.
- **SrvLookup** · [`export let order_targets = func(targets: SrvTarget[list], source: Random) -> SrvTarget[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/srv-lookup.nra#L141)
  Orders targets by priority, and within a priority by RFC 2782 weighted random selection. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
