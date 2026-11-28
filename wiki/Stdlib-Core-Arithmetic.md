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

### 6.6 `Arithmetic`

*Status: proposed in [the 0.3.0 records note](https://github.com/Kayyo321/neura/blob/main/docs/design/Records030.md); implemented.*

`Std.Core.Arithmetic` declares four static contracts ([Polymorphism](Polymorphism#static-interface-requirements)): interfaces of static requirements only, which a generic function calls through its type parameter, as `T.zero()`, and which each specialization calls directly.

| Contract | Static requirements | Conforming primitives |
| --- | --- | --- |
| `Additive<T>` | `zero() -> T`, `add(left: T, right: T) -> T` | Every sized integer and float |
| `Multiplicative<T>` | `one() -> T`, `multiply(left: T, right: T) -> T` | Every sized integer and float |
| `Ordered<T>` | `compare(a: T, b: T) -> Ordering` | Every sized integer, `bool` |
| `Parseable<T>` | `parse(text: string) -> Result<T>` | Every sized integer and float, `bool` |

A primitive conforms only through its companion module, which declares `module implements` in the standard library; no type conforms by having functions of the right names, and only a trusted companion may declare a conformance ([E0578](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0578.md)). The companions' `zero`, `add`, `one` and `multiply` are the operators at the type's width: integer `add` and `multiply` trap on overflow (reason 1), and float ones round as float arithmetic does. `compare` and `parse` are the companions' existing functions. Floats do not conform to `Ordered`, since their companions have no total `compare`; `int` and `float` use the companions of the types they name. A class conforms by listing a contract after `implements` and declaring each requirement as an exported static function.

<!-- example: check -->
```neura
let.promote _ = import:Std.Core.Arithmetic;

let sum = func<T: Additive<T>>(items: T[]) -> T {
    let total = T.zero();
    for (let item in items) {
        total = T.add(total, item);
    }
    return total;
};

let small: int8[] = [1, 2, 3];
let total = sum(small);
let precise = sum<float64>([0.1, 0.2, 0.3]);
```

A contract's name promises nothing beyond its signature: no pass reassociates or reorders calls because a requirement is named `add`, so `precise` is the left-to-right sum, bit for bit. A companion's conformance is checked, and `Std.Core.Arithmetic` loaded, only when a bound needs it, so a program that uses no contract builds the image it built before.
