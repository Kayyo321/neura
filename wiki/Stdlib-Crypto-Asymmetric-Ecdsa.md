# `Std.Crypto.Asymmetric.Ecdsa`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.Ecdsa` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ecdsa** · [`export let Curve = enum (P256, P384, P521, Secp256k1)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L37)
  The curves. Since 0.3.0.
- **Ecdsa** · [`export let algorithm_of = func(curve: Curve) -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L41)
  The policy identifier of a curve: `P256`, `P384`, `P521` or `Secp256k1`. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let default_hash = func(curve: Curve) -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L50)
  The default hash of a curve: SHA-256, SHA-384, SHA-512 or SHA-256. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let scalar_length = func(curve: Curve) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L59)
  The byte length of a scalar or a field element: 32, 48, 66 or 32. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let group = func(curve: Curve) -> weierstrass.Curve`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L67)
  The internal group of a curve, built for one operation. Tagged `#NoDiscard`.
- **Ecdsa** · [`export let EcPublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L84)
  An elliptic-curve public key: a point of the curve, held as its uncompressed SEC1 encoding. Equal keys have equal curves and points. Tagged `#Final`. Since 0.3.0.
- **Ecdsa.EcPublicKey** · [`export last curve: Curve`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L86)
  The curve.
- **Ecdsa.EcPublicKey** · [`export let init = func(key_curve: Curve, sec1: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L91)
  The key of a SEC1 point, compressed or not. An encoding that is not a point of the curve traps (use `parse_public_key` for outside input).
- **Ecdsa.EcPublicKey** · [`export let to_sec1 = func(compressed: bool) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L107)
  The SEC1 encoding: 0x04 || x || y, or 0x02 / 0x03 || x when compressed. Tagged `#NoDiscard`.
- **Ecdsa.EcPublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L117)
- **Ecdsa.EcPublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L125)
- **Ecdsa.EcPublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L133)
- **Ecdsa** · [`export let EcPrivateKey = class implements PrivateKey<EcdsaSigner>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L139)
  An elliptic-curve private key: the scalar d (1 <= d < n) as big-endian bytes of the curve's scalar length in secret memory, with its public key. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **Ecdsa.EcPrivateKey** · [`export last curve: Curve`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L141)
  The curve.
- **Ecdsa.EcPrivateKey** · [`export let init = func(key_curve: Curve, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L147)
  The key of the scalar's big-endian bytes (exactly the curve's scalar length). A scalar outside 1 to n - 1 or a wrong length traps (use `parse_private_key` for outside input).
- **Ecdsa.EcPrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L166)
- **Ecdsa.EcPrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L167)
- **Ecdsa.EcPrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L168)
- **Ecdsa.EcPrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L169)
- **Ecdsa.EcPrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L170)
- **Ecdsa.EcPrivateKey** · [`export let public_key = func -> EcPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L174)
  The matching public key. Tagged `#NoDiscard`.
- **Ecdsa.EcPrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L177)
  A fixed placeholder, never the key.
- **Ecdsa** · [`export let EcdsaSigner = class implements Signer<EcPrivateKey, uint8[list]>, Verifier<EcPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L182)
  Signs with RFC 6979 deterministic ECDSA; it also verifies against the matching public key. Tagged `#Final`. Since 0.3.0.
- **Ecdsa.EcdsaSigner** · [`export last hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L185)
  The hash of the message and of HMAC_DRBG.
- **Ecdsa.EcdsaSigner** · [`export last low_s: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L187)
  Whether s is normalized to at most (n - 1) / 2.
- **Ecdsa.EcdsaSigner** · [`export let init = func(private_key: EcPrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L191)
  A signer with the curve's default hash, or the given SHA-2 or SHA-3 hash, and optionally low-S normalization. Another hash traps.
- **Ecdsa.EcdsaSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L215)
  The curve's identifier.
- **Ecdsa.EcdsaSigner** · [`export let public_key = func -> EcPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L218)
  The public key.
- **Ecdsa.EcdsaSigner** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L221)
  The DER signature of the message.
- **Ecdsa.EcdsaSigner** · [`export let sign_raw = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L226)
  The r || s signature of the message, each the curve's scalar length. Tagged `#NoDiscard`.
- **Ecdsa.EcdsaSigner** · [`export let sign_digest = func(digest: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L232)
  The DER signature of a digest computed elsewhere (of any length; RFC 6979's bits2int takes its leftmost bits). The nonce still uses this signer's hash. Tagged `#NoDiscard`.
- **Ecdsa.EcdsaSigner** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L236)
  Whether the DER signature is valid for the message under the matching public key.
- **Ecdsa.EcdsaSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L239)
- **Ecdsa** · [`export let EcdsaVerifier = class implements Verifier<EcPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L244)
  Verifies ECDSA signatures under a public key. Tagged `#Final`. Since 0.3.0.
- **Ecdsa.EcdsaVerifier** · [`export last hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L247)
  The hash of the message.
- **Ecdsa.EcdsaVerifier** · [`export last require_low_s: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L249)
  Whether s above (n - 1) / 2 is refused.
- **Ecdsa.EcdsaVerifier** · [`export let init = func(public_key: EcPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L252)
  A verifier with the curve's default hash, or the given hash, and optionally the low-S rule.
- **Ecdsa.EcdsaVerifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L276)
  The curve's identifier.
- **Ecdsa.EcdsaVerifier** · [`export let public_key = func -> EcPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L279)
  The public key.
- **Ecdsa.EcdsaVerifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L282)
  Whether the DER signature is valid for the message. Malformed DER is false.
- **Ecdsa.EcdsaVerifier** · [`export let verify_raw = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L287)
  Whether the r || s signature is valid for the message. A wrong length is false. Tagged `#NoDiscard`.
- **Ecdsa.EcdsaVerifier** · [`export let verify_digest = func(digest: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L295)
  Whether the DER signature is valid for a digest computed elsewhere. Tagged `#NoDiscard`.
- **Ecdsa.EcdsaVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L301)
- **Ecdsa** · [`export let der_to_raw = func(curve: Curve, der: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L533)
  The r || s form of a DER signature. Malformed DER or values outside 1 to n - 1 are `Custom` code `crypto.invalid_signature`. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let raw_to_der = func(curve: Curve, raw: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L544)
  The DER form of an r || s signature. A wrong length or values outside 1 to n - 1 are `Custom` code `crypto.invalid_signature`. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let parse_public_key = func(curve: Curve, sec1: uint8[list]) -> Result<EcPublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L555)
  A public key from a SEC1 point. A point that is not on the curve, or a malformed encoding, is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let parse_private_key = func(curve: Curve, bytes: uint8[list]) -> Result<EcPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L566)
  A private key from big-endian scalar bytes. A wrong length or a scalar outside 1 to n - 1 is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
- **Ecdsa** · [`export let generate_key_pair = func(curve: Curve) -> Result<EcPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdsa.nra#L583)
  A new private key by rejection sampling (FIPS 186-5, appendix A.4.2): scalar-length bytes, with the bits above n's length cleared, until the value is from 1 to n - 1. The bytes come from `Entropy.os_bytes` or from the given generator; an entropy failure is returned. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
