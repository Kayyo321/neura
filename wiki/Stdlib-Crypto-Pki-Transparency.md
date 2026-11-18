# `Std.Crypto.Pki.Transparency`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.Transparency` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Transparency** · [`export let EntryType = enum (X509Entry, PrecertEntry)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L39)
  LogEntryType of RFC 6962, section 3.1. Since 0.3.0.
- **Transparency** · [`export let SignedCertificateTimestamp = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L43)
  One SignedCertificateTimestamp (version v1). Tagged `#Final`. Since 0.3.0.
- **Transparency.SignedCertificateTimestamp** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L44)
- **Transparency.SignedCertificateTimestamp** · [`export last log_id: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L46)
  The log's ID: SHA-256 of its public key's SubjectPublicKeyInfo.
- **Transparency.SignedCertificateTimestamp** · [`export last timestamp: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L48)
  Milliseconds since the epoch.
- **Transparency.SignedCertificateTimestamp** · [`export last extensions: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L49)
- **Transparency.SignedCertificateTimestamp** · [`export last hash_algorithm: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L51)
  TLS HashAlgorithm code (4 = SHA-256).
- **Transparency.SignedCertificateTimestamp** · [`export last signature_algorithm: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L53)
  TLS SignatureAlgorithm code (1 = RSA, 3 = ECDSA).
- **Transparency.SignedCertificateTimestamp** · [`export last signature: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L54)
- **Transparency.SignedCertificateTimestamp** · [`export last encoded: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L56)
  The serialized SCT.
- **Transparency.SignedCertificateTimestamp** · [`export let init = func(sct_version: int, id: uint8[list], time: uint64, ext: uint8[list], hash: int, sig_alg: int, sig: uint8[list], bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L58)
- **Transparency.SignedCertificateTimestamp** · [`export let algorithm = func -> Optional<AlgorithmIdentifier>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L71)
  The AlgorithmIdentifier of the signature, when RFC 6962 allows the pair.
- **Transparency.SignedCertificateTimestamp** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L73)
- **Transparency.SignedCertificateTimestamp** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L76)
- **Transparency.SignedCertificateTimestamp** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L78)
- **Transparency** · [`export let SignedTreeHead = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L84)
  A signed tree head (the get-sth response of RFC 6962, section 4.3). Tagged `#Final`. Since 0.3.0.
- **Transparency.SignedTreeHead** · [`export last tree_size: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L85)
- **Transparency.SignedTreeHead** · [`export last timestamp: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L86)
- **Transparency.SignedTreeHead** · [`export last root_hash: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L87)
- **Transparency.SignedTreeHead** · [`export last hash_algorithm: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L88)
- **Transparency.SignedTreeHead** · [`export last signature_algorithm: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L89)
- **Transparency.SignedTreeHead** · [`export last signature: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L90)
- **Transparency.SignedTreeHead** · [`export let init = func(size: int64, time: uint64, root: uint8[list], hash: int, sig_alg: int, sig: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L92)
- **Transparency.SignedTreeHead** · [`export let signed_data = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L102)
  The TreeHeadSignature input: version v1, tree_hash, timestamp, tree_size and the root.
- **Transparency.SignedTreeHead** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L110)
- **Transparency.SignedTreeHead** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L118)
- **Transparency.SignedTreeHead** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L120)
- **Transparency** · [`export let parse_list = func(bytes: uint8[list]) -> Result<SignedCertificateTimestamp[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L165)
  Reads a TLS SignedCertificateTimestampList: a 2-byte length, then SCTs each with a 2-byte length. The list and each SCT must be non-empty, and every length must fit. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let parse_sct = func(bytes: uint8[list], offset: int) -> Result<SignedCertificateTimestamp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L191)
  Reads one serialized SCT; `offset` places error offsets in the enclosing input. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let scts_of = func(certificate: Certificate) -> Result<SignedCertificateTimestamp[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L220)
  The SCTs of a certificate's SCT list extension (RFC 6962, section 3.3); empty when it has none. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let precert_tbs = func(certificate: Certificate) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L235)
  The certificate's TBSCertificate with the SCT list extension removed (and the extensions field removed if that leaves it empty), re-encoded in DER: the precertificate's TBSCertificate. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let signed_data = func(sct: SignedCertificateTimestamp, entry: EntryType, certificate: Certificate, issuer: Optional<Certificate>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L297)
  The bytes the log signed for the SCT (RFC 6962, section 3.2): version, certificate_timestamp, timestamp, entry type and signed entry, and the SCT's extensions. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let verify_sct = func(sct: SignedCertificateTimestamp, entry: EntryType, certificate: Certificate, issuer: Optional<Certificate>, log_key: SubjectPublicKeyInfo) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L311)
  Verifies the SCT under the log's key: the log ID must be the SHA-256 of the key's SubjectPublicKeyInfo, and the signature must verify over `signed_data`. Unless supplied, the verifier is the library's AlgorithmVerifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let verify_sth = func(sth: SignedTreeHead, log_key: SubjectPublicKeyInfo) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L349)
  Verifies a signed tree head under the log's key using the library verifier or a supplied verifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let leaf_input = func(sct: SignedCertificateTimestamp, entry: EntryType, certificate: Certificate, issuer: Optional<Certificate>) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L360)
  The MerkleTreeLeaf (RFC 6962, section 3.4) of an SCT's entry: version v1, timestamped_entry, and the TimestampedEntry. Its leaf hash is `merkle.leaf_hash<Sha256>(leaf)`. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let leaf_hash = func(leaf: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L371)
  The leaf hash of a MerkleTreeLeaf: SHA-256(0x00 || leaf). Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let verify_inclusion = func(leaf_hash_bytes: uint8[list], index: int64, tree_size: int64, proof: uint8[list][list], root: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L375)
  Whether the audit path proves the leaf hash at `index` in the tree of `tree_size` with the root. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let verify_consistency = func(old_size: int64, new_size: int64, old_root: uint8[list], new_root: uint8[list], proof: uint8[list][list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L389)
  Whether the consistency proof shows that the tree of `old_size` with `old_root` is a prefix of the tree of `new_size` with `new_root`. Tagged `#NoDiscard`. Since 0.3.0.
- **Transparency** · [`export let parse_sth_json = func(text: string) -> Result<SignedTreeHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/transparency.nra#L403)
  Parses a get-sth JSON response: tree_size, timestamp, sha256_root_hash (Base64, 32 bytes) and tree_head_signature (Base64 of a TLS DigitallySigned). Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
