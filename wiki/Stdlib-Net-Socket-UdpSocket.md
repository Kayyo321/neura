# `Std.Net.Socket.UdpSocket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.UdpSocket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.UdpSocket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UdpSocket** · [`export let UdpSocket = class implements DatagramSocket<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L29)
  One bound UDP socket. Tagged `#Final`, `#NoDiscard(reason: "a socket must be closed")`, `#NotThreadSafe`.
- **UdpSocket.UdpSocket** · [`export let init = func(opened: int64, address: SocketAddr)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L40)
  Use `bind`; the ID comes from the library.
- **UdpSocket.UdpSocket** · [`export let local_address = func -> Result<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L47)
  The bound address; its port is the one the host chose for a bind to port 0. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let send_to = func(data: uint8[list], target: SocketAddr) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L56)
  Sends `data` as one datagram to `target`; the count the host sent. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let receive_from = func(max: int) -> Result<Datagram<SocketAddr>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L66)
  One datagram of at most `max` bytes (longer ones are truncated), waiting up to the read timeout; TimedOut when none arrives. A maximum below 1 traps. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let send_segmented = func(data: uint8[list], segment_size: int, target: SocketAddr) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L82)
  Sends `data` to `target` as datagrams of `segment_size` bytes in one call with UDP GSO (`UDP_SEGMENT`); the count sent. The cut is `Offload.split`. A segment size below 1 or above `Offload.MaxUdpPayload` traps. Traps in Neura 0.3.0: no runtime offers a GSO service. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let receive_coalesced = func(max: int) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L94)
  Receives datagrams the kernel coalesced with UDP GRO, split back into datagrams with `Offload.split`. Traps in Neura 0.3.0: no runtime offers a GRO service. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L107)
  The longest a receive waits; zero waits without limit.
- **UdpSocket.UdpSocket** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L110)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **UdpSocket.UdpSocket** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L115)
  Releases the socket. Closing again returns Ok.
- **UdpSocket.UdpSocket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L128)
  Text such as "UdpSocket(127.0.0.1:5353)".
- **UdpSocket** · [`export let bind = func(address: SocketAddr) -> Result<UdpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L133)
  Binds a UDP socket to `address` (port 0 asks for a free port) with no options. Tagged `#NoDiscard`.
- **UdpSocket** · [`export let bind_with = func(address: SocketAddr, options: SocketOptions) -> Result<UdpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L137)
  Binds with an option set built for `SocketKind.Udp` (another kind traps with "invalid argument"). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
