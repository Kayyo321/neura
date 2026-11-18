# `Std.Net.Http.Status`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Status`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Status`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Status** · [`export let reason_phrase = func(code: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L11)
  The reason phrase of `code`, or "" for a code without one. Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_valid = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L159)
  Whether `code` is in 100 through 599. Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_informational = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L163)
  1xx (RFC 9110 section 15.2). Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_success = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L167)
  2xx (RFC 9110 section 15.3). Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_redirection = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L171)
  3xx (RFC 9110 section 15.4). Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_client_error = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L175)
  4xx (RFC 9110 section 15.5). Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_server_error = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L179)
  5xx (RFC 9110 section 15.6). Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let has_no_content = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L183)
  Whether a response with `code` never has content: 1xx, 204 and 304 (RFC 9110 section 6.4.1). Tagged `#Pure`, `#ThreadSafe`.
- **Status** · [`export let is_heuristically_cacheable = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/status.nra#L188)
  The codes heuristically cacheable by default (RFC 9110 section 15.1): 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
