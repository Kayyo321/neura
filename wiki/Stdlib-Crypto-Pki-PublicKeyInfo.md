# `Std.Crypto.Pki.PublicKeyInfo`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.PublicKeyInfo` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PublicKeyInfo** · [`export let KeyAlgorithm = enum ( Rsa, RsaPss, Ec, Ed25519, Ed448, X25519, X448, MlDsa44, MlDsa65, MlDsa87, MlKem512, MlKem768, MlKem1024, SlhDsa, Unknown, MlDsa65Ed25519 )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L25)
  The public-key algorithms this module identifies. Since 0.3.0.
- **PublicKeyInfo** · [`export let SubjectPublicKeyInfo = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L32)
  A subject public key with its algorithm. Compared by its DER encoding. Tagged `#Final`. Since 0.3.0.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last algorithm: AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L34)
  The AlgorithmIdentifier.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last key: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L36)
  The subjectPublicKey BIT STRING's bytes (whole bytes).
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last key_algorithm: KeyAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L38)
  The algorithm, identified.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last parameter_set: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L41)
  "P-256", "P-384", "P-521" or "secp256k1" for EC keys, the SLH-DSA parameter set (such as "SLH-DSA-SHA2-128s"), else empty.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last rsa_modulus: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L43)
  The RSA modulus as an unsigned big-endian magnitude (RSA and RSA-PSS keys), else empty.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last rsa_exponent: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L45)
  The RSA public exponent as an unsigned big-endian magnitude, else empty.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L47)
  The DER encoding of the whole SubjectPublicKeyInfo.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export let init = func(id: AlgorithmIdentifier, key_bytes: uint8[list], which: KeyAlgorithm, set_name: string, modulus: uint8[list], exponent: uint8[list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L50)
  Records a checked key; use `parse` or `parse_der`.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export let key_bits = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L64)
  The key size in bits: the RSA modulus length, the curve's field size, or the security category's size for the fixed-size schemes (256 for Ed25519 and X25519, 448 for Ed448 and X448); 0 for Unknown and the post-quantum schemes.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export let policy_algorithm = func -> Optional<Algorithm>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L98)
  The `Std.Crypto.Algorithm` of a key whose scheme the library names, for policy checks.
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L161)
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L176)
- **PublicKeyInfo.SubjectPublicKeyInfo** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L179)
  "SubjectPublicKeyInfo(<algorithm>[ <parameter set>], <n> key bytes)".
- **PublicKeyInfo** · [`export let parse = func(e: der.Element) -> Result<SubjectPublicKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L204)
  Reads a SubjectPublicKeyInfo element. Tagged `#NoDiscard`. Since 0.3.0.
- **PublicKeyInfo** · [`export let parse_der = func(bytes: uint8[list]) -> Result<SubjectPublicKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L332)
  Parses the DER encoding of a SubjectPublicKeyInfo. Tagged `#NoDiscard`. Since 0.3.0.
- **PublicKeyInfo** · [`export let of = func(id: AlgorithmIdentifier, key: uint8[list]) -> Result<SubjectPublicKeyInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/public-key-info.nra#L336)
  A SubjectPublicKeyInfo from an AlgorithmIdentifier and the raw key bytes (checked like `parse`). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
