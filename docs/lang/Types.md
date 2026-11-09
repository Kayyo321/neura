# Types

Types describe values used in declarations, parameters, and return values. A declaration places the type after the name; function parameters place it before the name.

```neura
let count: int32 = 1;
let advance = func(int32 count) -> int32 {
    return count + 1;
};
```

## Built-in types

The following is the intended built-in type vocabulary. Details such as literal suffixes, conversion rules, overflow behavior, and hardware support remain to be specified.

| Type | Purpose |
| --- | --- |
| `bool` | Boolean value. |
| `byte` | A byte of data. |
| `int8`, `int16`, `int32`, `int64` | Signed integers of the indicated width in bits. |
| `uint8`, `uint16`, `uint32`, `uint64` | Unsigned integers of the indicated width in bits. |
| `int` | General-purpose integer; its width is not yet specified. |
| `float16`, `float32`, `float64` | Floating-point values of the indicated width. |
| `bfloat16` | Brain floating point, a 16-bit format commonly used in machine learning (BF16). |
| `string` | Text. Its encoding and indexing rules remain to be specified. |
| `void` | No return value. |

Low-precision formats such as FP8 and packed integer formats are useful for AI workloads, but their names, encodings, arithmetic, and conversion rules have not yet been specified. Do not assume that a particular accelerator supports every type or operation.

Arrays and lists are built-in collection types, written `string[]` and `string[list]` respectively. Arrays have a fixed length; lists can grow and shrink. Suffixes can be combined for nested collections. See [Arrays and lists](Collections.md) for their syntax and safety rules.

## Type overlays

A type definition can give an existing type another name. These overlays describe the role of a value without introducing a separate representation or a distinct type.

```neura
let Types = module (
    export let UserId = int32;
    export let DisplayName = string;
    export let default_user_id: UserId = 1;

    export let next_user_id = func(UserId user_id) -> UserId {
        return user_id + 1;
    };
);
```

Overlays can refer to other overlays. See `examples/types/overlay.nra` for more examples.

## Classes

A class defines a type with fields and methods. Mark members `export` to expose them and use `new` to create an instance.

```neura
let LogEntry = class (
    export let level: string;
    export let message: string;

    export let format = func() -> string {
        return "[" + level + "] " + message;
    }
);

let entry = new LogEntry();
entry.level = "INFO";
```

### Class inheritance

Class inheritance parallels module inheritance: put a colon and the base class after `class`.

```neura
let NamedEntry = class:LogEntry (
    export let name: string;
);
```

The derived class extends the base class's interface. A derived instance can be passed where its base type is expected; an overridden method dispatches to the implementation for the actual instance. Declare an override with `let.override` or `const.override`. See `examples/types/classes.nra` for an example of polymorphism. The precise rules for constructors, visibility, and initialization in inherited classes remain to be specified. See [Polymorphism](Polymorphism.md) for dynamic dispatch and proposed abstract methods and generics, and [Object lifecycle](ObjectLifecycle.md) for proposed construction and cleanup rules.

## Enums

Enums group named variants. Existing examples use qualified variants such as `Entry.ExitCode.Ok`; enum declaration syntax and representation remain to be specified.

## Inference, conversions, equality, and absence

The proposed initial inference rule is local: an initialized binding without an annotation takes the initializer's type when that type is unambiguous. Public APIs should use explicit parameter and return types. Integer literal default types, inference across generic calls, and ambiguous literals need a precise specification. Empty collection literals require an expected type as described in [Collections](Collections.md).

Implicit numeric widening, narrowing, signed/unsigned conversion, integer-to-float conversion, and casts are not yet specified. Code should use matching types rather than assume a conversion is safe. Likewise, equality for classes, collections, floating point, and strings needs a defined contract; do not assume structural equality or identity equality for these values. Basic scalar comparisons are illustrated in [core.nra](../../examples/guide/core.nra).

Nullability, optional values, and their syntax are open. No implicit null value or null check should be assumed. Enum declarations, payload variants, exhaustive matching, and their representation are also open. These decisions are prerequisites for a general result type and fallible standard APIs. Generic syntax and its limitations are described in [Polymorphism](Polymorphism.md).
