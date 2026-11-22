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

- **UdpSocket** · [`export let UdpSocket = class implements DatagramSocket<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L27)
  One bound UDP socket. Tagged `#Final`, `#NoDiscard(reason: "a socket must be closed")`, `#NotThreadSafe`.
- **UdpSocket.UdpSocket** · [`export let init = func(opened: int64, address: SocketAddr)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L38)
  Use `bind`; the ID comes from the library.
- **UdpSocket.UdpSocket** · [`export let local_address = func -> Result<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L45)
  The bound address; its port is the one the host chose for a bind to port 0. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let send_to = func(data: uint8[list], target: SocketAddr) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L52)
  Sends `data` as one datagram to `target`; the count the host sent. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let receive_from = func(max: int) -> Result<Datagram<SocketAddr>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L60)
  One datagram of at most `max` bytes (longer ones are truncated), waiting up to the read timeout; TimedOut when none arrives. A maximum below 1 traps. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let send_segmented = func(data: uint8[list], segment_size: int, target: SocketAddr) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L73)
  Sends `data` to `target` as datagrams of `segment_size` bytes using kernel segmentation when available. Returns the count sent. A segment size below 1 or above `Offload.MaxUdpPayload` traps. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let set_receive_coalescing = func(enabled: bool) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L83)
  Enables or disables UDP receive coalescing. On hosts without this socket option it returns Unsupported. When enabled, use `receive_coalesced` to retain datagram boundaries. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let receive_coalesced = func(max: int) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L94)
  Receives one datagram or group of coalesced datagrams and returns the individual payloads. Tagged `#NoDiscard`.
- **UdpSocket.UdpSocket** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L105)
  The longest a receive waits; zero waits without limit.
- **UdpSocket.UdpSocket** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L108)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **UdpSocket.UdpSocket** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L113)
  Releases the socket. Closing again returns Ok.
- **UdpSocket.UdpSocket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L124)
  Text such as "UdpSocket(127.0.0.1:5353)".
- **UdpSocket** · [`export let bind = func(address: SocketAddr) -> Result<UdpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L129)
  Binds a UDP socket to `address` (port 0 asks for a free port) with no options. Tagged `#NoDiscard`.
- **UdpSocket** · [`export let bind_with = func(address: SocketAddr, options: SocketOptions) -> Result<UdpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/udp-socket.nra#L133)
  Binds with an option set built for `SocketKind.Udp` (another kind traps with "invalid argument"). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. The current API and host requirements are recorded above. The historical design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
