# `Std.Net.Socket.SocketOptions`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.SocketOptions`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.SocketOptions`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SocketOptions** · [`export let SocketKind = enum (TcpStream, TcpListener, Udp, UnixStream, UnixDatagram)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L33)
  The kind of socket an option set is built for.
- **SocketOptions** · [`export let SocketOptions = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L55)
  A validated option set: the host option numbers and values in option-number order, and whether the socket uses Multipath TCP. Tagged `#Final`.
- **SocketOptions.SocketOptions** · [`export last kind: SocketKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L57)
  The kind the set was validated for.
- **SocketOptions.SocketOptions** · [`export last multipath: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L59)
  True when the socket is created with Multipath TCP (`IPPROTO_MPTCP`).
- **SocketOptions.SocketOptions** · [`export let init = func(for_kind: SocketKind, settings: [int, int64][list], uses_multipath: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L64)
  Use `SocketOptionsBuilder.build` or `none`.
- **SocketOptions.SocketOptions** · [`export let settings = func -> [int, int64][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L74)
  The option numbers and values in option-number order (the numbers of `neura.net.option.set`: 1 `SO_REUSEADDR` through 12 `SO_BROADCAST`; booleans are 0 or 1, the user timeout is in milliseconds and the keepalive timers in seconds). A copy. Tagged `#NoDiscard`.
- **SocketOptions.SocketOptions** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L84)
  True when no option is set and multipath is off, so applying needs no host call. Tagged `#Pure`.
- **SocketOptions.SocketOptions** · [`export let value_of = func(option: int) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L88)
  The value set for option number `option`, or -1 when it is not set. Tagged `#Pure`.
- **SocketOptions.SocketOptions** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L99)
  Equal when the kind, the settings and the multipath flag are.
- **SocketOptions.SocketOptions** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L119)
  A hash consistent with equals.
- **SocketOptions.SocketOptions** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L130)
  Text such as "SocketOptions(TcpStream: no_delay=1, keep_alive=1)".
- **SocketOptions** · [`export let SocketOptionsBuilder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L148)
  Collects option values; `build` validates them for one socket kind. Setting an option again replaces its value. Tagged `#Final`, `#NotThreadSafe`.
- **SocketOptions.SocketOptionsBuilder** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L155)
  An empty builder.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_reuse_address = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L165)
  `SO_REUSEADDR` (on Windows, the semantics of `SO_EXCLUSIVEADDRUSE`).
- **SocketOptions.SocketOptionsBuilder** · [`export let set_reuse_port = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L171)
  `SO_REUSEPORT`: several sockets bind the same port and the kernel spreads the load. Unsupported on Windows.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_no_delay = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L176)
  `TCP_NODELAY`: disables Nagle's algorithm.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_fast_open = func(queue_length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L181)
  `TCP_FASTOPEN` on a listener: the queue length of pending fast-open requests.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_user_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L187)
  `TCP_USER_TIMEOUT`: how long sent data may stay unacknowledged before the connection is dropped; zero keeps the system default. Unsupported on Windows.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_keep_alive = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L192)
  `SO_KEEPALIVE`.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_keep_alive_idle = func(idle: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L197)
  `TCP_KEEPIDLE`: idle time before the first probe. Needs keepalive.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_keep_alive_interval = func(interval: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L202)
  `TCP_KEEPINTVL`: time between probes. Needs keepalive.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_keep_alive_count = func(probes: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L207)
  `TCP_KEEPCNT`: unanswered probes before the connection is dropped. Needs keepalive.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_receive_buffer = func(bytes: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L212)
  `SO_RCVBUF` in bytes.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_send_buffer = func(bytes: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L217)
  `SO_SNDBUF` in bytes.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_broadcast = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L222)
  `SO_BROADCAST`.
- **SocketOptions.SocketOptionsBuilder** · [`export let set_multipath = func(enabled: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L228)
  Creates the socket with Multipath TCP (`IPPROTO_MPTCP`, Linux 5.6 and later; Unsupported on Windows).
- **SocketOptions.SocketOptionsBuilder** · [`export let build = func(kind: SocketKind) -> Result<SocketOptions>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L235)
  The validated option set for `kind`, or the first problem as a Custom error with the code `socket.options` (see the module documentation). Tagged `#NoDiscard`.
- **SocketOptions.SocketOptionsBuilder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L293)
  Text such as "SocketOptionsBuilder".
- **SocketOptions** · [`export let builder = func -> SocketOptionsBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L311)
  An empty builder. Tagged `#NoDiscard`.
- **SocketOptions** · [`export let none = func(kind: SocketKind) -> SocketOptions`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L315)
  The empty option set for `kind`: nothing to apply. Tagged `#NoDiscard`.
- **SocketOptions** · [`export let applies = func(option: int, kind: SocketKind) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/socket-options.nra#L320)
  Whether option number `option` (13 for multipath) applies to sockets of `kind`. Tagged `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
