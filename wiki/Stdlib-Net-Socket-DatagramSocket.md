# `Std.Net.Socket.DatagramSocket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.DatagramSocket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.DatagramSocket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DatagramSocket** · [`export let Datagram = class<Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra#L16)
  One received datagram and the address of its sender, when it has one. Tagged `#Final`.
- **DatagramSocket.Datagram** · [`export last data: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra#L18)
  The payload, truncated to the maximum of the receive call.
- **DatagramSocket.Datagram** · [`export last sender: Optional<Addr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra#L20)
  The sender's address; `None` for an unnamed Unix-domain sender.
- **DatagramSocket.Datagram** · [`export let init = func(payload: uint8[list], from: Optional<Addr>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra#L23)
  Pairs a payload with its sender.
- **DatagramSocket.Datagram** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra#L30)
  Text such as "Datagram(5 bytes from 127.0.0.1:9000)" or "Datagram(5 bytes from an unnamed sender)".
- **DatagramSocket** · [`export let DatagramSocket = interface<Addr>:Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/datagram-socket.nra#L38)
  A socket that exchanges whole datagrams with addresses of type `Addr`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. The current API and host requirements are recorded above. The historical design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
