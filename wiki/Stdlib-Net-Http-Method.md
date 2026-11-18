# `Std.Net.Http.Method`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Method`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Method`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Method** · [`export let HttpMethod = enum (Get, Head, Post, Put, Delete, Connect, Options, Trace, Patch, Extension(string))`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L12)
  A request method.
- **Method** · [`export let parse = func(text: string) -> Optional<HttpMethod>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L16)
  The method named by `text` (exact case), or None when `text` is not a token. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Method** · [`export let standard = func(text: string) -> Optional<HttpMethod>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L29)
  One of the nine standard methods named by `text`, or None. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Method** · [`export let name = func(m: HttpMethod) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L62)
  The method's token. Tagged `#Pure`, `#ThreadSafe`.
- **Method** · [`export let is_safe = func(m: HttpMethod) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L77)
  Whether the method is safe: GET, HEAD, OPTIONS, TRACE (RFC 9110 section 9.2.1). Tagged `#Pure`, `#ThreadSafe`.
- **Method** · [`export let is_idempotent = func(m: HttpMethod) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L81)
  Whether the method is idempotent: the safe methods, PUT and DELETE (RFC 9110 section 9.2.2). Tagged `#Pure`, `#ThreadSafe`.
- **Method** · [`export let is_safe_name = func(text: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L85)
  `is_safe` for a method token. Tagged `#Pure`, `#ThreadSafe`.
- **Method** · [`export let is_idempotent_name = func(text: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/method.nra#L90)
  `is_idempotent` for a method token. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
