# `Std.Crypto.Kdf.Calibration`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.Calibration`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/calibration.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.Calibration`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#21-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Calibration** · [`export let calibrate_argon2id = func(clock: Clock, target: Duration, parallelism: int, max_memory_kib: int) -> Result<argon2.Argon2Params>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/calibration.nra#L43)
  Argon2id parameters for `parallelism` lanes and 32-byte tags that take about `target` on this clock: starting from 8 KiB per lane and one pass, memory doubles while a hash is faster than the target and the memory stays within `max_memory_kib`, then passes grow (up to 10) while it is still faster. Memory is preferred to passes, as RFC 9106, section 4 recommends. A clock error is returned. Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 21](Stdlib-Design#216-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
