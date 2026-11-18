# `Std.Crypto.Token.Jwe`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Token.Jwe` to use this module. The [package specification](Stdlib-Design#2111-stdcryptotoken) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Jwe** · [`export let ContentEncryption = enum (A128Gcm, A192Gcm, A256Gcm, A128CbcHs256, A192CbcHs384, A256CbcHs512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L37)
  The content encryption algorithm bound to a JWE key. Since 0.3.0.
- **Jwe** · [`export let WrapAlgorithm = enum (A128Kw, A192Kw, A256Kw)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L42)
  The AES Key Wrap algorithm bound to a JWE wrapping key. Since 0.3.0.
- **Jwe** · [`export let DirectKey = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L116)
  A direct JWE content encryption key. The only permitted key management algorithm is dir. Tagged `#Final`. Since 0.3.0.
- **Jwe.DirectKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L120)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.DirectKey** · [`export let init = func(enc: ContentEncryption, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L126)
  Copies the key. Its length must match the specified encryption algorithm. Since 0.3.0.
- **Jwe.DirectKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L138)
  A copy of the key. Exposing a wiped key traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.DirectKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L143)
  Overwrites and invalidates the stored key. Since 0.3.0.
- **Jwe.DirectKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L148)
  Whether the stored key has been invalidated. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.DirectKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L153)
  Requests the underlying SecretBytes storage lock. Since 0.3.0.
- **Jwe.DirectKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L158)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let WrappingKey = class implements SecretMemory<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L164)
  An AES JWE key-encryption key with both expected algorithms fixed by the caller. Tagged `#Final`. Since 0.3.0.
- **Jwe.WrappingKey** · [`export last algorithm: WrapAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L168)
  The expected key wrap algorithm. Since 0.3.0.
- **Jwe.WrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L172)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let init = func(alg: WrapAlgorithm, enc: ContentEncryption, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L178)
  Copies the wrapping key. Its length must match the specified wrap algorithm. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let expose = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L192)
  A copy of the key. Exposing a wiped key traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L197)
  Overwrites and invalidates the stored key. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L202)
  Whether the stored key has been invalidated. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let lock = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L207)
  Requests the underlying SecretBytes storage lock. Since 0.3.0.
- **Jwe.WrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L212)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let RsaWrappingKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L229)
  An RSA public key bound to RSA-OAEP-256 and one content encryption algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L233)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export last public_key: rsa.RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L238)
  The RSA public key. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let init = func(enc: ContentEncryption, rsa_key: rsa.RsaPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L243)
  Binds the public key. A modulus shorter than 2048 bits traps. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L252)
  Whether the key and the content encryption algorithm are equal. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L263)
  A hash of the public key and the content algorithm. Since 0.3.0.
- **Jwe.RsaWrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L268)
  The key management and content encryption names. Since 0.3.0.
- **Jwe** · [`export let RsaUnwrappingKey = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L274)
  An RSA private key bound to RSA-OAEP-256 and one content encryption algorithm. Tagged `#Final`. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export last encryption: ContentEncryption`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L278)
  The expected content encryption algorithm. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export let init = func(enc: ContentEncryption, rsa_key: rsa.RsaPrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L284)
  Retains the private key. A modulus shorter than 2048 bits traps. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export let open = func(compact: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L296)
  Authenticates and decrypts an RSA-OAEP-256 compact JWE. Traps after the underlying key is wiped. On failure to recover the expected CEK, it still checks content authentication using a pseudorandom substitute derived under the private key, and returns the same authentication error. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe.RsaUnwrappingKey** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L329)
  A fixed placeholder for the key. Since 0.3.0.
- **Jwe** · [`export let seal_with = func(key: DirectKey, plaintext: uint8[list], iv: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L538)
  Deterministic compact encryption with a caller-supplied IV, and a CEK when using a wrapping key. The RSA-OAEP-256 overload also takes a 32-byte seed. Never reuse an IV under the same CEK. With a caller-supplied protected JSON header, its exact UTF-8 bytes are authenticated, and a malformed or mismatching header returns an error. An incorrect IV or CEK length traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe** · [`export let seal = func(key: DirectKey, plaintext: uint8[list]) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L607)
  Compact encryption with fresh IV and, for a wrapping key, a fresh CEK. An OS entropy failure is returned to the caller. A direct AES-GCM key using random IVs is limited to 2^32 seal operations across all users of that key. Overloads accept a caller's DRBG. Tagged `#NoDiscard`. Since 0.3.0.
- **Jwe** · [`export let open = func(key: DirectKey, compact: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/token/jwe.nra#L625)
  Authenticates and decrypts a compact JWE using the caller's expected algorithms. Malformed input and an algorithm mismatch return crypto.invalid_jwe. Integrity and decryption failures return crypto.authentication_failed, with no unauthenticated plaintext. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
