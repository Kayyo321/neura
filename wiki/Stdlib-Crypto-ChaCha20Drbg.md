# `Std.Crypto.Random.ChaCha20Drbg`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Random.ChaCha20Drbg`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Random.ChaCha20Drbg`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ChaCha20Drbg** · [`export let ChaCha20Drbg = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L37)
  A ChaCha20 generator. Seeded from caller bytes it is reproducible; `from_entropy` seeds it from the operating system. A fork-safe generator (`fork_safe`) checks Entropy.fork_generation before every output and reseeds from the operating system when it changed; that check is a stub until the runtime reports fork and VM-clone generations, so a fork-safe generator traps on its first use. Tagged `#Final`, `#NotThreadSafe`. Since 0.3.0.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let init = func(seed: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L45)
  A generator seeded with the bytes; a seed shorter than 32 bytes traps with "invalid argument: seed must be at least 32 bytes".
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let reseed = func(material: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L54)
  Mixes more material into the key.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let reseed_from_entropy = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L62)
  Mixes 32 bytes from the operating system into the key. Tagged `#NoDiscard`.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let enable_fork_checks = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L78)
  Turns on fork detection: from now on every output first compares Entropy.fork_generation with the value it saw last and reseeds from the operating system when it changed.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let fill = func(count: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L84)
  `count` random bytes. A negative count traps.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let next_uint32 = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L110)
  Four random bytes, little-endian.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let next_uint64 = func -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L113)
  Eight random bytes, little-endian.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let next_below = func(bound: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L117)
  Uniform in 0 up to but excluding `bound`, by rejection of 32-bit draws above the largest multiple of `bound`. A bound below 1 traps.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let wipe = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L131)
  Overwrites the key. Later output traps.
- **ChaCha20Drbg.ChaCha20Drbg** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L137)
  A fixed placeholder, never the key.
- **ChaCha20Drbg** · [`export let from_entropy = func -> Result<ChaCha20Drbg>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L142)
  A generator seeded with 32 bytes from the operating system. Fails as Entropy.os_bytes does. Tagged `#NoDiscard`. Since 0.3.0.
- **ChaCha20Drbg** · [`export let fork_safe = func -> Result<ChaCha20Drbg>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/random/cha-cha20-drbg.nra#L158)
  A generator seeded from the operating system that checks for fork and VM clones before every output (see the class). Traps while Entropy.fork_generation is a stub. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#213-stdcryptorandom). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
