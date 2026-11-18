# `Std.Crypto.Pqc.X25519MlKem768`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pqc.X25519MlKem768` to use this module. The [package specification](Stdlib-Design#219-stdcryptopqc) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **X25519MlKem768** · [`export const PublicKeyLength = hybrid.PublicKeyLength`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L30)
  The public key, in bytes. Since 0.3.0.
- **X25519MlKem768** · [`export const PrivateKeyLength = 96`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L35)
  The private-key seed, in bytes. Since 0.3.0.
- **X25519MlKem768** · [`export const CiphertextLength = hybrid.CiphertextLength`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L40)
  The ciphertext, in bytes. Since 0.3.0.
- **X25519MlKem768** · [`export const SharedSecretLength = 64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L45)
  The shared secret, in bytes. Since 0.3.0.
- **X25519MlKem768** · [`export let X25519MlKem768PublicKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L50)
  A 1216-byte X25519MLKEM768 public key containing the ML-KEM-768 encapsulation key followed by the X25519 share. Tagged `#Final`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PublicKey** · [`export let init = func(bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L57)
  Validates and copies the public key. Invalid bytes trap. Use `parse_public_key` for untrusted input. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PublicKey** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L71)
  A copy of the encoded key. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PublicKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L76)
  Whether the other key has the same type and bytes. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PublicKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L86)
  A hash of the encoded key bytes. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PublicKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L97)
  The type and size, without the key contents. Since 0.3.0.
- **X25519MlKem768** · [`export let X25519MlKem768PrivateKey = class implements PrivateKey<X25519MlKem768>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L103)
  A private key stored as a 96-byte Neura seed. TLS uses no serialized private-key format. Tagged `#Final`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let init = func(seed: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L110)
  Derives the key from the 96-byte Neura seed `d || z || x`. A wrong length traps. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L121)
  A copy of the private-key seed. Traps after wiping. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L126)
  Overwrites the stored seed and invalidates it. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L131)
  Whether the private-key seed has been invalidated. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L136)
  Requests the same storage lock as SecretBytes. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let public_key_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L141)
  A copy of the encoded public key. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let public_key = func -> X25519MlKem768PublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L146)
  The matching X25519MLKEM768 public key. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768PrivateKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L151)
  A fixed placeholder for the private key. Since 0.3.0.
- **X25519MlKem768** · [`export let X25519MlKem768 = class implements KeyEncapsulation<X25519MlKem768PublicKey, X25519MlKem768PrivateKey, SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L157)
  X25519MLKEM768 key generation, encapsulation and decapsulation. Tagged `#Final`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L165)
  Uses OS entropy or a caller's generator. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L176)
  The policy identifier. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let key_pair_from_seed = func(seed: uint8[list]) -> X25519MlKem768PrivateKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L188)
  Deterministic key generation from the 96-byte Neura seed `d || z || x`. A wrong length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let generate_key_pair = func -> Result<X25519MlKem768PrivateKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L193)
  Generates a private key, or reports an entropy failure. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let parse_public_key = func(bytes: uint8[list]) -> Result<X25519MlKem768PublicKey>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L203)
  Checks a received public key's length and ML-KEM modulus. Also rejects low-order X25519 shares. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let encapsulate_with = func(public_key: X25519MlKem768PublicKey, seed: uint8[list]) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L212)
  Deterministic encapsulation. The seed is the 32-byte ML-KEM input m followed by the 32-byte ephemeral X25519 scalar. Another length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let try_encapsulate = func(public_key: X25519MlKem768PublicKey) -> Result<Encapsulation<SecretBytes, uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L218)
  Encapsulates with fresh randomness, or reports an entropy failure. Tagged `#NoDiscard`. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let encapsulate = func(public_key: X25519MlKem768PublicKey) -> Encapsulation<SecretBytes, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L229)
  Encapsulates with fresh randomness. An entropy failure traps. Use `try_encapsulate` to handle that failure. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let decapsulate = func(private_key: X25519MlKem768PrivateKey, ciphertext: uint8[list]) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L244)
  Recovers the shared secret, or returns an error for a wrong-length ciphertext. A low-order X25519 share also fails. Since 0.3.0.
- **X25519MlKem768.X25519MlKem768** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pqc/x25519-ml-kem768.nra#L259)
  The scheme name. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
