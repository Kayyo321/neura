# `Std.Net.Rpc.Status`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Status`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Status`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Status** · [`export let Code = enum ( Ok, Cancelled, Unknown, InvalidArgument, DeadlineExceeded, NotFound, AlreadyExists, PermissionDenied, ResourceExhausted, FailedPrecondition, Aborted, OutOfRange, Unimplemented, Internal, Unavailable, DataLoss, Unauthenticated )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L21)
  The canonical status codes, numbered 0 to 16 in declaration order.
- **Status** · [`export const ErrorCode: string = "grpc.status"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L42)
  The error code text of a status failure.
- **Status** · [`export const StatusHeader: string = "grpc-status"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L44)
  The trailer holding the code.
- **Status** · [`export const MessageHeader: string = "grpc-message"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L46)
  The trailer holding the percent-encoded message.
- **Status** · [`export const DetailsHeader: string = "grpc-status-details-bin"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L48)
  The binary trailer holding a serialized `google.rpc.Status`.
- **Status** · [`export let all = func -> Code[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L52)
  Every code in number order. Tagged `#NoDiscard`.
- **Status** · [`export let number = func(code: Code) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L59)
  The code's number, 0 to 16. Tagged `#NoDiscard`.
- **Status** · [`export let of_number = func(value: int) -> Optional<Code>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L81)
  The code with that number, or `None` outside 0 to 16. Tagged `#NoDiscard`.
- **Status** · [`export let name = func(code: Code) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L90)
  The canonical name, such as `DEADLINE_EXCEEDED`. Tagged `#NoDiscard`.
- **Status** · [`export let of_name = func(text: string) -> Optional<Code>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L94)
  The code with the canonical name, or `None`. Tagged `#NoDiscard`.
- **Status** · [`export let from_http = func(http_status: int) -> Code`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L108)
  The gRPC code a client synthesizes for an HTTP response without `grpc-status` (http-grpc-status-mapping.md): 400 INTERNAL, 401 UNAUTHENTICATED, 403 PERMISSION_DENIED, 404 UNIMPLEMENTED, 429/502/503/504 UNAVAILABLE, every other status (200 included) UNKNOWN. Tagged `#NoDiscard`.
- **Status** · [`export let to_http = func(code: Code) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L130)
  The HTTP status of a code per google/rpc/code.proto ("HTTP Mapping"), for gateways that answer a gRPC failure over plain HTTP. A gRPC server itself always answers `:status 200`. Tagged `#NoDiscard`.
- **Status** · [`export let encode_message = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L153)
  Percent-encodes a status message (PROTOCOL-HTTP2.md "Percent-Encoded"): its UTF-8 bytes in 0x20 to 0x7E except `%` stay, every other byte becomes `%XX` with upper-case hex. Tagged `#NoDiscard`.
- **Status** · [`export let decode_message = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L173)
  Decodes a percent-encoded status message; never fails. A `%` not followed by two hex digits is kept as is; when the decoded bytes are not UTF-8 the raw text is returned unchanged (both allowed by PROTOCOL-HTTP2.md). Tagged `#NoDiscard`.
- **Status** · [`export let RpcStatus = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L193)
  A call's final status: a code, a message, and optional serialized `google.rpc.Status` details. Tagged `#Final`.
- **Status.RpcStatus** · [`export last code: Code`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L195)
  The code.
- **Status.RpcStatus** · [`export last message: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L197)
  The message, possibly empty.
- **Status.RpcStatus** · [`export last details: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L199)
  The serialized `google.rpc.Status`, empty when there are no details.
- **Status.RpcStatus** · [`export let init = func(status_code: Code, text: string, detail_bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L202)
  A status with every part given.
- **Status.RpcStatus** · [`export let is_ok = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L210)
  Whether the code is OK. Tagged `#NoDiscard`.
- **Status.RpcStatus** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L213)
  Equal when the code, message and details are.
- **Status.RpcStatus** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L230)
  Combines the code, message and details.
- **Status.RpcStatus** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L239)
  `NAME: message`.
- **Status** · [`export let of = func(code: Code, message: string) -> RpcStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L245)
  A status without details. Tagged `#NoDiscard`.
- **Status** · [`export let ok = func -> RpcStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L249)
  The OK status. Tagged `#NoDiscard`.
- **Status** · [`export let to_trailers = func(status: RpcStatus) -> metadata.Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L254)
  The trailers of a status: `grpc-status`, then `grpc-message` when the message is not empty, then `grpc-status-details-bin` when the code is not OK and details are present. Tagged `#NoDiscard`.
- **Status** · [`export let from_trailers = func(trailers: metadata.Metadata, http_status: int) -> RpcStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L274)
  The status a client reads from trailers (or a Trailers-Only header block) of a response with HTTP status `http_status`. Without `grpc-status`, the code comes from `from_http` and the message names the HTTP status. A value that is not a decimal code 0 to 16 gives UNKNOWN. Details are kept only for a non-OK code, when they decode as base64 and as a `google.rpc.Status` whose code field (if present) equals `grpc-status`; otherwise they are dropped. Tagged `#NoDiscard`.
- **Status** · [`export let encode_rpc_status = func(status: RpcStatus) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L305)
  The `google.rpc.Status` message of a status: code (field 1), message (field 2); its `details` (field 3, `google.protobuf.Any`) are not modelled. Tagged `#NoDiscard`.
- **Status** · [`export let RpcFailure = class:errors.ErrorDetail`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L318)
  The error detail of a gRPC failure: the status a call ends with. Tagged `#Final`.
- **Status.RpcFailure** · [`export last status: RpcStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L320)
  The status.
- **Status.RpcFailure** · [`export let init = func(value: RpcStatus)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L323)
  Wraps `value`.
- **Status.RpcFailure** · [`export let.override code = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L329)
  Always `grpc.status`.
- **Status.RpcFailure** · [`export let.override message = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L332)
  `NAME: message`.
- **Status.RpcFailure** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L335)
  Equal when the statuses are.
- **Status.RpcFailure** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L339)
  The status's hash.
- **Status.RpcFailure** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L342)
  `grpc.status NAME: message`.
- **Status** · [`export let to_error = func(status: RpcStatus) -> errors.Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L347)
  A Custom error carrying `status`. Tagged `#NoDiscard`.
- **Status** · [`export let error = func(code: Code, message: string) -> errors.Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L351)
  A Custom error carrying a status with `code` and `message`. Tagged `#NoDiscard`.
- **Status** · [`export let of_error = func(problem: errors.Error) -> RpcStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/status.nra#L356)
  The status an error maps to: an `RpcFailure`'s status, the status inside a Context error's cause, otherwise UNKNOWN with the error's message (statuscodes.md: an application failure without a status). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
