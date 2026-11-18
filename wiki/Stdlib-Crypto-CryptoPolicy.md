# `Std.Crypto.Core.CryptoPolicy`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Core.CryptoPolicy`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Core.CryptoPolicy`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CryptoPolicy** · [`export let Policy = enum (Modern, Fips140_3, Cnsa2_0)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra#L16)
  The policies. `Modern` (the default) allows everything Std.Crypto implements and denies MD5, SHA-1 and RSA-1024. `Fips140_3` allows only FIPS-approved algorithms. `Cnsa2_0` allows only the CNSA 2.0 suite (AES-256, SHA-384 and SHA-512, ML-KEM-1024 and ML-DSA-87) and the constructions over it. Since 0.3.0.
- **CryptoPolicy** · [`export let set_policy = func(policy: Policy)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra#L23)
  Replaces the process-wide policy. Since 0.3.0.
- **CryptoPolicy** · [`export let policy = func -> Policy`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra#L29)
  The process-wide policy. Tagged `#NoDiscard`. Since 0.3.0.
- **CryptoPolicy** · [`export let allowed_by = func(policy: Policy, algorithm: Algorithm) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra#L33)
  Whether the policy allows the algorithm. Tagged `#NoDiscard`, `#Pure`. Since 0.3.0.
- **CryptoPolicy** · [`export let allows = func(algorithm: Algorithm) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra#L41)
  Whether the current policy allows the algorithm. Tagged `#NoDiscard`. Since 0.3.0.
- **CryptoPolicy** · [`export let require = func(algorithm: Algorithm)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/crypto-policy.nra#L46)
  Traps with "denied: <Algorithm> is not allowed under the <Policy> policy" unless the current policy allows the algorithm. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#202-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
