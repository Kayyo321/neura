# `Std.Net.Dns.NaptrLookup`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.NaptrLookup`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.NaptrLookup`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **NaptrLookup** · [`export const MaxRewrites: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L22)
  The most non-terminal rewrites `follow` makes.
- **NaptrLookup** · [`export let NaptrRule = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L26)
  One NAPTR rule. Tagged `#Final`.
- **NaptrLookup.NaptrRule** · [`export last order: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L28)
  ORDER (lower first; a different order is a different rule).
- **NaptrLookup.NaptrRule** · [`export last preference: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L30)
  PREFERENCE among rules of one order (lower first).
- **NaptrLookup.NaptrRule** · [`export last flags: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L32)
  FLAGS ("S", "A", "U", "P" or empty).
- **NaptrLookup.NaptrRule** · [`export last services: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L34)
  SERVICES, such as "SIP+D2U" or "E2U+sip".
- **NaptrLookup.NaptrRule** · [`export last regexp: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L36)
  REGEXP (not evaluated).
- **NaptrLookup.NaptrRule** · [`export last replacement: names.DnsName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L38)
  REPLACEMENT (the root when the regexp is used).
- **NaptrLookup.NaptrRule** · [`export let init = func(rule_order: int, rule_preference: int, rule_flags: string, rule_services: string, rule_regexp: string, rule_replacement: names.DnsName)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L41)
  A rule.
- **NaptrLookup.NaptrRule** · [`export let is_terminal = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L53)
  Whether the flags end the lookup ("S", "A" or "U", any case). Tagged `#NoDiscard`.
- **NaptrLookup.NaptrRule** · [`export let offers = func(service: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L60)
  Whether `service` is one of the services, '+'-separated, ignoring ASCII case. Tagged `#NoDiscard`.
- **NaptrLookup.NaptrRule** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L73)
  Equal when every field is.
- **NaptrLookup.NaptrRule** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L83)
  Combines the fields.
- **NaptrLookup.NaptrRule** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L87)
  Text such as "100 10 \"S\" \"SIP+D2U\" \"\" _sip._udp.example.com.".
- **NaptrLookup** · [`export let NaptrLookup = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L93)
  NAPTR lookups through a resolver. Tagged `#Final`, `#NotThreadSafe`.
- **NaptrLookup.NaptrLookup** · [`export let init = func(stub: resolvers.StubResolver)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L98)
  A lookup over `stub`.
- **NaptrLookup.NaptrLookup** · [`export let lookup = func(domain: string) -> Result<NaptrRule[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L104)
  The NAPTR rules of `domain`, sorted. Tagged `#NoDiscard`.
- **NaptrLookup.NaptrLookup** · [`export let follow = func(domain: string, service: string) -> Result<NaptrRule>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L112)
  Follows rules offering `service` from `domain` to a terminal rule. Err (Custom `dns.naptr`) when no rule matches, only regexp rules match, or after `MaxRewrites` rewrites. Tagged `#NoDiscard`.
- **NaptrLookup.NaptrLookup** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L148)
  Text "NaptrLookup".
- **NaptrLookup** · [`export let rules_from = func(records: messages.ResourceRecord[list]) -> NaptrRule[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L153)
  The NAPTR rules among records, in record order. Tagged `#NoDiscard`.
- **NaptrLookup** · [`export let sort_rules = func(rules: NaptrRule[list]) -> NaptrRule[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L169)
  The rules sorted by order, then preference (stable). Tagged `#NoDiscard`.
- **NaptrLookup** · [`export let select = func(rules: NaptrRule[list], service: string) -> NaptrRule[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/naptr-lookup.nra#L188)
  The sorted rules offering `service` within the lowest order that has one. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
