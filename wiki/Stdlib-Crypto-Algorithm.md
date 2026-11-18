# `Std.Crypto.Algorithm`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Algorithm`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/algorithm.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Algorithm`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Algorithm** · [`export let Algorithm = enum ( Md5, Sha1, Sha224, Sha256, Sha384, Sha512, Sha512_256, Sha3_224, Sha3_256, Sha3_384, Sha3_512, Shake128, Shake256, CShake128, CShake256, TupleHash128, TupleHash256, TurboShake128, TurboShake256, Blake3, MerkleTree, Hmac, Kmac128, Kmac256, Poly1305, AesCmac, Aes128, Aes192, Aes256, Aes128Gcm, Aes192Gcm, Aes256Gcm, Aes128GcmSiv, Aes256GcmSiv, Aes256Siv, Aes384Siv, Aes512Siv, ChaCha20, ChaCha20Poly1305, XChaCha20Poly1305, Stream, Ff1, Hkdf, Pbkdf2, Scrypt, Argon2id, Balloon, ChaCha20Drbg, OsEntropy, X25519, X448, Ed25519, Ed448, P256, P384, P521, Secp256k1, Rsa1024, RsaPss, RsaOaep, Hpke, MlKem512, MlKem768, MlKem1024, MlDsa44, MlDsa65, MlDsa87, SlhDsa, FnDsa, X25519MlKem768, Ed25519MlDsa65, RsaPkcs1v15, Blake2b, Ristretto255, Shamir, Frost, RsaBlindSignature, Oprf, Bulletproofs, XSalsa20, XWing, Merlin )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/algorithm.nra#L12)
  One algorithm. `to_string` gives the variant name. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#211-contracts). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
