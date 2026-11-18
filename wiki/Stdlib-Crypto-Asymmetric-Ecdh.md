# `Std.Crypto.Asymmetric.Ecdh`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Asymmetric.Ecdh` to use this module. The [package specification](Stdlib-Design#218-stdcryptoasymmetric) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ecdh** · [`export let Ecdh = class implements CryptographicPrimitive<Ecdh>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L26)
  The ECDH primitive of one curve, for the policy. Tagged `#Final`. Since 0.3.0.
- **Ecdh.Ecdh** · [`export last curve: ecdsa.Curve`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L28)
  The curve.
- **Ecdh.Ecdh** · [`export let init = func(key_curve: ecdsa.Curve)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L31)
  Checks the policy for the curve.
- **Ecdh.Ecdh** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L37)
  The curve's identifier.
- **Ecdh.Ecdh** · [`export let diffie_hellman = func(private_key: ecdsa.EcPrivateKey, peer: ecdsa.EcPublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L41)
  See the module function `diffie_hellman`; keys of another curve trap. Tagged `#NoDiscard`.
- **Ecdh.Ecdh** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L48)
- **Ecdh** · [`export let diffie_hellman = func(private_key: ecdsa.EcPrivateKey, peer: ecdsa.EcPublicKey) -> Result<SecretBytes>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/asymmetric/ecdh.nra#L55)
  The x-coordinate of d Q. Keys on different curves trap with `invalid argument`. A result at infinity, which a validated key on these prime-order curves cannot give, would be `Custom` code `crypto.key_agreement`. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
