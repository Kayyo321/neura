# `Std.Net.Socket.RawSocket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.RawSocket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.RawSocket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RawSocket** · [`export let RawSocket = class implements DatagramSocket<ips.IpAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L25)
  One raw socket of an address family and IP protocol. Tagged `#Final`, `#NoDiscard(reason: "a socket must be closed")`, `#NotThreadSafe`.
- **RawSocket.RawSocket** · [`export last family: AddressFamily`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L29)
  Ipv4 or Ipv6.
- **RawSocket.RawSocket** · [`export last protocol: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L31)
  The IP protocol number, 0 through 255.
- **RawSocket.RawSocket** · [`export let init = func(opened: int64, address_family: AddressFamily, protocol_number: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L38)
  Use `open`; the ID comes from the library.
- **RawSocket.RawSocket** · [`export let local_address = func -> Result<ips.IpAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L46)
  The local address, from the host; traps in Neura 0.3.0 (no local-address service). Tagged `#NoDiscard`.
- **RawSocket.RawSocket** · [`export let send_to = func(data: uint8[list], target: ips.IpAddr) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L60)
  Sends one IP payload to `target`, which must be of the socket's family (else the call traps with "invalid argument: address family"). Tagged `#NoDiscard`.
- **RawSocket.RawSocket** · [`export let receive_from = func(max: int) -> Result<Datagram<ips.IpAddr>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L76)
  One payload of at most `max` bytes and its sender, waiting up to the read timeout. Tagged `#NoDiscard`.
- **RawSocket.RawSocket** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L89)
  The longest a receive waits; zero waits without limit.
- **RawSocket.RawSocket** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L92)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **RawSocket.RawSocket** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L97)
  Releases the socket. Closing again returns Ok.
- **RawSocket.RawSocket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L109)
  Text such as "RawSocket(Ipv4, protocol 1)".
- **RawSocket** · [`export let open = func(family: AddressFamily, protocol: int) -> Result<RawSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/raw-socket.nra#L117)
  A raw socket for `family` (Ipv4 or Ipv6; Unix traps with "invalid argument: address family") and IP protocol number `protocol` (0 through 255, else "invalid argument: protocol"). Traps in Neura 0.3.0 (no raw-socket host service). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
