# `Std.Crypto.Advanced.RsaBlindSignature`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.RsaBlindSignature` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RsaBlindSignature** · [`export let Variant = enum (PssRandomized, PssZeroRandomized, PssDeterministic, PssZeroDeterministic)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L31)
  Randomized or identity preparation, each with a 48-byte or zero-byte salt. Since 0.3.0.
- **RsaBlindSignature** · [`export let BlindRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L42)
  A blinded request and its private, one-use unblinding state. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **RsaBlindSignature.BlindRequest** · [`export let init = func(message: uint8[list], factor_inverse: uint8[list], issuer: rsa.RsaPublicKey, variant: Variant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L49)
- **RsaBlindSignature.BlindRequest** · [`export let message = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L60)
  A copy of the modulus-width message to send to the signer. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.BlindRequest** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L65)
  Whether the private inverse has been wiped or consumed. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.BlindRequest** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L70)
  Wipes the storage holding the inverse. Since 0.3.0.
- **RsaBlindSignature.BlindRequest** · [`export let matches = func(issuer: rsa.RsaPublicKey, variant: Variant) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L73)
- **RsaBlindSignature.BlindRequest** · [`export let consume = func(limbs: int) -> uint32[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L77)
- **RsaBlindSignature.BlindRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L86)
  A fixed placeholder for the request and inverse. Since 0.3.0.
- **RsaBlindSignature** · [`export let Client = class implements Verifier<rsa.RsaPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L92)
  The client and public verifier for one key and variant. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let init = func(server_key: rsa.RsaPublicKey, variant: Variant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L100)
  Uses the server's public key and the application's configured variant. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L110)
  `Algorithm.RsaBlindSignature`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let public_key = func -> rsa.RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L115)
  The configured server public key. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let prepare = func(message: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L121)
  Prepares with an OS-generated 32-byte prefix for randomized variants. Identity variants copy the input and require no entropy. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let prepare_with_drbg = func(message: uint8[list], generator: drbg.ChaCha20Drbg) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L132)
  Prepares using a caller-supplied generator if this variant needs a prefix. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let prepare_with_prefix = func(message: uint8[list], prefix: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L143)
  Prepares using exactly 32 supplied prefix bytes for randomized variants or an empty prefix for identity variants. Another prefix length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let blind = func(prepared: uint8[list]) -> Result<BlindRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L153)
  Uses OS entropy for the PSS salt (if any) and uniform RSA blinding factor. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let blind_with_drbg = func(prepared: uint8[list], generator: drbg.ChaCha20Drbg) -> Result<BlindRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L159)
  Uses a caller-supplied generator for the PSS salt and blinding factor. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let blind_with_factor = func(prepared: uint8[list], salt: uint8[list], factor: uint8[list]) -> Result<BlindRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L168)
  Deterministic input for known answers. The salt must match this variant and the factor must have the modulus's byte width. Invalid scalar values return crypto.blinding. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let finalize = func(prepared: uint8[list], response: uint8[list], request: BlindRequest) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L244)
  Consumes the inverse and verifies the unblinded signature with RSA-PSS. An invalid response, mismatched request or signature failure returns `crypto.invalid_signature`. Using a wiped request traps with invalid state. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L273)
  Verifies a finalized signature over the prepared message with this variant's exact salt length. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.Client** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L281)
  The verifier's type. Since 0.3.0.
- **RsaBlindSignature** · [`export let BlindSigner = class implements Signer<rsa.RsaPrivateKey, Result<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L287)
  The server's signer under a dedicated RSA private key. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export last variant: Variant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L293)
  The only variant for which the application may use this key. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export let init = func(private_key: rsa.RsaPrivateKey, suite: Variant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L298)
  Retains a private key dedicated to this variant and protocol. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L307)
  `Algorithm.RsaBlindSignature`. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export let sign = func(message: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L312)
  Signs a modulus-width blinded request. The server's independent blinding uses OS entropy. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export let sign_with_drbg = func(message: uint8[list], generator: drbg.ChaCha20Drbg) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L318)
  Signs with a caller-supplied generator for the server's independent blinding. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export let sign_with_factor = func(message: uint8[list], factor: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L326)
  Signs with explicit server-side blinding input for tests. The factor's byte width must equal the modulus's or the call traps. Tagged `#NoDiscard`. Since 0.3.0.
- **RsaBlindSignature.BlindSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/rsa-blind-signature.nra#L353)
  A fixed placeholder for the signing key. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
