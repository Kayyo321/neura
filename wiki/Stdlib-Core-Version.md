# `Std.Core.Version`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Version`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/version.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Version`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Version** · [`export const Major: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/version.nra#L6)
  The major version component of the shipped library.
- **Version** · [`export const Minor: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/version.nra#L8)
  The minor version component of the shipped library.
- **Version** · [`export const Patch: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/version.nra#L10)
  The patch version component of the shipped library.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

This module has no dedicated section in the historical design. Use the declaration reference and source above for its public API; the shared contracts and implementation decisions apply where relevant.
