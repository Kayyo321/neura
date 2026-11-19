# `Std.Net.Core.Urn`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Core.Urn`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Core.Urn`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Urn** · [`export let Urn = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L25)
  A parsed URN. Equality is URN-equivalence: the lowercased NID and the NSS with uppercase percent-encoding hex digits are compared, and the r-, q- and f-components are ignored. Tagged `#Final`, `#Immutable`, `#ThreadSafe`. Since 0.3.0.
- **Urn.Urn** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L29)
  The scheme as written (`urn` in any case).
- **Urn.Urn** · [`export last nid: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L31)
  The namespace identifier as written.
- **Urn.Urn** · [`export last nss: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L33)
  The namespace-specific string as written (percent-encoded).
- **Urn.Urn** · [`export last r_component: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L35)
  The r-component after `?+`, as written.
- **Urn.Urn** · [`export last q_component: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L37)
  The q-component after `?=`, as written.
- **Urn.Urn** · [`export last f_component: optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L39)
  The f-component after `#`, as written.
- **Urn.Urn** · [`export let init = func(seal: Seal, source: string, scheme_text: string, nid_text: string, nss_text: string, r: optional.Optional<string>, q: optional.Optional<string>, f: optional.Optional<string>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L42)
  Builds a URN from parsed parts; only this module can name the seal.
- **Urn.Urn** · [`export let equivalence_key = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L57)
  `urn:` + the lowercased NID + `:` + the NSS with percent-encoding hex in uppercase: the text URN-equivalence compares (RFC 8141 section 3.1). Percent-encoded bytes are not decoded. Tagged `#Pure`.
- **Urn.Urn** · [`export let assigned_name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L62)
  The assigned name `urn:<NID>:<NSS>` as written, without the optional components. Tagged `#Pure`.
- **Urn.Urn** · [`export let decoded_nss = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L66)
  The NSS with percent-encoding decoded. Errors as in PercentEncoding.decode. Tagged `#NoDiscard`.
- **Urn.Urn** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L69)
  URN-equivalence (RFC 8141 section 3).
- **Urn.Urn** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L78)
  The hash of the equivalence key, consistent with equals. Tagged `#Pure`.
- **Urn.Urn** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L82)
  The text as parsed. Tagged `#Pure`.
- **Urn** · [`export let parse = func(text: string) -> Result<Urn>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/core/urn.nra#L92)
  Parses a URN. Errors (Parse, offset in bytes): Empty for empty text; Syntax when the text does not start with `urn:` in any case, the NID is malformed or not followed by `:`, the NSS is empty or starts with `/`, an r- or q-component is empty or starts with `/` or `?`, or a `?` is not followed by `+` or `=` in its place; InvalidCharacter at a byte that the component does not allow, including a `%` without two hex digits. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
