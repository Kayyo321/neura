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

- **Acme** · [`export let ChallengeType = enum (Http01, Dns01, TlsAlpn01)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L42)
  The challenge types of RFC 8555 section 8 and RFC 8737.
- **Acme** · [`export const AcmeTlsAlpn: string = "acme-tls/1"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L45)
  The ALPN protocol of TLS-ALPN-01 (RFC 8737 section 6.2).
- **Acme** · [`export let type_name = func(challenge: ChallengeType) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L49)
  The wire name of a challenge type: "http-01", "dns-01" or "tls-alpn-01". Tagged `#Pure`.
- **Acme** · [`export let rsa_thumbprint = func(modulus: string, exponent: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L55)
  The RFC 7638 thumbprint of an RSA public key given as the base64url `n` and `e` members: base64url(SHA-256 of `{"e":..,"kty":"RSA","n":..}`). Tagged `#NoDiscard`.
- **Acme** · [`export let ec_thumbprint = func(curve: string, x: string, y: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L61)
  The RFC 7638 thumbprint of an EC public key given as `crv` and the base64url `x` and `y` members: base64url(SHA-256 of `{"crv":..,"kty":"EC","x":..,"y":..}`). Tagged `#NoDiscard`.
- **Acme** · [`export let key_authorization = func(token: string, thumbprint: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L66)
  The key authorization `token.thumbprint` (RFC 8555 section 8.1). Tagged `#NoDiscard`.
- **Acme** · [`export let http01_path = func(token: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L74)
  The HTTP-01 resource path `/.well-known/acme-challenge/<token>` (RFC 8555 section 8.3); the response body is the key authorization. Tagged `#NoDiscard`.
- **Acme** · [`export let dns01_record_name = func(domain: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L81)
  The DNS-01 record name `_acme-challenge.<domain>` (RFC 8555 section 8.4). A trailing dot is kept. Tagged `#Pure`.
- **Acme** · [`export let dns01_txt_value = func(key_authorization: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L85)
  The DNS-01 TXT value: base64url(SHA-256(key authorization)) (RFC 8555 section 8.4). Tagged `#NoDiscard`.
- **Acme** · [`export let tls_alpn01_extension_value = func(key_authorization: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L90)
  The DER `extnValue` of the TLS-ALPN-01 `acmeIdentifier` extension: OCTET STRING (tag 4, length 32) of SHA-256(key authorization) (RFC 8737 section 3). Tagged `#NoDiscard`.
- **Acme** · [`export let HttpsTransport = class implements http.Transport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L103)
  A verified, non-redirecting HTTP/1.1 transport for ACME. An empty CA file uses system trust. Tagged `#Final`. Since 0.3.1.
- **Acme.HttpsTransport** · [`export let init = func(ca_file: string = "")`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L106)
- **Acme.HttpsTransport** · [`export let fetch = func(request: http.ClientRequest) -> Result<http.Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L110)
- **Acme** · [`export let AccountSigner = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L245)
  An asymmetric account signing key. `sign` returns raw JOSE signature bytes for its algorithm. Tagged `#Final`. Since 0.3.1.
- **Acme.AccountSigner** · [`export last algorithm: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L246)
- **Acme.AccountSigner** · [`export last public_jwk: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L247)
- **Acme.AccountSigner** · [`export last sign: func(uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L248)
- **Acme.AccountSigner** · [`export let init = func(alg: string, jwk: string, sign_input: func(uint8[list]) -> Result<uint8[list]>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L250)
- **Acme** · [`export let AcmeClient = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L259)
  An ACME client bound to one HTTPS directory, verified transport and signing identity. Tagged `#Final`, `#NotThreadSafe`.
- **Acme.AcmeClient** · [`export last directory_url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L261)
  The ACME directory URL.
- **Acme.AcmeClient** · [`export last account_thumbprint: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L263)
  The RFC 7638 thumbprint of the account key.
- **Acme.AcmeClient** · [`export let init = func(directory: string, thumbprint: string, https: http.Transport = nil, signing_key: AccountSigner = nil, account: string = "")`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L274)
  A client for `directory`. A thumbprint-only client can compute challenges. The network methods also need an account signer. Set `account` to resume a prior account.
- **Acme.AcmeClient** · [`export let key_authorization_for = func(token: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L285)
  The key authorization for `token` with this client's account key. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let register_account = func(contacts: string[list], terms_agreed: bool) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L290)
  Registers or finds the account (RFC 8555 section 7.3) and remembers and returns its URL. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let new_order = func(domains: string[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L305)
  Places an order for DNS identifiers (section 7.4) and returns the order URL. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let fetch_resource = func(url: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L326)
  Fetches an account-owned ACME resource using POST-as-GET. Use this for order state and authorizations. Tagged `#NoDiscard`. Since 0.3.1.
- **Acme.AcmeClient** · [`export let respond_to_challenge = func(challenge_url: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L330)
  Tells the server a challenge is ready (section 7.5.1) and returns the resource body. Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let finalize = func(order_url: string, csr_der: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L335)
  Reads the order's finalize URL and submits a DER certificate signing request (section 7.4). Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let download_certificate = func(certificate_url: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L353)
  Downloads the issued PEM certificate chain using POST-as-GET (section 7.4.2). Tagged `#NoDiscard`.
- **Acme.AcmeClient** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/acme.nra#L367)
  Text such as "AcmeClient(https://ca.example/directory)".
<!-- END SOURCE API -->

## Detailed contracts

Set an account signer to use the ACME directory, account, order, challenge, finalization and download calls. The default transport verifies HTTPS against system trust. Pass `HttpsTransport(ca_file)` for a private CA, or supply a transport that verifies peers and rejects redirects. Resource URLs stay on the directory origin. The client signs JWS requests, retries one `badNonce` response and leaves challenge provisioning and status polling to the caller.

See [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) and the source comments for the complete module contract.
