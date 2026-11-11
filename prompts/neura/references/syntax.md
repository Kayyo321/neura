# Neura syntax and semantics

The complete language as of Neura 0.1.0, in the order you need it. The specification is [docs/lang/](https://github.com/Kayyo321/neura/tree/main/docs/lang); this page condenses it and adds what the compiler actually reports.

## Contents

- [Lexical rules](#lexical-rules)
- [Bindings](#bindings)
- [Types at a glance](#types-at-a-glance)
- [Functions](#functions)
- [Control flow and loops](#control-flow-and-loops)
- [Operators](#operators)
- [Equality: `==` versus `===`](#equality--versus-)
- [Comparison chains](#comparison-chains)
- [Ranges](#ranges)
- [Collections](#collections)
- [Strings](#strings)
- [Tuples and destructuring](#tuples-and-destructuring)
- [Enums and `match`](#enums-and-match)
- [Classes](#classes)
- [Static members and singletons](#static-members-and-singletons)
- [Inheritance and abstract classes](#inheritance-and-abstract-classes)
- [Interfaces](#interfaces)
- [Generics](#generics)
- [The umbrella `class` type, `nil`, `is`, and `as`](#the-umbrella-class-type-nil-is-and-as)
- [Object casts](#object-casts)
- [Primitive references](#primitive-references)
- [Primitive companions](#primitive-companions)
- [Object lifecycle](#object-lifecycle)
- [Type overlays](#type-overlays)
- [Documentation comments](#documentation-comments)

## Lexical rules

- Source is UTF-8. `//` starts a line comment; `///` starts a documentation comment. There are no block comments.
- Identifiers: ASCII letters, digits, and `_`, not starting with a digit.
- Reserved words include `let`, `const`, `func`, `return`, `if`, `else`, `while`, `do`, `for`, `in`, `break`, `continue`, `module`, `class`, `interface`, `implements`, `enum`, `singleton`, `match`, `new`, `destroy`, `import`, `export`, `static`, `super`, `as`, `is`, `nil`, `true`, `false`, `and`, `or`. `scope`, `spawn`, and `select` are contextual: they are forms only in specific positions and ordinary names elsewhere.
- Integer literals are decimal digits only. No hexadecimal, octal, binary, digit separators, or suffixes. A leading `-` belongs to the literal, so `-128` is a valid `int8`.
- Float literals need a fraction or exponent: `1.5`, `1e3`, `2.5e-4`. There are no literals for infinity or NaN (`1.0 / 0.0` and `0.0 / 0.0` produce them).
- String literals use double quotes with escapes `\"`, `\\`, `\t`, `\r`, `\n`. There are no character literals and no string interpolation; use `+` with `to_string()`, or `Console.format`.
- Statements and every declaration end with `;`, including a declaration whose value is a block or `( )` body (`};`, `);`). Statement blocks (`if`, `while`, `for`, `match`, `scope`, bare `{ }`) take no `;`. Blocks use `{ }`. Class, module, interface, and enum bodies use `( )`.

## Bindings

<!-- example: check -->
```neura
let count: int32 = 0;          // mutable, annotated
let total = 5;                 // type inferred: int32
const Limit: int32 = 3;        // cannot be reassigned
count = count + 1;
count += Limit;

let _ = total * 2;             // evaluate and discard; `_` can repeat
let _ = 4;
```

- A local binding needs an initializer (`a local binding requires an initializer`). Module, class, and file members may omit one (fields start uninitialized; see [Classes](#classes)).
- Duplicate names in one scope are errors. A nested block or function body may shadow an outer name; whether shadowing stays allowed is an open decision, so avoid relying on it.
- `const` fixes the binding. An object held in a `const` can still be mutated.

## Types at a glance

| Type | Notes |
| --- | --- |
| `bool` | `true`, `false`. No conversion to or from integers. |
| `int8` `int16` `int32` `int64` | Two's complement. `int` is `int32`. |
| `uint8` `uint16` `uint32` `uint64` | Unsigned. `byte` is `uint8`. |
| `float16` `bfloat16` `float32` `float64` | IEEE 754 (bfloat16 is binary32's exponent with a 7-bit fraction). `float` is `float32`. |
| `string` | Immutable UTF-8 text. |
| `T[]`, `T[list]` | Fixed-length array, growable list. Nest suffixes: `int[][list]` is a list of int arrays. |
| `[T1, T2]` | Tuple. |
| `func(A, B) -> R` | Function type. Parenthesize before a suffix: `(func(int) -> int)[]`. |
| `class` | Umbrella type for any class instance. |
| `&T` | Non-null alias to a primitive's storage ([Primitive references](#primitive-references)). |
| `void` | No value. |

Numbers in detail:

- Arithmetic operands have the same type. A literal takes the other operand's type, so `big + 1` works when `big` is `int64`; `big + small` with `small: int32` fails with `expected int64, got int32`.
- `+ - * / %` on integers produce the exact result or **trap** (overflow, division by zero, `MIN / -1`). Division truncates toward zero; `%` takes the dividend's sign: `-7 / 2` is `-3`, `-7 % 2` is `-1`.
- Floats never trap: overflow gives infinity, invalid operations give NaN. There is no `%` on floats. `x != x` is true exactly when `x` is NaN.
- `<<`, `>>` shift by any integer type; a count outside `0 .. width` traps. `>>` is arithmetic on signed types. `~ & | ^` are bitwise.
- `value as Type` converts explicitly. Integer to integer traps when out of range (including negative to unsigned). Float to integer truncates and traps on NaN, infinity, or out of range. Enum (fieldless) to integer gives the ordinal. No conversions to or from `bool`, and no integer-to-enum conversion.
- `as` binds tighter than `*` and looser than unary operators: `a * b as int64` converts only `b`. Parenthesize: `(a * b) as int64`.
- `value.to_bits()` and `float32.from_bits(bits)` reinterpret floats exactly.
- For wrapping, saturating, or checked arithmetic, call the companion module: `Std.Core.Int32.wrapping_add(a, b)`, `checked_mul` (returns `Optional`), `saturating_add`.

## Functions

<!-- example: check -->
```neura
let sum_to = func(limit: int32) -> int32 {
    let total: int32 = 0;
    let current: int32 = 1;
    while (current <= limit) {
        total = total + current;
        current = current + 1;
    }
    return total;
};

let twice = func(value: int) -> int => value * 2;   // expression body
let forty_two = func -> int => twice(21);           // no parameters: () optional
let reset = func {                                  // void: -> void optional
    // nothing to do
};
```

- Arguments are evaluated left to right. A non-`void` function must return on every path.
- Recursion works through a named binding (`let fact = func(...) { ... fact(n - 1) ... };`). A lambda bound inside a function cannot call itself (`unknown name`).

### Overloads

Join alternatives with `or func`. They share the name and visibility. The group is one declaration and ends with `;` after its final alternative.

<!-- example: check -->
```neura
let console = import:Std.Io.Console;

let write = func(text: string) {
    write(text, text.byte_length);
} or func(text: string, byte_count: int) {
    console.print_line(text.slice(0, byte_count));
};
```

Resolution filters by arity, then ranks each argument: exact type, then assignable (a more specific class wins), then binding a primitive reference, then a literal that fits, then one visible object cast. The unique best candidate wins; otherwise the call is ambiguous. Alternatives must differ in resolved parameter types (`int` and `int32` are the same type). Overloads cannot be generic themselves. Constructors (`init`) overload the same way.

### Variadic parameters

<!-- example: check -->
```neura
let count = func(items: ...int) -> int => items.length;   // items: int[]
let three = count(10, 20, 30);
let none = count();
```

The final parameter `name: ...T` packs zero or more arguments into a new `T[]`. A bare `name: ...` packs mixed types into `Std.Core.Variadic.Argument[]` (this is how `Console.format` takes any values).

### Closures and function values

<!-- example: check -->
```neura
let make_adder = func(offset: int) -> func(int) -> int {
    return func(value: int) -> int {
        return value + offset;
    };
};

let add_two = make_adder(2);
let result = add_two(40);

let apply = func(f: func(int) -> int, value: int) -> int => f(value);
let tripled = apply(func(x: int) -> int => x * 3, 5);
let handlers: (func(int) -> int)[] = [add_two, func(x: int) -> int => x - 1];
```

- A lambda captures the *values* of enclosing locals and parameters when it is created, and cannot reassign them. Captured objects and collections are shared, so their contents can change. Module and file globals are read when the body runs.
- A named non-generic function, and a method read as a value (`object.method`), can be used as a function value; the method keeps its receiver and virtual dispatch.
- Lambda parameters are always typed. There is no shorthand like `x => x + 1`.

## Control flow and loops

<!-- example: check -->
```neura
let console = import:Std.Io.Console;

let walk = func(limit: int32, names: string[list]) -> int32 {
    let total: int32 = 0;
    let current: int32 = 0;

    if (limit < 0) {
        return 0;
    } else if (limit == 0) {
        return 1;
    } else {
        total = 1;
    }

    do {                                   // runs at least once
        total += current++;
    } while (current <= limit);

    for (let i: int32 = 0; i < limit; i++) {
        if (i == 2) {
            continue;
        }
        total += i;
    }

    for (let name in names) {              // arrays, lists, any Iterable<T>
        console.print_line(name);
    }

    for (const index in 0 .. 3) {          // const element binding
        total += index;
    }

    while (true) {
        break;
    }

    do {                                   // no trailing while: loops forever
        break;
    }
    return total;
};
```

- Conditions must be `bool`. Bodies always need braces (`if (x) return 1;` fails with `expected '{'`).
- A `while` that directly follows a bare `do { ... }` block is parsed as that block's condition (`do { } while (c);`). Put a `while` loop before an infinite `do`, or separate them with another statement.
- Three-part `for`: the initializer may declare a binding visible in the loop only; `for (;;)` loops forever. The step cannot contain `break`, `continue`, or `return`.
- For-each evaluates the source once. It accepts an array, a list, or any `Std.Collections.Iterable<T>` (every standard collection). The element binding cannot destructure: write `for (let entry in map) { entry[0] ... }` for a map's `[K, V]` tuples.
- Appending to or removing from a list while a for-each visits it traps (reason 5), even through another alias. Assigning elements by index is fine.
- `cond ? a : b` evaluates only the chosen arm and associates to the right.

## Operators

Precedence, tightest first:

| Level | Operators |
| --- | --- |
| 1 | member access `.`, indexing `[]`, calls `()` |
| 2 | postfix `++` `--` |
| 3 | prefix `++` `--` `!` `-` `~` |
| 4 | `as`, `is` |
| 5 | `*` `/` `%` |
| 6 | `+` `-` |
| 7 | `<<` `>>` |
| 8 | `&` |
| 9 | `^` |
| 10 | `\|` |
| 11 | `<` `<=` `>` `>=` |
| 12 | `==` `!=` `===` |
| 13 | `&&` |
| 14 | `\|\|` |
| 15 | `..` with optional `: step` |
| 16 | `? :` |

- Assignments are statements, not expressions: `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `^=`, `|=`, `<<=`, `>>=`. A compound assignment evaluates the target's receiver and index once.
- `++x` yields the new value, `x++` the old one. They need a mutable numeric binding, field, or element.
- `&&` and `||` short-circuit. `+` and `+=` also concatenate strings (both sides must be `string`).

## Equality: `==` versus `===`

| Expression | Meaning |
| --- | --- |
| `a == b`, `a != b` | Numbers and booleans by value; strings by bytes; fieldless enums by variant; **class and interface references by identity** (two `nil`s are equal). Not available on payload enums. |
| `a === b` | Calls `a.equals(b)` with normal method selection and dispatch, and requires a `bool` result. Built-in values (numbers, strings, tuples, arrays, lists) have value-semantics `equals`; classes use identity unless they override `equals`. |

<!-- example: check -->
```neura
let Point = class (
    export let x: int = 0;
    export let y: int = 0;

    export let init = func(px: int, py: int) {
        x = px;
        y = py;
    };

    export let.override equals = func(other: class) -> bool {
        if (other is Point) {
            let point = other as Point;
            return x == point.x && y == point.y;
        }
        return false;
    };

    export let.override hash_code = func -> uint32 => (x * 31 + y) as uint32;
);

let a = new Point(1, 2);
let b = new Point(1, 2);
let same_object = a == b;      // false: different instances
let same_value = a === b;      // true: Point.equals
let lists_equal = [1, 2] === [1, 2];
let text_equal = "ab" === "ab";
```

Override `equals(other: class) -> bool`, `hash_code() -> uint32`, and `to_string() -> string` together; `equals` must return false for `nil` and agree with `hash_code`. The standard library's hash maps and sets use these.

## Comparison chains

One subject compared against several alternatives, evaluated once:

<!-- example: check -->
```neura
let classify = func(x: int, y: int) -> string {
    if (x == 0 or 1 or 2) {             // x equals any of them
        return "small";
    }
    if (x != 3 and 4) {                 // x differs from both
        return "not three or four";
    }
    if (x == y + 1 or y - 1 && y > 0) {  // alternatives can be arithmetic
        return "neighbor";
    }
    return "other";
};
```

`or` chains stop at the first true comparison; `and` chains at the first false. A chain uses one connector. The chain binds before `&&`/`||`, so the last example is `(x == y + 1 or y - 1) && y > 0`. Parenthesize when in doubt, and never use `and`/`or` as Boolean operators.

## Ranges

<!-- example: check -->
```neura
let numbers = {0 .. 5};                 // int[]: 0, 1, 2, 3, 4
let evens: int[list] = 0 .. 8 : 2;      // list: 0, 2, 4, 6
let descending = 5 .. 0 : -2;           // 5, 3, 1
let wide: int64[] = 0 .. 3;             // the expected type picks the element type
let total = func -> int {
    let sum = 0;
    for (let i in 1 .. 4) {
        sum += i;
    }
    return sum;
};
```

`start .. stop` excludes `stop`. Without an expected type the result is an array whose element type comes from `start`. A zero step traps. Group a range with `{ }` or `( )` before indexing it or inside a conditional's middle arm.

## Collections

<!-- example: check -->
```neura
let names: string[] = ["Ada", "Lin"];          // array: fixed length
let queue: string[list] = ["first", "second"]; // list: grows and shrinks
let empty: string[list] = [];                  // empty literal needs a type
let rows: string[][] = [["a", "b"], ["c"]];    // jagged

let edit = func {
    names[0] = "Grace";                 // replace an element
    queue.append("third");
    queue.remove_at(0);                 // returns nothing; shifts later elements left
    let size = queue.length;            // int
    let version = queue.version;        // uint64, bumps on append/remove_at
};
```

- Indexing is zero-based and always bounds-checked; a bad index ends the run (status 139).
- Arrays never change length. Lists support only `append`, `remove_at`, `length`, `version`, indexing, and for-each as built-ins. `remove_at` returns nothing: read the element first if you need it. Insertions, sorting, searching, and copies are in `Std.Collections.Lists`, `Std.Collections.Sorting`, and the collection classes (`ArrayList`, `HashMap`, ...); see [stdlib.md](stdlib.md).
- `Std.Collections.Lists.from_array<T>(array)` converts an array to a list; `Lists.filled<T>(count, value)` makes a list of a runtime length.
- Collections are reference values: assigning one to another binding aliases it.

## Strings

<!-- example: check -->
```neura
let text = "héllo, world";
let bytes = text.byte_length;          // 13: UTF-8 bytes
let first = text.byte_at(0);           // uint8
let hello = text.slice(0, 6);          // byte offsets on character boundaries
let rest = text.substring(8);          // companion function, see below
let parts = text.split(", ");          // string[list]
let shout = text.to_upper_case();
let has = text.contains("world");
let trimmed = "  x ".trim();
let number = 42.to_string();
let joined = "n=" + number;
let [ok, decoded] = string.from_utf8([104 as uint8, 105 as uint8]);
```

- `string` has a few built-in members (`byte_length`, `byte_at`, `slice`) and many more through the `Std.Core.String` companion (`substring`, `split`, `contains`, `starts_with`, `trim`, `replace`, `to_upper_case`, `lines`, `matches`, `formatted`, ...). See [Primitive companions](#primitive-companions).
- Strings are immutable. Build large text with `Std.Core.StringBuilder` (`builders.create().append("a").append_int(3).to_string()`).
- Equality and ordering are bytewise.

## Tuples and destructuring

<!-- example: check -->
```neura
let measure = func -> [int, float64] {
    return [42, 0.5];
};

let [count, weight] = measure();
const [label, _] = ["answer", count];   // `_` skips an element
let pair: [string, float64] = [label, weight];
let description = pair[0];              // index with an integer literal
```

Tuple elements are immutable. A bracket literal is a tuple when the expected type is a tuple or it is on the right of a destructuring declaration; otherwise it is an array.

## Enums and `match`

<!-- example: check -->
```neura
export let Light = enum (Red, Yellow, Green);           // fieldless

let Optional = enum<T> (None, Some(T));                 // generic
let IntOptional = Optional<int>;                        // overlay of an instance

let Tree = enum (Empty, Leaf(int), Branch { left: Tree, right: Tree });  // payloads

let next = func(current: Light) -> Light {
    if (current == Light.Red) {                         // fieldless enums support ==
        return Light.Green;
    }
    return Light.Red;
};

let ordinal = Light.Yellow as int;                      // 1

let sum = func(tree: Tree) -> int {
    return match (tree) {                               // match expression
        Tree.Empty => 0,
        Tree.Leaf(value) => value,
        Tree.Branch { left, right } => sum(left) + sum(right),
    };
};

let describe = func(tree: Tree) -> string {
    match (tree) {                                      // match statement: block arms
        Tree.Leaf(value) => {
            return "leaf " + value.to_string();
        },
        Tree.Branch { left: l, right: _ } => {          // rename and ignore fields
            return "branch";
        },
        _ => {
            return "empty";
        },
    }
};

let tree = Tree.Branch { right: Tree.Leaf(2), left: Tree.Leaf(1) };
let maybe: IntOptional = IntOptional.Some(4);
let five = Optional<int>.Some(5);                       // generic constructor needs <int>
```

- Variants are always qualified by the enum: `Light.Red`, never bare `Red` (unless promoted from another module).
- `match` evaluates the subject once and runs one arm. Arms are comma-separated; a trailing comma is allowed. Every variant must be covered, or the last arm is `_`. Error: `non-exhaustive match, missing variant 'B'`.
- Positional payload patterns list every field (`Leaf(value)`, `_` to ignore); named payloads use `field`, `field: binding`, or `field: _`.
- No nested patterns (`Some(Leaf(x))` is a syntax error) and no guards (`pattern if cond`). Bind, then `match` or `if` again.
- Pattern bindings are constants scoped to their arm.
- Payload enums have no built-in `==` and no `as` conversion.

## Classes

<!-- example: check -->
```neura
let LogEntry = class (
    export let level: string = "INFO";        // field with initializer
    export let message: string;               // uninitialized until stored
    let sequence: int = 0;                    // private to the class

    export let init = func(text: string) {    // constructor
        message = text;
    } or func(text: string, entry_level: string) {
        message = text;
        level = entry_level;
    };

    export let format = func -> string {
        return "[" + level + "] " + message;
    };

    export let renumber = func(value: int) -> LogEntry {
        instance.sequence = value;            // `instance` is the receiver
        return instance;
    };
);

let entry = new LogEntry("started");
let warning = new LogEntry("disk", "WARN");
let line = warning.format();
```

- Every member in a class body ends with `;`, including a method whose block body ends at `}` (`};`). Fields and methods are private unless `export`ed.
- Inside methods, fields and methods are in scope unqualified; `instance` is the receiver when no other binding has that name.
- A class without `init` gets a zero-argument constructor (if its base can be built with none), and code can set exported fields after `new`.
- Reading a field before anything stored to it traps at run time (`read of an uninitialized field`).
- `new` with the wrong arguments: `expected 2 argument(s)`. Several standard library classes have private constructors (a `Seal` parameter): use their module's `create`/`of` functions.

## Static members and singletons

<!-- example: check -->
```neura
let Limits = class (
    export static const Max = 100;
    export static let calls: int;                      // defaults to 0

    export static let record = func -> int => ++calls;

    export static let Label = class (                  // nested type
        export let text = "limits";
    );
);

let first = Limits.record();
let label = new Limits.Label();
let cap = Limits.Max;

export let counter = singleton (                       // exactly one instance
    let index = 0;

    export let next = func -> int => index++;
);

let ticket = counter.next();
```

- Static fields belong to the declaring type and initialize on first active use (construction, static call, static field write, or reading a runtime-initialized static). Numeric and Boolean statics default to zero/false, class references to `nil`.
- Static functions cannot use instance members without an explicit instance, and a generic class's statics cannot use its type parameters.
- A `singleton` is constructed before `main`, cannot be subclassed, built with `new`, or destroyed, and may implement interfaces. Use it as both a value and a type name.

## Inheritance and abstract classes

<!-- example: check -->
```neura
let Renderable = class (
    export let render = func -> string;               // bodyless: abstract requirement

    export let framed = func -> string {
        return "[" + render() + "]";                  // dispatches to the override
    };
);

let Text = class:Renderable (                         // `class:Base`
    export let value: string = "";

    export let.override render = func -> string {
        return value;
    };
);

let Alert = class:Text (
    export let init = func(text: string) {
        super.init();                                 // first statement
        value = text;
    };

    export let.override render = func -> string {
        return "Alert: " + value;
    };
);

let display = func(item: Renderable) -> string => item.framed();
let shown = display(new Alert("Attention"));          // "[Alert: Attention]"
```

- One base class; any number of interfaces (`class:Base implements A, B ( ... )`).
- An override must match the inherited parameter and result types exactly and uses `let.override` / `const.override`.
- A class with an unimplemented bodyless method is abstract and cannot be instantiated; concrete subclasses implement every requirement.
- The base class must be non-generic. Generic methods cannot override or be overridden.

## Interfaces

<!-- example: check -->
```neura
let Comparable = interface<T> (
    let compare_to = func(other: T) -> int;
);

let Named = interface (
    static const Unknown = "unknown";                 // interface constants are static
    let name = func -> string;
    let greeting = func -> string => "Hello, " + name();   // default body
);

let Labeled = interface:Named (                       // interface inheritance
    let label = func -> string => "<" + name() + ">";
);

let Rank = class implements Comparable<Rank>, Labeled (
    export let value: int = 0;
    export let title: string = Named.Unknown;

    export let compare_to = func(other: Rank) -> int {
        return value - other.value;
    };

    export let name = func -> string => title;        // must be exported
);

let best = func<T: Comparable<T>>(a: T, b: T) -> T => a.compare_to(b) >= 0 ? a : b;

let pick = func -> string {
    let low = new Rank();
    let high = new Rank();
    high.value = 5;
    high.title = "five";
    let shown: Labeled = best<Rank>(low, high);
    return shown.label() + " " + shown.greeting();
};
```

- Interface members are written with plain `let` (no `export`), as signatures or with default bodies. The implementing class **must `export`** each member it supplies.
- A class method with a matching signature beats an interface default. Two independent defaults for the same method require an explicit override in the class.
- Interfaces work as types of bindings, parameters, results, collection elements, and bounds. Calls dispatch on the concrete class. `x is Interface` tests a reference.
- Leaving a requirement bodyless in the class makes the class abstract.
- For the module-level counterpart (a module that declares required functions and another module that implements them), see [modules.md](modules.md#module-interfaces-and-inheritance).

## Generics

<!-- example: check -->
```neura
let identity = func<T>(value: T) -> T {
    return value;
};
let name: string = identity<string>("Neura");          // type arguments are required

let Pair = class<A, B> (
    export let first: A;
    export let second: B;
);

let Box = class<T> (
    export let value: T;

    export let pair = func<U>(other: U) -> Pair<T, U> {  // generic method
        let result = new Pair<T, U>();
        result.first = value;
        result.second = other;
        return result;
    };
);

let Shape = interface (
    let area = func -> float64;
);

let largest = func<T: Shape>(items: T[list]) -> float64 {   // bound
    let best = 0.0;
    for (let item in items) {
        if (item.area() > best) {
            best = item.area();
        }
    }
    return best;
};

let box = new Box<int>();
```

- Type arguments are never inferred: `identity(7)` fails with `expected 1 type argument(s)`.
- Bounds: `<T: Shape>`, several parameters `<K, V>`, intersections `<T: [Comparable<T>, Iterable<int>]>`. A bound gives the body access to the bound's interface.
- An unbounded `T` supports only what every type supports (assignment, passing, `equals` via `===`, `to_string`, `hash_code`). `new T()` is invalid.
- Every instantiation is specialized at build time. `Box<Alert>` is not a subtype of `Box<Renderable>` (no variance).
- Primitive references cannot be type arguments.

## The umbrella `class` type, `nil`, `is`, and `as`

<!-- example: check -->
```neura
let LogEntry = class (
    export let message: string = "";
);

let entry: class = new LogEntry();       // any instance fits `class`
let missing: LogEntry = nil;             // nil: absent class/interface reference

let use = func {
    if (entry is LogEntry) {             // runtime type test; nil gives false
        let exact = entry as LogEntry;   // checked cast; traps if incompatible
        exact.message = "ready";
    }
    if (missing == nil) {
        missing = new LogEntry();
    }
};
```

- `nil` exists only for class and interface types (and type parameters bounded by them). For other values use `Std.Core.Optional<T>`.
- Field access, method calls, or `destroy` on `nil` trap (status 139).
- Every class has `equals(other: class) -> bool`, `hash_code() -> uint32`, and `to_string() -> string`; the defaults use identity and a name such as `Shapes.Circle#3`.

## Object casts

A class can declare conversions to other types with `let.as Target = func -> Target`:

<!-- example: check -->
```neura
let Reading = class (
    let value: int16 = 0;

    export let init = func(initial: int16) {
        instance.value = initial;
    };

    export let.as int16 = func -> int16 => value
        or as int64 = func -> int64 => value as int64;

    export let.as string = func -> string => value.to_string();
);

let accept = func(value: int64) -> int64 => value;
let reading = new Reading(7);
let explicit = reading as int64;       // explicit conversion
let implicit = accept(reading);        // one cast may apply to an argument
let text = reading as string;
```

Arguments may use one visible cast implicitly; assignments and returns need explicit `as`. `is` always tests the reference type, never a cast.

## Primitive references

`&T` (for `bool`, integers, floats, `string`) is a non-null alias to mutable storage:

<!-- example: check -->
```neura
let increment = func(value: &int) {
    value += 1;                        // writes through to the caller's variable
};

let count: int = 10;
let alias: &int = count;
increment(count);                      // pass the variable itself
increment(alias);
alias = 20;                            // count is now 20
```

The argument must be a mutable place (local, parameter, global, field) or another reference of the same type; literals, temporaries, collection elements, and `const` bindings are rejected. Reference declarations need initializers. A function may return `&T` to a local; the storage stays alive.

## Primitive companions

A primitive value gets the functions of its standard library companion module (`Std.Core.Int32` for `int32`, `Std.Core.String` for `string`, `Std.Core.Float64`, `Std.Core.Bool`, ...) as members when the function's first parameter has that type:

<!-- example: check -->
```neura
let score: int16 = -7;
let magnitude = score.abs();                 // Std.Core.Int16.abs(score)
let parsed = int32.parse("42");              // Result<int32>, type-qualified call
let biggest = int64.max(3, 9);
let suffix = "préfixe".substring(4);
let padded = 7.to_string().pad_start(3, "0");
```

No import is needed for this member syntax. Import the module when you need its constants or want qualified calls: `let int32s = import:Std.Core.Int32;` then `int32s.Max`.

## Object lifecycle

<!-- example: check -->
```neura
let Resource = class (
    export let name: string;

    export let init = func(label: string) {
        name = label;
    };

    let deinit = func {                    // runs on `destroy`, most-derived first
        // release what this class owns
    };
);

let use = func {
    let handle = new Resource("cache");
    destroy handle;                        // ends the object's life for every alias
};
```

- Memory is garbage collected (cycles included). The collector never runs `deinit`; only `destroy object;` does. Use `destroy` (or a resource's `close()`) when cleanup has visible effects.
- Using a destroyed object traps (`invalid reference handle`). Singletons cannot be destroyed.

## Type overlays

A type definition gives an existing type another name. It is an alias, not a new type:

<!-- example: check -->
```neura
let Types = module (
    export let UserId = int32;
    export let DisplayName = string;
    export let default_user_id: UserId = 1;

    export let next_user_id = func(user_id: UserId) -> UserId {
        return user_id + 1;
    };
);
```

## Documentation comments

<!-- example: check -->
```neura
/// @brief Doubles an integer.
/// @param value The input to double.
/// @return The result of adding the input to itself.
let twice = func(value: int) -> int => value + value;
```

`///` lines directly above a declaration form its documentation (Doxygen-style `@param`, `@return` are recognized). A blank line or `//` comment ends the block. `neura docs file.nra` exports the documentation as JSON; `neura docs --doxygen` is a Doxygen input filter.
