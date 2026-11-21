# `Std.Net.Socket.UnixDatagram`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.UnixDatagram`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.UnixDatagram`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UnixDatagram** · [`export let UnixDatagram = class implements DatagramSocket<UnixAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L27)
  One Unix-domain datagram socket. Tagged `#Final`, `#NoDiscard(reason: "a socket must be closed")`, `#NotThreadSafe`.
- **UnixDatagram.UnixDatagram** · [`export let init = func(opened: int64, address: Optional<UnixAddr>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L38)
  Use `bind` or `unbound`; the ID comes from the library.
- **UnixDatagram.UnixDatagram** · [`export let local_address = func -> Result<UnixAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L45)
  The bound address. An unbound socket has none: Err with InvalidInput. Tagged `#NoDiscard`.
- **UnixDatagram.UnixDatagram** · [`export let send_to = func(data: uint8[list], target: UnixAddr) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L56)
  Sends `data` as one datagram to `target`; the count the host sent. Tagged `#NoDiscard`.
- **UnixDatagram.UnixDatagram** · [`export let receive_from = func(max: int) -> Result<Datagram<UnixAddr>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L67)
  One datagram of at most `max` bytes, waiting up to the read timeout. A maximum below 1 traps. Tagged `#NoDiscard`.
- **UnixDatagram.UnixDatagram** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L78)
  The longest a receive waits; zero waits without limit.
- **UnixDatagram.UnixDatagram** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L81)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **UnixDatagram.UnixDatagram** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L86)
  Releases the socket. Closing again returns Ok.
- **UnixDatagram.UnixDatagram** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L96)
  Text such as "UnixDatagram(/run/app.sock)" or "UnixDatagram(unbound)".
- **UnixDatagram** · [`export let bind = func(address: UnixAddr) -> Result<UnixDatagram>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L107)
  A socket bound to `address`. Tagged `#NoDiscard`.
- **UnixDatagram** · [`export let unbound = func -> Result<UnixDatagram>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-datagram.nra#L112)
  An unnamed socket. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
