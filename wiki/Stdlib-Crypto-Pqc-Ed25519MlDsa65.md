# `Std.Crypto.Pqc.Ed25519MlDsa65`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.Ed25519MlDsa65` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ed25519MlDsa65** · [`export let CompositePublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L33)
  The composite public key, encoded as the ML-DSA-65 key followed by the Ed25519 key. Tagged `#Final`. Since 0.3.0.
- **Ed25519MlDsa65.CompositePublicKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L39)
  Copies the 1984 public key bytes. A different length traps. Since 0.3.0.
- **Ed25519MlDsa65.CompositePublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L50)
  A copy of the public key bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositePublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L55)
  Compares the encoded public keys by value. Since 0.3.0.
- **Ed25519MlDsa65.CompositePublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L71)
  Hashes the encoded public key. Since 0.3.0.
- **Ed25519MlDsa65.CompositePublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L82)
  The composite public key type's name. Since 0.3.0.
- **Ed25519MlDsa65** · [`export let CompositePrivateKey = class implements PrivateKey<CompositePrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L88)
  Two 32-byte private key seeds, ML-DSA-65 first, held with explicit wipe support. Tagged `#Final`. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L95)
  Imports the 64-byte private key and derives each public key. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L116)
  Returns a copy of both seeds, with the post-quantum seed first. A wiped key traps. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L121)
  Overwrites the retained seeds. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L126)
  Whether the private key was wiped. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L131)
  Requests the same page-locking service as SecretBytes. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L136)
  A copy of the encoded public key. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let public_key = func -> CompositePublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L141)
  The associated composite public key. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositePrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L146)
  Fixed redaction of private material. Since 0.3.0.
- **Ed25519MlDsa65** · [`export let CompositeSigner = class implements Signer<CompositePrivateKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L152)
  A composite signer. The interface signs with an empty application context and hedged ML-DSA. Tagged `#Final`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let init = func(private_key: CompositePrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L159)
  Uses OS entropy or the caller's DRBG for ML-DSA randomness. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L172)
  The composite algorithm identifier used by CryptoPolicy. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let sign_with = func(message: uint8[list], context: uint8[list], rnd: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L178)
  Signs with an application context of up to 255 bytes and explicit 32-byte ML-DSA randomness. The returned signature is the 3309-byte ML-DSA-65 signature and then the 64-byte Ed25519 signature. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let sign_deterministic = func(message: uint8[list], context: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L203)
  Signs with all-zero ML-DSA randomness, the deterministic variant. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let try_sign = func(message: uint8[list], context: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L209)
  Signs with the configured entropy source. Entropy failures propagate as Result errors. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L222)
  Signs with an empty context. A denied entropy request traps with an unavailable error. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L237)
  Fixed redaction of the signing key. Since 0.3.0.
- **Ed25519MlDsa65** · [`export let CompositeVerifier = class implements Verifier<CompositePublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L243)
  A verifier which requires both component signatures to verify under the corresponding keys. Tagged `#Final`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeVerifier** · [`export let init = func(public: CompositePublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L249)
  Verifies under the supplied composite public key. Since 0.3.0.
- **Ed25519MlDsa65.CompositeVerifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L257)
  The composite algorithm identifier used by CryptoPolicy. Since 0.3.0.
- **Ed25519MlDsa65.CompositeVerifier** · [`export let public_key = func -> CompositePublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L262)
  The composite public key. Since 0.3.0.
- **Ed25519MlDsa65.CompositeVerifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L267)
  Verifies with the empty application context. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeVerifier** · [`export let verify_with_context = func(message: uint8[list], signature: uint8[list], context: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L273)
  Verifies the message and context. A malformed signature or oversized context is false. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65.CompositeVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L279)
  The verifier class name. Since 0.3.0.
- **Ed25519MlDsa65** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<CompositePublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L285)
  Imports the public key's 1984-byte encoding, including a canonical Ed25519 public point. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519MlDsa65** · [`export let generate_key_pair = func -> Result<CompositePrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/ed25519-ml-dsa65.nra#L298)
  Generates both seeds from OS entropy or a caller's DRBG. Entropy failures propagate. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
