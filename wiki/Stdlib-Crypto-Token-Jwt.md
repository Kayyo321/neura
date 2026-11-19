# `Std.Crypto.Token.Jwt`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Jwt` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwt.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Jwt** · [`export let validate_claims = func(payload: uint8[list], clock: Clock, leeway_seconds: int64) -> Result<[string, json.JsonValue][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwt.nra#L25)
  Returns the members of an authenticated JWT payload after evaluating its time claims. First authenticate the payload with the caller's chosen JWS or JWE API, then validate the claims here. The payload must be a UTF-8 JSON object with unique claim names. The optional exp, nbf, and iat claims must be JSON integers in the int64 range, in Unix seconds. This profile requires iat to be at or before the current time, with the same clock-skew allowance as nbf. The exp boundary is exclusive. The clock is read once when time claims are present and its errors propagate. Negative leeway traps. Application-specific requirements such as issuer, audience, or maximum token age use the returned members. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwt** · [`export let verify = func<Alg>(token: string, key: jwk.VerifierKey<Alg>, clock: Clock, leeway_seconds: int64) -> Result<[string, json.JsonValue][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwt.nra#L70)
  Verifies a compact signed JWT with the caller's key before validating its time claims. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
