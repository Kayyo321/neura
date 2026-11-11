# Object lifecycle

This page specifies constructors and explicit destruction for the class model illustrated by [polymorphism.nra](../../examples/types/polymorphism.nra). The compiler lowers `new`, `init`, `super.init`, `deinit`, and `destroy` to NeuraIR operations, and the Neura Virtual Machine executes them. Ownership, alias safety, definite field initialization, and cleanup on failure are not specified; each limit is stated where it applies and listed in [Status](Status.md#open-language-decisions).

## Construction

A class may declare an `init` function. `new` first ensures the class's
[static initialization](Types.md#static-initialization). It then allocates
an instance and passes its arguments to `init`. The function returns
`void` and is called through construction. The resulting expression has
the constructed class type.

<!-- example: check -->
```neura
let Message = class (
    export let text: string;

    export let init = func(initial_text: string) {
        text = initial_text;
    };
);

let message = new Message("Hello");
```

Field initializers run before the body of their declaring class's `init`. A field without an initializer starts uninitialized. The compiler does not check that a constructor stores to such a field, and reading it before a store traps at run time (reason: `read of an uninitialized field`). A class without a declared `init` has an implicit zero-argument constructor when its base can be initialized with no arguments; otherwise `new` is rejected with `class 'C' needs a constructor to initialize its base`. Code that assigns fields after `new` is accepted for the same reason.

For a derived class, `super.init(...)` must be the first statement of each declared constructor. It initializes the direct base before derived field initializers and the rest of the derived constructor execute. A class without a declared constructor implicitly invokes the base's zero-argument constructor. `new Derived(...)` selects from the constructors declared by `Derived`, and `super.init(...)` selects from the direct base. Both use [function overload resolution](CoreLanguage.md#function-overloads). Failure during construction and the state of a partially constructed object are not specified.

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
