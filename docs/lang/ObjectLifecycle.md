# Object lifecycle

This page specifies constructors and explicit destruction for the class model illustrated by [polymorphism.nra](../../examples/types/polymorphism.nra). The compiler lowers `new`, `init`, `super.init`, `deinit`, and `destroy` to NeuraIR operations, and the Neura Virtual Machine executes them. Ownership, alias safety, definite initialization of fields other than [`last` fields](#last-fields), and cleanup on failure are not specified; each limit is stated where it applies and listed in [Status](Status.md#open-language-decisions).

## Construction

A class may declare an `init` function. `new` first ensures the class's
[static initialization](Types.md#static-initialization). It then allocates
an instance and passes its arguments to `init`. The function returns
`void` and is called through construction. The resulting expression has
the constructed class type.

<!-- example: check -->
```neura
let Message = class (
    export last text: string;

    export let init = func(initial_text: string) {
        text = initial_text;
    };
);

let message = new Message("Hello");
```

Field initializers run before the body of their declaring class's `init`. A field without an initializer starts uninitialized. Except for a [`last` field](#last-fields), the compiler does not check that a constructor stores to such a field, and reading it before a store traps at run time (reason: `read of an uninitialized field`). A class without a declared `init` has an implicit zero-argument constructor when its base can be initialized with no arguments; otherwise `new` is rejected with `class 'C' needs a constructor to initialize its base`. Code that assigns fields after `new` is accepted for the same reason.

For a derived class, `super.init(...)` must be the first statement of each declared constructor. It initializes the direct base before derived field initializers and the rest of the derived constructor execute. A class without a declared constructor implicitly invokes the base's zero-argument constructor. `new Derived(...)` selects from the constructors declared by `Derived`, and `super.init(...)` selects from the direct base. Both use [function overload resolution](CoreLanguage.md#function-overloads). Failure during construction and the state of a partially constructed object are not specified.

### Constructor shorthand

`init = func(x, y: int) => set;` is a constructor that stores each parameter in the instance field of the same name. It is the constructor written with those assignments, in parameter order and after the class's field initializers have run. It adds no constructor and changes no visibility.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

let Point = class (
    export last x: int;
    export last y: int;

    export let init = func(x, y: int) => set;
);

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let point = new Point(3, 4);
        console.print_line((point.x + point.y).to_string());
        return program.ExitCode.Ok;
    };
);
```

```text
7
```

| Rule | Detail |
| --- | --- |
| Only an instance `init` has it. | `set` is contextual. Elsewhere it is an ordinary name, and a constructor whose expression body is the name `set` is written as a block. |
| Each parameter names a field. | The field is an instance field the class declares itself, and the parameter's type is assignable to the field's. An inherited field, a static member, a method, or a missing name is `E0537` at the parameter; an incompatible type is `E0403` there. |
| The stores bind the class's own fields. | A parameter or local named like the receiver, `instance`, cannot redirect them, and a field accessor is bypassed as a store inside the class is. |
| It expands before binding and definite assignment. | A `last` field that has a value cannot be set again (`E0401`), and every `last` field without a value is still assigned by the shorthand or an initializer (`E0446`). A [reference field](Types.md#primitive-references) is bound as by the written assignment. |
| A derived class calls `super.init()` first. | The direct base needs a constructor that takes no arguments; without one the shorthand is `E0506`, and the constructor is written as a block that calls `super.init(...)`. |
| Each store has the span of its parameter. | A diagnostic or a debugger stop points at the parameter. |

In the editor, renaming a field renames the shorthand parameter with it, and renaming the parameter alone is refused, because that would change which field it sets. The [coding style](../style/CodingStyle.md) advice to name constructor parameters differently from fields applies to constructors written with assignments, not to the shorthand.

### `last` fields

A field declared `last` is assigned once. `last name: T = value;` is assigned by its initializer, and no constructor or method can assign it again (`E0401`). `last name: T;` has no initializer, and every `init` of its class assigns it exactly once, as `name = value;` or `instance.name = value;`, under the [deferred assignment](CoreLanguage.md#deferred-assignment) rules:

- Each path through each `init` overload that returns, by `return` or by reaching the end, assigns the field (`E0446`). A class with such a field must declare an `init` (`E0446`).
- `init` reads the field, or creates a closure that uses it, only where every path has assigned it (`E0441`, `E0443`). A second assignment, or one inside a loop, is an error (`E0440`, `E0442`).
- Nothing else assigns it: not a method, a closure, another class, a derived class's `init`, or `other.name` for another instance (`E0445`). Any code can read it.

<!-- example: check -->
```neura
let Account = class (
    export last owner: string;
    export last limit: int;
    export last currency: string = "EUR";

    export let init = func(owner: string) {
        instance.owner = owner;
        limit = 100;
    } or func(owner: string, premium: bool) {
        instance.owner = owner;
        if (premium) {
            limit = 1000;
        } else {
            limit = 10;
        }
    };
);
```

The analysis checks the body of `init` only. A method that `init` calls before assigning a field, or a base class constructor that calls an overriding method, can still read the field before its store; that read traps at run time as for any field without an initializer.

### Target-typed new

Where the type of a value is already known, `new` can leave the class out:

```text
new-expression := "new" type "(" arguments ")"
                | "new" "(" arguments ")"
                | "new"
```

After `new`, an opening parenthesis always begins the constructor arguments, and `new` followed by anything that cannot begin a type, such as `;`, `,`, `)`, `]`, or `}`, is the form without arguments. A class in parentheses, `new (T)(x)`, is therefore [E0435](../diagnostics/E0435.md); write `new T(x)`. `let a: T = new;` is the same as `let a: T = new T();`, and `let a: T = new(x, y);` is the same as `let a: T = new T(x, y);`: the constructor is selected, checked for access, and run exactly as for the written class, and errors in its arguments are the errors of `new T(...)`.

The class to construct is the expected type of the `new`, which comes from:

- the annotation of the binding or field it initializes, including an element of a [typed destructuring declaration](Types.md#typed-and-listed-destructuring);
- the declared type of the variable, field, or element that an assignment stores to;
- the declared result type of the function, for the value of a `return`;
- the parameter, for a call argument, once the callee is known. Among overloads, a target-typed `new` fits every alternative whose parameter is a class it can construct, as a contextual argument, and an ambiguity is the usual [overload error](CoreLanguage.md#function-overloads);
- the element type of a collection literal whose type is expected, and the arms of a conditional expression whose type is expected.

A generic class takes its type arguments from the expected type, so `let box: Box<string> = new("text");` constructs `Box<string>`. The expected type must be a concrete class. With no expected type, or with an interface, the umbrella type `class`, a singleton, a type parameter, or a type that is not a class, the `new` is [E0420](../diagnostics/E0420.md). An abstract class is rejected as for `new T()`.

<!-- example: check -->
```neura
let Point = class (
    export let x: int = 0;
    export let y: int = 0;
    export let init = func() {
    } or func(x0: int, y0: int) {
        x = x0;
        y = y0;
    };
);

let Box = class<T> (
    export let item: T;
    export let init = func(value: T) {
        item = value;
    };
);

let distance = func(from: Point, to: Point) -> int => (to.x - from.x) + (to.y - from.y);

let origin: Point = new;
let corner: Point = new(3, 4);
let label: Box<string> = new("corner");
let far = distance(new, new(10, 10));
let make = func() -> Point {
    return new(1, 1);
};
```

[Anonymous classes](Types.md#anonymous-classes) describe which field initializers `new` runs for a class written in a type position.

## Destruction

A class may declare a zero-argument `deinit` function returning `void`. The `destroy object;` statement ends the object's lifetime and invokes cleanup from the most-derived class toward its bases, regardless of the static type of the reference. Each class's `deinit` runs at most once; base cleanup is automatic and must not be called explicitly. A class without `deinit` has no cleanup body of its own. Cleanup should release resources owned by that class, not resources it merely borrows.

Destruction invalidates the object for every alias. The VM traps with `invalid reference handle` if an operation subsequently dereferences the invalid handle. The example uses `destroy` only after the last use of its object. The compiler performs no ownership or alias check, and nothing in the language specifies cleanup on errors, destructor visibility, the behavior of failing cleanup, or calls to overridable methods during cleanup.

The VM automatically reclaims unreachable heap storage, including cyclic object graphs. Reclamation can occur at allocation and list growth. It does not run `deinit`, and leaving a source scope does not schedule cleanup. Use explicit `destroy` for cleanup with program-visible effects. [Garbage collection in NVM](../runtime/NVM.md#garbage-collection) describes which references retain an object and how reclamation interacts with runtime limits.

## Singleton lifecycle

A [singleton](Types.md#singletons) runs its field initializers and optional
zero-argument `init` at its declaration's place in program initialization.
Its construction also initializes the singleton type's own statics and any
base dependencies. Ordinary static fields of an enclosing class still wait
for an active use of that class.
The instance remains reachable for the life of the program. Singletons
cannot declare `deinit`. Attempting to destroy a reference known to be a
singleton is a compile-time error. When its type has been erased to
`class` or to an interface, an attempted destruction traps before
invalidating the reference.

## Generic and polymorphic values

A generic class can define `init` and `deinit` like any other class. A type parameter does not grant permission to construct or destroy a value of that type; the language has no constructor bounds, no ownership transfer, and no rule for cleanup of generic fields. Destroying through a base-typed reference runs the actual derived object's cleanup. Generic type arguments do not change dispatch of overridden methods.
