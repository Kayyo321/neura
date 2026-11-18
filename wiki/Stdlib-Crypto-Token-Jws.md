# `Std.Crypto.Token.Jws`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Jws` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Jws** · [`export let verify = func<Alg>(token: string, key: jwk.VerifierKey<Alg>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L48)
  Verifies a compact JWS and returns the authenticated payload. The key sets the only allowed algorithm. Unknown critical extensions, unsecured tokens, duplicate header members and noncanonical base64url fail. Header-supplied keys and key locators do not grant trust. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_ed25519 = func(payload: uint8[list], key: ed25519.Ed25519PrivateKey) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L64)
  Signs a compact JWS with the caller's Ed25519 private key and a standard EdDSA protected header. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_hmac = func(payload: uint8[list], key: jwk.HmacSigningKey, protected: string = "") -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L89)
  Signs a compact JWS using the HMAC algorithm selected by the signing key. An empty protected argument selects the default algorithm header. Other protected JSON is validated and used byte for byte. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_rsa = func(payload: uint8[list], key: rsa.RsaPrivateKey, algorithm: jwk.RsaAlgorithm, protected: string = "") -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L129)
  Signs under the selected RSA JOSE algorithm using independent OS entropy for RSA blinding. PS algorithms also request a fresh salt as long as their digest. Entropy failures are returned. A wiped key or a key shorter than 2048 bits traps before entropy is requested. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_rsa_with_factor = func(payload: uint8[list], key: rsa.RsaPrivateKey, algorithm: jwk.RsaAlgorithm, factor: uint8[list], protected: string = "") -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L148)
  Signs for RS256, RS384, or RS512 using a caller-supplied modulus-width RSA blinding factor. Use fresh, independent cryptographic randomness for the factor in production. A PS algorithm or a wrong factor length traps. An unusable factor returns an error. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_rsa_with_salt = func(payload: uint8[list], key: rsa.RsaPrivateKey, algorithm: jwk.RsaAlgorithm, salt: uint8[list], factor: uint8[list], protected: string = "") -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L163)
  Signs for PS256, PS384, or PS512 with a caller-supplied salt and modulus-width RSA blinding factor. In production, draw the salt and factor independently from fresh cryptographic randomness. The salt must have the selected digest's byte length. Wrong algorithms or lengths trap. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_ec = func(payload: uint8[list], key: ecdsa.EcPrivateKey, protected: string = "") -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L178)
  Signs for ES256, ES384, or ES512 as selected by the private key's NIST curve. The signature uses the fixed-width JOSE r || s representation. Another curve returns an error. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_ed448 = func(payload: uint8[list], key: ed448.Ed448PrivateKey, protected: string = "") -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L191)
  Signs a compact JWS with the fully specified Ed448 algorithm of RFC 9864. An empty protected argument selects the default Ed448 header. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
