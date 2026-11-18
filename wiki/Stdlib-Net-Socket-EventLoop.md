# `Std.Net.Socket.EventLoop`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.EventLoop`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.EventLoop`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **EventLoop** · [`export let EventLoopBackend = enum (IoUring, Epoll, Kqueue, Poll)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L25)
  A kernel interface the driver can wait with.
- **EventLoop** · [`export let Platform = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L29)
  An operating system and kernel version, as a deployment describes its target. Tagged `#Final`, `#Immutable`.
- **EventLoop.Platform** · [`export last system: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L32)
  The lowercase system name: "linux", "macos", "freebsd", "openbsd", "netbsd", "dragonfly", "windows", or another.
- **EventLoop.Platform** · [`export last major: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L34)
  The kernel's major version.
- **EventLoop.Platform** · [`export last minor: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L36)
  The kernel's minor version.
- **EventLoop.Platform** · [`export let init = func(name: string, major_version: int, minor_version: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L39)
  A platform. The name is lowercased; a negative version traps with "invalid argument: version".
- **EventLoop.Platform** · [`export let at_least = func(major_version: int, minor_version: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L50)
  True when the kernel is at least `major_version`.`minor_version`. Tagged `#Pure`.
- **EventLoop.Platform** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L54)
  Equal when the system and version are.
- **EventLoop.Platform** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L62)
  A hash consistent with equals.
- **EventLoop.Platform** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L66)
  Text such as "linux 6.1".
- **EventLoop** · [`export let default_order = func -> EventLoopBackend[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L71)
  The default preference: `IoUring`, `Epoll`, `Kqueue`, `Poll`. Tagged `#NoDiscard`.
- **EventLoop** · [`export let is_available = func(backend: EventLoopBackend, platform: Platform) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L76)
  Whether `platform` has `backend` (see the module documentation). Tagged `#Pure`.
- **EventLoop** · [`export let select = func(platform: Platform, preference: EventLoopBackend[list]) -> Result<EventLoopBackend>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L93)
  The first backend of `preference` that `platform` has, else the first of the default order it has. Err (a Custom error with the code `socket.event_loop`) when it has none, as on Windows. Tagged `#NoDiscard`.
- **EventLoop** · [`export let install = func(backend: EventLoopBackend) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/event-loop.nra#L111)
  Asks the runtime's driver to wait with `backend`. Traps in Neura 0.3.0: the driver waits with `poll(2)` only and no runtime offers a service to switch. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
