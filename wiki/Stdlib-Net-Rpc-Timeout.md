# `Std.Net.Rpc.Timeout`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Timeout`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/timeout.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Timeout`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Timeout** · [`export const Header: string = "grpc-timeout"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/timeout.nra#L17)
  The header name.
- **Timeout** · [`export const MaxDigits: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/timeout.nra#L19)
  The most digits a TimeoutValue has.
- **Timeout** · [`export const MaxValue: int64 = 99999999`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/timeout.nra#L21)
  The largest TimeoutValue.
- **Timeout** · [`export let parse = func(text: string) -> Result<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/timeout.nra#L29)
  Parses a `grpc-timeout` value. Fails with a Parse error for an empty value, more than 8 digits, a non-digit, or a missing or unknown unit. Tagged `#NoDiscard`.
- **Timeout** · [`export let format = func(span: Duration) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/timeout.nra#L54)
  Formats a duration in the finest unit whose value fits 8 digits, rounding up. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
