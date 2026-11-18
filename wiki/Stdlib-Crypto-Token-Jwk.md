# `Std.Crypto.Token.Jwk`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Jwk` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Jwk** · [`export let VerifierKey = interface<Alg>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L28)
  Verification keys carry an expected JOSE algorithm chosen by the caller. Tagged `#Sealed`. Since 0.3.0.
- **Jwk** · [`export let HmacAlgorithm = enum (Hs256, Hs384, Hs512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L43)
  The HMAC SHA-2 choices for JWS. Since 0.3.0.
- **Jwk** · [`export let RsaAlgorithm = enum (Rs256, Rs384, Rs512, Ps256, Ps384, Ps512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L48)
  The RSA verification choices for JWS. PSS uses an MGF1 digest and a salt the size of the hash. Since 0.3.0.
- **Jwk** · [`export let HmacKey = class implements VerifierKey<HmacKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L131)
  An HMAC JWS verification key. The minimum key length is the length of the hash output. Tagged `#Final`. Since 0.3.0.
- **Jwk.HmacKey** · [`export let init = func(key: uint8[list], alg: HmacAlgorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L138)
  Copies a key with a caller-selected HMAC algorithm. Since 0.3.0.
- **Jwk.HmacKey** · [`export let jose_algorithm = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L147)
  The configured HMAC algorithm's JOSE name. Since 0.3.0.
- **Jwk.HmacKey** · [`export let verify_signature = func(input: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L152)
  Checks a full-length HMAC. Comparison is branch-free in source; the runtime does not guarantee constant time. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk.HmacKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L164)
  Overwrites the bytes held by this key. Since 0.3.0.
- **Jwk.HmacKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L169)
  Fixed redacted display. Since 0.3.0.
- **Jwk** · [`export let HmacSigningKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L175)
  An octet signing key restricted to one HMAC SHA-2 algorithm in JWS. Tagged `#Final`. Since 0.3.0.
- **Jwk.HmacSigningKey** · [`export let init = func(key: uint8[list], alg: HmacAlgorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L182)
  Copies key material for signing. It must be at least as long as the configured hash output. Since 0.3.0.
- **Jwk.HmacSigningKey** · [`export let jose_algorithm = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L191)
  The configured JWS algorithm name. Since 0.3.0.
- **Jwk.HmacSigningKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L206)
  Overwrites the key held by this instance. Since 0.3.0.
- **Jwk.HmacSigningKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L211)
  A fixed redacted display. Since 0.3.0.
- **Jwk** · [`export let RsaKey = class implements VerifierKey<RsaKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L217)
  An RSA public key restricted to one JOSE signature algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwk.RsaKey** · [`export let init = func(key: rsa.RsaPublicKey, alg: RsaAlgorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L224)
  Restricts a public RSA key to a particular JOSE algorithm. The modulus must have at least 2048 bits. Since 0.3.0.
- **Jwk.RsaKey** · [`export let jose_algorithm = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L237)
  The configured RSA algorithm's JOSE name. Since 0.3.0.
- **Jwk.RsaKey** · [`export let verify_signature = func(input: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L242)
  Checks an RSA signature with the configured algorithm. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk.RsaKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L253)
  Equality by public key and JOSE algorithm. Since 0.3.0.
- **Jwk.RsaKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L263)
  Hash of the public key and JOSE algorithm. Since 0.3.0.
- **Jwk.RsaKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L268)
  Displays the JOSE algorithm and modulus size. Since 0.3.0.
- **Jwk** · [`export let EcKey = class implements VerifierKey<EcKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L275)
  An EC verification key bound to the JWS algorithm of its curve. Tagged `#Final`. Since 0.3.0.
- **Jwk.EcKey** · [`export let init = func(key: ecdsa.EcPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L281)
  Binds a checked EC public key to ES256, ES384, ES512, or ES256K. Since 0.3.0.
- **Jwk.EcKey** · [`export let jose_algorithm = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L290)
  The JOSE algorithm name for this key's curve. Since 0.3.0.
- **Jwk.EcKey** · [`export let verify_signature = func(input: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L295)
  Checks the JWS fixed-width ECDSA signature r || s. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk.EcKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L301)
  Equality by curve and public key. Since 0.3.0.
- **Jwk.EcKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L307)
  Hash of the public key. Since 0.3.0.
- **Jwk.EcKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L312)
  Displays the JOSE algorithm name. Since 0.3.0.
- **Jwk** · [`export let Ed25519Key = class implements VerifierKey<Ed25519Key>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L318)
  An Ed25519 verification key for EdDSA in JWS (RFC 8037). Tagged `#Final`. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let init = func(key: ed25519.Ed25519PublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L324)
  Binds a checked Ed25519 public key to the EdDSA header identifier. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let jose_algorithm = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L332)
  The EdDSA header identifier of RFC 8037. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let verify_signature = func(input: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L337)
  Checks the Ed25519 signature over the JWS signing input. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let thumbprint = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L343)
  RFC 7638 SHA-256 thumbprint of the public members. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L351)
  Equality by public key. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L357)
  Hash of the public key. Since 0.3.0.
- **Jwk.Ed25519Key** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L362)
  The key type's name. Since 0.3.0.
- **Jwk** · [`export let Ed448Key = class implements VerifierKey<Ed448Key>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L368)
  A JWS verification key for the fully specified Ed448 algorithm (RFC 9864). Tagged `#Final`. Since 0.3.0.
- **Jwk.Ed448Key** · [`export let init = func(key: ed448.Ed448PublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L374)
  Binds a checked Ed448 public key to the Ed448 header identifier. Since 0.3.0.
- **Jwk.Ed448Key** · [`export let jose_algorithm = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L382)
  The Ed448 header identifier. Since 0.3.0.
- **Jwk.Ed448Key** · [`export let verify_signature = func(input: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L387)
  Checks a pure Ed448 signature with the empty JOSE context. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk.Ed448Key** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L393)
  Equality by public key. Since 0.3.0.
- **Jwk.Ed448Key** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L399)
  Hash of the public key. Since 0.3.0.
- **Jwk.Ed448Key** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L404)
  The key type's name. Since 0.3.0.
- **Jwk** · [`export let parse_hmac = func(text: string, alg: HmacAlgorithm) -> Result<HmacKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L478)
  Imports an oct JWK of sufficient length for the caller-selected HMAC algorithm. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk** · [`export let parse_hmac_signing = func(text: string, alg: HmacAlgorithm) -> Result<HmacSigningKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L488)
  Imports an oct JWK for signing. A present key_ops must include sign and contain only signature operations. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk** · [`export let parse_rsa = func(text: string, alg: RsaAlgorithm) -> Result<RsaKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L498)
  Imports an RSA JWK with a modulus of at least 2048 bits and binds it to the given JOSE algorithm. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk** · [`export let parse_ec = func(text: string, curve: ecdsa.Curve) -> Result<EcKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L520)
  Imports a checked EC JWK for the given curve and its JOSE signature algorithm. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk** · [`export let parse_ed25519 = func(text: string) -> Result<Ed25519Key>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L543)
  Imports an OKP JWK whose curve is Ed25519 and whose algorithm is EdDSA. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwk** · [`export let parse_ed448 = func(text: string) -> Result<Ed448Key>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwk.nra#L556)
  Imports an OKP JWK for the fully specified Ed448 algorithm. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
