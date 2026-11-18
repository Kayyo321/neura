# `Std.Crypto.Pki.PathValidation`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import `Std.Crypto.Pki.PathValidation` to use this module. The [package specification](Stdlib-Design#2110-stdcryptopki) describes the supported profile. The [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra) defines the implementation.

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PathValidation** · [`export const MaxNameComparisons: int = 100000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L46)
  The most comparisons of names against name-constraint subtrees for one certificate; a certificate that would need more fails with pki.name_constraints (a guard against crafted certificates, like Go's). Since 0.3.0.
- **PathValidation** · [`export let ValidationOptions = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L50)
  The inputs of section 6.1.1 and this library's extra checks. Set the fields after construction. Tagged `#Final`. Since 0.3.0.
- **PathValidation.ValidationOptions** · [`export let time: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L52)
  The instant at which every certificate must be valid.
- **PathValidation.ValidationOptions** · [`export let verifier: SignatureVerifier`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L54)
  The signature step.
- **PathValidation.ValidationOptions** · [`export let initial_policies: string[list] = new([oids.AnyPolicy])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L56)
  user-initial-policy-set (dotted OIDs); anyPolicy by default.
- **PathValidation.ValidationOptions** · [`export let initial_explicit_policy: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L57)
- **PathValidation.ValidationOptions** · [`export let initial_policy_mapping_inhibit: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L58)
- **PathValidation.ValidationOptions** · [`export let initial_any_policy_inhibit: bool = false`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L59)
- **PathValidation.ValidationOptions** · [`export let purpose: string = ""`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L61)
  The extended key usage the leaf must allow (dotted), or empty for none.
- **PathValidation.ValidationOptions** · [`export let dns_name: string = ""`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L63)
  The DNS name the leaf must match (RFC 9525), or empty.
- **PathValidation.ValidationOptions** · [`export let ip_address: uint8[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L65)
  The IP address the leaf must match (4 or 16 bytes), or empty.
- **PathValidation.ValidationOptions** · [`export let email_address: string = ""`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L67)
  The e-mail address the leaf must match, or empty.
- **PathValidation.ValidationOptions** · [`export let max_intermediates: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L69)
  The most intermediates that are not self-issued.
- **PathValidation.ValidationOptions** · [`export let max_signature_checks: int = 64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L71)
  The most signature checks one validation may make (a guard against crafted pools).
- **PathValidation.ValidationOptions** · [`export let strict_profile: bool = true`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L73)
  Whether to apply the profile rules of RFC 5280, section 4 listed in the module's description.
- **PathValidation.ValidationOptions** · [`export let min_rsa_bits: int = 2048`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L75)
  The smallest RSA modulus accepted, in bits.
- **PathValidation.ValidationOptions** · [`export let init = func(at: instant.Instant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L80)
  Options for validation at the instant using the library verifier or a supplied verifier. Since 0.3.0.
- **PathValidation.ValidationOptions** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L88)
- **PathValidation** · [`export let ValidatedPath = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L94)
  A validated path: the certificates from the leaf to the trust anchor, and the outputs of section 6.1.6. Tagged `#Final`. Since 0.3.0.
- **PathValidation.ValidatedPath** · [`export last chain: Certificate[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L96)
  Leaf first, trust anchor last.
- **PathValidation.ValidatedPath** · [`export last policies: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L99)
  The valid policies at the leaf after the user-initial-policy-set was applied (2.5.29.32.0 stands for any); empty when the policy tree is NULL.
- **PathValidation.ValidatedPath** · [`export let init = func(certificates: Certificate[list], valid_policies: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L101)
- **PathValidation.ValidatedPath** · [`export let anchor = func -> Certificate`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L107)
  The trust anchor.
- **PathValidation.ValidatedPath** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L109)
- **PathValidation.ValidatedPath** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L125)
- **PathValidation.ValidatedPath** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L133)
- **PathValidation** · [`export let validate = func(leaf: Certificate, intermediates: Certificate[list], anchors: Certificate[list], options: ValidationOptions) -> Result<ValidatedPath>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/pki/path-validation.nra#L180)
  Builds and validates a path from the leaf to one of the anchors. Failures are `Custom` errors whose code names the check: pki.no_path, pki.signature, pki.validity, pki.issuer_name, pki.not_ca, pki.path_length, pki.key_usage, pki.name_constraints, pki.policy, pki.unknown_critical_extension, pki.extended_key_usage, pki.host_name, pki.profile, pki.weak_key or pki.budget. When several candidate paths fail, the error of the first is returned. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Shared contracts

- [`Std.Crypto` contracts and runtime guarantees](Stdlib-Design#21-stdcrypto)
- [Failure and resource handling](Stdlib-Design#5-core-contracts)
