# `Std.Net.Socket.TcpSocket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.TcpSocket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.TcpSocket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TcpSocket** · [`export let TcpSocket = class implements StreamSocket<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L40)
  One connected TCP socket. Tagged `#Final`, `#NoDiscard(reason: "a socket must be closed")`, `#NotThreadSafe`.
- **TcpSocket.TcpSocket** · [`export last multipath: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L52)
  True when the socket was created with Multipath TCP.
- **TcpSocket.TcpSocket** · [`export let init = func(opened: socket.Handle, dialed: Optional<SocketAddr>, local: Optional<SocketAddr>, uses_multipath: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L57)
  Use `connect`, `TcpConnector` or `TcpServer`; the handle comes from the library.
- **TcpSocket.TcpSocket** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L69)
  Up to `max` bytes; an empty list once the peer has finished sending. A maximum below 1 traps. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L73)
  Writes every byte, as `TcpStream.write`. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L77)
  The next bytes the peer sent (at most `ReceiveChunk`), or `None` at the end of its data. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L87)
  Sends every byte; the `Std.Net.Transport` form of `write`. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let read_timeout = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L90)
  The longest a read waits; zero waits without limit.
- **TcpSocket.TcpSocket** · [`export let set_read_timeout = func(timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L93)
  Sets the read timeout. A negative timeout traps with "invalid argument: negative timeout".
- **TcpSocket.TcpSocket** · [`export let peer_address = func -> Optional<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L98)
  The peer this socket dialed, or `None` for an accepted socket (not reported by the host).
- **TcpSocket.TcpSocket** · [`export let local_port = func -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L102)
  The local port, from the host. Works today. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let local_address = func -> Result<SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L113)
  The local address. Known without the host for an accepted socket whose server is bound to a specific address; otherwise it asks the host, which traps in Neura 0.3.0 (no local-address service). Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let shutdown = func(how: Shutdown) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L128)
  Ends reading, writing (the peer reads the end of the stream) or both, keeping the socket open. Traps in Neura 0.3.0: the runtime has no shutdown service. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let set_options = func(options: SocketOptions) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L141)
  Applies an option set built for `SocketKind.TcpStream`; another kind, or a set with multipath (chosen at connect), traps with "invalid argument". An empty set does nothing; a non-empty set is applied with the host's option service. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let send_file = func(path: string, offset: int64, count: int64) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L155)
  Sends up to `count` bytes of the file at `path`, from byte `offset`, straight from the file to the socket (`sendfile(2)` on Linux); the count sent, which is short at the end of the file. Opening the file needs the fs capability. The transfer needs the host's send-file service, which traps in Neura 0.3.0. A negative offset or count traps with "invalid argument". Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let splice_to = func(target: TcpSocket, count: int64) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L180)
  Moves up to `count` bytes from this socket to `target` inside the kernel (`splice(2)` through a pipe); the count moved. Needs a splice host service, which no runtime offers yet: traps in Neura 0.3.0. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let send_zerocopy = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L195)
  Sends every byte with `MSG_ZEROCOPY`, so the kernel reads the caller's pages instead of copying them. Needs a zero-copy host service, which no runtime offers yet (host values are copied at the boundary): traps in Neura 0.3.0. Tagged `#NoDiscard`.
- **TcpSocket.TcpSocket** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L205)
  Releases the connection, as `TcpStream.close`. Closing again returns Ok.
- **TcpSocket.TcpSocket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L211)
  Text such as "TcpSocket(to 127.0.0.1:8080)" or "TcpSocket(accepted on 127.0.0.1:9000)".
- **TcpSocket** · [`export let connect = func(address: SocketAddr, timeout: Duration) -> Result<TcpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L220)
  Connects to `address`, waiting at most `timeout` (zero waits without limit). A negative timeout traps with "invalid argument: negative timeout". Tagged `#NoDiscard`.
- **TcpSocket** · [`export let connect_with = func(address: SocketAddr, timeout: Duration, options: SocketOptions) -> Result<TcpSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/tcp-socket.nra#L228)
  Connects to `address` with an option set built for `SocketKind.TcpStream` (another kind traps). With multipath the socket is created with Multipath TCP, which needs a host service that no runtime offers yet and traps. The other options are applied after the connection is made; if that fails the socket is closed and the error returned. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
