# `Std.Crypto.Token.Jws`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Jws` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Jws** · [`export let verify = func<Alg>(token: string, key: jwk.VerifierKey<Alg>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L38)
  Verifies a compact JWS and returns the authenticated payload. The key sets the only allowed algorithm. Unknown critical extensions, unsecured tokens, duplicate header members and noncanonical base64url fail. Header-supplied keys and key locators do not grant trust. Tagged `#NoDiscard`. Since 0.3.0.
- **Jws** · [`export let sign_ed25519 = func(payload: uint8[list], key: ed25519.Ed25519PrivateKey) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jws.nra#L54)
  Signs a compact JWS with the caller's Ed25519 private key and a standard EdDSA protected header. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
