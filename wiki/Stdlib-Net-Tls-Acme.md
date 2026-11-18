# `Std.Net.Tls.Acme`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.Acme`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.Acme`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Acme** · [`export let ChallengeType = enum (Http01, Dns01, TlsAlpn01)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L26)
  The challenge types of RFC 8555 section 8 and RFC 8737.
- **Acme** · [`export const AcmeTlsAlpn: string = "acme-tls/1"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L29)
  The ALPN protocol of TLS-ALPN-01 (RFC 8737 section 6.2).
- **Acme** · [`export let type_name = func(challenge: ChallengeType) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L33)
  The wire name of a challenge type: "http-01", "dns-01" or "tls-alpn-01". Tagged `#Pure`.
- **Acme** · [`export let rsa_thumbprint = func(modulus: string, exponent: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L39)
  The RFC 7638 thumbprint of an RSA public key given as the base64url `n` and `e` members: base64url(SHA-256 of `{"e":..,"kty":"RSA","n":..}`). Tagged `#NoDiscard`.
- **Acme** · [`export let ec_thumbprint = func(curve: string, x: string, y: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L45)
  The RFC 7638 thumbprint of an EC public key given as `crv` and the base64url `x` and `y` members: base64url(SHA-256 of `{"crv":..,"kty":"EC","x":..,"y":..}`). Tagged `#NoDiscard`.
- **Acme** · [`export let key_authorization = func(token: string, thumbprint: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L50)
  The key authorization `token.thumbprint` (RFC 8555 section 8.1). Tagged `#NoDiscard`.
- **Acme** · [`export let http01_path = func(token: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L58)
  The HTTP-01 resource path `/.well-known/acme-challenge/<token>` (RFC 8555 section 8.3); the response body is the key authorization. Tagged `#NoDiscard`.
- **Acme** · [`export let dns01_record_name = func(domain: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L65)
  The DNS-01 record name `_acme-challenge.<domain>` (RFC 8555 section 8.4). A trailing dot is kept. Tagged `#Pure`.
- **Acme** · [`export let dns01_txt_value = func(key_authorization: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L69)
  The DNS-01 TXT value: base64url(SHA-256(key authorization)) (RFC 8555 section 8.4). Tagged `#NoDiscard`.
- **Acme** · [`export let tls_alpn01_extension_value = func(key_authorization: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L74)
  The DER `extnValue` of the TLS-ALPN-01 `acmeIdentifier` extension: OCTET STRING (tag 4, length 32) of SHA-256(key authorization) (RFC 8737 section 3). Tagged `#NoDiscard`.
- **Acme** · [`export let AcmeClient = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L84)
  An ACME client bound to a directory URL and an account key thumbprint. Its flows are stubs. Tagged `#Final`.
- **Acme.AcmeClient** · [`export last directory_url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L86)
  The directory URL (for example a step-ca or Let's Encrypt directory).
- **Acme.AcmeClient** · [`export last account_thumbprint: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L88)
  The RFC 7638 thumbprint of the account key.
- **Acme.AcmeClient** · [`export let init = func(directory: string, thumbprint: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L91)
  A client for `directory` with the account key's thumbprint. Nothing is fetched here.
- **Acme.AcmeClient** · [`export let key_authorization_for = func(token: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L98)
  The key authorization for `token` with this client's account key. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let register_account = func(contacts: string[list], terms_agreed: bool) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L104)
  Registers or finds the account (RFC 8555 section 7.3); the account URL. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let new_order = func(domains: string[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L111)
  Places an order for DNS identifiers (section 7.4); the order URL. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let respond_to_challenge = func(challenge_url: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L118)
  Tells the server a challenge is ready (section 7.5.1). Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let finalize = func(order_url: string, csr_der: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L125)
  Finalizes an order with a DER certificate signing request (section 7.4). Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let download_certificate = func(certificate_url: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L132)
  Downloads the issued PEM certificate chain (section 7.4.2). Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L137)
  Text such as "AcmeClient(https://ca.example/directory)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
