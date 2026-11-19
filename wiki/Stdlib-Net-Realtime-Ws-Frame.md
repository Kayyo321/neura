# `Std.Net.Realtime.Ws.Frame`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.Frame`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.Frame`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Frame** · [`export const Continuation: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L19)
  Opcode 0: a continuation frame.
- **Frame** · [`export const Text: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L21)
  Opcode 1: a text frame.
- **Frame** · [`export const Binary: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L23)
  Opcode 2: a binary frame.
- **Frame** · [`export const Close: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L25)
  Opcode 8: a connection close.
- **Frame** · [`export const Ping: int = 9`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L27)
  Opcode 9: a ping.
- **Frame** · [`export const Pong: int = 10`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L29)
  Opcode 10: a pong.
- **Frame** · [`export const MaxControlPayload: int = 125`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L32)
  The largest control frame payload (RFC 6455 section 5.5).
- **Frame** · [`export let Role = enum (Client, Server)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L35)
  Which end of the connection a codec serves; it fixes the masking rule.
- **Frame** · [`export let Frame = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L39)
  One WebSocket frame with its payload unmasked. Tagged `#Final`.
- **Frame.Frame** · [`export last fin: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L41)
  The final fragment of a message.
- **Frame.Frame** · [`export last rsv1: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L43)
  RSV1, used by an extension such as permessage-deflate.
- **Frame.Frame** · [`export last rsv2: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L45)
  RSV2.
- **Frame.Frame** · [`export last rsv3: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L47)
  RSV3.
- **Frame.Frame** · [`export last opcode: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L49)
  The opcode, 0 to 15.
- **Frame.Frame** · [`export last payload: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L51)
  The application data, unmasked.
- **Frame.Frame** · [`export let init = func(final: bool, reserved1: bool, reserved2: bool, reserved3: bool, code: int, data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L55)
  A frame with every field given. An opcode outside 0 to 15 traps with "invalid argument: opcode".
- **Frame.Frame** · [`export let is_control = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L70)
  Whether the opcode is a control opcode (8 to 15). Tagged `#NoDiscard`.
- **Frame.Frame** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L73)
  Equal when every field and every payload byte are.
- **Frame.Frame** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L90)
  Combines the header bits, the opcode and the payload bytes.
- **Frame.Frame** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L100)
  The opcode, flags and payload length.
- **Frame** · [`export let of = func(opcode: int, payload: uint8[list]) -> Frame`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L118)
  A final frame with no reserved bits. Tagged `#NoDiscard`.
- **Frame** · [`export let encode = func(frame: Frame, mask: Optional<uint8[list]>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L127)
  Encodes the frame, masked with `mask` when it is `Some` (it must hold 4 bytes). Fails with status 1002 when the frame breaks a rule a sender must keep: a control frame longer than 125 bytes or not final, or a reserved opcode (3 to 7, 11 to 15). A mask that is not 4 bytes traps with "invalid argument: mask". Tagged `#NoDiscard`.
- **Frame** · [`export let encode_as = func(role: Role, frame: Frame, random: Random) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L193)
  Encodes a frame as `role` must send it: a client masks with 4 fresh bytes from `random`, a server does not mask. Tagged `#NoDiscard`.
- **Frame** · [`export let FrameDecoder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L207)
  Decodes frames from a byte stream that arrives in chunks of any size. `receiver` is the role of the endpoint that reads: a server requires every frame to be masked and a client requires none to be (RFC 6455 section 5.1). `max_frame_size` bounds a frame's payload; a larger length fails with status 1009 as soon as the header is read, before the payload arrives. `allowed_rsv` is a bit set of the reserved bits an extension negotiated (4 for RSV1, 2 for RSV2, 1 for RSV3); any other reserved bit fails. After a failure the decoder stays failed. The buffered bytes are read through an offset and compacted once the consumed prefix is at least half the buffer. Tagged `#Final`, `#NotThreadSafe`.
- **Frame.FrameDecoder** · [`export let init = func(role: Role, max_size: int, rsv_bits: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L223)
  A decoder for the endpoint `role`. A maximum below 125 traps with "invalid argument: max_frame_size".
- **Frame.FrameDecoder** · [`export let buffered = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L234)
  The number of received bytes not yet decoded. Tagged `#NoDiscard`.
- **Frame.FrameDecoder** · [`export let push = func(chunk: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L237)
  Adds received bytes.
- **Frame.FrameDecoder** · [`export let next = func -> Result<Optional<Frame>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L245)
  The next whole frame, `None` when more bytes are needed, or the failure of a malformed frame. Tagged `#NoDiscard`.
- **Frame.FrameDecoder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/frame.nra#L339)
  The role and buffered byte count.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
