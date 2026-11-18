# `Std.Net.Http.CacheControl`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.CacheControl`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.CacheControl`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CacheControl** · [`export const MaxDeltaSeconds: int64 = 2147483648`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L26)
  The value a delta-seconds that does not fit is read as: 2^31 (RFC 9111 section 1.2.2).
- **CacheControl** · [`export let Directive = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L31)
  One cache directive: a lowercase name and an optional argument (quoted-string arguments are stored unquoted). Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **CacheControl.Directive** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L33)
  The directive name in lowercase.
- **CacheControl.Directive** · [`export last argument: Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L35)
  The argument, unquoted, or None when the directive has none.
- **CacheControl.Directive** · [`export let init = func(directive_name: string, value: Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L38)
  Stores a name (lowercased) and an argument.
- **CacheControl.Directive** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L44)
  The directive as it is written in a field value.
- **CacheControl.Directive** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L50)
  Equal names and arguments.
- **CacheControl.Directive** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L58)
  The hash of the written form.
- **CacheControl** · [`export let CacheControl = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L63)
  An ordered list of cache directives with typed accessors for the RFC 9111 directives. Tagged `#Final`, `#NotThreadSafe`.
- **CacheControl.CacheControl** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L70)
  An empty list of directives.
- **CacheControl.CacheControl** · [`export let add = func(name: string, argument: Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L76)
  Appends a directive. Traps with "invalid argument: directive name" unless `name` is a token, and with "invalid argument: directive argument" when the argument holds CR, LF, NUL or another control byte except tab.
- **CacheControl.CacheControl** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L92)
  The number of directives.
- **CacheControl.CacheControl** · [`export let at = func(index: int) -> Directive`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L95)
  Directive `index`.
- **CacheControl.CacheControl** · [`export let is_malformed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L99)
  Whether a parsed element did not fit the grammar (the directive was kept or dropped as the module description says).
- **CacheControl.CacheControl** · [`export let has = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L102)
  Whether `name` (any case) is present.
- **CacheControl.CacheControl** · [`export let argument = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L106)
  The argument of the first `name` directive; None when absent or without an argument. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let max_age = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L117)
  `max-age` (request, section 5.2.1.1; response, section 5.2.2.1) in seconds; an invalid argument reads as 0. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let s_maxage = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L121)
  `s-maxage` (section 5.2.2.10) in seconds; an invalid argument reads as 0. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let min_fresh = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L125)
  `min-fresh` (section 5.2.1.3) in seconds; None when absent or invalid. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let max_stale = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L130)
  `max-stale` (section 5.2.1.2) in seconds: 2147483648 (any staleness) without an argument, None when absent or when the argument is invalid. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let stale_while_revalidate = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L144)
  `stale-while-revalidate` (RFC 5861, kept as an extension) in seconds; None when absent or invalid. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let stale_if_error = func -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L148)
  `stale-if-error` (RFC 5861, kept as an extension) in seconds; None when absent or invalid. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let no_cache = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L151)
  `no-cache`, qualified or not (sections 5.2.1.4, 5.2.2.4).
- **CacheControl.CacheControl** · [`export let no_cache_fields = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L156)
  The field names of a qualified `no-cache` (section 5.2.2.4); empty when unqualified or absent. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let no_store = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L159)
  `no-store` (sections 5.2.1.5, 5.2.2.5).
- **CacheControl.CacheControl** · [`export let no_transform = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L162)
  `no-transform` (sections 5.2.1.6, 5.2.2.6).
- **CacheControl.CacheControl** · [`export let only_if_cached = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L165)
  `only-if-cached` (section 5.2.1.7).
- **CacheControl.CacheControl** · [`export let must_revalidate = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L168)
  `must-revalidate` (section 5.2.2.2).
- **CacheControl.CacheControl** · [`export let proxy_revalidate = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L171)
  `proxy-revalidate` (section 5.2.2.8).
- **CacheControl.CacheControl** · [`export let must_understand = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L174)
  `must-understand` (section 5.2.2.3).
- **CacheControl.CacheControl** · [`export let is_public = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L177)
  `public` (section 5.2.2.9).
- **CacheControl.CacheControl** · [`export let is_private = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L180)
  `private`, qualified or not (section 5.2.2.7).
- **CacheControl.CacheControl** · [`export let private_fields = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L184)
  The field names of a qualified `private` (section 5.2.2.7); empty when unqualified or absent. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let extensions = func -> Directive[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L188)
  The directives that RFC 9111 section 5.2 does not define, in order. Tagged `#NoDiscard`.
- **CacheControl.CacheControl** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L200)
  The directives as a field value: joined with ", ", arguments as tokens where possible and as quoted-strings otherwise (always quoted for the field lists of `no-cache` and `private`).
- **CacheControl.CacheControl** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L209)
  Equal when both hold equal directives in the same order.
- **CacheControl.CacheControl** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L226)
  Combines the directives' hashes in order.
- **CacheControl.CacheControl** · [`export let mark_malformed = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L235)
  Records that a parsed element did not fit the grammar.
- **CacheControl.CacheControl** · [`export let append_directive = func(d: Directive)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L240)
  Appends an already-built directive (used by the parser).
- **CacheControl** · [`export let parse = func(text: string) -> CacheControl`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L300)
  The directives of a `Cache-Control` field value (RFC 9111 section 5.2). Never fails; see the module description for malformed elements. Tagged `#NoDiscard`.
- **CacheControl** · [`export let of_headers = func(fields: headers.Headers) -> CacheControl`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L308)
  The directives of every `Cache-Control` field line of `fields`, in order. Tagged `#NoDiscard`.
- **CacheControl** · [`export let parse_strict = func(text: string) -> Result<CacheControl>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L319)
  `parse`, but Err (Syntax, at the start of the offending element) when an element does not fit `token [ "=" ( token / quoted-string ) ]`. Tagged `#NoDiscard`.
- **CacheControl** · [`export let parse_delta_seconds = func(text: string) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L332)
  delta-seconds (RFC 9111 section 1.2.2): one or more ASCII digits; values above 2147483648 read as 2147483648. None for an empty text or any other byte. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **CacheControl** · [`export let is_known = func(name: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L354)
  Whether RFC 9111 section 5.2 defines `name` (lowercase), as a request or a response directive. Tagged `#Pure`, `#ThreadSafe`.
- **CacheControl** · [`export let quote = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/cache-control.nra#L362)
  `text` as a quoted-string: in double quotes, with `"` and `\` escaped by a backslash. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
