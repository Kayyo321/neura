# `Std.Net.Socket.UnixListener`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.UnixListener`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.UnixListener`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **UnixListener** · [`export let UnixListener = class implements Listener<UnixAddr, UnixStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L22)
  A listening Unix-domain stream socket. Tagged `#Final`, `#NoDiscard(reason: "a listener must be closed")`, `#NotThreadSafe`.
- **UnixListener.UnixListener** · [`export let init = func(opened: int64, address: UnixAddr)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L31)
  Use `bind`; the ID comes from the library.
- **UnixListener.UnixListener** · [`export let address = func -> UnixAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L37)
  The bound address.
- **UnixListener.UnixListener** · [`export let accept = func -> Result<UnixStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L41)
  Waits for the next connection; its local address is this listener's and its peer is unknown. Tagged `#NoDiscard`.
- **UnixListener.UnixListener** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L53)
  Releases the listener. Closing again returns Ok.
- **UnixListener.UnixListener** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L64)
  Text such as "UnixListener(/run/app.sock)".
- **UnixListener** · [`export let bind = func(address: UnixAddr) -> Result<UnixListener>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/unix-listener.nra#L69)
  Listens on `address`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
