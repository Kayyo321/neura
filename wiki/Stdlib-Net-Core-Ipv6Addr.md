# `Std.Net.Core.Ipv6Addr`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Ipv6Addr`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Ipv6Addr`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ipv6Addr** · [`export let Ipv6Addr = class implements NetAddress, Comparable<Ipv6Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L26)
  An IPv6 address and zone, ordered by the unsigned 128-bit value and then by the zone's bytes. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **Ipv6Addr.Ipv6Addr** · [`export last high: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L28)
  The most significant 64 bits (groups 1 to 4).
- **Ipv6Addr.Ipv6Addr** · [`export last low: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L30)
  The least significant 64 bits (groups 5 to 8).
- **Ipv6Addr.Ipv6Addr** · [`export last zone: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L32)
  The zone index text without `%`; "" when there is none.
- **Ipv6Addr.Ipv6Addr** · [`export let init = func(first: uint64, second: uint64, zone_id: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L35)
  Constructs the address from its halves and zone; traps when the zone contains `%`.
- **Ipv6Addr.Ipv6Addr** · [`export let segments = func -> uint16[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L48)
  The eight 16-bit groups, most significant first, as a new list. Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let octets = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L58)
  The sixteen bytes in network order, as a new list. Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let without_zone = func -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L70)
  The same address with no zone. Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let to_ipv4_mapped = func -> Optional<v4.Ipv4Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L75)
  The IPv4 address of an IPv4-mapped address (`::ffff:a.b.c.d`, RFC 4291 section 2.5.5.2); None for any other address. Tagged `#NoDiscard`, `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_unspecified = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L82)
  Tests for `::` (zone ignored). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_loopback = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L86)
  Tests for `::1` (zone ignored). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_multicast = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L90)
  Tests for `ff00::/8` (RFC 4291 section 2.7). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_link_local = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L94)
  Tests for the link-local unicast prefix `fe80::/10` (RFC 4291 section 2.5.6). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_unique_local = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L98)
  Tests for unique local addresses, `fc00::/7` (RFC 4193). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_ipv4_mapped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L102)
  Tests for `::ffff:0:0/96` (RFC 4291 section 2.5.5.2). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let is_documentation = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L106)
  Tests for the documentation prefix `2001:db8::/32` (RFC 3849). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let family = func -> AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L110)
  Always `AddressFamily.Ipv6`. Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let compare_to = func(other: Ipv6Addr) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L114)
  Compares the unsigned 128-bit values, then the zones bytewise ("" first). Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L123)
  Tests whether another object is an `Ipv6Addr` with the same value and zone.
- **Ipv6Addr.Ipv6Addr** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L132)
  Combines 17 with both halves and the zone's hash. Tagged `#Pure`.
- **Ipv6Addr.Ipv6Addr** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L138)
  The RFC 5952 text, followed by `%` and the zone when there is one. Tagged `#Pure`.
- **Ipv6Addr** · [`export let parse = func(text: string) -> Result<Ipv6Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L204)
  Parses the text forms of RFC 4291 section 2.2, optionally followed by `%` and a zone. Accepted: eight groups of 1 to 4 hex digits in either case (`2001:DB8:0:0:8:800:200C:417A`); one `::` standing for one or more zero groups (`ff01::101`, `::1`, `::`); and a final dotted IPv4 address in place of the last two groups (`::ffff:129.144.52.38`, `0:0:0:0:0:0:13.1.68.3`), parsed as strictly as `Ipv4Addr.parse`. The zone is everything after the first `%`; it must be non-empty and contain no further `%`. Errors: Empty for ""; InvalidCharacter at the first unexpected byte (a group of five digits fails at its fifth digit); Syntax for a second `::`, a lone leading or trailing `:`, a wrong group count (at the end of the address), an IPv4 part not at the end, or an empty or invalid zone; UnexpectedEnd when the text stops after `:`; errors of the embedded IPv4 part are reported at their offset in the whole text. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv6Addr** · [`export let from_bytes = func(bytes: uint8[list], offset: int) -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L314)
  The address held in `bytes[offset]` to `bytes[offset + 15]`, network order, with no zone; traps when fewer than sixteen bytes follow `offset`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv6Addr** · [`export let of_segments = func(segments: uint16[list]) -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L330)
  The address with the eight given groups, most significant first, and no zone; traps unless the list holds exactly eight groups. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv6Addr** · [`export let from_ipv4_mapped = func(address: v4.Ipv4Addr) -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L343)
  The IPv4-mapped address `::ffff:a.b.c.d` (RFC 4291 section 2.5.5.2). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv6Addr** · [`export let localhost = func -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L348)
  `::1`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv6Addr** · [`export let unspecified = func -> Ipv6Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv6-addr.nra#L352)
  `::`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
