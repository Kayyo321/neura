# Object lifecycle

This page proposes constructors and explicit destruction for the class model illustrated by [polymorphism.nra](../../examples/types/polymorphism.nra). The syntax and rules here are a language design, not implemented or compiler-verified behavior. Ownership, allocation, and error handling remain open design areas.

## Construction

A class may declare an `init` function. `new` allocates an instance and passes its arguments to that function; `init` returns `void` and is not an ordinary callable method. The resulting expression has the constructed class type.

```neura
let Message = class (
    export let text: string;

    export let init = func(string initial_text) -> void {
        text = initial_text;
    }
);

let message = new Message("Hello");
```

Field initializers run before the body of their declaring class's `init`. A constructor must initialize its class's fields without initializers before it completes. A class without a declared `init` has an implicit zero-argument constructor when its base can be initialized with no arguments and its own fields have initializers. Existing examples that assign fields after `new` depend on initialization rules that remain to be resolved; they should not be taken as verified examples of this proposed definite-initialization rule.

For a derived class, `super.init(...)` must be the first statement of a declared constructor. It initializes the direct base before derived field initializers and the rest of the derived constructor execute. A class without a declared constructor implicitly invokes the base's zero-argument constructor. Constructors are not inherited or dynamically dispatched; `new Derived(...)` selects the constructor declared by `Derived`. Constructor overloading, access control, failure, and partially constructed objects are not yet specified. Avoid calling overridable methods during construction: their access to derived state before initialization needs further specification.

## Destruction

A class may declare a zero-argument `deinit` function returning `void`. The proposed `destroy object;` operation ends the object's lifetime and invokes cleanup from the most-derived class toward its bases, regardless of the static type of the reference. Each class's `deinit` runs at most once; base cleanup is automatic and must not be called explicitly. A class without `deinit` has no cleanup body of its own. Cleanup should release resources owned by that class, not resources it merely borrows.

After destruction the object must not be accessed or destroyed again through any alias. How the language enforces this, and whether destruction is allowed when aliases exist, are unresolved. The example uses `destroy` only after the last use of its object. Automatic destruction at scope exit, garbage collection, cleanup on errors, destructor visibility, and the behavior of failing cleanup are not specified; do not assume deterministic cleanup without an explicit `destroy`. Calling overridable methods during cleanup is likewise unspecified.

## Generic and polymorphic values

A generic class can define `init` and `deinit` like any other class. A type parameter does not automatically grant permission to construct or destroy a value of that type; constructor bounds, ownership transfer, and cleanup of generic fields need their own rules. Destroying through a base-typed reference must still run the actual derived object's cleanup, subject to the unresolved aliasing rules above. Generic type arguments do not change dispatch of overridden methods.
