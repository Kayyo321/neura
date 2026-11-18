# `Std.Crypto.Asymmetric.Ed25519`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.Ed25519` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ed25519** · [`export let Ed25519PublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L34)
  An Ed25519 public key: the 32-byte encoding of A. Equal keys have equal bytes. The point is checked when a signature is verified. Tagged `#Final`. Since 0.3.0.
- **Ed25519.Ed25519PublicKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L38)
  Copies the 32 bytes; another length traps (use `parse_public_key` for outside input).
- **Ed25519.Ed25519PublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L48)
  A copy of the 32 bytes. Tagged `#NoDiscard`.
- **Ed25519.Ed25519PublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L50)
- **Ed25519.Ed25519PublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L57)
- **Ed25519.Ed25519PublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L65)
- **Ed25519** · [`export let Ed25519PrivateKey = class implements PrivateKey<Ed25519Signer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L71)
  An Ed25519 private key: the 32-byte seed of RFC 8032 in secret memory, with its public key. `to_string` never prints it. Tagged `#Final`. Since 0.3.0.
- **Ed25519.Ed25519PrivateKey** · [`export let init = func(seed: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L76)
  Copies the 32-byte seed and computes the public key; another length traps.
- **Ed25519.Ed25519PrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L88)
- **Ed25519.Ed25519PrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L89)
- **Ed25519.Ed25519PrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L90)
- **Ed25519.Ed25519PrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L91)
- **Ed25519.Ed25519PrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L92)
- **Ed25519.Ed25519PrivateKey** · [`export let public_key = func -> Ed25519PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L96)
  The matching public key. Tagged `#NoDiscard`.
- **Ed25519.Ed25519PrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L99)
  A fixed placeholder, never the key.
- **Ed25519** · [`export let Ed25519Signer = class implements Signer<Ed25519PrivateKey, uint8[list]>, Verifier<Ed25519PublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L104)
  Signs with an Ed25519 private key; it also verifies against the matching public key. Tagged `#Final`. Since 0.3.0.
- **Ed25519.Ed25519Signer** · [`export let init = func(private_key: Ed25519PrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L109)
  A signer over the key.
- **Ed25519.Ed25519Signer** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L115)
  `Algorithm.Ed25519`.
- **Ed25519.Ed25519Signer** · [`export let public_key = func -> Ed25519PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L118)
  The public key.
- **Ed25519.Ed25519Signer** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L121)
  The 64-byte Ed25519 signature R || S of the message (RFC 8032, section 5.1.6).
- **Ed25519.Ed25519Signer** · [`export let sign_with_context = func(message: uint8[list], context: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L125)
  The Ed25519ctx signature under a context of 1 to 255 bytes; another length traps. Tagged `#NoDiscard`.
- **Ed25519.Ed25519Signer** · [`export let sign_prehashed = func(message: uint8[list], context: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L133)
  The Ed25519ph signature: the message is hashed with SHA-512 first. The context has at most 255 bytes; another length traps. Tagged `#NoDiscard`.
- **Ed25519.Ed25519Signer** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L139)
  Strict Ed25519 verification against the matching public key (see the module).
- **Ed25519.Ed25519Signer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L144)
- **Ed25519** · [`export let Ed25519Verifier = class implements Verifier<Ed25519PublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L150)
  Verifies Ed25519, Ed25519ctx and Ed25519ph signatures under a public key, strictly (see the module). Tagged `#Final`. Since 0.3.0.
- **Ed25519.Ed25519Verifier** · [`export let init = func(public_key: Ed25519PublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L154)
  A verifier for the key.
- **Ed25519.Ed25519Verifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L160)
  `Algorithm.Ed25519`.
- **Ed25519.Ed25519Verifier** · [`export let public_key = func -> Ed25519PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L163)
  The public key.
- **Ed25519.Ed25519Verifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L167)
  Whether the 64-byte signature is a valid Ed25519 signature of the message. Any other length is false.
- **Ed25519.Ed25519Verifier** · [`export let verify_with_context = func(message: uint8[list], signature: uint8[list], context: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L173)
  Whether the signature is a valid Ed25519ctx signature under the context (1 to 255 bytes; another length traps). Tagged `#NoDiscard`.
- **Ed25519.Ed25519Verifier** · [`export let verify_prehashed = func(message: uint8[list], signature: uint8[list], context: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L181)
  Whether the signature is a valid Ed25519ph signature (context of at most 255 bytes). Tagged `#NoDiscard`.
- **Ed25519.Ed25519Verifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L187)
- **Ed25519** · [`export let generate_key_pair = func -> Result<Ed25519PrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L289)
  A new private key from a 32-byte seed of `Entropy.os_bytes`, or from the given generator. Fails when the entropy source fails. Tagged `#NoDiscard`. Since 0.3.0.
- **Ed25519** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<Ed25519PublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ed25519.nra#L310)
  A public key from outside the program. A wrong length, or bytes that are not the canonical encoding of a point, is `Custom` code `crypto.invalid_key`. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
