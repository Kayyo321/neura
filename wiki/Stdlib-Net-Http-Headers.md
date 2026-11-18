# `Std.Net.Http.Headers`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Headers`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Headers`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Headers** · [`export let Headers = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L18)
  Header fields in insertion order, looked up without regard to ASCII case. Tagged `#Final`, `#NotThreadSafe`.
- **Headers.Headers** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L27)
  An empty map.
- **Headers.Headers** · [`export let add = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L31)
  Appends a field; earlier values of the same name stay.
- **Headers.Headers** · [`export let set = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L41)
  Replaces every value of `name` with `value`, at the position of the first one (or at the end).
- **Headers.Headers** · [`export let remove = func(name: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L59)
  Removes every value of `name`; returns how many were removed.
- **Headers.Headers** · [`export let get = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L68)
  The first value of `name`. Tagged `#NoDiscard`.
- **Headers.Headers** · [`export let get_all = func(name: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L78)
  Every value of `name` in order, as a new list. Tagged `#NoDiscard`.
- **Headers.Headers** · [`export let joined = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L91)
  Every value of `name` joined with ", " (RFC 9110 section 5.3), or None when absent. Tagged `#NoDiscard`.
- **Headers.Headers** · [`export let list_elements = func(name: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L102)
  The comma-separated elements of every value of `name` (RFC 9110 section 5.6.1), each trimmed, empty elements dropped. Quoted strings are not split. Tagged `#NoDiscard`.
- **Headers.Headers** · [`export let contains = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L113)
  Whether `name` has a value.
- **Headers.Headers** · [`export let has_token = func(name: string, token: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L116)
  Whether any element of `name`'s list values equals `token` without regard to ASCII case.
- **Headers.Headers** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L126)
  The number of fields (a name with two values counts twice).
- **Headers.Headers** · [`export let name_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L129)
  The name of field `index` as it was added.
- **Headers.Headers** · [`export let value_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L132)
  The value of field `index`.
- **Headers.Headers** · [`export let lower_names = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L136)
  The distinct lowercase names in order of first appearance. Tagged `#NoDiscard`.
- **Headers.Headers** · [`export let copy = func -> Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L155)
  An independent copy. Tagged `#NoDiscard`.
- **Headers.Headers** · [`export let add_all = func(other: Headers)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L164)
  Adds every field of `other` after this map's fields.
- **Headers.Headers** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L171)
  Equal when both hold the same fields in the same order, names compared without case.
- **Headers.Headers** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L188)
  Combines each lowercase name and value in order.
- **Headers.Headers** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L198)
  The fields as "Name: value" lines joined by "; ", in braces.
- **Headers** · [`export let of = func(field_names: string[list], field_values: string[list]) -> Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L232)
  A map holding the given names and values in order. The lists must have the same length. Tagged `#NoDiscard`.
- **Headers** · [`export let is_token = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L245)
  Whether `name` is a non-empty token (RFC 9110 section 5.6.2). Tagged `#Pure`, `#ThreadSafe`.
- **Headers** · [`export let is_tchar = func(b: uint8) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L259)
  Whether byte `b` is a tchar (RFC 9110 section 5.6.2). Tagged `#Pure`, `#ThreadSafe`.
- **Headers** · [`export let is_valid_value = func(value: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L270)
  Whether `value` can be a field value: no CR, LF or NUL, and no other control byte except tab (RFC 9110 section 5.5). Tagged `#Pure`, `#ThreadSafe`.
- **Headers** · [`export let trim_value = func(value: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L282)
  `value` without leading and trailing spaces and tabs. Tagged `#Pure`, `#ThreadSafe`.
- **Headers** · [`export let split_list = func(value: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/headers.nra#L297)
  The comma-separated elements of one field value, trimmed, empty ones dropped; commas inside quoted strings do not separate. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
