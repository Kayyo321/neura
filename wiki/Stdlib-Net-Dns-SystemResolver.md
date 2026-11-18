# `Std.Net.Dns.SystemResolver`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Dns.SystemResolver`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/system-resolver.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Dns.SystemResolver`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SystemResolver** · [`export const AnyFamily: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/system-resolver.nra#L14)
  Any family.
- **SystemResolver** · [`export const Ipv4Only: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/system-resolver.nra#L16)
  IPv4 only.
- **SystemResolver** · [`export const Ipv6Only: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/system-resolver.nra#L18)
  IPv6 only.
- **SystemResolver** · [`export let resolve = func(host: string, family: int) -> Result<ips.IpAddr[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/system-resolver.nra#L27)
  The addresses of `host` from the system resolver: `family` 0 (any), 4 or 6. Err (Io NotFound) when the name does not exist; other host errors as Stdlib.md section 4.5 maps them. A family other than 0, 4 or 6 traps with "invalid argument: family". Addresses the host returns that do not parse are skipped. Tagged `#NoDiscard`.
- **SystemResolver** · [`export let parse_all = func(texts: string[list]) -> ips.IpAddr[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/dns/system-resolver.nra#L40)
  The addresses among host texts, skipping any that do not parse. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
