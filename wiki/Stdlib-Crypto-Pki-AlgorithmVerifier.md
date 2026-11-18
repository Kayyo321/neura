# `Std.Crypto.Pki.AlgorithmVerifier`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.AlgorithmVerifier` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-verifier.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AlgorithmVerifier** · [`export let AlgorithmVerifier = class implements SignatureVerifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-verifier.nra#L27)
  Dispatches by signature OID. Unknown or policy-denied algorithms give UnsupportedAlgorithm. Mismatched keys, malformed parameters and failed signature checks give Invalid. Tagged `#Final`. Since 0.3.0.
- **AlgorithmVerifier.AlgorithmVerifier** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-verifier.nra#L31)
  Creates a stateless verifier with the library's algorithm dispatch. Since 0.3.0.
- **AlgorithmVerifier.AlgorithmVerifier** · [`export let verify_signature = func(algorithm: AlgorithmIdentifier, key: SubjectPublicKeyInfo, signed: uint8[list], signature: uint8[list]) -> SignatureCheck`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-verifier.nra#L36)
  Checks the signature under the supplied key and signature AlgorithmIdentifier. Tagged `#NoDiscard`. Since 0.3.0.
- **AlgorithmVerifier.AlgorithmVerifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-verifier.nra#L74)
  The verifier's class name. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
