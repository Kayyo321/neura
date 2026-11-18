# `Std.Net.Core.SocketAddr`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.SocketAddr`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.SocketAddr`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SocketAddr** · [`export let SocketAddr = class implements NetAddress`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L20)
  An IP address and a port number 0 to 65535. Tagged `#Final`, `#NoDiscard`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **SocketAddr.SocketAddr** · [`export last ip: ips.IpAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L22)
  The IP address.
- **SocketAddr.SocketAddr** · [`export last port: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L24)
  The port, 0 to 65535.
- **SocketAddr.SocketAddr** · [`export let init = func(address: ips.IpAddr, port_number: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L27)
  Constructs the socket address; traps when the port is outside 0 to 65535.
- **SocketAddr.SocketAddr** · [`export let host_text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L37)
  The IP text without brackets (with any IPv6 zone), as `TcpStream.connect` takes a host. Tagged `#Pure`.
- **SocketAddr.SocketAddr** · [`export let family = func -> AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L41)
  The family of the IP address. Tagged `#Pure`.
- **SocketAddr.SocketAddr** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L44)
  Tests whether another object is a `SocketAddr` with an equal address and the same port.
- **SocketAddr.SocketAddr** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L53)
  Combines the address hash with the port. Tagged `#Pure`.
- **SocketAddr.SocketAddr** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L57)
  `a.b.c.d:port` or `[ipv6]:port`. Tagged `#Pure`.
- **SocketAddr** · [`export let of_v4 = func(ip: v4.Ipv4Addr, port: int) -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L65)
  The socket address of an IPv4 address and a port; traps when the port is outside 0 to 65535. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **SocketAddr** · [`export let of_v6 = func(ip: v6.Ipv6Addr, port: int) -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L69)
  The socket address of an IPv6 address and a port; traps when the port is outside 0 to 65535. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **SocketAddr** · [`export let parse = func(text: string) -> Result<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/socket-addr.nra#L79)
  Parses `a.b.c.d:port` or `[ipv6]:port` (the IPv6 text may carry `%zone`). The port is 1 to 5 decimal digits, 0 to 65535, with no leading zero except a lone `0`. Errors: Empty for ""; the address parser's error (offsets in the whole text); Syntax when the `:port` part or the closing `]` is missing (at the length or after the `]`); UnexpectedEnd for an empty port; InvalidCharacter at a byte of the port that is not a digit or at the byte after a leading zero; OutOfRange at the port's start when it exceeds 65535. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
