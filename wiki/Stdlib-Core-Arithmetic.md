# `Std.Core.Arithmetic`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Arithmetic`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/arithmetic.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Arithmetic`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Arithmetic** · [`export let Additive = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/arithmetic.nra#L17)
  A type with a zero and an addition: every sized integer and float. Since 0.3.0.
- **Arithmetic** · [`export let Multiplicative = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/arithmetic.nra#L26)
  A type with a one and a multiplication: every sized integer and float. Since 0.3.0.
- **Arithmetic** · [`export let Ordered = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/arithmetic.nra#L35)
  A type with a total order: every sized integer and bool. Floats have no total compare here. Since 0.3.0.
- **Arithmetic** · [`export let Parseable = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/arithmetic.nra#L42)
  A type that parses from text: every sized integer and float, and bool. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 6.6 Arithmetic

`Std.Core.Arithmetic` declares four static contracts: interfaces of static requirements only, which a generic function calls through its type parameter, as `T.zero()`, and which each specialization calls directly.

| Contract | Static requirements | Conforming primitives |
| --- | --- | --- |
| `Additive<T>` | `zero() -> T`, `add(left: T, right: T) -> T` | Every sized integer and float |
| `Multiplicative<T>` | `one() -> T`, `multiply(left: T, right: T) -> T` | Every sized integer and float |
| `Ordered<T>` | `compare(a: T, b: T) -> Ordering` | Every sized integer, `bool` |
| `Parseable<T>` | `parse(text: string) -> Result<T>` | Every sized integer and float, `bool` |

A primitive conforms only through its companion module, which declares `module implements` in the standard library. The companions' `zero`, `add`, `one` and `multiply` are the operators at the type's width: integer `add` and `multiply` trap on overflow, and float ones round as float arithmetic does. Floats do not conform to `Ordered`. A class conforms by listing a contract after `implements` and declaring each requirement as an exported static function.
