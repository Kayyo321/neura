# `Std.Crypto.Pki.Csr`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Csr` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Csr** · [`export let Attribute = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L33)
  One Attribute: a type and the DER of each of its values. Tagged `#Final`. Since 0.3.0.
- **Csr.Attribute** · [`export last type: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L34)
- **Csr.Attribute** · [`export last values: uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L35)
- **Csr.Attribute** · [`export let init = func(attribute: Oid, value_ders: uint8[list][list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L37)
- **Csr.Attribute** · [`export let encode = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L43)
  The DER of SEQUENCE { type, SET OF values }.
- **Csr.Attribute** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L45)
- **Csr.Attribute** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L55)
- **Csr.Attribute** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L60)
- **Csr** · [`export let CertificationRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L65)
  A parsed certification request. Equal requests have equal DER. Tagged `#Final`. Since 0.3.0.
- **Csr.CertificationRequest** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L66)
- **Csr.CertificationRequest** · [`export last info_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L68)
  The DER of CertificationRequestInfo, which the signature covers.
- **Csr.CertificationRequest** · [`export last subject: Name`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L69)
- **Csr.CertificationRequest** · [`export last public_key: SubjectPublicKeyInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L70)
- **Csr.CertificationRequest** · [`export last attributes: Attribute[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L71)
- **Csr.CertificationRequest** · [`export last extensions: Extension[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L73)
  The extensions of the extensionRequest attribute (empty without one).
- **Csr.CertificationRequest** · [`export last challenge_password: Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L74)
- **Csr.CertificationRequest** · [`export last signature_algorithm: AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L75)
- **Csr.CertificationRequest** · [`export last signature: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L76)
- **Csr.CertificationRequest** · [`export let init = func(encoded: uint8[list], info: uint8[list], name: Name, key: SubjectPublicKeyInfo, attrs: Attribute[list], requested: Extension[list], password: Optional<string>, algorithm: AlgorithmIdentifier, signature_bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L78)
- **Csr.CertificationRequest** · [`export let verify_signature = func -> SignatureCheck`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L96)
  Checks the request's signature under its public key. A supplied verifier overrides the library's AlgorithmVerifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Csr.CertificationRequest** · [`export let subject_alt_names = func -> Result<Optional<GeneralName[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L104)
  The requested subjectAltName entries, if the extensionRequest has them. Tagged `#NoDiscard`.
- **Csr.CertificationRequest** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L114)
- **Csr.CertificationRequest** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L117)
- **Csr.CertificationRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L120)
  "CertificationRequest(<subject>)". A challenge password is not shown.
- **Csr** · [`export let parse = func(bytes: uint8[list]) -> Result<CertificationRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L143)
  Parses a DER certification request (version v1 only). Tagged `#NoDiscard`. Since 0.3.0.
- **Csr** · [`export let parse_pem = func(text: string) -> Result<CertificationRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L204)
  The only CERTIFICATE REQUEST block of PEM text, parsed. Tagged `#NoDiscard`. Since 0.3.0.
- **Csr** · [`export let verify_signature = func(request: CertificationRequest) -> SignatureCheck`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L210)
  Checks the request's signature under its public key using the library verifier or a supplied verifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Csr** · [`export let extension_request = func(items: Extension[list]) -> Attribute`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L218)
  The extensionRequest attribute carrying the extensions. Tagged `#NoDiscard`. Since 0.3.0.
- **Csr** · [`export let challenge_password = func(password: string) -> Attribute`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L229)
  The challengePassword attribute (a PrintableString when every character allows it, else UTF8String). Tagged `#NoDiscard`. Since 0.3.0.
- **Csr** · [`export let build = func(subject: Name, public_key: SubjectPublicKeyInfo, attributes: Attribute[list], algorithm: AlgorithmIdentifier, sign: func(uint8[list]) -> uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/csr.nra#L248)
  The DER of a certification request: CertificationRequestInfo (v1, the subject, the key, the attributes) signed by `sign`, which receives the DER of CertificationRequestInfo and returns the signature bytes for `algorithm`. The function is the caller's signer (a private key never enters this module). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
