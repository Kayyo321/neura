# `Std.Crypto.Pki.SignatureVerifier`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.SignatureVerifier` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/signature-verifier.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SignatureVerifier** · [`export let SignatureCheck = enum (Valid, Invalid, UnsupportedAlgorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/signature-verifier.nra#L16)
  The outcome of one signature check. Since 0.3.0.
- **SignatureVerifier** · [`export let SignatureVerifier = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/signature-verifier.nra#L20)
  Checks one signature. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
