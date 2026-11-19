# `Std.Net.Http.AltSvc`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.AltSvc`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.AltSvc`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AltSvc** · [`export const DefaultMaxAge: int64 = 86400`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L26)
  The freshness of an advertisement without `ma`: 24 hours (section 3.1).
- **AltSvc** · [`export let Alternative = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L30)
  One alternative service: a protocol, an optional host, a port and the parameters. Tagged `#Final`, `#NotThreadSafe`.
- **AltSvc.Alternative** · [`export last protocol_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L32)
  The protocol-id as written (percent-encoded).
- **AltSvc.Alternative** · [`export last protocol: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L34)
  The decoded ALPN protocol name.
- **AltSvc.Alternative** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L36)
  The host, or "" for the origin's own host.
- **AltSvc.Alternative** · [`export last port: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L38)
  The port, 1 through 65535.
- **AltSvc.Alternative** · [`export last max_age: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L40)
  The `ma` parameter in seconds, 86400 when absent.
- **AltSvc.Alternative** · [`export last persist: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L42)
  Whether `persist=1` is present.
- **AltSvc.Alternative** · [`export last parameter_names: string[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L44)
  The names of the other parameters, in order.
- **AltSvc.Alternative** · [`export last parameter_values: string[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L46)
  The values of the other parameters (unquoted), parallel to `parameter_names`.
- **AltSvc.Alternative** · [`export let init = func(written_id: string, decoded: uint8[list], alt_host: string, alt_port: int, ma: int64, keep: bool, names: string[list], values: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L49)
  Stores the fields; the parameter lists are copied.
- **AltSvc.Alternative** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L67)
  The alternative as written in a field value, with `ma` only when it is not the default.
- **AltSvc.Alternative** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L83)
  Equal written forms.
- **AltSvc.Alternative** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L91)
  The hash of the written form.
- **AltSvc** · [`export let AltSvc = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L96)
  A parsed `Alt-Svc` field: `clear`, or the alternatives in preference order. Tagged `#Final`, `#NotThreadSafe`.
- **AltSvc.AltSvc** · [`export last clear: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L98)
  Whether the field is `clear`.
- **AltSvc.AltSvc** · [`export let init = func(is_clear: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L103)
  A `clear` value or an empty list of alternatives.
- **AltSvc.AltSvc** · [`export let add = func(alternative: Alternative)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L108)
  Appends an alternative; traps with "invalid argument: clear" on a `clear` value.
- **AltSvc.AltSvc** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L116)
  The number of alternatives.
- **AltSvc.AltSvc** · [`export let at = func(index: int) -> Alternative`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L119)
  Alternative `index`.
- **AltSvc.AltSvc** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L122)
  The field value: `clear`, or the alternatives joined with ", ".
- **AltSvc.AltSvc** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L134)
  Equal field values.
- **AltSvc.AltSvc** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L142)
  The hash of the field value.
- **AltSvc** · [`export let parse = func(text: string) -> Result<AltSvc>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L147)
  Parses an `Alt-Svc` field value (section 3); see the module description for the errors. Tagged `#NoDiscard`.
- **AltSvc** · [`export let preferred = func(value: AltSvc, supported: uint8[list][list]) -> Optional<Alternative>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L167)
  The first alternative, in the server's preference order, whose protocol is one of `supported`; None for `clear` or when none matches. Tagged `#NoDiscard`.
- **AltSvc** · [`export let encode_protocol_id = func(name: uint8[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L182)
  The protocol-id of an ALPN name (section 3): token characters other than `%` stay, every other byte becomes `%XX` with uppercase hex digits. Tagged `#Pure`, `#ThreadSafe`.
- **AltSvc** · [`export let decode_protocol_id = func(text: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alt-svc.nra#L200)
  The ALPN name of a protocol-id: `%XX` escapes (either case) decoded, other bytes kept. Err (InvalidCharacter) at a byte that is not a token character or at a broken escape. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
