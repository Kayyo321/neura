# `Std.Net.Realtime.Ws.Extensions`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.Extensions`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.Extensions`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Extensions** · [`export let ExtensionParam = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L20)
  One extension parameter: a name and an optional value. Tagged `#Final`, `#Immutable`.
- **Extensions.ExtensionParam** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L22)
  The parameter name.
- **Extensions.ExtensionParam** · [`export last value: Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L24)
  The value, unquoted, or `None` for a parameter without one.
- **Extensions.ExtensionParam** · [`export let init = func(param_name: string, param_value: Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L27)
  Stores the name and value.
- **Extensions.ExtensionParam** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L33)
  Equal when the name and value are.
- **Extensions.ExtensionParam** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L41)
  Combines the name and value.
- **Extensions.ExtensionParam** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L45)
  `name` or `name=value`.
- **Extensions** · [`export let Extension = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L53)
  One element of the list: an extension name and its parameters. Tagged `#Final`.
- **Extensions.Extension** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L55)
  The extension token, such as `permessage-deflate`.
- **Extensions.Extension** · [`export last params: ExtensionParam[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L57)
  The parameters in order.
- **Extensions.Extension** · [`export let init = func(token: string, parameters: ExtensionParam[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L60)
  Stores the name and parameters.
- **Extensions.Extension** · [`export let count = func(param_name: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L67)
  How many parameters are named `param_name`. Tagged `#NoDiscard`.
- **Extensions.Extension** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L78)
  Equal when the names and parameter lists are.
- **Extensions.Extension** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L92)
  Combines the name and parameters.
- **Extensions.Extension** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L101)
  The element as it is written in the header.
- **Extensions** · [`export let is_token_byte = func(c: uint8) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L112)
  Whether `c` is a tchar of RFC 9110 section 5.6.2. Tagged `#NoDiscard`, `#Pure`.
- **Extensions** · [`export let is_token = func(text: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L122)
  Whether the text is a non-empty token. Tagged `#NoDiscard`, `#Pure`.
- **Extensions** · [`export let parse = func(value: string) -> Result<Extension[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L135)
  Parses a header value into its extensions, in order. Empty list elements (`a, , b`) are skipped, as RFC 9110 section 5.6.1 allows; a value with no element fails. Tagged `#NoDiscard`.
- **Extensions** · [`export let format = func(list: Extension[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extensions.nra#L208)
  The header value of the extensions, `, `-separated. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
