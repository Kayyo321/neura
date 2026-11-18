# `Std.Crypto.Asymmetric.RsaOaep`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.RsaOaep` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RsaOaep** · [`export let RsaOaep = class implements CryptographicPrimitive<RsaOaep>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L27)
  RSA-OAEP with one hash. Tagged `#Final`. Since 0.3.0.
- **RsaOaep.RsaOaep** · [`export last hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L29)
  The hash of the label and of MGF1.
- **RsaOaep.RsaOaep** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L35)
  SHA-256, or the given SHA-2 or SHA-3 hash (another traps), with seeds from `Entropy.os_bytes`, or from the given generator.
- **RsaOaep.RsaOaep** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L55)
  `Algorithm.RsaOaep`.
- **RsaOaep.RsaOaep** · [`export let max_message_length = func(key: rsa.RsaPublicKey) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L59)
  The largest message for the key, k - 2 hLen - 2 bytes. Tagged `#NoDiscard`.
- **RsaOaep.RsaOaep** · [`export let encrypt_with_seed = func(key: rsa.RsaPublicKey, message: uint8[list], label: uint8[list], seed: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L65)
  The ciphertext (k bytes) with the given hLen-byte seed. A message too long or a seed of another length traps. Tagged `#NoDiscard`.
- **RsaOaep.RsaOaep** · [`export let encrypt = func(key: rsa.RsaPublicKey, message: uint8[list], label: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L74)
  The ciphertext with a fresh seed. A message too long traps; an entropy failure is returned. Tagged `#NoDiscard`.
- **RsaOaep.RsaOaep** · [`export let decrypt = func(key: rsa.RsaPrivateKey, ciphertext: uint8[list], label: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L90)
  The message, or `Custom` code `crypto.decryption` for any decryption failure. A wiped key traps. Tagged `#NoDiscard`.
- **RsaOaep.RsaOaep** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-oaep.nra#L114)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
