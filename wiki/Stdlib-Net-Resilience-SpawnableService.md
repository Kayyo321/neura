# `Std.Net.Resilience.SpawnableService`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.SpawnableService`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.SpawnableService`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SpawnableService** · [`export let SpawnableService = class<Req, Resp> implements Service<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra#L20)
  A service that runs a spawnable function. Tagged `#Final`.
- **SpawnableService.SpawnableService** · [`export let init = func(handler: func(Req) -> Result<Resp>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra#L25)
  Wraps `handler`.
- **SpawnableService.SpawnableService** · [`export let call = func(request: Req) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra#L30)
  Runs the function in the calling task.
- **SpawnableService.SpawnableService** · [`export let function = func -> func(Req) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra#L33)
  The function, for a filter that runs it in a task.
- **SpawnableService.SpawnableService** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra#L36)
  The fixed text "SpawnableService".
- **SpawnableService** · [`export let of = func<Req, Resp>(handler: func(Req) -> Result<Resp>) -> SpawnableService<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/spawnable-service.nra#L41)
  A spawnable service running `handler`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
