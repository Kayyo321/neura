# `Std.Crypto.Pki.Extensions`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Extensions` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Extensions** · [`export const DigitalSignature: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L22)
  KeyUsage bits (RFC 5280, section 4.2.1.3).
- **Extensions** · [`export const NonRepudiation: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L23)
- **Extensions** · [`export const KeyEncipherment: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L24)
- **Extensions** · [`export const DataEncipherment: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L25)
- **Extensions** · [`export const KeyAgreement: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L26)
- **Extensions** · [`export const KeyCertSign: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L27)
- **Extensions** · [`export const CrlSign: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L28)
- **Extensions** · [`export const EncipherOnly: int = 7`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L29)
- **Extensions** · [`export const DecipherOnly: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L30)
- **Extensions** · [`export let Extension = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L34)
  One extension as encoded: its OID, criticality and the contents of extnValue. Tagged `#Final`. Since 0.3.0.
- **Extensions.Extension** · [`export last oid: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L35)
- **Extensions.Extension** · [`export last critical: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L36)
- **Extensions.Extension** · [`export last value: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L38)
  The contents of the extnValue OCTET STRING (the DER of the extension's own value).
- **Extensions.Extension** · [`export let init = func(id: Oid, is_critical: bool, value_bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L40)
- **Extensions.Extension** · [`export let encode = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L47)
  The DER of the Extension SEQUENCE (criticality omitted when false, as DER requires).
- **Extensions.Extension** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L56)
- **Extensions.Extension** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L63)
- **Extensions.Extension** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L67)
  "Extension(<oid>[, critical], <n> bytes)".
- **Extensions** · [`export let BasicConstraints = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L75)
  basicConstraints: cA and pathLenConstraint (-1 when absent). Tagged `#Final`. Since 0.3.0.
- **Extensions.BasicConstraints** · [`export last ca: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L76)
- **Extensions.BasicConstraints** · [`export last path_length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L77)
- **Extensions.BasicConstraints** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L78)
- **Extensions.BasicConstraints** · [`export let init = func(is_ca: bool, path_len: int, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L80)
- **Extensions.BasicConstraints** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L86)
- **Extensions.BasicConstraints** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L89)
- **Extensions.BasicConstraints** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L90)
- **Extensions** · [`export let KeyUsage = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L95)
  keyUsage: a named bit list; `has(KeyCertSign)` and so on. Tagged `#Final`. Since 0.3.0.
- **Extensions.KeyUsage** · [`export last bits: BitString`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L96)
- **Extensions.KeyUsage** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L97)
- **Extensions.KeyUsage** · [`export let init = func(value: BitString, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L99)
- **Extensions.KeyUsage** · [`export let has = func(bit: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L105)
  Whether the bit (DigitalSignature to DecipherOnly) is set.
- **Extensions.KeyUsage** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L107)
- **Extensions.KeyUsage** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L110)
- **Extensions.KeyUsage** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L111)
- **Extensions** · [`export let ExtendedKeyUsage = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L126)
  extKeyUsage: the key purpose OIDs. Tagged `#Final`. Since 0.3.0.
- **Extensions.ExtendedKeyUsage** · [`export last purposes: Oid[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L127)
- **Extensions.ExtendedKeyUsage** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L128)
- **Extensions.ExtendedKeyUsage** · [`export let init = func(values: Oid[list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L130)
- **Extensions.ExtendedKeyUsage** · [`export let has = func(dotted: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L136)
  Whether the purpose (dotted) is listed.
- **Extensions.ExtendedKeyUsage** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L145)
- **Extensions.ExtendedKeyUsage** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L148)
- **Extensions.ExtendedKeyUsage** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L149)
- **Extensions** · [`export let GeneralSubtree = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L155)
  A GeneralSubtree of nameConstraints: the base name (minimum 0, maximum absent, as RFC 5280 requires). Tagged `#Final`. Since 0.3.0.
- **Extensions.GeneralSubtree** · [`export last base: GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L156)
- **Extensions.GeneralSubtree** · [`export let init = func(name: GeneralName)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L158)
- **Extensions.GeneralSubtree** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L162)
- **Extensions.GeneralSubtree** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L165)
- **Extensions.GeneralSubtree** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L166)
- **Extensions** · [`export let NameConstraints = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L172)
  nameConstraints: permitted and excluded subtrees. `has_permitted` and `has_excluded` say whether each field was present. Tagged `#Final`. Since 0.3.0.
- **Extensions.NameConstraints** · [`export last permitted: GeneralSubtree[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L173)
- **Extensions.NameConstraints** · [`export last excluded: GeneralSubtree[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L174)
- **Extensions.NameConstraints** · [`export last has_permitted: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L175)
- **Extensions.NameConstraints** · [`export last has_excluded: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L176)
- **Extensions.NameConstraints** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L177)
- **Extensions.NameConstraints** · [`export let init = func(allowed: GeneralSubtree[list], denied: GeneralSubtree[list], allowed_present: bool, denied_present: bool, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L179)
- **Extensions.NameConstraints** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L188)
- **Extensions.NameConstraints** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L191)
- **Extensions.NameConstraints** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L192)
- **Extensions** · [`export let AuthorityKeyIdentifier = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L199)
  authorityKeyIdentifier: keyIdentifier, authorityCertIssuer and authorityCertSerialNumber, each possibly empty. Tagged `#Final`. Since 0.3.0.
- **Extensions.AuthorityKeyIdentifier** · [`export last key_identifier: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L200)
- **Extensions.AuthorityKeyIdentifier** · [`export last has_key_identifier: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L201)
- **Extensions.AuthorityKeyIdentifier** · [`export last issuer: GeneralName[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L202)
- **Extensions.AuthorityKeyIdentifier** · [`export last serial: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L203)
- **Extensions.AuthorityKeyIdentifier** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L204)
- **Extensions.AuthorityKeyIdentifier** · [`export let init = func(id: uint8[list], id_present: bool, names: GeneralName[list], serial_number: uint8[list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L206)
- **Extensions.AuthorityKeyIdentifier** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L215)
- **Extensions.AuthorityKeyIdentifier** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L218)
- **Extensions.AuthorityKeyIdentifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L219)
- **Extensions** · [`export let PolicyQualifier = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L225)
  A policy qualifier: CPS pointer (`text` is the URI), user notice (`text` is the explicit text, if any), or another qualifier kept raw. Tagged `#Final`. Since 0.3.0.
- **Extensions.PolicyQualifier** · [`export last id: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L226)
- **Extensions.PolicyQualifier** · [`export last text: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L227)
- **Extensions.PolicyQualifier** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L228)
- **Extensions.PolicyQualifier** · [`export let init = func(qualifier: Oid, value_text: string, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L230)
- **Extensions.PolicyQualifier** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L236)
- **Extensions.PolicyQualifier** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L239)
- **Extensions.PolicyQualifier** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L240)
- **Extensions** · [`export let PolicyInformation = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L245)
  One PolicyInformation of certificatePolicies. Tagged `#Final`. Since 0.3.0.
- **Extensions.PolicyInformation** · [`export last policy: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L246)
- **Extensions.PolicyInformation** · [`export last qualifiers: PolicyQualifier[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L247)
- **Extensions.PolicyInformation** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L248)
- **Extensions.PolicyInformation** · [`export let init = func(id: Oid, items: PolicyQualifier[list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L250)
- **Extensions.PolicyInformation** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L256)
- **Extensions.PolicyInformation** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L259)
- **Extensions.PolicyInformation** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L260)
- **Extensions** · [`export let PolicyMapping = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L265)
  One pair of policyMappings. Tagged `#Final`. Since 0.3.0.
- **Extensions.PolicyMapping** · [`export last issuer_domain: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L266)
- **Extensions.PolicyMapping** · [`export last subject_domain: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L267)
- **Extensions.PolicyMapping** · [`export let init = func(issuer_policy: Oid, subject_policy: Oid)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L269)
- **Extensions.PolicyMapping** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L274)
- **Extensions.PolicyMapping** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L278)
- **Extensions.PolicyMapping** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L281)
- **Extensions** · [`export let PolicyConstraints = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L286)
  policyConstraints: requireExplicitPolicy and inhibitPolicyMapping skip counts (-1 when absent). Tagged `#Final`. Since 0.3.0.
- **Extensions.PolicyConstraints** · [`export last require_explicit_policy: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L287)
- **Extensions.PolicyConstraints** · [`export last inhibit_policy_mapping: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L288)
- **Extensions.PolicyConstraints** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L289)
- **Extensions.PolicyConstraints** · [`export let init = func(require_skip: int, inhibit_skip: int, encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L291)
- **Extensions.PolicyConstraints** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L297)
- **Extensions.PolicyConstraints** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L300)
- **Extensions.PolicyConstraints** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L301)
- **Extensions** · [`export let DistributionPoint = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L308)
  One DistributionPoint of cRLDistributionPoints. `full_name` holds the fullName alternative (or is empty); `relative_name` keeps nameRelativeToCRLIssuer's DER; `reasons` is empty when absent. Tagged `#Final`. Since 0.3.0.
- **Extensions.DistributionPoint** · [`export last full_name: GeneralName[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L309)
- **Extensions.DistributionPoint** · [`export last relative_name: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L310)
- **Extensions.DistributionPoint** · [`export last reasons: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L311)
- **Extensions.DistributionPoint** · [`export last crl_issuer: GeneralName[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L312)
- **Extensions.DistributionPoint** · [`export last der_bytes: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L313)
- **Extensions.DistributionPoint** · [`export let init = func(full: GeneralName[list], relative: uint8[list], reason_bits: uint8[list], issuer: GeneralName[list], encoded: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L315)
- **Extensions.DistributionPoint** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L324)
- **Extensions.DistributionPoint** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L327)
- **Extensions.DistributionPoint** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L328)
- **Extensions** · [`export let AccessDescription = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L333)
  One AccessDescription of authorityInfoAccess or subjectInfoAccess. Tagged `#Final`. Since 0.3.0.
- **Extensions.AccessDescription** · [`export last method: Oid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L334)
- **Extensions.AccessDescription** · [`export last location: GeneralName`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L335)
- **Extensions.AccessDescription** · [`export let init = func(access_method: Oid, where: GeneralName)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L337)
- **Extensions.AccessDescription** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L342)
- **Extensions.AccessDescription** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L345)
- **Extensions.AccessDescription** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L348)
- **Extensions** · [`export let parse_extension = func(e: der.Element) -> Result<Extension>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L385)
  Reads an Extension SEQUENCE { extnID, critical BOOLEAN DEFAULT FALSE, extnValue OCTET STRING }. An explicit FALSE is not DER and is refused. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_basic_constraints = func(value: uint8[list]) -> Result<BasicConstraints>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L406)
  basicConstraints: SEQUENCE { cA BOOLEAN DEFAULT FALSE, pathLenConstraint INTEGER (0..MAX) OPTIONAL }. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_key_usage = func(value: uint8[list]) -> Result<KeyUsage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L430)
  keyUsage: a non-empty named BIT STRING without trailing zero bits (DER). Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_extended_key_usage = func(value: uint8[list]) -> Result<ExtendedKeyUsage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L444)
  extKeyUsage: SEQUENCE SIZE (1..MAX) OF KeyPurposeId. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_general_names = func(value: uint8[list]) -> Result<GeneralName[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L458)
  subjectAltName or issuerAltName: GeneralNames. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_subject_key_identifier = func(value: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L463)
  subjectKeyIdentifier: OCTET STRING. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_authority_key_identifier = func(value: uint8[list]) -> Result<AuthorityKeyIdentifier>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L469)
  authorityKeyIdentifier: SEQUENCE { [0] keyIdentifier, [1] authorityCertIssuer, [2] authorityCertSerialNumber }, the last two together or not at all. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_name_constraints = func(value: uint8[list]) -> Result<NameConstraints>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L511)
  nameConstraints: SEQUENCE { permittedSubtrees [0], excludedSubtrees [1] }, at least one present and none empty; each subtree's minimum must be 0 and its maximum absent. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_certificate_policies = func(value: uint8[list]) -> Result<PolicyInformation[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L571)
  certificatePolicies: SEQUENCE SIZE (1..MAX) OF PolicyInformation, each policy at most once. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_policy_mappings = func(value: uint8[list]) -> Result<PolicyMapping[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L637)
  policyMappings: SEQUENCE SIZE (1..MAX) OF SEQUENCE { issuerDomainPolicy, subjectDomainPolicy }. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_policy_constraints = func(value: uint8[list]) -> Result<PolicyConstraints>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L658)
  policyConstraints: SEQUENCE { requireExplicitPolicy [0], inhibitPolicyMapping [1] }, at least one. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_inhibit_any_policy = func(value: uint8[list]) -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L700)
  inhibitAnyPolicy: SkipCerts INTEGER (0..MAX). Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_crl_distribution_points = func(value: uint8[list]) -> Result<DistributionPoint[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L707)
  cRLDistributionPoints: SEQUENCE SIZE (1..MAX) OF DistributionPoint. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_info_access = func(value: uint8[list]) -> Result<AccessDescription[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L764)
  authorityInfoAccess or subjectInfoAccess: SEQUENCE SIZE (1..MAX) OF AccessDescription. Tagged `#NoDiscard`. Since 0.3.0.
- **Extensions** · [`export let parse_sct_list = func(value: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/extensions.nra#L785)
  The RFC 6962 SCT list extension: an OCTET STRING holding the TLS-encoded list. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
