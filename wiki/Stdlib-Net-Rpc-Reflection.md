# `Std.Net.Rpc.Reflection`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Reflection`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Reflection`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Reflection** · [`export const ServiceName: string = "grpc.reflection.v1.ServerReflection"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L21)
  The service name of the protocol.
- **Reflection** · [`export let MethodDescriptor = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L25)
  One method of a service: its name, message types and streaming kind. Tagged `#Final`.
- **Reflection.MethodDescriptor** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L27)
  The method name, such as `Check`.
- **Reflection.MethodDescriptor** · [`export last input_type: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L29)
  The fully qualified request message type.
- **Reflection.MethodDescriptor** · [`export last output_type: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L31)
  The fully qualified response message type.
- **Reflection.MethodDescriptor** · [`export last client_streaming: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L33)
  Whether the client streams.
- **Reflection.MethodDescriptor** · [`export last server_streaming: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L35)
  Whether the server streams.
- **Reflection.MethodDescriptor** · [`export let init = func(method_name: string, input: string, output: string, client_streams: bool, server_streams: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L38)
  A descriptor with every part given.
- **Reflection.MethodDescriptor** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L48)
  Equal when every part is.
- **Reflection.MethodDescriptor** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L57)
  Combines the parts.
- **Reflection.MethodDescriptor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L62)
  `name(input) returns (output)` with `stream` markers, as in a `.proto` file.
- **Reflection** · [`export let ReflectionRegistry = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L68)
  Registered services and their methods. Tagged `#Final`, `#NotThreadSafe`.
- **Reflection.ReflectionRegistry** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L75)
  An empty registry.
- **Reflection.ReflectionRegistry** · [`export let add_service = func(name: string, service_methods: MethodDescriptor[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L79)
  Registers a service with its methods, replacing an earlier registration of the name.
- **Reflection.ReflectionRegistry** · [`export let list_services = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L92)
  The registered service names, sorted. Tagged `#NoDiscard`.
- **Reflection.ReflectionRegistry** · [`export let methods_of = func(service: string) -> MethodDescriptor[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L103)
  The methods of `service`, empty when unknown. Tagged `#NoDiscard`.
- **Reflection.ReflectionRegistry** · [`export let method_paths = func -> calls.MethodPath[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L114)
  The method paths of every registered method. Tagged `#NoDiscard`.
- **Reflection.ReflectionRegistry** · [`export let file_by_filename = func(filename: string) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L127)
  The serialized `FileDescriptorProto`s of the file named `filename` and its dependencies. Tagged `#NoDiscard`.
- **Reflection.ReflectionRegistry** · [`export let file_containing_symbol = func(symbol: string) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L134)
  The serialized `FileDescriptorProto`s of the file defining `symbol` and its dependencies. Tagged `#NoDiscard`.
- **Reflection.ReflectionRegistry** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L139)
  The number of services.
- **Reflection** · [`export let encode_list_services_response = func(names: string[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L146)
  The `ServerReflectionResponse` answering a `list_services` request: field 6 holding a `ListServiceResponse` with one `ServiceResponse` per name, in the order given. Tagged `#NoDiscard`.
- **Reflection** · [`export let decode_list_services_response = func(bytes: uint8[list]) -> Result<string[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/reflection.nra#L161)
  The service names of a `ServerReflectionResponse` holding a `ListServiceResponse`; unknown fields are skipped. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
