# `Std.Net.Tls.Spiffe`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.Spiffe`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.Spiffe`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Spiffe** · [`export const MaxIdLength: int = 2048`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L24)
  The longest SPIFFE ID implementations must support, in bytes (section 2.3).
- **Spiffe** · [`export const MaxTrustDomainLength: int = 255`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L26)
  The longest trust domain name, in bytes (section 2.3).
- **Spiffe** · [`export let SpiffeId = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L33)
  A validated SPIFFE ID. Tagged `#Final`, `#Immutable`.
- **Spiffe.SpiffeId** · [`export last trust_domain: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L35)
  The trust domain name.
- **Spiffe.SpiffeId** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L37)
  The path, empty or starting with `/`.
- **Spiffe.SpiffeId** · [`export let init = func(seal: Seal, domain: string, path_text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L40)
  Use `parse` or `of`.
- **Spiffe.SpiffeId** · [`export let member_of = func(domain: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L47)
  True when the ID belongs to `domain`. Tagged `#Pure`.
- **Spiffe.SpiffeId** · [`export let segments = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L51)
  The path segments; empty for an ID without a path. Tagged `#NoDiscard`.
- **Spiffe.SpiffeId** · [`export let trust_domain_id = func -> SpiffeId`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L58)
  The ID of the trust domain itself (`spiffe://<domain>`), as a signing authority uses. Tagged `#NoDiscard`.
- **Spiffe.SpiffeId** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L61)
  Equal when the trust domain and path are.
- **Spiffe.SpiffeId** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L69)
  A hash consistent with equals.
- **Spiffe.SpiffeId** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L72)
  The URI text, `spiffe://<trust domain><path>`.
- **Spiffe** · [`export let parse = func(text: string) -> Result<SpiffeId>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L77)
  Parses a SPIFFE ID (see the module documentation). Tagged `#NoDiscard`.
- **Spiffe** · [`export let of = func(trust_domain: string, path: string) -> Result<SpiffeId>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L96)
  A SPIFFE ID from a trust domain and a path (empty, or starting with `/`), checked as by `parse`; offsets refer to the joined text. Tagged `#NoDiscard`.
- **Spiffe** · [`export let check_trust_domain_name = func(domain: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L101)
  Ok when `domain` is a valid trust domain name (section 2.1). Tagged `#NoDiscard`.
- **Spiffe** · [`export let verify_x509_svid = func(chain_pem: string, bundle_pem: string) -> Result<SpiffeId>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L109)
  Verifies an X.509-SVID chain (PEM) against a trust bundle (PEM) and returns its SPIFFE ID. Tagged `#NoDiscard`.
- **Spiffe** · [`export let verify_jwt_svid = func(token: string, jwks_json: string, audience: string) -> Result<SpiffeId>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/spiffe.nra#L116)
  Verifies a JWT-SVID against a JWK set (JSON) and an audience and returns its SPIFFE ID. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
