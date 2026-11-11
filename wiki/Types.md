<!-- Generated from docs/lang/Types.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Types

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

Types describe values used in declarations, parameters, and return values.
Bindings and function parameters put the type after the name.

<!-- example: check -->
```neura
let count: int32 = 1;
let advance = func(count: int32) -> int32 {
    return count + 1;
};
```

## Built-in types

The built-in types are listed below. [Numbers](Numbers) specifies the numeric types' representation, literals, arithmetic, overflow, rounding, and conversions.

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

Arrays and lists are built-in collection types, written `string[]` and `string[list]` respectively. Arrays have a fixed length; lists can grow and shrink. Suffixes can be combined for nested collections. See [Arrays and lists](Collections) for their syntax and safety rules.

### Text primitives

String equality compares UTF-8 bytes. `text.byte_length` returns the byte count as `int` and `text.byte_at(index)` returns a checked `uint8`. `text.slice(start, end)` accepts a half-open range with endpoints on UTF-8 character boundaries. `string.from_utf8(bytes)` copies a `uint8[]` or `uint8[list]` and returns `[bool, string]`. Invalid input returns `[false, ""]`. Valid empty input returns `[true, ""]`.

### Primitive companions

Booleans, sized integers and floats, and strings have [standard library companions](Stdlib-Design#7-primitive-companion-modules). A primitive-qualified call such as `int32.parse("42")` looks up an exported function in that companion. A call on a value passes it as the first argument to a function whose first parameter has the same primitive type.

<!-- example: check -->
```neura
let score: int16 = -7;
let magnitude = score.abs();
let suffix = "préfixe".substring(4);
```

Aliases use the companion of their resolved type. Built-in members take precedence. The compiler resolves the companion from the trusted standard library on first use and emits an ordinary module call with primitive values.

### Primitive references

Prefix a Boolean, integer, float, or string type with `&` to declare a non-null alias to mutable storage of that exact type. An ordinary read through the alias yields the stored primitive value. Assigning through the alias changes the stored value and leaves the alias pointing at the same storage.

<!-- example: check -->
```neura
let increment = func(value: &int) {
    value += 1;
};

let count: int = 10;
let alias: &int = count;
increment(alias);
alias = 20;
// count is now 20.
```

A reference initializer, argument, or return takes a mutable place or an existing reference to the same resolved primitive type. Mutable locals, ordinary parameters, globals, static fields, and instance fields can supply storage. Literals, temporary values, collection elements, constant primitive bindings, and `nil` are invalid in this context. Use an explicit reference type when a declaration must preserve an alias. A `const` reference binding still writes through to its mutable referent because the alias itself is fixed.

Local and global reference declarations require initializers. Static reference fields are unavailable. An instance reference field can bind in its declaration or in its constructor. For a field without a declaration initializer, the first assignment in the constructor binds it to storage. Each successful constructor path must establish that binding before returning. Later assignments write through the established reference.

A constructor can bind a field in separate branches. Binding for the first time inside a loop body or step is rejected, as is an assignment reached with the field bound on only some paths. Direct reads of a field before binding are errors.

A reference keeps its storage alive after the original binding leaves scope. A function declared to return `&T` can return a reference to a mutable local. A caller can keep that alias in a reference binding. The storage of a referenced field also survives destruction of its containing object. Reading a referent before a primitive value has been stored still traps. Primitive references are unavailable as generic type arguments. See [primitive-references.nra](https://github.com/Kayyo321/neura/blob/main/examples/types/primitive-references.nra).

## Tuples

A tuple has one or more ordered elements, each with its own static type. Write
`[T1, T2, ...]` in a type position and `[value1, value2, ...]` where a tuple is
expected. Tuple types may appear in bindings, fields, function signatures, type
arguments, and collection element types.

<!-- example: check -->
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

### Typed and listed destructuring

A destructuring declaration can annotate its names with a tuple type, and it
can give each name its own value:

```text
destructuring := ("let" | "const") "[" names "]" [":" tuple-type] "=" values ";"
names         := name ("," name)* [","]
values        := expression ("," expression)*
```

With one value, that value is the tuple to destructure, as above; an
annotation checks it, and a bracket literal takes the annotation as its
expected type. With two or more values, there is one value for each name, in
order, and each value is lowered with its element of the annotation as its
expected type, so a value that needs a type, such as `new` without a class
([Object lifecycle](ObjectLifecycle#target-typed-new)), takes it from the
annotation. Without an annotation each name has its value's type. The values
are evaluated from left to right before any name is bound, so no name is in
scope in the values. A count that differs from the number of names is
[E0436](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0436.md), and an annotation that is not a tuple type is
[E0437](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0437.md). The declaration is a statement in a block or
at file level, where destructuring is allowed; a module or class body declares
its members one at a time.

<!-- example: check -->
```neura
let Point = class (
    export let x: int = 0;
    export let init = func() {
    } or func(x0: int) {
        x = x0;
    };
);

let [width, height] = 640, 480;
const [origin, corner]: [Point, Point] = new, new(width);
let [label, scale]: [string, float64] = "zoom", 2.0;
```

## Type overlays

A type definition can give an existing type another name. These overlays describe the role of a value without introducing a separate representation or a distinct type.

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

Overlays can refer to other overlays. See `examples/types/overlay.nra` for more examples.

## Classes

A class defines a type with fields and methods. Mark members `export` to expose them and use `new` to create an instance.
Inside an instance method, constructor, or field initializer, `instance` names the current receiver if no ordinary binding supplies that name.

<!-- example: check -->
```neura
let LogEntry = class (
    export let level: string;
    export let message: string;

    export let format = func() -> string {
        return "[" + level + "] " + message;
    };
);

let entry = new LogEntry();
entry.level = "INFO";
```

### Class inheritance

Class inheritance parallels module inheritance: put a colon and the base class after `class`.

<!-- example: check -->
```neura
let LogEntry = class (
    export let level: string;
    export let message: string;
);

let NamedEntry = class:LogEntry (
    export let name: string;
);
```

The derived class extends the base class's interface. A derived instance can be passed where its base type is expected, and an overridden method dispatches to the implementation for the actual instance. Declare an override with `let.override` or `const.override`. See [Polymorphism](Polymorphism) for interfaces and generics and [Object lifecycle](ObjectLifecycle) for construction and cleanup.

### Static members

Add `static` before `let` or `const` in a class or interface. Static fields
belong to the declaring type. Static functions and nested types do not need
an enclosing instance.

<!-- example: check -->
```neura
let Limits = class (
    export static const Max = 100;
    export static let calls: int;

    export static let record = func -> int => ++calls;

    export static let Label = class (
        export let text = "limits";
    );
);

let first = Limits.record();
let label = new Limits.Label();
```

Access a static member through the class or interface name, or by its
unqualified name in scope. An instance expression can also qualify a
static field or a class's static function. The expression is evaluated once
and its result is discarded, even when the result is `nil`. A derived class
inherits exported static members. It can hide a static field with another
field, or hide a static function with another static function that has the
same parameter types and a compatible result type. A static function does
not implement an interface's instance requirement. An interface's static
fields are constants, and its static functions belong only to that interface.

A static function needs a body. It may use the surrounding class's static
members, and needs an explicit instance to access instance members. A
generic class shares its static members across all type arguments, so they
cannot use the enclosing class's type parameters. Mutable numeric and Boolean
fields may omit an initializer and default to zero and false. Mutable class
and interface references may omit an initializer and default to `nil`.
Other static fields and all static constants require an initializer.

#### Static initialization

A class or interface initializes once per execution on its first active use.
Constructing a class, invoking a static function, assigning a static field,
or reading a static field with a runtime initializer or a mutable binding
are active uses. Access to an inherited static member initializes the type
that declares it. A read of a compile-time constant, a type annotation, a
type test, and a checked cast are passive. Taking a static function as a
value delays the active use until that function is invoked.

A static `const` of numeric, Boolean, or string type is a compile-time
constant when its initializer can be resolved from literals, other
compile-time static constants, and scalar expressions whose evaluation is
known to complete without a trap. Const bindings initialized by calls
follow the same first-use rule as other runtime initializers.

Initialization runs before evaluating constructor arguments. For a static
call or simple assignment, any qualifier and the arguments or right operand
are evaluated first. A compound assignment or increment initializes the
declaring type when it reads the old field value.

A class first initializes its base class. It then initializes each interface
in its inheritance tree that declares an instance method with a default
body, visiting interface parents before their children in declaration order.
An interface's own initialization leaves its parents untouched. The
declaring type then evaluates its static field initializers in source order.
Generic type applications share the initialization state of their
declaration.

On reentry, the active initializer continues without running a second time.
Reads can observe an earlier store or a field's default while its initializer
is pending. Numeric fields default to zero, Boolean fields to false, and
class and interface references to `nil`. A pending field of another type
faults on a read until it receives a value. A trap during initialization
ends the execution.

Initialization runs on the root task, the task that runs `main`. A spawned
call that could run a static initializer is therefore not
[task-safe](#task-safe-calls): the compiler rejects it at the `spawn`. That
covers constructing a class, calling a static function, and reading or
assigning a static field of any type that has a run-time static initializer,
or that inherits one from a base class or an interface with a default
method. A type whose static fields are all compile-time constants has
nothing to initialize, so a spawned call can use it. The rule follows the
call graph and not the state of the run, so initializing the type earlier on
the root task does not make the call acceptable; pass the value the task
needs as an argument instead.

### Singletons

A `singleton` declares a class with one instance. A singleton binding
names that instance in expressions and its type in annotations. The program
initializes the instance before entry, and every alias refers to the same
state.

<!-- example: check -->
```neura
export let counter = singleton (
    let index = 0;

    export let next = func -> int => index++;
);
```

Singletons can be declared at file scope, in modules, or as static class
members. Construction of a nested singleton at startup follows its
declaration position. Reading that singleton alone is a passive use of the
enclosing class. A singleton may implement interfaces. It cannot be
subclassed, constructed with `new`, or destroyed. See
[Object lifecycle](ObjectLifecycle#singleton-lifecycle) for its
initialization rules.

### Umbrella class and nil

`class` is a type in annotation, parameter, result, and type argument positions. Every class instance can be stored in it. An interface reference can also be passed as `class`. The runtime keeps the concrete identity and dispatches methods to the instance's implementation.

<!-- example: check -->
```neura
let LogEntry = class (
    export let level: string;
    export let message: string;
);

let entry: class = new LogEntry();
let missing: LogEntry = nil;

if (entry is LogEntry) {
    let exact = entry as LogEntry;
    exact.message = "ready";
}
```

- `nil` is an explicit absent reference for class and interface types, including type parameters bounded by them. With no contextual type, it has type `class`. An instance field without an initializer starts uninitialized. Use `= nil` to initialize it to absence.
- `==` and `!=` compare references by identity. Two nil references compare equal. Use `is` to test a live reference against a class or interface. A nil reference gives false. A checked reference cast with `as` preserves nil and traps for an incompatible instance.
- Reading or writing a field, invoking an instance method, or explicitly destroying a nil reference traps. It has no heap allocation and tracing skips it.
- `class` supplies `equals(other: class) -> bool`, `hash_code() -> uint32`, and `to_string() -> string`. A class can override those signatures. The defaults use identity and a qualified class name with an identity number, such as `Acme.Shapes.Circle#3`. The runtime assigns that number at first use by default hashing or formatting. An equality override must agree with its hash and return false for nil.
- The built-in value members of scalars, tuples, arrays, and lists use value semantics. Generic and aggregate value operations treat nil as a distinct value with hash zero and text `nil`. A direct method call on a nil class reference follows the receiver trap rule.

### Object casts

A class can declare a zero-argument conversion method with `let.as Target = func -> Target`. Each target appears at most once per class and must exactly match the method's declared return type. `or as` gives another conversion the same visibility. Separate declarations can choose visibility independently.

<!-- example: check -->
```neura
let Reading = class (
    let value: int16 = 0;

    export let init = func(initial: int16) {
        instance.value = initial;
    };

    export let.as int16 = func -> int16 => value
        or as int64 = func -> int64 => value as int64;
);

let accept = func(value: int64) -> int64 => value;
let reading = new Reading(7);
let explicit = reading as int64;
let implicit = accept(reading);
```

`as` looks for a visible conversion to the exact target on the source's static class, then on its base classes. When none matches, the usual numeric or checked reference cast rules apply. `is` always tests the reference type. Calling a conversion on a nil receiver traps like any other instance method.

Function arguments can use one visible conversion when their type is otherwise incompatible with the parameter. Assignments and returns require ordinary assignability or an explicit `as`. A conversion from `Reading` to `int64` therefore cannot satisfy an `int32` parameter. An unexported cast is only available within its declaring class.

### Accessors

An exported instance field can have a getter and a setter. Each is an accessor clause written after the field's `;`, indented under the field. Code outside the class still reads and writes the field as a member: `my_data.data` calls the getter and `my_data.data = text` calls the setter. There is no call syntax and no visible accessor method.

<!-- example: check -->
```neura
let failure = import:Std.Core.Failure;

let MyData = class (
    export let data: string = "";
        | set: data = value;
        | get: data;

    export let code = "XY729H08";
        | get: code;

    export let non_zero = 18000;
        | get: non_zero;
        | set: {
            if (value == 0) {
                failure.trap("non_zero must not be zero");
            }
            non_zero = value;
        };
);

let my_data = new MyData();
my_data.data = "payload";
let my_var = my_data.data;
my_data.non_zero += 1;
```

```text
field     = "export" ("let" | "const" | "last") name [":" type] ["=" expression] ";" { accessor }
accessor  = "|" "get" ":" (expression | block) ";"
          | "|" "set" ":" (simple-statement | block) ";"
```

- `get`, `set`, and `value` are contextual. They have this meaning only in an accessor clause and stay ordinary identifiers everywhere else.
- A field has at most one `get` and one `set` clause, in either order. Every clause ends with `;`, including one whose body is a block: `| set: { ... };`.
- A getter's expression has the field's type. A block getter returns a value of that type on every path or traps.
- In a setter, `value` names the incoming value, which has the field's type. The body is one simple statement, such as an assignment or a call, or a block. A setter that never assigns its field is accepted with the warning `setter-never-stores` ([W0005](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/W0005.md)), which `#Allow` and `#Deny` on the field or an enclosing declaration control ([Tags](Tags#allow-and-deny)).
- A setter rejects a value with a trap such as `Std.Core.Failure.trap` ([Errors](Errors)). The trap ends the run.

**Inside and outside the class.** Within the declaring class (its methods, constructors, field initializers, the accessor bodies, and closures in them), a bare field name and `instance.data` name the field's storage. That is why `data = value` in a setter stores the value instead of calling the setter again, and why a constructor can write `instance.data = data`. Every other access calls the accessors:

- an explicit receiver other than `instance`, such as `other.data`, even inside the declaring class;
- all code outside the class;
- a subclass, including its bare name of the inherited field.

A field with only `get` is read-only outside its class: assigning it there, or updating it with a compound assignment, `++`, or `--`, is error `E0510`. A field with only `set` is write-only outside its class: reading or updating it there is error `E0515`. Inside the declaring class, an access whose accessor is missing uses the storage. A field without accessors behaves as before.

**Evaluation order.** `object.field = expression` evaluates the receiver, then the expression, then calls the setter. A compound assignment such as `object.field += expression`, and `++` and `--` in prefix or postfix form, evaluate the receiver once, call the getter, evaluate any right operand, compute the new value with the field's type, and call the setter. Checked arithmetic traps before the setter runs. A postfix update yields the getter's value and a prefix update the new value, as for any variable ([Numbers](Numbers)). In a chain such as `holder.counter.count = 5`, `holder.counter` is a read through its getter and `count` is then set on the result.

**Places.** A primitive reference cannot alias a field that is reached through its accessors. Passing `my_data.non_zero` to an `&int` parameter, or binding it to a reference, is error `E0524`. Copy the value into a local, pass the local, and assign the result back.

**Limits in this release.** Accessors resolve statically to the declaring class: they are not virtual and cannot be overridden, and a subclass cannot redeclare the field. Interfaces cannot require accessors. Accessors are allowed only on exported instance fields (`E0516`, `E0517`, `E0518`, `E0519`). A `const` or `last` field can have a getter but not a setter (`E0521`). A field named `value` cannot have a setter (`E0522`). A primitive reference field cannot have accessors (`E0523`). Calling an accessor through a nil reference traps like any instance method. A getter or setter call is an ordinary method call to the [task-safety](#task-safe-calls) analysis and the optimizer. In NeuraIR the clauses are the hidden methods `$get_<field>` and `$set_<field>`, linked from the field by the [1.10 attributes](NirFormat#revision-110-symbol-attributes-and-deferred-initialization) `accessor.get`, `accessor.set`, and `accessor.of`, so a program that imports the class from a `.nir` file uses the same accessors. See [accessors.nra](https://github.com/Kayyo321/neura/blob/main/examples/types/accessors.nra).

### Anonymous classes

A class body can be written wherever a type is expected: in the annotation of a binding or field, a parameter or result type, a tuple element, a collection element, and a type argument.

```text
anonymous-class := "class" [":" base] ["implements" interface ("," interface)*] "(" member* ")"
```

The members use the grammar of a class declaration's body, and each ends with `;`. In a type position, `class` begins a body only when it is followed by `(` and then `)` or a member (`export`, `static`, `let`, `const`, or `last` and a name, or the `#` of a member's tags), by `implements`, or by `: Base` and then `implements` or `(` and a member. Anything else after `class` leaves it the [umbrella type](#umbrella-class-and-nil), so `x as class`, `x is class`, `<T: class>`, and `c ? x as class : y` keep their meaning. A body with a base and no members, `class : Base ()`, is therefore not recognized; write a member, or use `Base`. After `new`, `class()` followed by anything but `(` is the umbrella type with no arguments, as before, and `new class()()` constructs the empty anonymous class. The rule against an annotation on a definition, `let X: T = class (...)` ([E0208](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0208.md)), is unrelated: an anonymous class is a type, not a definition.

<!-- example: check -->
```neura
let describe = func(point: class (export let x: int; export let y: int;)) -> string {
    return point.x.to_string() + "," + point.y.to_string();
};

let origin: class (
    export let x: int = 0;
    export let y: int = 0;
) = new;
origin.x = 3;
let text = describe(origin);
```

**Identity.** A body whose members are all `let` instance fields without accessor clauses or [tags](Tags) is *fields-only*. Its class is identified by its fields: their order, names, and types. Two fields-only bodies with the same fields, anywhere in the program or in the prebuilt modules it links, are the same class, so the parameter of `describe` above and the annotation of `origin` name one type. Field initializers are not part of the identity. Every field of a fields-only body is exported (otherwise nothing could use it, [E0532](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0532.md)) and has a type annotation ([E0534](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0534.md)), so exportedness and mutability are part of the identity only in that they are always the same. Any other body is a class of its own at each place it is written: one with a method, an `init` or `deinit`, a static member, a cast, a base, or interfaces, and also one with a `const` or `last` field, a field with [accessors](#accessors), or a member with a tag, because each of these gives the class behaviour or attributes that its fields alone do not describe. A tag such as `#Deprecated` or `#Unit("px")` is an attribute of the field's declaration, which a structural identity shared across the program could not keep apart: two bodies whose fields differ only in their tags would otherwise be one class with one set of tags. Two such bodies are different types even when their text is the same. A class of its own accepts every member that a declared class accepts, including unexported fields and fields whose type is inferred from their initializer.

**Construction.** A class of its own behaves like a declared class: its field initializers run when it is constructed, wherever `new` names it. A fields-only class takes its field initializers from one body: the body in the annotation of the `let`, `const`, or `last` binding (local, file-level, or field) that a `new` initializes, or the body written after `new` itself, as in `new class (export let x: int = 1;)()`. `new` allocates the object and then runs that body's initializers in field order, at the `new`, with the names visible there. Fields without an initializer start uninitialized, as in any class. Initializers written in any other occurrence, such as a parameter or result type or a collection element type, would never run and are [E0530](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0530.md). A target-typed `new` whose type arrives without a body, such as the value of an assignment to a binding whose body has initializers, cannot tell which initializers to run ([E0533](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0533.md)); a `new` for a parameter or result type whose body has no initializers constructs the object with every field uninitialized.

**Accessors and `last` fields.** A field of an anonymous class can have `| get:` and `| set:` clauses. The rules of [accessors](#accessors) apply with the body as the declaring class: inside the body a bare field name is the storage, and every access from outside it, such as `my_data.data = text` or `let v = my_data.code;`, calls the accessors. Writing a get-only field from outside is [E0510](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0510.md). A `last` field follows the rules of [`last` fields](ObjectLifecycle#last-fields), so one declared without a value needs an `init` in the body that assigns it ([E0446](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0446.md)).

<!-- example: check -->
```neura
let failure = import:Std.Core.Failure;

let my_data: class (
    export let data: string = "";
        | set: data = value;
        | get: data;

    export let code = "XY729H08";
        | get: code;

    export let non_zero = 18000;
        | get: non_zero;
        | set: {
            if (value == 0) {
                failure.trap("non_zero must not be zero");
            }
            non_zero = value;
        };
) = new;

my_data.data = "payload";
let v = my_data.code;
my_data.non_zero += 1;
```

The body has accessors, so it is a class of its own: `code` and `non_zero` take their types from their initializers, and `new` runs the body's own initializers. See [anonymous-accessors.nra](https://github.com/Kayyo321/neura/blob/main/examples/types/anonymous-accessors.nra).

**Scope.** Names in the body resolve in the module or file that encloses it, as for a class declared there. A field type cannot use a type parameter of an enclosing generic function or class ([E0531](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0531.md)): an anonymous class is not generic. Unexported members of a class of its own are visible only in its body, as for any class.

**Names.** Diagnostics, `to_string`, and the names an image keeps for tools describe a fields-only class by its fields, as in `class(x: int32, y: int32)`, with each type spelled as diagnostics spell it. A `const` or `last` field is marked with its keyword, and a field whose type is inferred appears by its name alone. A class of its own adds where it is written, as in `class(count: int32)@counter.nra:3:12`; its runtime name is qualified by its enclosing module like any class's. `neura docs` prints the fields with the types as written and the accessors they have, as in `class(x: int, y: int)` or `class(data: string { get; set }, code { get })`.

**Separate compilation.** A fields-only class is the same class in every module that writes its fields, including prebuilt NeuraIR modules compiled separately: a program can pass an object that one module created to a function of another module whose parameter writes the same fields. In NeuraIR the class is declared in a generated file of its own, named after a hash of its fields, so every module that writes those fields declares the same file and linking coalesces it ([NeuraIR format](NirFormat#anonymous-classes)). A class of its own belongs to the source file where it is written. Every anonymous class carries the [`class.anonymous`](NirFormat#symbol-attributes) attribute.

## Enums

An enum declares a closed set of named variants. Variants are written qualified by the enum's name.

<!-- example: check -->
```neura
export let Light = enum (Red, Yellow, Green);

let next = func(current: Light) -> Light {
    if (current == Light.Red) {
        return Light.Green;
    }
    return Light.Red;
};
```

An enum can annotate bindings, fields, parameters, results, collection elements, and type arguments. Its variant names are visible wherever the enum is visible. Each enum declares at least one variant, and names are unique. An entirely fieldless enum supports `==` and `!=`. The checked conversion `variant as T` converts one of its values to its zero-based declaration ordinal in integer type `T`. `Std.Program.ExitCode.Ok` is the entry function's status 0.

Generic enums declare parameters after `enum`. Qualify a value constructor with an explicit instance, such as `Optional<int>.Some(4)` or `Optional<int>.None`. Patterns such as `Optional.Some(value)` get their payload types from the subject. A type overlay for a concrete instance can also qualify constructors and patterns.

<!-- example: check -->
```neura
let Optional = enum<T> (None, Some(T));
let IntOptional = Optional<int>;
let value: IntOptional = IntOptional.Some(4);
```

An enum can mix unit variants, positional payloads, and named payloads. A
constructor evaluates its fields in written order, even when named arguments
are reordered. The payload belongs to the selected variant and is read through
a match. An enum with payloads has no built-in equality or numeric cast.

<!-- example: check -->
```neura
let Tree = enum (Empty, Leaf(int), Branch { left: Tree, right: Tree });

let sum = func(tree: Tree) -> int {
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

Patterns match one variant level. A pattern inside a pattern is a syntax
error (`expected ')'`), and a guard written as `pattern if condition =>` is a
syntax error (`expected '=>'`). To test a nested value, bind it and `match`
again.

## Scope-bound and transferable types

[Tasks](CoreLanguage#tasks-scopes-and-select) are isolated: a task's mutable objects, arrays, and lists stay with it, and another task can affect them only by sending a message. Three rules of the type system enforce this. The compiler checks them on the whole linked program, including prebuilt NeuraIR modules, and reports a violation at the span of the offending expression. [Concurrency](Concurrency#5-isolation-between-tasks) specifies them with the runtime.

### Scope-bound types

`Std.Concurrent.Task<T>` and `Std.Concurrent.Scope` are *scope-bound*, and so is an array, list, or tuple with a scope-bound element. A scope-bound value must not be reachable after the `scope` block that created it exits.

| A scope-bound value can | A scope-bound value cannot |
| --- | --- |
| Be held in a local declared inside the scope, including a local list or array of handles | Be returned from a function, or be the result of a spawned call |
| Be passed as an argument to a call, and be the type of a parameter | Be stored in a field, a global, or an enum payload |
| Be an element of an array, list, or tuple held in such a local | Be captured by a lambda |
| | Be passed to `spawn`, or sent on a channel |
| | Be a type argument, as in `Optional<Task<int>>` or a channel of tasks |
| | Be assigned to a binding declared outside the innermost enclosing scope. A parameter is not a local of the scope, so a function cannot `append` a task it received to a list it received. |

Each violation is reported with the form `a value of type Task<int32> cannot leave its scope: it cannot be returned from a function`, naming the rule that applies.

### Transferable types

A value given to another task through `spawn` (the callee value, a method's receiver, and each argument), through a channel, or to a parallel map must have a *transferable* type. The check is recursive and runs on the specialized program, so a generic function is checked at each specialization.

| Type | Transferable |
| --- | --- |
| `bool`, integers, floating-point types, `string` | Yes |
| A fieldless enum | Yes |
| An enum with payloads | When every payload of every variant is transferable. `Optional<int>` is; `Result<int>` is not, because the `Custom` variant of `Std.Core.Error` holds an object. |
| A tuple | When every element is |
| A function value | When every closure of its type that the program creates has transferable captures. A named function has no captures. A closure that captures an object, array, or list is not transferable, because a copy of a closure shares what it captured. |
| A `Std.Concurrent` handle: `Sender<T>`, `Receiver<T>`, `Closer<T>`, `Timer`, `AtomicInt64`, `Semaphore`, `Guarded<T>` | Yes, and its fields are transferable |
| An instance of a class marked `#Immutable` ([below](#immutable-classes)) | When its fields are, which a specialization with mutable type arguments may not be; tasks share the instance |
| An instance of a monitor, a class whose exported methods are all `#Synchronized` ([below](#monitors)) | Yes, for a task: tasks share the instance. Not for a parallel map item, which receives copies |
| Any other class instance, including library collections, `StringBuilder`, classes whose fields are all constant but that are not marked `#Immutable`, and classes marked `#NotThreadSafe` | No |
| An array or a list | No. Its elements can be assigned, so a variadic call whose arguments are packed into an array cannot be spawned. |
| A primitive reference, `MemorySegment`, and every native handle | No |
| `Task<T>` and `Scope` | No; they are scope-bound |

<!-- example: check -->
```neura
let total = func(items: int[]) -> int => items.length;
```

`spawn total(items)` is rejected at `items` with `a value of type int32[] is not transferable, so it cannot be given to a spawned task`. A channel opened as `channels.open<result.Result<int>>(1)` is rejected with `a value of type Result<int32> is not transferable, so it cannot be a channel's element type`. An error is sent as its transferable copy, `Std.Core.Error.PortableError` ([Standard library](StandardLibrary)). A task returns its result to the task that joins it whatever the result's type, because nothing else can reach what the finished task built.

### Immutable classes

A class marked `#Immutable` promises that no field of an instance changes after its constructor returns, and the compiler checks it ([Tags](Tags#immutable)): every instance field is declared `last` or `const` and has an *immutable type*, and every base and subclass is `#Immutable` too.

| Type | Immutable |
| --- | --- |
| `bool`, integers, floating-point types, `string` | Yes |
| A tuple, or an enum whose payloads are immutable | When every element or payload is |
| A class marked `#Immutable` | When its type arguments are |
| A type parameter of the declaring generic class | Counts as immutable; each specialization is judged with its concrete types |
| Arrays, lists, function values, interfaces, and other classes | No |

<!-- example: check -->
```neura
#Immutable
let Money = class (
    export last currency: string;
    export last cents: int64;

    export let init = func(code: string, amount: int64) {
        currency = code;
        cents = amount;
    };

    export let plus = func(other: Money) -> Money => new Money(currency, cents + other.cents);
);
```

Tasks share the instances of an `#Immutable` class: passing one to `spawn` or sending it on a channel gives the other task the same object, which neither task can change. A `const` binding of an immutable type may be read by a `#Pure` function ([Tags](Tags#pure)).

### Monitors

A *monitor* is a class whose exported methods are all `#Synchronized`: each runs with exclusive access to its receiver, under a guard per instance, and a method may call the methods of its own receiver. Tasks share a monitor's instances when its fields have transferable types and its state is reached only through the receiver of its own methods. [Concurrency, section 8.1](Concurrency#81-monitors) gives the rules, the guard, and the choice between a monitor and `Guarded<T>`.

### Task-safe calls

A value check is not enough: a function can reach shared mutable state without being handed it. The call a `spawn` starts must be *task-safe*. Neither it nor anything it can call, transitively, may

- read or write a module, file, or static binding that is reassignable or whose type is not transferable (reading a `const` of transferable type is allowed);
- make an active use that could run a [static initializer](#static-initialization);
- issue a request to a native library;
- call a function or method marked `#NotThreadSafe`. A class marked `#NotThreadSafe` is different: a task may create and use its own instance, and what is rejected is giving an instance to another task.

For a virtual call or a call through a function value, the compiler takes the union over every override and over every function value of that type that code which can run creates (the program's own functions, initializers, and what they reach), so it can reject a call because of an implementation that never runs. Narrow the type, or pass the needed value as an argument. The diagnostic lists the chain of calls from the `spawn` to the offending operation. `neura check --task-safety` prints the verdict for every function of the program. The task that runs `main` is not restricted.

Under the last rule a program cannot both use tasks and use `Std.DynamicForeign` or `import:c(...)`. The compiler rejects the combination for any scheduler operation, not only a `spawn`: a scope, a channel, a timer, a guard, a select, or a parallel map. A parallel map's worker must also issue no host request, use no scheduler operation, and not use an object's identity; [Concurrency](Concurrency#12-multicore-parallelmap) gives the rule.

## Inference, conversions, equality, and absence

An initialized binding without an annotation takes the initializer's type: `let total = 5;` has type `int32`. The compiler does not infer the type arguments of a generic call; `id(7)` is rejected with `expected 1 type argument(s)`, and `id<int>(7)` is accepted ([Polymorphism](Polymorphism)). Public APIs should use explicit parameter and return types. Empty collection literals require an expected type as described in [Collections](Collections).

There are no implicit numeric conversions: operands of an operation have the same type, and `expression as Type` converts explicitly with the checked rules of [Numbers](Numbers#conversions). Floating-point equality follows IEEE 754. String equality compares bytes. `==` and `!=` on class and interface references compare identity; `equals` is a separate method ([Umbrella class and nil](#umbrella-class-and-nil)). A fieldless enum supports `==`; an enum with payloads has no built-in equality. Basic scalar comparisons are illustrated in [core.nra](https://github.com/Kayyo321/neura/blob/main/examples/guide/core.nra).

Absence of a class or interface reference is `nil`. Absence of any other value is `Std.Core.Optional<T>`, and an expected failure is `Std.Core.Result<T>` ([Errors](Errors)). Generic syntax and its limitations are described in [Polymorphism](Polymorphism).
