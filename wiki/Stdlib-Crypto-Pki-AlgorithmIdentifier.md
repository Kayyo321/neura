# `Std.Crypto.Pki.AlgorithmIdentifier`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.AlgorithmIdentifier` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AlgorithmIdentifier** · [`export let AlgorithmIdentifier = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L16)
  An algorithm OID with its optional parameters, kept as DER. Tagged `#Final`. Since 0.3.0.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export last oid: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L18)
  The algorithm.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export last parameters: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L20)
  The DER encoding of the parameters; empty when they are absent.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L22)
  The DER encoding of the whole AlgorithmIdentifier.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let init = func(algorithm: Oid, parameter_der: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L25)
  An identifier with the OID and the DER of its parameters (empty for none).
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let has_parameters = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L36)
  Whether parameters are present.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let has_null_parameters = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L39)
  Whether the parameters are exactly NULL.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let pss_parameters = func -> Result<RsaPssParameters>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L44)
  The RSASSA-PSS parameters, when this is id-RSASSA-PSS. Tagged `#NoDiscard`.
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L54)
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L62)
- **AlgorithmIdentifier.AlgorithmIdentifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L65)
  "AlgorithmIdentifier(<name>)".
- **AlgorithmIdentifier** · [`export let RsaPssParameters = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L70)
  RSASSA-PSS-params (RFC 4055, section 3.1), with the defaults filled in. Tagged `#Final`. Since 0.3.0.
- **AlgorithmIdentifier.RsaPssParameters** · [`export last hash: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L72)
  The message digest (default SHA-1).
- **AlgorithmIdentifier.RsaPssParameters** · [`export last mask_generation: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L74)
  The mask generation function (MGF1).
- **AlgorithmIdentifier.RsaPssParameters** · [`export last mask_hash: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L76)
  The digest of MGF1 (default SHA-1).
- **AlgorithmIdentifier.RsaPssParameters** · [`export last salt_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L78)
  The salt length in bytes (default 20).
- **AlgorithmIdentifier.RsaPssParameters** · [`export last trailer_field: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L80)
  The trailer field (must be 1).
- **AlgorithmIdentifier.RsaPssParameters** · [`export let init = func(digest: Oid, mgf: Oid, mgf_digest: Oid, salt: int, trailer: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L82)
- **AlgorithmIdentifier.RsaPssParameters** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L90)
- **AlgorithmIdentifier.RsaPssParameters** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L100)
- **AlgorithmIdentifier.RsaPssParameters** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L104)
  "RsaPssParameters(<hash>, MGF1 <hash>, salt <n>)".
- **AlgorithmIdentifier** · [`export let parse = func(e: der.Element) -> Result<AlgorithmIdentifier>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L125)
  Reads an AlgorithmIdentifier: SEQUENCE { OBJECT IDENTIFIER, parameters ANY OPTIONAL }. Tagged `#NoDiscard`. Since 0.3.0.
- **AlgorithmIdentifier** · [`export let without_parameters = func(dotted: string) -> AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L141)
  The identifier with no parameters (EdDSA, ML-DSA, SLH-DSA, ECDSA signatures). Tagged `#NoDiscard`. Since 0.3.0.
- **AlgorithmIdentifier** · [`export let with_null = func(dotted: string) -> AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/algorithm-identifier.nra#L146)
  The identifier with NULL parameters (the RSA PKCS #1 v1.5 signatures and rsaEncryption). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
