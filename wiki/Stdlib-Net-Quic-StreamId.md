# `Std.Net.Quic.StreamId`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.StreamId`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.StreamId`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **StreamId** · [`export const MaxStreamId: int64 = 4611686018427387903`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L18)
  The largest stream ID, 2^62 - 1.
- **StreamId** · [`export const MaxStreamCount: int64 = 1152921504606846976`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L20)
  The largest number of streams of one type, 2^60 (section 4.6).
- **StreamId** · [`export let StreamType = enum (ClientBidirectional, ServerBidirectional, ClientUnidirectional, ServerUnidirectional)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L23)
  The four stream types of RFC 9000 Table 1, in the order of their low bits 0x00 through 0x03.
- **StreamId** · [`export let type_of = func(id: int64) -> StreamType`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L28)
  The type of a stream ID. Tagged `#Pure`.
- **StreamId** · [`export let type_bits = func(kind: StreamType) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L37)
  The two low bits of a type (0 through 3). Tagged `#Pure`.
- **StreamId** · [`export let type_for = func(client: bool, bidirectional: bool) -> StreamType`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L42)
  The type of streams that `client` (true) or the server opens in the given direction. Tagged `#Pure`.
- **StreamId** · [`export let is_client_initiated = func(id: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L48)
  True when the client opened the stream (bit 0x01 clear). Tagged `#Pure`.
- **StreamId** · [`export let is_bidirectional = func(id: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L55)
  True for a bidirectional stream (bit 0x02 clear). Tagged `#Pure`.
- **StreamId** · [`export let index_of = func(id: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L62)
  The position of the stream among the streams of its type, from 0. Tagged `#Pure`.
- **StreamId** · [`export let nth = func(kind: StreamType, index: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L70)
  The ID of the `index`-th stream of `kind` (from 0). An index from which no valid ID follows traps with "invalid argument: stream index". Tagged `#Pure`.
- **StreamId** · [`export let may_send = func(id: int64, local_client: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L80)
  True when the client (`local_client`) or the server may send on stream `id`: on every bidirectional stream and on its own unidirectional streams. Tagged `#Pure`.
- **StreamId** · [`export let may_receive = func(id: int64, local_client: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L86)
  True when the client (`local_client`) or the server may receive on stream `id`: on every bidirectional stream and on the peer's unidirectional streams. Tagged `#Pure`.
- **StreamId** · [`export let StreamIdAllocator = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L91)
  Allocates the IDs of the streams one endpoint opens, within the peer's stream limits. Tagged `#Final`, `#NotThreadSafe`.
- **StreamId.StreamIdAllocator** · [`export last client: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L93)
  True for the client side.
- **StreamId.StreamIdAllocator** · [`export let init = func(is_client: bool, max_bidirectional: int64, max_unidirectional: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L106)
  An allocator for the client or the server with the peer's initial limits (`initial_max_streams_bidi` and `initial_max_streams_uni`). A limit outside 0 through 2^60 traps with "invalid argument: stream limit".
- **StreamId.StreamIdAllocator** · [`export let open = func(bidirectional: bool) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L117)
  The next stream ID of the direction, or a `quic.stream_limit` error when the peer's limit is reached (the endpoint would then send STREAMS_BLOCKED). Tagged `#NoDiscard`.
- **StreamId.StreamIdAllocator** · [`export let raise_limit = func(bidirectional: bool, max_streams: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L134)
  Applies a MAX_STREAMS frame from the peer; a smaller value than the current limit is ignored (section 19.11). A value above 2^60 traps with "invalid argument: stream limit".
- **StreamId.StreamIdAllocator** · [`export let opened = func(bidirectional: bool) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L146)
  The number of streams opened in the direction. Tagged `#Pure`.
- **StreamId.StreamIdAllocator** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L150)
  Text such as "StreamIdAllocator(client, 3/100 bidirectional, 0/3 unidirectional)".
- **StreamId** · [`export let PeerStreams = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L157)
  Tracks the streams a peer opens, applying the implicit opening of lower-numbered streams. Tagged `#Final`, `#NotThreadSafe`.
- **StreamId.PeerStreams** · [`export last local_client: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L159)
  True when the local endpoint is the client (so the peer is the server).
- **StreamId.PeerStreams** · [`export let init = func(is_client: bool, max_bidirectional: int64, max_unidirectional: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L170)
  A tracker with the limits this endpoint advertised. A limit outside 0 through 2^60 traps.
- **StreamId.PeerStreams** · [`export let receive = func(id: int64) -> Result<int64[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L182)
  Records a frame on stream `id` from the peer and returns the IDs it newly opens, in increasing order (empty when the stream was already open). Err `quic.stream_state` when `id` is of a type the local endpoint opens, and `quic.stream_limit` when it exceeds the advertised limit. Tagged `#NoDiscard`.
- **StreamId.PeerStreams** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/stream-id.nra#L212)
  Text such as "PeerStreams(2 bidirectional, 0 unidirectional)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
