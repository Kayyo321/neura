# `Std.Crypto.Pki.Certificate`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Certificate` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Certificate** · [`export let CertificateParts = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L35)
  The parts of a certificate while it is read. Internal to the parser. Since 0.3.0.
- **Certificate.CertificateParts** · [`export let der_bytes: uint8[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L36)
- **Certificate.CertificateParts** · [`export let tbs_bytes: uint8[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L37)
- **Certificate.CertificateParts** · [`export let version: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L38)
- **Certificate.CertificateParts** · [`export let serial: uint8[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L39)
- **Certificate.CertificateParts** · [`export let tbs_signature_algorithm: Optional<AlgorithmIdentifier> = Optional<AlgorithmIdentifier>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L40)
- **Certificate.CertificateParts** · [`export let issuer: Optional<Name> = Optional<Name>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L41)
- **Certificate.CertificateParts** · [`export let not_before: instant.Instant = instant.Epoch`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L42)
- **Certificate.CertificateParts** · [`export let not_after: instant.Instant = instant.Epoch`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L43)
- **Certificate.CertificateParts** · [`export let subject: Optional<Name> = Optional<Name>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L44)
- **Certificate.CertificateParts** · [`export let public_key: Optional<SubjectPublicKeyInfo> = Optional<SubjectPublicKeyInfo>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L45)
- **Certificate.CertificateParts** · [`export let issuer_unique_id: Optional<BitString> = Optional<BitString>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L46)
- **Certificate.CertificateParts** · [`export let subject_unique_id: Optional<BitString> = Optional<BitString>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L47)
- **Certificate.CertificateParts** · [`export let extensions: Extension[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L48)
- **Certificate.CertificateParts** · [`export let signature_algorithm: Optional<AlgorithmIdentifier> = Optional<AlgorithmIdentifier>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L49)
- **Certificate.CertificateParts** · [`export let signature: uint8[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L50)
- **Certificate.CertificateParts** · [`export let basic_constraints: Optional<BasicConstraints> = Optional<BasicConstraints>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L51)
- **Certificate.CertificateParts** · [`export let key_usage: Optional<KeyUsage> = Optional<KeyUsage>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L52)
- **Certificate.CertificateParts** · [`export let extended_key_usage: Optional<ExtendedKeyUsage> = Optional<ExtendedKeyUsage>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L53)
- **Certificate.CertificateParts** · [`export let subject_alt_names: Optional<GeneralName[list]> = Optional<GeneralName[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L54)
- **Certificate.CertificateParts** · [`export let issuer_alt_names: Optional<GeneralName[list]> = Optional<GeneralName[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L55)
- **Certificate.CertificateParts** · [`export let name_constraints: Optional<NameConstraints> = Optional<NameConstraints>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L56)
- **Certificate.CertificateParts** · [`export let authority_key_identifier: Optional<AuthorityKeyIdentifier> = Optional<AuthorityKeyIdentifier>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L57)
- **Certificate.CertificateParts** · [`export let subject_key_identifier: Optional<uint8[list]> = Optional<uint8[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L58)
- **Certificate.CertificateParts** · [`export let policies: Optional<PolicyInformation[list]> = Optional<PolicyInformation[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L59)
- **Certificate.CertificateParts** · [`export let policy_mappings: Optional<PolicyMapping[list]> = Optional<PolicyMapping[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L60)
- **Certificate.CertificateParts** · [`export let policy_constraints: Optional<PolicyConstraints> = Optional<PolicyConstraints>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L61)
- **Certificate.CertificateParts** · [`export let inhibit_any_policy: Optional<int> = Optional<int>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L62)
- **Certificate.CertificateParts** · [`export let crl_distribution_points: Optional<DistributionPoint[list]> = Optional<DistributionPoint[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L63)
- **Certificate.CertificateParts** · [`export let authority_info_access: Optional<AccessDescription[list]> = Optional<AccessDescription[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L64)
- **Certificate.CertificateParts** · [`export let sct_list: Optional<uint8[list]> = Optional<uint8[list]>.None`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L65)
- **Certificate.CertificateParts** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L67)
- **Certificate** · [`export let Certificate = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L72)
  A parsed certificate. Equal certificates have equal DER. Tagged `#Final`. Since 0.3.0.
- **Certificate.Certificate** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L74)
  The DER of the whole certificate.
- **Certificate.Certificate** · [`export last tbs_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L76)
  The DER of the TBSCertificate, which the signature covers.
- **Certificate.Certificate** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L78)
  1, 2 or 3.
- **Certificate.Certificate** · [`export last serial: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L80)
  The serial number as two's-complement content bytes.
- **Certificate.Certificate** · [`export last issuer: Name`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L81)
- **Certificate.Certificate** · [`export last not_before: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L82)
- **Certificate.Certificate** · [`export last not_after: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L83)
- **Certificate.Certificate** · [`export last subject: Name`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L84)
- **Certificate.Certificate** · [`export last public_key: SubjectPublicKeyInfo`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L85)
- **Certificate.Certificate** · [`export last issuer_unique_id: Optional<BitString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L86)
- **Certificate.Certificate** · [`export last subject_unique_id: Optional<BitString>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L87)
- **Certificate.Certificate** · [`export last extensions: Extension[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L89)
  Every extension in order, known and unknown.
- **Certificate.Certificate** · [`export last signature_algorithm: AlgorithmIdentifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L91)
  The signature algorithm (equal to the one inside the TBSCertificate).
- **Certificate.Certificate** · [`export last signature: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L93)
  The signature bytes.
- **Certificate.Certificate** · [`export last basic_constraints: Optional<BasicConstraints>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L94)
- **Certificate.Certificate** · [`export last key_usage: Optional<KeyUsage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L95)
- **Certificate.Certificate** · [`export last extended_key_usage: Optional<ExtendedKeyUsage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L96)
- **Certificate.Certificate** · [`export last subject_alt_names: Optional<GeneralName[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L97)
- **Certificate.Certificate** · [`export last issuer_alt_names: Optional<GeneralName[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L98)
- **Certificate.Certificate** · [`export last name_constraints: Optional<NameConstraints>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L99)
- **Certificate.Certificate** · [`export last authority_key_identifier: Optional<AuthorityKeyIdentifier>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L100)
- **Certificate.Certificate** · [`export last subject_key_identifier: Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L101)
- **Certificate.Certificate** · [`export last policies: Optional<PolicyInformation[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L102)
- **Certificate.Certificate** · [`export last policy_mappings: Optional<PolicyMapping[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L103)
- **Certificate.Certificate** · [`export last policy_constraints: Optional<PolicyConstraints>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L104)
- **Certificate.Certificate** · [`export last inhibit_any_policy: Optional<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L105)
- **Certificate.Certificate** · [`export last crl_distribution_points: Optional<DistributionPoint[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L106)
- **Certificate.Certificate** · [`export last authority_info_access: Optional<AccessDescription[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L107)
- **Certificate.Certificate** · [`export last sct_list: Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L110)
  The TLS-encoded SignedCertificateTimestampList of RFC 6962 (read it with Std.Crypto.Pki.Transparency).
- **Certificate.Certificate** · [`export let init = func(p: CertificateParts)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L113)
  Takes the parts a successful `parse` collected.
- **Certificate.Certificate** · [`export let extension = func(dotted: string) -> Optional<Extension>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L146)
  The extension with the dotted OID.
- **Certificate.Certificate** · [`export let is_self_issued = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L156)
  Whether issuer and subject are the same name (RFC 5280's self-issued).
- **Certificate.Certificate** · [`export let is_ca = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L159)
  Whether basicConstraints says cA.
- **Certificate.Certificate** · [`export let is_valid_at = func(at: instant.Instant) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L172)
  Whether the instant lies in [notBefore, notAfter], both inclusive (RFC 5280, 4.1.2.5).
- **Certificate.Certificate** · [`export let fingerprint_sha256 = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L176)
  The SHA-256 of the DER.
- **Certificate.Certificate** · [`export let serial_hex = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L179)
  The serial number as lowercase hexadecimal of its content bytes.
- **Certificate.Certificate** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L181)
- **Certificate.Certificate** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L197)
- **Certificate.Certificate** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L200)
  "Certificate(<subject>, serial <hex>)".
- **Certificate** · [`export let parse = func(bytes: uint8[list]) -> Result<Certificate>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L220)
  Parses a DER certificate. Tagged `#NoDiscard`. Since 0.3.0.
- **Certificate** · [`export let parse_pem = func(text: string) -> Result<Certificate[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/certificate.nra#L265)
  Every CERTIFICATE block of PEM text, parsed. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
