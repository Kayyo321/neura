# `Std.Net.Core.IpAddr`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.IpAddr`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.IpAddr`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **IpAddr** · [`export let IpAddr = enum (V4(v4.Ipv4Addr), V6(v6.Ipv6Addr))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L18)
  An IPv4 or an IPv6 address. Since 0.3.0.
- **IpAddr** · [`export let parse = func(text: string) -> Result<IpAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L23)
  Parses an IPv6 address (with optional zone) when the text contains `:`, otherwise a strict dotted-decimal IPv4 address; the error is that of the parser chosen. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **IpAddr** · [`export let to_string = func(ip: IpAddr) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L40)
  The address's canonical text (dotted decimal, or RFC 5952 with any zone). Tagged `#Pure`, `#ThreadSafe`.
- **IpAddr** · [`export let octets = func(ip: IpAddr) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L47)
  The address bytes in network order: 4 for IPv4, 16 for IPv6 (the zone is not included). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **IpAddr** · [`export let family = func(ip: IpAddr) -> AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L54)
  `AddressFamily.Ipv4` or `AddressFamily.Ipv6`. Tagged `#Pure`, `#ThreadSafe`.
- **IpAddr** · [`export let equals = func(a: IpAddr, b: IpAddr) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L61)
  Tests whether both are the same family and equal addresses (an IPv6 zone included). Tagged `#ThreadSafe`.
- **IpAddr** · [`export let hash = func(ip: IpAddr) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L74)
  A hash consistent with `equals`: the family tag combined with the address's hash code. Tagged `#Pure`, `#ThreadSafe`.
- **IpAddr** · [`export let is_loopback = func(ip: IpAddr) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ip-addr.nra#L81)
  Tests for `127.0.0.0/8` or `::1`. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
