# `Std.Net.Rpc.Health`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Health`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Health`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Health** · [`export const ServiceName: string = "grpc.health.v1.Health"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L24)
  The service name of the protocol.
- **Health** · [`export let ServingStatus = enum (Unknown, Serving, NotServing, ServiceUnknown)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L27)
  `HealthCheckResponse.ServingStatus`, numbered 0 to 3 in declaration order.
- **Health** · [`export let number = func(value: ServingStatus) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L31)
  The proto number of a status. Tagged `#NoDiscard`.
- **Health** · [`export let of_number = func(value: int64) -> ServingStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L40)
  The status with a proto number; numbers outside 0 to 3 read as UNKNOWN (proto3 open enum). Tagged `#NoDiscard`.
- **Health** · [`export let check_path = func -> calls.MethodPath`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L53)
  The `Check` method path. Tagged `#NoDiscard`.
- **Health** · [`export let watch_path = func -> calls.MethodPath`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L57)
  The `Watch` method path. Tagged `#NoDiscard`.
- **Health** · [`export let encode_request = func(service: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L61)
  A `HealthCheckRequest` (field 1 omitted for the empty name, as proto3 does). Tagged `#NoDiscard`.
- **Health** · [`export let decode_request = func(bytes: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L71)
  The service name of a `HealthCheckRequest`; unknown fields are skipped. Tagged `#NoDiscard`.
- **Health** · [`export let encode_response = func(value: ServingStatus) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L87)
  A `HealthCheckResponse` (field 1 omitted for UNKNOWN). Tagged `#NoDiscard`.
- **Health** · [`export let decode_response = func(bytes: uint8[list]) -> Result<ServingStatus>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L97)
  The status of a `HealthCheckResponse`; unknown fields are skipped. Tagged `#NoDiscard`.
- **Health** · [`export let Watcher = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L113)
  A registered watcher of one service name. Tagged `#Final`.
- **Health.Watcher** · [`export last id: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L115)
  The id `watch` returned.
- **Health.Watcher** · [`export last service: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L117)
  The watched name.
- **Health.Watcher** · [`export let init = func(watcher_id: int, name: string, notify: func(ServingStatus) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L122)
  A watcher with every part given.
- **Health.Watcher** · [`export let report = func(value: ServingStatus)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L129)
  Reports `value`.
- **Health.Watcher** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L132)
  The id and name.
- **Health** · [`export let HealthService = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L137)
  Serving statuses by service name, with watchers. Tagged `#Final`, `#NotThreadSafe`.
- **Health.HealthService** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L148)
  A registry with only the server entry `""` set to SERVING.
- **Health.HealthService** · [`export let set_status = func(service: string, value: ServingStatus)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L154)
  Sets the status of `service` (registering it) and reports a change to its watchers.
- **Health.HealthService** · [`export let clear_status = func(service: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L170)
  Unregisters `service`; its watchers see SERVICE_UNKNOWN.
- **Health.HealthService** · [`export let shutdown = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L187)
  Marks every registered service NOT_SERVING, as a server does when it shuts down.
- **Health.HealthService** · [`export let check = func(service: string) -> Result<ServingStatus>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L198)
  The status of a registered `service`, or a NOT_FOUND status error. Tagged `#NoDiscard`.
- **Health.HealthService** · [`export let watch = func(service: string, callback: func(ServingStatus) -> void) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L209)
  Reports the current status of `service` to `callback` at once (SERVICE_UNKNOWN when not registered), then every change; returns the watcher id.
- **Health.HealthService** · [`export let cancel_watch = func(id: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L221)
  Removes a watcher; returns whether it existed.
- **Health.HealthService** · [`export let check_handler = func -> Service<calls.RpcRequest, calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L235)
  The `Check` handler: decodes the request, answers the status or NOT_FOUND. Tagged `#NoDiscard`.
- **Health.HealthService** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L239)
  The number of registered names.
- **Health** · [`export let register = func(health: HealthService, server: grpc.GrpcServer)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L264)
  Installs the `Check` method of `health` on `server`.
- **Health** · [`export let check_remote = func(channel: grpc.GrpcChannel, service: string, budget: optional.Optional<Duration>) -> Result<ServingStatus>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/health.nra#L269)
  Calls `Check` for `service` over `channel`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
