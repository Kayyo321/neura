# `Std.Crypto.Advanced.Frost`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Advanced.Frost` to use this module. The [package specification](Stdlib-Design#2113-stdcryptoadvanced) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Frost** · [`export let Suite = core.Suite`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L32)
  RFC 9591 ciphersuites. The overloads without a suite use ristretto255 and SHA-512. Since 0.3.0.
- **Frost** · [`export let Member = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L43)
  An enrolled participant's public verification key and identifier. Tagged `#Final`. Since 0.3.0.
- **Frost.Member** · [`export last suite: Suite`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L47)
  The ciphersuite that gives this value its wire format. Since 0.3.0.
- **Frost.Member** · [`export let identifier = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L65)
  A copy of the canonical nonzero scalar identifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.Member** · [`export let public_key = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L70)
  A copy of the canonical verification key encoding. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.Member** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L75)
  The suite-encoded identifier followed by the public verification key. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.Member** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L83)
  Whether the identifier and public key both match. Since 0.3.0.
- **Frost.Member** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L89)
  A hash of the identifier and public key. Since 0.3.0.
- **Frost.Member** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L94)
  The public member type. Since 0.3.0.
- **Frost** · [`export let Commitment = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L100)
  One public round-one commitment, identified by its participant. Tagged `#Final`. Since 0.3.0.
- **Frost.Commitment** · [`export last suite: Suite`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L104)
  The ciphersuite that gives this value its wire format. Since 0.3.0.
- **Frost.Commitment** · [`export let identifier = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L123)
  A copy of the participant's canonical scalar identifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.Commitment** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L128)
  The RFC 9591 encoding of one commitment entry, identifier || hiding || binding. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.Commitment** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L137)
  Whether the three canonical encodings match. Since 0.3.0.
- **Frost.Commitment** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L143)
  A hash of the encoded commitment. Since 0.3.0.
- **Frost.Commitment** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L148)
  The public commitment type. Since 0.3.0.
- **Frost** · [`export let SignatureShare = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L154)
  The public scalar output of one signer in round two. Tagged `#Final`. Since 0.3.0.
- **Frost.SignatureShare** · [`export last suite: Suite`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L158)
  The ciphersuite that gives this value its wire format. Since 0.3.0.
- **Frost.SignatureShare** · [`export let identifier = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L173)
  A copy of the signer's canonical scalar identifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SignatureShare** · [`export let scalar = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L178)
  A copy of the suite-encoded signature scalar. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SignatureShare** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L183)
  Whether the identifier and signature scalar both match. Since 0.3.0.
- **Frost.SignatureShare** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L194)
  A hash of the identifier and signature scalar. Since 0.3.0.
- **Frost.SignatureShare** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L200)
  The public share type. Since 0.3.0.
- **Frost** · [`export let SigningGroup = class implements Verifier<uint8[list], uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L206)
  A group's authenticated public key, threshold and roster. Tagged `#Final`. Since 0.3.0.
- **Frost.SigningGroup** · [`export last suite: Suite`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L210)
  The ciphersuite that gives this value its wire format. Since 0.3.0.
- **Frost.SigningGroup** · [`export last threshold: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L219)
  The minimum number of signing members. Since 0.3.0.
- **Frost.SigningGroup** · [`export let algorithm = func -> Algorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L237)
  `Algorithm.Frost`. Since 0.3.0.
- **Frost.SigningGroup** · [`export let public_key = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L242)
  A copy of the group public key encoding. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningGroup** · [`export let member_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L247)
  The number of enrolled members. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningGroup** · [`export let member_at = func(index: int) -> Member`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L252)
  The member at a zero-based roster index. An invalid index traps with invalid argument. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningGroup** · [`export let verify = func(message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L275)
  Verifies the canonical Schnorr signature for this suite and the exact message. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningGroup** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L281)
  Whether public key, threshold and ordered roster match. Since 0.3.0.
- **Frost.SigningGroup** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L301)
  A hash of the key, threshold and roster. Since 0.3.0.
- **Frost.SigningGroup** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L312)
  The threshold and number of members. Since 0.3.0.
- **Frost** · [`export let NonceState = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L318)
  One-use, private round-one nonce state. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Frost.NonceState** · [`export let commitment = func -> Commitment`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L333)
  The public round-one commitment to send to the coordinator. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.NonceState** · [`export let is_used = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L338)
  Whether this state has already been consumed or wiped. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.NonceState** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L343)
  Wipes nonce storage and makes the state unusable. Since 0.3.0.
- **Frost.NonceState** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L361)
  A fixed placeholder for the nonce pair. Since 0.3.0.
- **Frost** · [`export let SigningShare = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L367)
  An enrolled signing share and its nonce reuse guard. Keep each share in one task. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Frost.SigningShare** · [`export let verification_share = func -> Member`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L384)
  The public verification data for this signer. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let export_share = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L389)
  A private copy for trusted provisioning. Using a wiped signing share traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let is_wiped = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L394)
  Whether the signing share has been wiped. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L399)
  Wipes the held signing share. Since 0.3.0.
- **Frost.SigningShare** · [`export let commit = func -> Result<NonceState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L407)
  Generates round-one state using 64 fresh bytes from the operating system. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let commit_with_drbg = func(generator: drbg.ChaCha20Drbg) -> Result<NonceState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L417)
  Generates round-one state using a caller's generator. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let commit_with_randomness = func(randomness: uint8[list]) -> Result<NonceState>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L434)
  Deterministic round one for known answers. The two 32-byte halves are independent inputs to H3. Never reuse either half with the same share. A different length traps. Repeated or zero nonces return `crypto.frost_nonce`. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let sign_share = func(nonce: NonceState, round: SigningRound, approved_message: uint8[list]) -> Result<SignatureShare>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L480)
  Consumes the nonce state before checking the signing transcript and the locally approved message. The transcript must contain this signer's exact round-one commitment. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningShare** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L532)
  A fixed placeholder for private signing material. Since 0.3.0.
- **Frost** · [`export let SigningRound = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L558)
  One coordinator transcript for a validated signing set and message. Tagged `#Final`. Since 0.3.0.
- **Frost.SigningRound** · [`export let signer_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L599)
  The number of commitments selected in this signing round. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningRound** · [`export let group_commitment = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L604)
  A copy of the aggregate round-one commitment. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningRound** · [`export let verify_share = func(share: SignatureShare) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L609)
  Whether the signature share belongs to this transcript and satisfies the RFC equation. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningRound** · [`export let aggregate = func(shares: SignatureShare[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L622)
  Verifies every selected signer exactly once, sums their shares and verifies the final Schnorr signature. Missing, unknown, repeated or invalid shares return `crypto.frost_share`. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningRound** · [`export let message = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L653)
  A copy of the agreed signing message. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.SigningRound** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L658)
  Whether group, message and ordered commitments match. Since 0.3.0.
- **Frost.SigningRound** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L677)
  A hash of the group, message and ordered commitments. Since 0.3.0.
- **Frost.SigningRound** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L688)
  The transcript type and signing set size. Since 0.3.0.
- **Frost** · [`export let DealerPackage = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L694)
  Private shares and public configuration produced by a trusted dealer. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **Frost.DealerPackage** · [`export let signing_group = func -> SigningGroup`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L707)
  The authenticated public configuration to distribute. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.DealerPackage** · [`export let share_at = func(index: int) -> SigningShare`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L712)
  The private signing share at a zero-based roster index. An invalid index traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost.DealerPackage** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L722)
  Wipes the signing shares the package still holds, including outstanding local references. Since 0.3.0.
- **Frost.DealerPackage** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L731)
  A fixed placeholder for dealer secrets. Since 0.3.0.
- **Frost** · [`export let identifier = func(index: uint32) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L737)
  The canonical identifier for a nonzero integer. With no suite argument, it uses ristretto255. Zero traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let parse_member = func(bytes: uint8[list]) -> Result<Member>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L755)
  Parses identifier || verification key from authenticated enrollment data. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let signing_group = func(public_key: uint8[list], threshold: int, members: Member[list]) -> Result<SigningGroup>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L773)
  Imports the public group configuration from a trusted source. Members must be sorted by ascending scalar identifier. The threshold must be at least 2 and the roster at most 255. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let signing_share = func(group: SigningGroup, id: uint8[list], scalar: uint8[list]) -> Result<SigningShare>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L797)
  Imports a canonical nonzero signing scalar and checks it against the authenticated roster. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let parse_commitment = func(bytes: uint8[list]) -> Result<Commitment>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L817)
  Parses a canonical commitment entry. Rejects identity points and invalid identifiers. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let parse_signature_share = func(id: uint8[list], scalar: uint8[list]) -> Result<SignatureShare>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L835)
  Parses the authenticated participant identifier and canonical signature share. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let start_round = func(group: SigningGroup, message: uint8[list], commitments: Commitment[list]) -> Result<SigningRound>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L847)
  Builds the transcript for a quorum of enrolled commitments sorted by ascending identifier. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let verify_signature = func(public_key: uint8[list], message: uint8[list], signature: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L901)
  Verifies a FROST signature in the selected suite’s canonical encoding. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let deal = func(threshold: int, participants: int) -> Result<DealerPackage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L944)
  Trusted-dealer provisioning from independent OS entropy. The dealer can reconstruct the group secret and must distribute shares securely. Valid sizes are 2 <= threshold <= participants <= 255. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let deal_with_drbg = func(threshold: int, participants: int, generator: drbg.ChaCha20Drbg) -> Result<DealerPackage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L959)
  Trusted-dealer provisioning using a caller's generator. Tagged `#NoDiscard`. Since 0.3.0.
- **Frost** · [`export let deal_with_coefficients = func(coefficients: uint8[list][list], participants: int) -> Result<DealerPackage>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/advanced/frost.nra#L992)
  Trusted-dealer provisioning from supplied canonical coefficients, constant first. The coefficient count gives the threshold. A zero constant, zero highest coefficient or zero signing share is refused. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
