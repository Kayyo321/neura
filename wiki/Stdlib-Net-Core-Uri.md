# `Std.Net.Core.Uri`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Uri`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Uri`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Uri** · [`export let HostKind = enum (RegName, Ipv4, Ipv6, IpFuture)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L40)
  The form of a URI's host (RFC 3986 section 3.2.2). Since 0.3.0.
- **Uri** · [`export let Uri = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L44)
  A URI or relative reference, its components as written. Tagged `#Final`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **Uri.Uri** · [`export last scheme: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L46)
  The scheme as written, without `:`; None for a relative reference.
- **Uri.Uri** · [`export last userinfo: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L48)
  The userinfo as written, without `@`.
- **Uri.Uri** · [`export last host: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L51)
  The host as written; an IP literal keeps its brackets. Some (possibly empty) exactly when the URI has an authority.
- **Uri.Uri** · [`export last port: optional.Optional<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L53)
  The port number, 0 to 65535.
- **Uri.Uri** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L55)
  The path as written; possibly empty.
- **Uri.Uri** · [`export last query: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L57)
  The query as written, without `?`.
- **Uri.Uri** · [`export last fragment: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L59)
  The fragment as written, without `#`.
- **Uri.Uri** · [`export let init = func(seal: Seal, scheme_part: optional.Optional<string>, userinfo_part: optional.Optional<string>, host_part: optional.Optional<string>, port_part: optional.Optional<int>, path_part: string, query_part: optional.Optional<string>, fragment_part: optional.Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L62)
  Builds a URI from checked components; only this module can name the seal.
- **Uri.Uri** · [`export let has_authority = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L77)
  Tests whether the URI has an authority (`//` after the scheme). Tagged `#Pure`.
- **Uri.Uri** · [`export let is_relative = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L81)
  Tests whether this is a relative reference (no scheme). Tagged `#Pure`.
- **Uri.Uri** · [`export let authority = func -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L85)
  The authority as written (`userinfo@host:port`), if any. Tagged `#Pure`.
- **Uri.Uri** · [`export let host_kind = func -> optional.Optional<HostKind>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L92)
  The kind of host, if any. Tagged `#Pure`.
- **Uri.Uri** · [`export let host_name = func -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L99)
  The host without the brackets of an IP literal, as written; None without an authority. Tagged `#Pure`.
- **Uri.Uri** · [`export let decoded_userinfo = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L106)
  The userinfo percent-decoded, or empty when absent. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let decoded_host = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L111)
  The host percent-decoded without IP-literal brackets, or empty when absent. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let decoded_path = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L115)
  The path percent-decoded. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let decoded_query = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L120)
  The query percent-decoded (`+` stays `+`), or empty when absent. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let decoded_fragment = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L124)
  The fragment percent-decoded, or empty when absent. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let path_segments = func -> Result<string[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L129)
  The path's segments, each percent-decoded; the empty segment before a leading `/` is left out, so `/a/b/` gives `a`, `b`, and the empty text. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let query_parameters = func(mode: queries.QueryMode) -> Result<queries.QueryString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L145)
  The query parsed into key/value pairs (an empty list when absent). Errors as in Query.parse. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let resolve = func(reference: Uri) -> Result<Uri>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L151)
  The target of a reference resolved against this URI (RFC 3986 section 5.2.2, strict). Errors: Parse Syntax when this URI has no scheme. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let normalize = func -> Result<Uri>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L161)
  The normalized URI: syntax-based normalization (RFC 3986 section 6.2.2: lowercase scheme and host, percent-encoding normalized, dot segments removed when there is a scheme) and, for http and https, scheme-based normalization (section 6.2.3: the default port dropped and an empty path with an authority made `/`). A registered name with non-ASCII, written as UTF-8 or as percent-encoded UTF-8, is decoded and converted with Idna.to_ascii. Errors: those of PercentEncoding.decode and Idna.to_ascii for such a name. Tagged `#NoDiscard`.
- **Uri.Uri** · [`export let equivalent = func(other: Uri) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L165)
  Tests whether both URIs normalize to the same components; when either does not normalize, compares them as written.
- **Uri.Uri** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L178)
  Equal when every component is equal as written (not normalized).
- **Uri.Uri** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L187)
  The hash of the recomposed text, consistent with equals. Tagged `#Pure`.
- **Uri.Uri** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L191)
  The recomposed reference (RFC 3986 section 5.3). Tagged `#Pure`.
- **Uri** · [`export let parse = func(text: string) -> Result<Uri>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L204)
  Parses a URI or relative reference (RFC 3986 appendix A, `URI-reference`). Errors (Parse, offset in bytes): Syntax for an empty scheme, an unclosed `[`, text after `]` that is not `:port`, or an invalid IP literal; InvalidCharacter at a byte a component does not allow (including a `:` in the first segment of a relative path, non-ASCII outside a registered name, and a `%` without two hex digits); OutOfRange for a port above 65535. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Uri** · [`export let resolve = func(base: Uri, reference: Uri) -> Result<Uri>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L308)
  The target of a reference resolved against a base URI (RFC 3986 section 5.2.2, strict: a reference with a scheme is used as it is, apart from its dot segments). The base's fragment is ignored. Errors: Parse Syntax when the base has no scheme (section 5.2.1 requires an absolute base). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Uri** · [`export let remove_dot_segments = func(path: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L313)
  The path with `.` and `..` segments removed (RFC 3986 section 5.2.4). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Uri** · [`export let default_port = func(scheme: string) -> optional.Optional<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L344)
  The default port of http (80) and https (443), compared without case; None for other schemes. Tagged `#Pure`, `#ThreadSafe`. Since 0.3.0.
- **Uri** · [`export let UriBuilder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L357)
  Collects URI components and builds a checked Uri. Setters without `raw` percent-encode their argument for the component; `raw` setters take text that is already encoded. The builder does not return itself from setters (the language has no receiver expression); call them as statements. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Uri.UriBuilder** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L367)
  An empty builder: no components, empty path.
- **Uri.UriBuilder** · [`export let set_scheme = func(scheme: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L371)
  Sets the scheme (checked by build).
- **Uri.UriBuilder** · [`export let clear_scheme = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L376)
  Removes the scheme.
- **Uri.UriBuilder** · [`export let set_userinfo = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L381)
  Sets the userinfo from decoded text, encoding it with the UserInfo set.
- **Uri.UriBuilder** · [`export let set_raw_userinfo = func(raw: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L386)
  Sets the userinfo as already-encoded text.
- **Uri.UriBuilder** · [`export let clear_userinfo = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L391)
  Removes the userinfo.
- **Uri.UriBuilder** · [`export let set_host = func(name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L398)
  Sets the host from a name or address: text with a `:` is an IPv6 address and gets brackets (a `%` zone separator becomes `%25`); other text is encoded with the RegName set, except that its non-ASCII bytes are kept, and build converts a host with non-ASCII with Idna.to_ascii.
- **Uri.UriBuilder** · [`export let set_raw_host = func(raw: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L409)
  Sets the host as written (IP literals with brackets).
- **Uri.UriBuilder** · [`export let clear_host = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L414)
  Removes the host, and with it the authority.
- **Uri.UriBuilder** · [`export let set_port = func(port: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L419)
  Sets the port (checked by build).
- **Uri.UriBuilder** · [`export let clear_port = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L424)
  Removes the port.
- **Uri.UriBuilder** · [`export let set_path = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L429)
  Sets the path from decoded text, encoding it with the Path set (`/` separates segments).
- **Uri.UriBuilder** · [`export let set_raw_path = func(raw: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L434)
  Sets the path as already-encoded text.
- **Uri.UriBuilder** · [`export let add_path_segment = func(segment: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L439)
  Appends `/` and the segment encoded with the PathSegment set (so `/` in it is encoded).
- **Uri.UriBuilder** · [`export let set_query = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L444)
  Sets the query from decoded text, encoding it with the Query set.
- **Uri.UriBuilder** · [`export let set_raw_query = func(raw: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L449)
  Sets the query as already-encoded text.
- **Uri.UriBuilder** · [`export let set_query_parameters = func(parameters: queries.QueryString, mode: queries.QueryMode)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L454)
  Sets the query from key/value pairs, encoded for the mode.
- **Uri.UriBuilder** · [`export let add_query_parameter = func(key: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L460)
  Appends `key=value` to the query (with `&` when it is not empty), both encoded with the QueryComponent set.
- **Uri.UriBuilder** · [`export let clear_query = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L468)
  Removes the query.
- **Uri.UriBuilder** · [`export let set_fragment = func(text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L473)
  Sets the fragment from decoded text, encoding it with the Fragment set.
- **Uri.UriBuilder** · [`export let set_raw_fragment = func(raw: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L478)
  Sets the fragment as already-encoded text.
- **Uri.UriBuilder** · [`export let clear_fragment = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L483)
  Removes the fragment.
- **Uri.UriBuilder** · [`export let set_all = func(uri: Uri)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L488)
  Copies every component of a URI into the builder.
- **Uri.UriBuilder** · [`export let build = func -> Result<Uri>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L507)
  The URI of the components. The recomposed text is parsed again and must give back the same components, so a builder cannot make a URI that reads differently. Errors: Parse Syntax for a userinfo or port without a host, a non-empty path that does not start with `/` after a host, a path starting with `//` without a host, a relative path whose first segment contains `:`, or components that otherwise do not read back unchanged; the errors of parse for invalid raw components; those of PercentEncoding.decode and Idna.to_ascii for a non-ASCII host (which build decodes, converts, and encodes with the RegName set again). Tagged `#NoDiscard`.
- **Uri.UriBuilder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/uri.nra#L545)
  The text the components recompose to, unchecked.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
