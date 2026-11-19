# `Std.Crypto.Pqc.XWing`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.XWing` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **XWing** · [`export const PublicKeyLength = hybrid.PublicKeyLength`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L29)
  The public key, in bytes. Since 0.3.0.
- **XWing** · [`export const PrivateKeyLength = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L34)
  The private-key seed, in bytes. Since 0.3.0.
- **XWing** · [`export const CiphertextLength = hybrid.CiphertextLength`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L39)
  The ciphertext, in bytes. Since 0.3.0.
- **XWing** · [`export const SharedSecretLength = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L44)
  The shared secret, in bytes. Since 0.3.0.
- **XWing** · [`export let XWingPublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L49)
  A 1216-byte X-Wing public key containing the ML-KEM-768 encapsulation key followed by the X25519 share. Tagged `#Final`. Since 0.3.0.
- **XWing.XWingPublicKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L56)
  Validates and copies the public key. Invalid bytes trap. Use `parse_public_key` for untrusted input. Since 0.3.0.
- **XWing.XWingPublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L70)
  A copy of the encoded key. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWingPublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L75)
  Whether the other key has the same type and bytes. Since 0.3.0.
- **XWing.XWingPublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L85)
  A hash of the encoded key bytes. Since 0.3.0.
- **XWing.XWingPublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L96)
  The type and size, without the key contents. Since 0.3.0.
- **XWing** · [`export let XWingPrivateKey = class implements PrivateKey<XWing>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L102)
  A private key stored as X-Wing's 32-byte private-key encoding. Tagged `#Final`. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let init = func(seed: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L109)
  Derives the key from the 32-byte X-Wing seed. A wrong length traps. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L120)
  A copy of the private-key seed. Traps after wiping. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L125)
  Overwrites the stored seed and invalidates it. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L130)
  Whether the private-key seed has been invalidated. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L135)
  Requests the same storage lock as SecretBytes. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L140)
  A copy of the encoded public key. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let public_key = func -> XWingPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L145)
  The matching X-Wing public key. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWingPrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L150)
  A fixed placeholder for the private key. Since 0.3.0.
- **XWing** · [`export let XWing = class implements KeyEncapsulation<XWingPublicKey, XWingPrivateKey, SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L156)
  X-Wing key generation, encapsulation and decapsulation. Tagged `#Final`. Since 0.3.0.
- **XWing.XWing** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L164)
  Uses OS entropy or a caller's generator. Since 0.3.0.
- **XWing.XWing** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L175)
  The policy identifier. Since 0.3.0.
- **XWing.XWing** · [`export let key_pair_from_seed = func(seed: uint8[list]) -> XWingPrivateKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L187)
  Deterministic key generation from the 32-byte X-Wing seed. A wrong length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWing** · [`export let generate_key_pair = func -> Result<XWingPrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L192)
  Generates a private key, or reports an entropy failure. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWing** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<XWingPublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L202)
  Checks a received public key's length and ML-KEM modulus. Returns a Result error on failure. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWing** · [`export let encapsulate_with = func(public_key: XWingPublicKey, seed: uint8[list]) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L211)
  Deterministic encapsulation. The seed is the 32-byte ML-KEM input m followed by the 32-byte ephemeral X25519 scalar. Another length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWing** · [`export let try_encapsulate = func(public_key: XWingPublicKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L217)
  Encapsulates with fresh randomness, or reports an entropy failure. Tagged `#NoDiscard`. Since 0.3.0.
- **XWing.XWing** · [`export let encapsulate = func(public_key: XWingPublicKey) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L228)
  Encapsulates with fresh randomness. An entropy failure traps. Use `try_encapsulate` to handle that failure. Since 0.3.0.
- **XWing.XWing** · [`export let decapsulate = func(private_key: XWingPrivateKey, ciphertext: uint8[list]) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L242)
  Recovers the shared secret, or returns an error for a wrong-length ciphertext. Since 0.3.0.
- **XWing.XWing** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x-wing.nra#L257)
  A fixed placeholder for the encapsulation state. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
