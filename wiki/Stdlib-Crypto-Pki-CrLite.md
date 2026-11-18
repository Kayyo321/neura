# `Std.Crypto.Pki.CrLite`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.CrLite` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CrLite** · [`export let CascadeHash = enum (MurmurHash3, Sha256Low32, Sha256Counter)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L27)
  The hash of a cascade's layers. Since 0.3.0.
- **CrLite** · [`export let Decision = enum (Revoked, NotRevoked, NotCovered)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L31)
  The revocation decision for one certificate. Since 0.3.0.
- **CrLite** · [`export let BloomLayer = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L35)
  One Bloom filter layer. Tagged `#Final`. Since 0.3.0.
- **CrLite.BloomLayer** · [`export last size: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L37)
  The number of bits.
- **CrLite.BloomLayer** · [`export last hash_count: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L38)
- **CrLite.BloomLayer** · [`export last bits: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L39)
- **CrLite.BloomLayer** · [`export let init = func(bit_count: uint32, hashes: uint32, data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L41)
- **CrLite.BloomLayer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L47)
- **CrLite** · [`export let FilterCascade = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L52)
  A parsed filter cascade. Compared by its encoding. Tagged `#Final`. Since 0.3.0.
- **CrLite.FilterCascade** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L53)
- **CrLite.FilterCascade** · [`export last inverted: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L54)
- **CrLite.FilterCascade** · [`export last salt: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L55)
- **CrLite.FilterCascade** · [`export last hash: CascadeHash`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L56)
- **CrLite.FilterCascade** · [`export last layers: BloomLayer[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L57)
- **CrLite.FilterCascade** · [`export last encoded: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L58)
- **CrLite.FilterCascade** · [`export let init = func(format_version: int, is_inverted: bool, salt_bytes: uint8[list], algorithm: CascadeHash, filters: BloomLayer[list], bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L60)
- **CrLite.FilterCascade** · [`export let has = func(key: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L71)
  Whether the key is in the cascade.
- **CrLite.FilterCascade** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L92)
- **CrLite.FilterCascade** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L108)
- **CrLite.FilterCascade** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L110)
- **CrLite** · [`export let parse = func(bytes: uint8[list]) -> Result<FilterCascade>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L235)
  Parses a cascade. A version other than 1 or 2, an inverted flag other than 0 or 1, a layer whose bits run past the input, a layer of 0 bits, irregular layer numbers, mixed hash algorithms, an unknown algorithm, or no layer at all is a `Parse` error. Tagged `#NoDiscard`. Since 0.3.0.
- **CrLite** · [`export let key_of = func(certificate: Certificate, issuer: Certificate) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L302)
  The CRLite key: SHA-256 of the issuer's SubjectPublicKeyInfo, then the serial's content bytes. Tagged `#NoDiscard`. Since 0.3.0.
- **CrLite** · [`export let check = func(cascade: FilterCascade, certificate: Certificate, issuer: Certificate, covered_issuers: uint8[list][list]) -> Decision`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/cr-lite.nra#L312)
  The decision for the certificate: `NotCovered` unless SHA-256 of its issuer's SubjectPublicKeyInfo is in `covered_issuers` (empty means the caller vouches that the filter covers the issuer), else `Revoked` when the cascade holds its key, else `NotRevoked`. The cascade must hold the revoked certificates. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
