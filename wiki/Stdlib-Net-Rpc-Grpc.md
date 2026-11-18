# `Std.Net.Rpc.Grpc`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Grpc`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Grpc`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Grpc** · [`export let Protocol = enum (Http2, Http3)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L29)
  The HTTP version a host transport would use.
- **Grpc** · [`export let ServerStream = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L33)
  The server's side of one call: the request messages to read and the responses to send. Tagged `#Final`, `#NotThreadSafe`.
- **Grpc.ServerStream** · [`export last request: calls.RpcRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L35)
  The request.
- **Grpc.ServerStream** · [`export last sent: uint8[list][list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L39)
  The responses sent so far.
- **Grpc.ServerStream** · [`export last headers: meta.Metadata = meta.empty()`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L41)
  Response header metadata.
- **Grpc.ServerStream** · [`export last trailers: meta.Metadata = meta.empty()`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L43)
  Custom trailing metadata.
- **Grpc.ServerStream** · [`export let init = func(call: calls.RpcRequest)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L46)
  A stream over `call`.
- **Grpc.ServerStream** · [`export let receive = func -> optional.Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L52)
  The next request message, or `None` after the client's last one. Tagged `#NoDiscard`.
- **Grpc.ServerStream** · [`export let send = func(message: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L61)
  Sends one response message.
- **Grpc.ServerStream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L66)
  The method and the counts.
- **Grpc** · [`export let StreamHandler = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L72)
  A streaming handler: reads requests from and sends responses to a `ServerStream`, then ends with OK (`Ok`) or with the status of its error.
- **Grpc** · [`export let FunctionStreamHandler = class implements StreamHandler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L79)
  A streaming handler that runs a function. Tagged `#Final`.
- **Grpc.FunctionStreamHandler** · [`export let init = func(run: func(ServerStream) -> Result<Unit>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L84)
  Wraps `run`.
- **Grpc.FunctionStreamHandler** · [`export let handle = func(stream: ServerStream) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L89)
  Runs the function.
- **Grpc.FunctionStreamHandler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L92)
  `FunctionStreamHandler`.
- **Grpc** · [`export let GrpcServer = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L102)
  Routes calls to handlers by method path. A unary handler is a `Service<RpcRequest, RpcResponse>` and gets exactly one request message (any other count is UNIMPLEMENTED, statuscodes.md "Request cardinality violation"); a stream handler gets every message. An unknown method is UNIMPLEMENTED; a non-gRPC content type is HTTP 415 (PROTOCOL-HTTP2.md); a handler error ends the call with `Status.of_error` of it. Tagged `#Final`, `#NotThreadSafe`.
- **Grpc.GrpcServer** · [`export let init = func(max_size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L113)
  A server accepting messages up to `max_size` bytes.
- **Grpc.GrpcServer** · [`export let add_unary = func(path: calls.MethodPath, handler: Service<calls.RpcRequest, calls.RpcResponse>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L118)
  Registers a unary handler for `path`, replacing an earlier one.
- **Grpc.GrpcServer** · [`export let add_streaming = func(path: calls.MethodPath, handler: StreamHandler)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L126)
  Registers a streaming handler for `path`, replacing an earlier one.
- **Grpc.GrpcServer** · [`export let methods = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L135)
  The registered method paths in registration order. Tagged `#NoDiscard`.
- **Grpc.GrpcServer** · [`export let handle = func(wire: calls.WireRequest) -> calls.WireResponse`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L147)
  Answers one native gRPC request. Tagged `#NoDiscard`.
- **Grpc.GrpcServer** · [`export let handle_web = func(wire: calls.WireRequest) -> calls.WireResponse`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L157)
  Answers one gRPC-Web request (`application/grpc-web[+fmt]` or `application/grpc-web-text[+fmt]`): the trailers travel as the body's last frame, and the text variant is base64 both ways. Tagged `#NoDiscard`.
- **Grpc.GrpcServer** · [`export let as_transport = func -> Service<calls.WireRequest, calls.WireResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L184)
  The server as an in-memory transport for a `GrpcChannel`. Tagged `#NoDiscard`.
- **Grpc.GrpcServer** · [`export let serve = func(port: int, protocol: Protocol) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L189)
  Serves on a host HTTP/2 or HTTP/3 listener at `port` until the listener closes. Tagged `#NoDiscard`.
- **Grpc.GrpcServer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L193)
  The number of methods.
- **Grpc** · [`export let ServerTransport = class implements Service<calls.WireRequest, calls.WireResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L257)
  A transport that calls a server's `handle`. Tagged `#Final`.
- **Grpc.ServerTransport** · [`export let init = func(handle: func(calls.WireRequest) -> calls.WireResponse)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L262)
  Wraps `handle`.
- **Grpc.ServerTransport** · [`export let call = func(request: calls.WireRequest) -> Result<calls.WireResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L267)
  Runs the handler.
- **Grpc.ServerTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L271)
  `ServerTransport`.
- **Grpc** · [`export let GrpcChannel = class implements Service<calls.RpcRequest, calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L280)
  A client channel to one authority over a transport. `call` sends one request and returns the response whatever its status; `unary` and the streaming helpers turn a non-OK status into a `Status` error. A transport failure that is not a status error becomes UNAVAILABLE (statuscodes.md "Connection failure"). Tagged `#Final`, `#NotThreadSafe`.
- **Grpc.GrpcChannel** · [`export last authority: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L282)
  The `:authority`.
- **Grpc.GrpcChannel** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L284)
  The `:scheme`.
- **Grpc.GrpcChannel** · [`export last subtype: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L286)
  The content subtype sent, such as `proto`.
- **Grpc.GrpcChannel** · [`export let init = func(host: string, uri_scheme: string, content_subtype: string, exchange: Service<calls.WireRequest, calls.WireResponse>, max_size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L293)
  A channel with every part given.
- **Grpc.GrpcChannel** · [`export let call = func(request: calls.RpcRequest) -> Result<calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L303)
  Sends `request` and returns the response with its status.
- **Grpc.GrpcChannel** · [`export let unary = func(path: calls.MethodPath, message: uint8[list], call_metadata: meta.Metadata, budget: optional.Optional<Duration>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L313)
  A unary call: exactly one response message on OK, otherwise the status as an error. Tagged `#NoDiscard`.
- **Grpc.GrpcChannel** · [`export let stream = func(path: calls.MethodPath, messages: uint8[list][list], call_metadata: meta.Metadata, budget: optional.Optional<Duration>) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L326)
  A streaming call of any kind: sends every message of `messages`, then returns every response message on OK, otherwise the status as an error. Tagged `#NoDiscard`.
- **Grpc.GrpcChannel** · [`export let open = func(path: calls.MethodPath, call_metadata: meta.Metadata) -> ClientStream`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L334)
  Opens a client stream to `path`; messages are sent when it is closed. Tagged `#NoDiscard`.
- **Grpc.GrpcChannel** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L338)
  The authority and subtype.
- **Grpc** · [`export let ClientStream = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L344)
  The client's side of a streaming call over a whole-body exchange: `send` queues requests, `close_send` performs the call, then `receive` yields the responses and `status` the final status. Tagged `#Final`, `#NotThreadSafe`.
- **Grpc.ClientStream** · [`export last path: calls.MethodPath`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L348)
  The method.
- **Grpc.ClientStream** · [`export let init = func(run: func(calls.RpcRequest) -> Result<calls.RpcResponse>, method_path: calls.MethodPath, request_metadata: meta.Metadata)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L359)
  A stream that calls `run`.
- **Grpc.ClientStream** · [`export let send = func(message: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L367)
  Queues a request; after `close_send` it traps with "invalid state: stream closed".
- **Grpc.ClientStream** · [`export let close_send = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L376)
  Ends the requests and performs the call; fails only when the exchange fails. Tagged `#NoDiscard`.
- **Grpc.ClientStream** · [`export let receive = func -> optional.Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L384)
  The next response message, or `None` at the end (or before `close_send`). Tagged `#NoDiscard`.
- **Grpc.ClientStream** · [`export let status = func -> optional.Optional<statuses.RpcStatus>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L401)
  The final status, `None` before `close_send`. Tagged `#NoDiscard`.
- **Grpc.ClientStream** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L407)
  The method and queued count.
- **Grpc** · [`export let call_unary = func<Req, Resp>(channel: GrpcChannel, path: calls.MethodPath, request: Req, request_codec: codecs.RpcCodec<Req>, response_codec: codecs.RpcCodec<Resp>, call_metadata: meta.Metadata, budget: optional.Optional<Duration>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L413)
  A typed unary call through `codec`s for the request and response. Tagged `#NoDiscard`.
- **Grpc** · [`export let unary_handler = func(body: func(uint8[list]) -> Result<uint8[list]>) -> Service<calls.RpcRequest, calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L426)
  A unary handler from a function over message bytes. Tagged `#NoDiscard`.
- **Grpc** · [`export let BytesHandler = class implements Service<calls.RpcRequest, calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L431)
  A unary handler over the request's one message. Tagged `#Final`.
- **Grpc.BytesHandler** · [`export let init = func(run: func(uint8[list]) -> Result<uint8[list]>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L436)
  Wraps `run`.
- **Grpc.BytesHandler** · [`export let call = func(request: calls.RpcRequest) -> Result<calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L441)
  Runs the function on the first message.
- **Grpc.BytesHandler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L445)
  `BytesHandler`.
- **Grpc** · [`export let connect = func(authority: string, protocol: Protocol, tls: bool, subtype: string) -> Result<GrpcChannel>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L450)
  A channel over a host HTTP/2 or HTTP/3 connection to `authority` (`host:port`), TLS when `tls`. Tagged `#NoDiscard`.
- **Grpc** · [`export let HostTransport = class implements Service<calls.WireRequest, calls.WireResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L459)
  A transport over a host HTTP/2 or HTTP/3 connection handle. Tagged `#Final`.
- **Grpc.HostTransport** · [`export let init = func(connection: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L464)
  Wraps `connection`.
- **Grpc.HostTransport** · [`export let call = func(request: calls.WireRequest) -> Result<calls.WireResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L469)
  One exchange on a new stream of the connection.
- **Grpc.HostTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L473)
  The handle.
- **Grpc** · [`export let as_status_error = func(problem: errors.Error) -> errors.Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/grpc.nra#L478)
  The status error of a transport failure: a status error as is, otherwise UNAVAILABLE. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
