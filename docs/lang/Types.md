# Types

Types describe values used in declarations, parameters, and return values. A declaration places the type after the name; function parameters place it before the name.

```neura
let count: int32 = 1;
let advance = func(int32 count) -> int32 {
    return count + 1;
};
```

## Built-in types

The built-in types are listed below. [Numbers](Numbers.md) specifies the numeric types' representation, literals, arithmetic, overflow, rounding, and conversions.

| Type | Purpose |
| --- | --- |
| `bool` | Boolean value. |
| `byte` | A byte of data: another name for `uint8`. |
| `int8`, `int16`, `int32`, `int64` | Signed integers of the indicated width in bits. |
| `uint8`, `uint16`, `uint32`, `uint64` | Unsigned integers of the indicated width in bits. |
| `int` | General-purpose integer: another name for `int32`. |
| `float16`, `float32`, `float64` | IEEE 754 binary16, binary32, and binary64 floating point. |
| `bfloat16` | Brain floating point (BF16): binary32's 8-bit exponent with a 7-bit fraction. |
| `string` | Text. Its encoding and indexing rules remain to be specified. |
| `void` | No return value. |

Low-precision formats such as FP8 and packed integer formats are not part of the language. Every numeric operation is computed by neuron circuits, so no type depends on hardware support.

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

An enum declares a closed set of named variants. Variants are written qualified by the enum's name.

```neura
export let Light = enum (Red, Yellow, Green);

let next = func(Light current) -> Light {
    if (current == Light.Red) {
        return Light.Green;
    }
    return Light.Red;
};
```

An enum is a type: it can annotate bindings, fields, parameters, results, collection elements, and type arguments. Variants are values of their enum, visible wherever the enum is, and the variant list must not be empty or repeat a name. Enum values support `==` and `!=`, and `variant as T` converts a variant to its ordinal, its zero-based position in the declaration, in any integer type `T`. There is no ordering, no conversion from an integer to an enum, and no payload. The built-in `SysIo.Entry.ExitCode`, whose only variant is `Ok`, is the entry function's status: `Ok` is status 0.

## Inference, conversions, equality, and absence

The proposed initial inference rule is local: an initialized binding without an annotation takes the initializer's type when that type is unambiguous. Public APIs should use explicit parameter and return types. Integer literal default types, inference across generic calls, and ambiguous literals need a precise specification. Empty collection literals require an expected type as described in [Collections](Collections.md).

There are no implicit numeric conversions: operands of an operation have the same type, and `expression as Type` converts explicitly with the checked rules of [Numbers](Numbers.md#conversions). Floating-point equality follows IEEE 754. Equality for classes, collections, and strings needs a defined contract; do not assume structural equality or identity equality for these values. Basic scalar comparisons are illustrated in [core.nra](../../examples/guide/core.nra).

Nullability, optional values, and their syntax are open. No implicit null value or null check should be assumed. Enum payload variants and exhaustive matching are also open. These decisions are prerequisites for a general result type and fallible standard APIs. Generic syntax and its limitations are described in [Polymorphism](Polymorphism.md).
