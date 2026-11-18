# `Std.Crypto.Pki.Ocsp`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Ocsp` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ocsp** · [`export let CertIdHash = enum (Sha1, Sha256, Sha384, Sha512)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L40)
  The CertID hash. Since 0.3.0.
- **Ocsp** · [`export let ResponseStatus = enum (Successful, MalformedRequest, InternalError, TryLater, SigRequired, Unauthorized)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L44)
  The responseStatus values of RFC 6960, section 4.2.1 (4 is unused). Since 0.3.0.
- **Ocsp** · [`export let CertStatus = enum (Good, Revoked, Unknown)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L48)
  The certStatus of a SingleResponse. Since 0.3.0.
- **Ocsp** · [`export let CertId = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L52)
  A CertID: hash algorithm, issuer name hash, issuer key hash and serial number. Compared by value. Tagged `#Final`. Since 0.3.0.
- **Ocsp.CertId** · [`export last hash_algorithm: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L53)
- **Ocsp.CertId** · [`export last issuer_name_hash: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L54)
- **Ocsp.CertId** · [`export last issuer_key_hash: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L55)
- **Ocsp.CertId** · [`export last serial: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L57)
  The serial number's two's-complement content bytes.
- **Ocsp.CertId** · [`export let init = func(algorithm: Oid, name_hash: uint8[list], key_hash: uint8[list], serial_number: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L59)
- **Ocsp.CertId** · [`export let encode = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L67)
  The DER of the CertID (hash parameters NULL, as openssl and RFC 6960 examples write them).
- **Ocsp.CertId** · [`export let matches = func(certificate: Certificate, issuer: Certificate) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L72)
  Whether this CertID names the certificate issued by the issuer (with this CertID's hash).
- **Ocsp.CertId** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L86)
- **Ocsp.CertId** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L94)
- **Ocsp.CertId** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L97)
- **Ocsp** · [`export let SingleResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L103)
  One SingleResponse. Tagged `#Final`. Since 0.3.0.
- **Ocsp.SingleResponse** · [`export last cert_id: CertId`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L104)
- **Ocsp.SingleResponse** · [`export last status: CertStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L105)
- **Ocsp.SingleResponse** · [`export last revocation_time: Optional<instant.Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L107)
  The revocationTime of a revoked certificate.
- **Ocsp.SingleResponse** · [`export last revocation_reason: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L109)
  The CRLReason code (RFC 5280, section 5.3.1), or -1 when absent.
- **Ocsp.SingleResponse** · [`export last this_update: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L110)
- **Ocsp.SingleResponse** · [`export last next_update: Optional<instant.Instant>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L111)
- **Ocsp.SingleResponse** · [`export let init = func(id: CertId, cert_status: CertStatus, revoked_at: Optional<instant.Instant>, reason: int, this_time: instant.Instant, next_time: Optional<instant.Instant>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L113)
- **Ocsp.SingleResponse** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L124)
  Equal when the CertID, status, reason and times are.
- **Ocsp.SingleResponse** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L133)
- **Ocsp.SingleResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L135)
- **Ocsp** · [`export let BasicResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L143)
  A BasicOCSPResponse. Tagged `#Final`. Since 0.3.0.
- **Ocsp.BasicResponse** · [`export last tbs_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L145)
  The DER of ResponseData, which the signature covers.
- **Ocsp.BasicResponse** · [`export last responder_name: Optional<Name>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L147)
  The responder by name, or None when it is named by key hash.
- **Ocsp.BasicResponse** · [`export last responder_key_hash: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L149)
  The responder's KeyHash (SHA-1 of its public key bits), or empty.
- **Ocsp.BasicResponse** · [`export last produced_at: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L150)
- **Ocsp.BasicResponse** · [`export last responses: SingleResponse[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L151)
- **Ocsp.BasicResponse** · [`export last extensions: Extension[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L153)
  The response extensions.
- **Ocsp.BasicResponse** · [`export last signature_algorithm: AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L154)
- **Ocsp.BasicResponse** · [`export last signature: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L155)
- **Ocsp.BasicResponse** · [`export last certificates: Certificate[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L157)
  The certificates sent with the response (such as a delegated responder's).
- **Ocsp.BasicResponse** · [`export let init = func(tbs: uint8[list], by_name: Optional<Name>, by_key: uint8[list], produced: instant.Instant, items: SingleResponse[list], exts: Extension[list], algorithm: AlgorithmIdentifier, signature_bytes: uint8[list], certs: Certificate[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L159)
- **Ocsp.BasicResponse** · [`export let nonce = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L174)
  The nonce extension's value (the OCTET STRING contents), or empty.
- **Ocsp.BasicResponse** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L192)
  Equal when the signed data and the signature are.
- **Ocsp.BasicResponse** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L195)
- **Ocsp.BasicResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L197)
- **Ocsp** · [`export let OcspResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L202)
  An OCSPResponse: the status and, when successful, the basic response. Tagged `#Final`. Since 0.3.0.
- **Ocsp.OcspResponse** · [`export last status: ResponseStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L203)
- **Ocsp.OcspResponse** · [`export last basic: Optional<BasicResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L204)
- **Ocsp.OcspResponse** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L205)
- **Ocsp.OcspResponse** · [`export let init = func(response_status: ResponseStatus, body: Optional<BasicResponse>, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L207)
- **Ocsp.OcspResponse** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L213)
- **Ocsp.OcspResponse** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L216)
- **Ocsp.OcspResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L218)
- **Ocsp** · [`export let OcspRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L223)
  A parsed OCSPRequest (unsigned requests; a signature is kept but not checked). Tagged `#Final`. Since 0.3.0.
- **Ocsp.OcspRequest** · [`export last cert_ids: CertId[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L224)
- **Ocsp.OcspRequest** · [`export last nonce: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L226)
  The nonce, or empty.
- **Ocsp.OcspRequest** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L227)
- **Ocsp.OcspRequest** · [`export let init = func(ids: CertId[list], nonce_bytes: uint8[list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L229)
- **Ocsp.OcspRequest** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L235)
- **Ocsp.OcspRequest** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L238)
- **Ocsp.OcspRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L240)
- **Ocsp** · [`export let for_certificate = func(certificate: Certificate, issuer: Certificate, hash: CertIdHash) -> CertId`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L350)
  The CertID of the certificate issued by the issuer: hashes of the issuer's DER Name and of its public key's BIT STRING value (RFC 6960, section 4.1.1). Tagged `#NoDiscard`. Since 0.3.0.
- **Ocsp** · [`export let build_request = func(ids: CertId[list], nonce: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L357)
  The DER of an unsigned OCSPRequest for the CertIDs, with the nonce extension (RFC 8954: 1 to 32 bytes) when `nonce` is not empty. No CertID, or a nonce over 32 bytes, traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Ocsp** · [`export let parse_request = func(bytes: uint8[list]) -> Result<OcspRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L400)
  Parses an OCSPRequest. Tagged `#NoDiscard`. Since 0.3.0.
- **Ocsp** · [`export let parse_response = func(bytes: uint8[list]) -> Result<OcspResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L451)
  Parses an OCSPResponse; a successful one must carry a BasicOCSPResponse. Tagged `#NoDiscard`. Since 0.3.0.
- **Ocsp** · [`export let verify = func(response: OcspResponse, certificate: Certificate, issuer: Certificate, at: instant.Instant, skew_seconds: int64, nonce: uint8[list]) -> Result<SingleResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/ocsp.nra#L625)
  The SingleResponse for the certificate (issued by `issuer`) after checking the response as the module describes, at the instant `at` with `skew_seconds` of allowed clock skew. `nonce` is the nonce sent, or empty when none was. A revoked or unknown status is returned, not an error: the caller decides. Unless supplied, the verifier is the library's AlgorithmVerifier. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
