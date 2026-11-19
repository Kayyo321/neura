# `Std.Net.Core.Query`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Query`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Query`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Query** · [`export let QueryMode = enum (Rfc3986, Form)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L27)
  How `+` is read and how text is encoded. Rfc3986: `+` is a literal plus and is written `%2B`, a space is written `%20`, and keys and values keep the RFC 3986 query characters except `&`, `=`, `+`. Form: application/x-www-form-urlencoded, where `+` is a space and only ALPHA, DIGIT, `*-._` stay unencoded. Since 0.3.0.
- **Query** · [`export let QueryPair = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L31)
  One decoded key and its optional value. Tagged `#Final`, `#Immutable`. Since 0.3.0.
- **Query.QueryPair** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L33)
  The decoded key.
- **Query.QueryPair** · [`export last value: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L35)
  The decoded value, or None for a flag written without `=`.
- **Query.QueryPair** · [`export let init = func(pair_key: string, pair_value: optional.Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L38)
  A pair from a decoded key and an optional decoded value.
- **Query.QueryPair** · [`export let value_or_empty = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L45)
  The value, or the empty text for a flag. Tagged `#Pure`.
- **Query.QueryPair** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L48)
  Pairs are equal when keys are equal and both values are absent or equal.
- **Query.QueryPair** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L57)
  Combines the key and value hashes. Tagged `#Pure`.
- **Query.QueryPair** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L62)
  The pair encoded in the Rfc3986 mode. Tagged `#Pure`.
- **Query** · [`export let QueryString = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L68)
  An ordered, mutable list of query pairs. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Query.QueryString** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L73)
  An empty query.
- **Query.QueryString** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L77)
  The number of pairs.
- **Query.QueryString** · [`export let pair_at = func(index: int) -> QueryPair`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L80)
  The pair at an index from 0; an index out of range traps.
- **Query.QueryString** · [`export let get = func(key: string) -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L83)
  The first value for the key: Some("") for a flag, None when the key is missing.
- **Query.QueryString** · [`export let get_all = func(key: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L93)
  Every value for the key in order, flags as empty text.
- **Query.QueryString** · [`export let contains = func(key: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L104)
  Tests whether any pair has the key.
- **Query.QueryString** · [`export let add = func(key: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L114)
  Appends a pair with a value (`key=value`).
- **Query.QueryString** · [`export let add_flag = func(key: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L119)
  Appends a flag without a value (`key`).
- **Query.QueryString** · [`export let remove_all = func(key: string) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L124)
  Removes every pair with the key and returns how many were removed.
- **Query.QueryString** · [`export let set = func(key: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L137)
  Replaces the pairs with the key by one pair at the place of the first, or appends it.
- **Query.QueryString** · [`export let encode = func(mode: QueryMode) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L165)
  The raw query text (without `?`): pairs joined by `&`, keys and values encoded for the mode.
- **Query.QueryString** · [`export let tree = func -> QueryNode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L178)
  The nested-key tree of the pairs (see the module comment).
- **Query.QueryString** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L192)
  Two queries are equal when their pairs are equal in order.
- **Query.QueryString** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L206)
  Combines the pair hashes in order.
- **Query.QueryString** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L215)
  The query encoded in the Rfc3986 mode.
- **Query** · [`export let QueryNode = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L221)
  One node of the nested-key tree: the values assigned at its path and its children in first-seen order. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Query.QueryNode** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L223)
  The node's segment name; the root's is empty.
- **Query.QueryNode** · [`export last values: string[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L225)
  The values assigned at exactly this path, in order (a flag adds the empty text).
- **Query.QueryNode** · [`export last children: QueryNode[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L227)
  The child nodes in first-seen order.
- **Query.QueryNode** · [`export let init = func(node_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L230)
  A node with no values or children.
- **Query.QueryNode** · [`export let child = func(child_name: string) -> optional.Optional<QueryNode>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L235)
  The child with the name, if any.
- **Query.QueryNode** · [`export let value = func -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L245)
  The first value at this path, if any.
- **Query.QueryNode** · [`export let child_names = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L251)
  The names of the children in order.
- **Query.QueryNode** · [`export let child_for = func(segment: string) -> QueryNode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L261)
  The existing child with the name, or a new one appended; the empty name appends a child named by its index.
- **Query.QueryNode** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L278)
  The node as `name{values}[children]`, for diagnostics.
- **Query** · [`export let parse = func(raw: string, mode: QueryMode) -> Result<QueryString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L302)
  The pairs of a raw query (the text after `?`, without it). Errors: Parse InvalidCharacter at the offset in `raw` of a `%` not followed by two hex digits; Encoding at the offset in `raw` of decoded bytes that are not UTF-8. Tagged `#NoDiscard`. Since 0.3.0.
- **Query** · [`export let key_path = func(key: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/query.nra#L333)
  The path segments of a key under the bracket convention; a key that does not fit it is one segment. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
