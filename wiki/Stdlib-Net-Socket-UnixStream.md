# `Std.Net.Socket.UnixStream`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.UnixStream`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.UnixStream`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UnixStream** · [`export let UnixStream = class implements StreamSocket<UnixAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L28)
  One connected Unix-domain stream. Tagged `#Final`, `#NoDiscard(reason: "a socket must be closed")`, `#NotThreadSafe`.
- **UnixStream.UnixStream** · [`export let init = func(opened: socket.Handle, dialed: Optional<UnixAddr>, local: Optional<UnixAddr>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L43)
  Use `connect` or `UnixListener.accept`; the handle comes from the library.
- **UnixStream.UnixStream** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L53)
  Up to `max` bytes; an empty list once the peer has finished sending. A maximum below 1 traps. Tagged `#NoDiscard`.
- **UnixStream.UnixStream** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L57)
  Writes every byte. Tagged `#NoDiscard`.
- **UnixStream.UnixStream** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L61)
  The next bytes the peer sent (at most `ReceiveChunk`), or `None` at the end of its data. Tagged `#NoDiscard`.
- **UnixStream.UnixStream** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L69)
  Sends every byte; the `Std.Net.Transport` form of `write`. Tagged `#NoDiscard`.
- **UnixStream.UnixStream** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L72)
  The longest a read waits; zero waits without limit.
- **UnixStream.UnixStream** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L75)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **UnixStream.UnixStream** · [`export let peer_address = func -> Optional<UnixAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L80)
  The dialed peer, or `None` for an accepted socket.
- **UnixStream.UnixStream** · [`export let local_address = func -> Result<UnixAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L85)
  The listener's address for an accepted socket; otherwise it asks the host, which traps in Neura 0.3.0 (no local-address service). Tagged `#NoDiscard`.
- **UnixStream.UnixStream** · [`export let shutdown = func(how: Shutdown) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L99)
  Ends reading, writing or both. Traps in Neura 0.3.0: the runtime has no shutdown service. Tagged `#NoDiscard`.
- **UnixStream.UnixStream** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L109)
  Releases the connection. Closing again returns Ok.
- **UnixStream.UnixStream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L115)
  Text such as "UnixStream(to /run/app.sock)" or "UnixStream(accepted on @bus)".
- **UnixStream** · [`export let connect = func(address: UnixAddr) -> Result<UnixStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-stream.nra#L123)
  Connects to the Unix-domain stream socket at `address` (`neura.net.unix.connect`). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
