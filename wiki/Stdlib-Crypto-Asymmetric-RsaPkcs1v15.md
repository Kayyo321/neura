# `Std.Crypto.Asymmetric.RsaPkcs1v15`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.RsaPkcs1v15` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RsaPkcs1v15** · [`export let RsaPkcs1v15Verifier = class implements Verifier<rsa.RsaPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L21)
  Verifies RSASSA-PKCS1-v1_5 signatures under a public key with one hash. Tagged `#Final`. Since 0.3.0.
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export last hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L24)
  The hash of the message.
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export let init = func(public_key: rsa.RsaPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L27)
  A verifier with SHA-256, or the given SHA-2 or SHA-3 hash (another traps).
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L44)
  `Algorithm.RsaPkcs1v15`.
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export let public_key = func -> rsa.RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L47)
  The public key.
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L50)
  Whether the signature (exactly k bytes) is valid for the message.
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export let verify_digest = func(digest: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L55)
  Whether the signature is valid for a digest computed elsewhere (of the hash's length). Tagged `#NoDiscard`.
- **RsaPkcs1v15.RsaPkcs1v15Verifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pkcs1v15.nra#L71)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
