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
| `float` | Another name for `float32`. |
| `bfloat16` | Brain floating point (BF16): binary32's 8-bit exponent with a 7-bit fraction. |
| `string` | Immutable UTF-8 text with bytewise equality and byte-indexed primitives. |
| `class` | An umbrella reference type for every class instance. |
| `void` | No return value. |

Low-precision formats such as FP8 and packed integer formats are not part of the language. Every numeric operation is computed by neuron circuits, so no type depends on hardware support.

Arrays and lists are built-in collection types, written `string[]` and `string[list]` respectively. Arrays have a fixed length; lists can grow and shrink. Suffixes can be combined for nested collections. See [Arrays and lists](Collections.md) for their syntax and safety rules.

### Text primitives

String equality compares UTF-8 bytes. `text.byte_length` returns the byte count as `int` and `text.byte_at(index)` returns a checked `uint8`. `text.slice(start, end)` accepts a half-open range with endpoints on UTF-8 character boundaries. `string.from_utf8(bytes)` copies a `uint8[]` or `uint8[list]` and returns `[bool, string]`. Invalid input returns `[false, ""]`. Valid empty input returns `[true, ""]`.

## Tuples

A tuple has one or more ordered elements, each with its own static type. Write
`[T1, T2, ...]` in a type position and `[value1, value2, ...]` where a tuple is
expected. Tuple types may appear in bindings, fields, function signatures, type
arguments, and collection element types.

```neura
let measure = func() -> [int, float] {
    return [42, 0.5];
};

let [count, weight] = measure();
const [label, _] = ["answer", count];
let pair: [string, float] = [label, weight];
let description = pair[0];
```

Destructuring evaluates its initializer once. Each name introduces a new
binding with the corresponding element type. `_` ignores an element while
preserving its evaluation. `let` creates mutable bindings and `const` creates
constant bindings. A bracket literal on the right of a destructuring declaration
infers a tuple from its elements. An uncontextualized bracket literal elsewhere
continues to infer an array. A tuple can be indexed with a zero-based integer
literal in range. The tuple's elements are immutable. A reference stored in a
tuple retains the behavior of the referenced value.

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

The derived class extends the base class's interface. A derived instance can be passed where its base type is expected, and an overridden method dispatches to the implementation for the actual instance. Declare an override with `let.override` or `const.override`. See [Polymorphism](Polymorphism.md) for interfaces and generics and [Object lifecycle](ObjectLifecycle.md) for construction and cleanup.

### Umbrella class and nil

`class` is a type in annotation, parameter, result, and type argument positions. Every class instance can be stored in it. An interface reference can also be passed as `class`. The runtime keeps the concrete identity and dispatches methods to the instance's implementation.

```neura
let entry: class = new LogEntry();
let missing: LogEntry = nil;

if (entry is LogEntry) {
    let exact = entry as LogEntry;
    exact.message = "ready";
}
```

- `nil` is an explicit absent reference for class and interface types, including type parameters bounded by them. With no contextual type, it has type `class`. A field without an initializer is still uninitialized. Use `= nil` to initialize it to absence.
- `==` and `!=` compare references by identity. Two nil references compare equal. Use `is` to test a live reference against a class or interface. A nil reference gives false. A checked reference cast with `as` preserves nil and traps for an incompatible instance.
- Reading or writing a field, invoking an instance method, or explicitly destroying a nil reference traps. It has no heap allocation and tracing skips it.
- `class` supplies `equals(class other) -> bool`, `hash_code() -> uint32`, and `to_string() -> string`. A class can override those signatures. The defaults use identity and a qualified class name with an identity number, such as `Acme.Shapes.Circle#3`. The runtime assigns that number at first use by default hashing or formatting. An equality override must agree with its hash and return false for nil.
- The built-in value members of scalars, tuples, arrays, and lists use value semantics. Generic and aggregate value operations treat nil as a distinct value with hash zero and text `nil`. A direct method call on a nil class reference follows the receiver trap rule.

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

An enum can annotate bindings, fields, parameters, results, collection elements, and type arguments. Its variant names are visible wherever the enum is visible. Each enum declares at least one variant, and names are unique. An entirely fieldless enum supports `==` and `!=`. The checked conversion `variant as T` converts one of its values to its zero-based declaration ordinal in integer type `T`. `Std.Program.ExitCode.Ok` is the entry function's status 0.

Generic enums declare parameters after `enum`. Qualify a value constructor with an explicit instance, such as `Optional<int>.Some(4)` or `Optional<int>.None`. Patterns such as `Optional.Some(value)` get their payload types from the subject. A type overlay for a concrete instance can also qualify constructors and patterns.

```neura
let Optional = enum<T> (None, Some(T));
let IntOptional = Optional<int>;
let value: IntOptional = IntOptional.Some(4);
```

An enum can mix unit variants, positional payloads, and named payloads. A
constructor evaluates its fields in written order, even when named arguments
are reordered. The payload belongs to the selected variant and is read through
a match. An enum with payloads has no built-in equality or numeric cast.

```neura
let Tree = enum (Empty, Leaf(int), Branch { left: Tree, right: Tree });

let sum = func(Tree tree) -> int {
    return match (tree) {
        Tree.Empty => 0,
        Tree.Leaf(value) => value,
        Tree.Branch { left, right } => sum(left) + sum(right),
    };
};

let tree = Tree.Branch { right: Tree.Leaf(2), left: Tree.Leaf(1) };
```

`match (value)` evaluates the subject once and evaluates only the selected arm.
Each arm names a different variant, or a final `_` arm covers the remaining
variants. Without a wildcard, every variant must appear. A payload pattern
specifies each field, using `_` to ignore a field. Named patterns allow
`field: binding` and the shorthand `field`. Pattern bindings are constant and
visible only in their arm. A match expression uses expression arms with a
common type. When no surrounding type is available, the first arm establishes
the result type. A match statement uses a statement block in each arm.

Patterns currently cover one variant level. Generic enum declarations, nested
patterns, guards, and derives are open language decisions.

## Inference, conversions, equality, and absence

The proposed initial inference rule is local: an initialized binding without an annotation takes the initializer's type when that type is unambiguous. Public APIs should use explicit parameter and return types. Integer literal default types, inference across generic calls, and ambiguous literals need a precise specification. Empty collection literals require an expected type as described in [Collections](Collections.md).

There are no implicit numeric conversions: operands of an operation have the same type, and `expression as Type` converts explicitly with the checked rules of [Numbers](Numbers.md#conversions). Floating-point equality follows IEEE 754. Equality for classes, collections, and strings needs a defined contract; do not assume structural equality or identity equality for these values. Basic scalar comparisons are illustrated in [core.nra](../../examples/guide/core.nra).

Nullability, optional values, and their syntax are open. No implicit null value or null check should be assumed. Generic syntax and its limitations are described in [Polymorphism](Polymorphism.md).
