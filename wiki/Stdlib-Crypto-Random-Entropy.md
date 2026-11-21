# `Std.Crypto.Random.Entropy`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Random.Entropy`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Random.Entropy`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Entropy** · [`export let os_bytes = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L44)
  `count` bytes from the operating system's cryptographic generator, requested in pieces of at most 1 MiB. A denied capability or a failed request is an `Io` error ("entropy request failed", `CapabilityDenied` when denied). A short answer is `InvalidData`. A negative count traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Entropy** · [`export let hardware_bytes = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L65)
  `count` bytes from the native RDSEED or RNDR provider, up to 1 MiB per request. A negative count traps. The provider checks the entropy capability even for zero bytes. A count above 1 MiB gives `Io(InvalidInput)`. Missing hardware gives `Io(Unsupported)`. Retry exhaustion gives `Io(Other)`. The Modern policy admits this source. Tagged `#NoDiscard`. Since 0.3.0.
- **Entropy** · [`export let fused_seed = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L77)
  SHA-512("Neura entropy fusion" || os_bytes(64) || hardware_bytes(64)). Both sources must be allowed by policy. Returns either provider's error without a fallback. Tagged `#NoDiscard`. Since 0.3.0.
- **Entropy** · [`export let fork_generation = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L96)
  A number that changes whenever the process may have been duplicated: a POSIX `fork()` in the child, or a restore of a virtual machine snapshot (the Linux VM generation ID, `MADV_WIPEONFORK` pages). A generator that sees it change must reseed before it produces output. This function is not implemented in 0.3.0 and traps on every host. The host service it would use, `neura.process.generation`, is the constant 0 on Windows: Windows has no `fork()`, and a snapshot restore is not detected. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#213-stdcryptorandom). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
