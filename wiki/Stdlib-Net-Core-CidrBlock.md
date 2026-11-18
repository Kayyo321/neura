# `Std.Net.Core.CidrBlock`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.CidrBlock`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.CidrBlock`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CidrBlock** · [`export let CidrBlock = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L25)
  A network address with its host bits cleared and a prefix length. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **CidrBlock.CidrBlock** · [`export last network: ips.IpAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L27)
  The network address: the host bits are clear and an IPv6 network has no zone.
- **CidrBlock.CidrBlock** · [`export last prefix_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L29)
  The prefix length: 0 to 32 for IPv4, 0 to 128 for IPv6.
- **CidrBlock.CidrBlock** · [`export let init = func(address: ips.IpAddr, prefix: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L33)
  Constructs the block; traps when the prefix is out of range for the family, when a host bit is set, or when an IPv6 network has a zone.
- **CidrBlock.CidrBlock** · [`export let contains = func(ip: ips.IpAddr) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L44)
  Tests whether the address lies in the block; false for the other family. Tagged `#Pure`.
- **CidrBlock.CidrBlock** · [`export let family = func -> AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L58)
  The family of the network address. Tagged `#Pure`.
- **CidrBlock.CidrBlock** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L61)
  Tests whether another object is a `CidrBlock` with an equal network and prefix length.
- **CidrBlock.CidrBlock** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L70)
  Combines the network hash with the prefix length. Tagged `#Pure`.
- **CidrBlock.CidrBlock** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L75)
  `network/prefix`, for example `10.0.0.0/8` or `2001:db8::/32`. Tagged `#Pure`.
- **CidrBlock** · [`export let of = func(ip: ips.IpAddr, prefix: int) -> CidrBlock`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L82)
  The block of `ip` with the host bits cleared and any zone dropped; traps when the prefix is out of range for the family. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **CidrBlock** · [`export let parse = func(text: string) -> Result<CidrBlock>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L100)
  Parses `address/prefix`, where the prefix is 1 to 3 decimal digits without a leading zero (except `0`), at most 32 for IPv4 and 128 for IPv6. Python's `ipaddress.ip_network` is laxer: it also takes a bare address (as a /32 or /128), a netmask (`10.0.0.0/255.0.0.0`), a leading zero (`/08`) and a zone; this parser rejects each. Errors: Empty for ""; Syntax at the length when there is no `/`; the address parser's error; Syntax at the `%` of a zone; InvalidCharacter at a byte of the prefix that is not a digit or follows a leading zero; UnexpectedEnd for an empty prefix; OutOfRange at the prefix's start when it is too long for the family; Syntax at the `/` when host bits are set. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **CidrBlock** · [`export let parse_lenient = func(text: string) -> Result<CidrBlock>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/cidr-block.nra#L105)
  Parses like `parse`, but clears host bits (`10.1.2.3/8` gives `10.0.0.0/8`) and drops an IPv6 zone instead of reporting them. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
