# `Std.Crypto.Asymmetric.RsaPss`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.RsaPss` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RsaPss** · [`export let RsaPssSigner = class implements Signer<rsa.RsaPrivateKey, uint8[list]>, Verifier<rsa.RsaPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L29)
  Signs with RSASSA-PSS; it also verifies against the matching public key. Tagged `#Final`. Since 0.3.0.
- **RsaPss.RsaPssSigner** · [`export last hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L33)
  The hash of the message and of MGF1.
- **RsaPss.RsaPssSigner** · [`export last salt_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L35)
  The salt length in bytes.
- **RsaPss.RsaPssSigner** · [`export let init = func(private_key: rsa.RsaPrivateKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L41)
  A signer with SHA-256 and a 32-byte salt, or the given hash and salt length (from 0), with salts from `Entropy.os_bytes`, or from the given generator. Another hash or a negative salt length traps.
- **RsaPss.RsaPssSigner** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L70)
  `Algorithm.RsaPss`.
- **RsaPss.RsaPssSigner** · [`export let public_key = func -> rsa.RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L73)
  The public key.
- **RsaPss.RsaPssSigner** · [`export let sign_with_salt = func(message: uint8[list], salt: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L77)
  The signature with the given salt. A wiped key or a salt of the wrong length traps. Tagged `#NoDiscard`.
- **RsaPss.RsaPssSigner** · [`export let try_sign = func(message: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L94)
  The signature with a fresh salt. Fails when the entropy source or the private operation's check fails. A wiped key traps before requesting a salt. Tagged `#NoDiscard`.
- **RsaPss.RsaPssSigner** · [`export let sign = func(message: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L109)
  The signature with a fresh salt. Traps with `unavailable: ...` when `try_sign` fails.
- **RsaPss.RsaPssSigner** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L121)
  Whether the signature is valid for the message under the matching public key.
- **RsaPss.RsaPssSigner** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L124)
- **RsaPss** · [`export let RsaPssVerifier = class implements Verifier<rsa.RsaPublicKey, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L129)
  Verifies RSASSA-PSS signatures under a public key. Tagged `#Final`. Since 0.3.0.
- **RsaPss.RsaPssVerifier** · [`export last hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L132)
  The hash of the message.
- **RsaPss.RsaPssVerifier** · [`export last mask_hash: Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L136)
  The digest used by MGF1. Since 0.3.0.
- **RsaPss.RsaPssVerifier** · [`export last salt_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L138)
  The salt length, or -1 for any.
- **RsaPss.RsaPssVerifier** · [`export let init = func(public_key: rsa.RsaPublicKey)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L144)
  A verifier with SHA-256 and a 32-byte salt, or the given digest and salt length (-1 accepts any). MGF1 uses the message digest unless an explicit MGF1 digest is supplied. Since 0.3.0.
- **RsaPss.RsaPssVerifier** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L176)
  `Algorithm.RsaPss`.
- **RsaPss.RsaPssVerifier** · [`export let public_key = func -> rsa.RsaPublicKey`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L179)
  The public key.
- **RsaPss.RsaPssVerifier** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L182)
  Whether the signature is valid for the message.
- **RsaPss.RsaPssVerifier** · [`export let verify_digest = func(digest: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L187)
  Whether the signature is valid for a digest computed elsewhere. Tagged `#NoDiscard`.
- **RsaPss.RsaPssVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/rsa-pss.nra#L201)
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
