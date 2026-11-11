<!-- Generated from docs/lang/ObjectLifecycle.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Object lifecycle

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page proposes constructors and explicit destruction for the class model illustrated by [polymorphism.nra](https://github.com/Kayyo321/neura/blob/main/examples/types/polymorphism.nra). The frontend recognizes the lifecycle syntax and emits NeuraIR operations for it. Ownership, allocation, and error handling remain open design areas.

## Construction

A class may declare an `init` function. `new` first ensures the class's
[static initialization](Types#static-initialization). It then allocates
an instance and passes its arguments to `init`. The function returns
`void` and is called through construction. The resulting expression has
the constructed class type.

<!-- example: check -->
```neura
let Message = class (
    export let text: string;

    export let init = func(initial_text: string) {
        text = initial_text;
    }
);

let message = new Message("Hello");
```

Field initializers run before the body of their declaring class's `init`. A constructor must initialize its class's fields without initializers before it completes. A class without a declared `init` has an implicit zero-argument constructor when its base can be initialized with no arguments and its own fields have initializers. Existing examples that assign fields after `new` depend on initialization rules that remain to be resolved; they should not be taken as verified examples of this proposed definite-initialization rule.

For a derived class, `super.init(...)` must be the first statement of each declared constructor. It initializes the direct base before derived field initializers and the rest of the derived constructor execute. A class without a declared constructor implicitly invokes the base's zero-argument constructor. `new Derived(...)` selects from the constructors declared by `Derived`, and `super.init(...)` selects from the direct base. Both use [function overload resolution](CoreLanguage#function-overloads). Failure and partially constructed objects remain open design areas.

## Destruction

A class may declare a zero-argument `deinit` function returning `void`. The proposed `destroy object;` operation ends the object's lifetime and invokes cleanup from the most-derived class toward its bases, regardless of the static type of the reference. Each class's `deinit` runs at most once; base cleanup is automatic and must not be called explicitly. A class without `deinit` has no cleanup body of its own. Cleanup should release resources owned by that class, not resources it merely borrows.

Destruction invalidates the object for every alias. The VM traps if an operation subsequently dereferences the invalid handle. The example uses `destroy` only after the last use of its object. Static ownership checks, cleanup on errors, destructor visibility, and the behavior of failing cleanup remain open. Calling overridable methods during cleanup is likewise unspecified.

The VM automatically reclaims unreachable heap storage, including cyclic object graphs. Reclamation can occur at allocation and list growth. It does not run `deinit`, and leaving a source scope does not schedule cleanup. Use explicit `destroy` for cleanup with program-visible effects. [Garbage collection in NVM](NVM#garbage-collection) describes which references retain an object and how reclamation interacts with runtime limits.

## Singleton lifecycle

A [singleton](Types#singletons) runs its field initializers and optional
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

A generic class can define `init` and `deinit` like any other class. A type parameter does not automatically grant permission to construct or destroy a value of that type; constructor bounds, ownership transfer, and cleanup of generic fields need their own rules. Destroying through a base-typed reference must still run the actual derived object's cleanup, subject to the unresolved aliasing rules above. Generic type arguments do not change dispatch of overridden methods.
