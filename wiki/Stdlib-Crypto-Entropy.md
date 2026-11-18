# `Std.Crypto.Random.Entropy`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Random.Entropy`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Random.Entropy`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Entropy** · [`export let os_bytes = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L28)
  `count` bytes from the operating system's cryptographic generator, requested in pieces of at most 1 MiB. A denied capability or a failed request is an `Io` error ("entropy request failed", `CapabilityDenied` when denied); a short answer is `InvalidData`. A negative count traps. Tagged `#NoDiscard`. Since 0.3.0.
- **Entropy** · [`export let hardware_bytes = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L60)
  `count` bytes from the processor's entropy instruction (RDSEED on x86-64, RNDR on AArch64). Tagged `#NoDiscard`. Since 0.3.0.
- **Entropy** · [`export let fused_seed = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L69)
  A 64-byte seed that fuses the operating system's bytes with the hardware source: SHA-512("Neura entropy fusion" || os_bytes(64) || hardware_bytes(64)). It fails if the OS source fails, and traps while `hardware_bytes` is a stub. Use `os_bytes` until then. Tagged `#NoDiscard`. Since 0.3.0.
- **Entropy** · [`export let fork_generation = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/entropy.nra#L98)
  A number that changes whenever the process may have been duplicated: a POSIX `fork()` in the child, or a restore of a virtual machine snapshot (the Linux VM generation ID, `MADV_WIPEONFORK` pages). A generator that sees it change must reseed before it produces output. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#203-stdcryptorandom). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
