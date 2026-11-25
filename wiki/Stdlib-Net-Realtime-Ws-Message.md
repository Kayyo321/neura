# `Std.Net.Realtime.Ws.Message`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.Message`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.Message`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Message** · [`export let Message = enum (Text(string), Binary(uint8[list]))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L26)
  A whole data message.
- **Message** · [`export let bytes_of = func(message: Message) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L30)
  The payload bytes of a message: the UTF-8 bytes of text. Tagged `#NoDiscard`.
- **Message** · [`export let fragment = func(message: Message, max_fragment: int) -> frames.Frame[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L38)
  The frames of a message with fragments of at most `max_fragment` payload bytes (at least 1). An empty message is one empty final frame. A maximum below 1 traps with "invalid argument: max_fragment". Tagged `#NoDiscard`.
- **Message** · [`export let fragment_payload = func(first_code: int, data: uint8[list], max_fragment: int, compressed: bool) -> frames.Frame[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L50)
  The frames of a message payload with opcode `first_code` (1 or 2), as `fragment` splits them, with RSV1 set on the first frame when `compressed` (RFC 7692 section 6). A maximum below 1 traps with "invalid argument: max_fragment". Tagged `#NoDiscard`.
- **Message** · [`export let MessageAssembler = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L72)
  Reassembles messages from data frames received in order. Tagged `#Final`, `#NotThreadSafe`.
- **Message.MessageAssembler** · [`export let init = func(max_size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L86)
  An assembler that accepts messages of at most `max_size` bytes. A negative maximum traps with "invalid argument: max_message_size".
- **Message.MessageAssembler** · [`export let use_deflate = func(agreed: permessage.DeflateCodec)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L95)
  Decompresses messages whose first frame has RSV1 with the codec of an agreed permessage-deflate.
- **Message.MessageAssembler** · [`export let in_message = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L101)
  Whether a fragmented message is open. Tagged `#NoDiscard`.
- **Message.MessageAssembler** · [`export let add = func(frame: frames.Frame) -> Result<Optional<Message>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L107)
  Adds a data frame (opcode 0, 1 or 2). Returns the message when the frame completes one, `None` when more fragments are needed, or the failure of a broken sequence. A control frame traps with "invalid argument: control frame". Tagged `#NoDiscard`.
- **Message.MessageAssembler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/message.nra#L173)
  Whether a message is open and how many bytes it holds.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
