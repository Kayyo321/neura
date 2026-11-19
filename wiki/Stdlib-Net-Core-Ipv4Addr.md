# `Std.Net.Core.Ipv4Addr`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Ipv4Addr`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Ipv4Addr`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ipv4Addr** · [`export let Ipv4Addr = class implements NetAddress, Comparable<Ipv4Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L20)
  An IPv4 address, ordered by its unsigned 32-bit value (the same as its octets in order). Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **Ipv4Addr.Ipv4Addr** · [`export last bits: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L22)
  The address as a number: the first octet is the most significant byte.
- **Ipv4Addr.Ipv4Addr** · [`export let init = func(value: uint32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L25)
  Constructs the address with the given numeric value.
- **Ipv4Addr.Ipv4Addr** · [`export let octets = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L31)
  The four octets in network order, as a new list. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_unspecified = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L42)
  Tests for `0.0.0.0`. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_loopback = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L46)
  Tests for `127.0.0.0/8` (RFC 1122 section 3.2.1.3). Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_private = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L50)
  Tests for the private ranges of RFC 1918: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_link_local = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L58)
  Tests for `169.254.0.0/16` (RFC 3927). Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_multicast = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L62)
  Tests for `224.0.0.0/4` (RFC 5771). Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_broadcast = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L66)
  Tests for the limited broadcast address `255.255.255.255` (RFC 919). Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let is_documentation = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L71)
  Tests for the documentation ranges of RFC 5737: `192.0.2.0/24`, `198.51.100.0/24`, `203.0.113.0/24`. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let family = func -> AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L81)
  Always `AddressFamily.Ipv4`. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let compare_to = func(other: Ipv4Addr) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L85)
  Compares the unsigned values. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L91)
  Tests whether another address is an `Ipv4Addr` with the same value.
- **Ipv4Addr.Ipv4Addr** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L100)
  Combines 17 with the value. Tagged `#Pure`.
- **Ipv4Addr.Ipv4Addr** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L104)
  The dotted-decimal form, for example `192.0.2.1`. Tagged `#Pure`.
- **Ipv4Addr** · [`export let of = func(a: int, b: int, c: int, d: int) -> Ipv4Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L114)
  The address `a.b.c.d`; traps when a part is outside 0 to 255. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv4Addr** · [`export let from_bytes = func(bytes: uint8[list], offset: int) -> Ipv4Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L124)
  The address held in `bytes[offset]` to `bytes[offset + 3]`, network order; traps when fewer than four bytes follow `offset`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv4Addr** · [`export let localhost = func -> Ipv4Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L137)
  `127.0.0.1`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv4Addr** · [`export let unspecified = func -> Ipv4Addr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L141)
  `0.0.0.0`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Ipv4Addr** · [`export let parse = func(text: string) -> Result<Ipv4Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/ipv4-addr.nra#L150)
  Parses strict dotted-decimal text: exactly four parts of 1 to 3 decimal digits, each 0 to 255, with no leading zero except a lone `0`. Errors: Empty for ""; InvalidCharacter at the first byte that is not a digit or a dot (also a leading zero, at the zero); Syntax at an empty part or a fifth part; OutOfRange at the start of a part above 255; UnexpectedEnd at the length when fewer than four parts are given. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
