# Polymorphism

Polymorphism lets one piece of code work with values of different types. This document develops the language design illustrated by [polymorphism.nra](../../examples/types/polymorphism.nra). The frontend represents inheritance, overrides, generic types, and dynamic calls in NeuraIR. Runtime representation and dispatch behavior remain part of the execution design.

## Subtypes and dynamic dispatch

A derived class is a subtype of its base. A value of a derived class can be passed to a parameter of the base type. The parameter exposes the base interface, while calls to overridden methods use the actual object's implementation. For example, `display(Renderable item)` calls `item.framed()`. The inherited `framed` method calls `render()` on that same object, so passing an `Alert` produces `[Alert: Attention]`, even though `framed` is defined on `Renderable`.

An override uses `let.override` or `const.override` and must match the inherited method's parameter and return types. Calls through the base and calls from inherited methods dispatch to the most-derived override. Classes inherit fields and [overload groups](CoreLanguage.md#function-overloads). Overload groups support calls through the declaring class or a subclass. Override and interface declarations currently require a single signature per name.

A bodyless class method declares a requirement. A class with an unimplemented requirement is abstract and cannot be instantiated; a concrete subclass must implement every inherited requirement. An intermediate subclass may remain abstract. The example's `Renderable` is abstract, `Text` implements `render`, and `Alert` overrides it again. This extends the existing use of bodyless function signatures in module interfaces to classes.

The [`#Final` tag](Tags.md#final-and-sealed) on a class forbids subclasses (`E0912`), and on a method forbids overrides (`E0913`). `#Sealed` on a class or interface allows subclasses and implementations only inside the module that declares it, or its file for a type declared at the top of a file (`E0914`), so every subtype is known where the type is declared.

<!-- example: check -->
```neura
let Shapes = module (
    #Sealed
    export let Shape = interface (
        let sides = func() -> int32;
    );

    #Final
    export let Square = class implements Shape (
        export let.override sides = func() -> int32 {
            return 4;
        };
    );
);
```

## Generic functions and classes

A generic declaration introduces type parameters in angle brackets after `func` or `class`. A call or type application supplies arguments in angle brackets:

<!-- example: check -->
```neura
let identity = func<T>(value: T) -> T {
    return value;
};
let name: string = identity<string>("Neura");
```

`T` is one type throughout an application. An unconstrained type parameter supports only operations valid for every type; it does not promise arbitrary members. A generic class such as `Box<T>` uses its type argument for its fields and methods, so `Box<Text>.get()` returns `Text`. The example initializes `value` before reading it; general initialization rules remain open.

A bound, written `T:Renderable`, requires the supplied type to be `Renderable` or a subtype and makes the bound's exported interface available inside the generic body. Thus `render_twice<Alert>(alert)` can call `render` while retaining `Alert` as the type argument. The bound is checked at compile time; overridden methods still dispatch using the actual object. Unlike `display`, a function returning `T` can preserve the caller's chosen type rather than returning only the base type.

### Type arguments and specialization

Uses of generic classes supply their type arguments explicitly, and a call of a generic function either writes all of its type arguments or [infers](#type-argument-inference) them. A class or function can have several type parameters, as in `class<K, V>`. A bound can name one class or interface, as in `<T: Comparable>`, or a bracketed list, as in `<T: [Comparable<T>, Iterable<int>], K: Key>`. A type argument must satisfy every bound on its parameter. Type arguments include enums, collections, function types, and other generic instances such as `Box<Box<int32>>` and `Box<int32>[]`. `Box<int>` and `Box<int32>` are the same type because `int` is an alias.

A method of a class can be generic itself:

<!-- example: check -->
```neura
let Pair = class<A, B> (
    export let first: A;
    export let second: B;
);

let Box = class<T> (
    export let value: T;
    export let pair = func<U>(other: U) -> Pair<T, U> {
        let result = new Pair<T, U>();
        result.first = value;
        result.second = other;
        return result;
    };
);
```

A type argument that does not satisfy its parameter's bound is a compile-time error at the type argument, naming the bound. Written type arguments that are too few or too many are errors too (`E0417`).

### Type argument inference

A call of a generic function or method that writes no type arguments infers them from its arguments and its context.

<!-- example: check -->
```neura
let identity = func<T>(value: T) -> T => value;
let same = func<T>(a: T, b: T) -> T => b;
let empty = func<T>() -> T[list] => new;

let use = func(big: int64) {
    let seven = identity(7);          // T is int32, a literal's default type
    let wide = same(1, big);          // T is int64 from big; the literal takes it
    let names: string[list] = empty(); // T is string from the expected type
};
```

| Rule | Detail |
| --- | --- |
| Inference has four steps, in order and without going back. | 1: the receiver and every argument except a lambda with untyped parameters give constraints. 2: each such lambda, in written order, takes its parameter types from its parameter's function type, which must by then mention no unsolved type parameter ([Core language](CoreLanguage.md#function-values-and-closures)). 3: the result type of each lambda gives constraints. 4: a type parameter still unsolved is taken from the expected type of the call, when there is one. |
| A constraint matches a parameter's type against an argument's static type, structure by structure. | `T` against `A` gives `T = A`; `T[]` against `A[]` gives `T = A`; a generic instance is matched argument by argument, also through the class or interface instance that the argument's type extends or implements. |
| Every constraint on one type parameter gives the same type. | Two different types are `E0480`, also when one is assignable to the other: no common base is computed. Inference never chooses `any`, never widens a number, and never inserts a constructor or a box. |
| A numeric literal, `nil`, an empty collection literal and a target-typed `new` give no constraint. | They are typed from the solution. When numeric literals are the only source of a type parameter, each gives its default type, `int32` or `float64`, and those must agree. A type parameter that nothing gives is `E0419`. |
| The solution is checked like written type arguments. | Bounds (`E0418`, at the argument that gave the type), `void`, primitive references, scope-bound types, and the specialization limits. |
| An inferred call is the written call. | It lowers to the NeuraIR of the same call with the type arguments written, and builds the same image apart from its source map. Overload groups have no function type parameters, so inference and overload selection never meet in one call; `new` and enum constructors keep written or target-typed type arguments. |

Every generic class and function is specialized: the compiler makes a concrete copy for each combination of declaration and type arguments that the program instantiates, and each distinct combination gets exactly one copy. Specialization happens when the program is built, so generic code runs at the same cost as code written for the concrete types. A program whose instances never stop growing, such as a function that calls itself with `T[]` or `Box<T>`, is rejected. The compiler stops when a type argument nests more than 32 generic instances or collections deep, or when a program needs more than 4,096 instances. The diagnostic points at the instantiating call and names the chain's starting instantiation.

These restrictions apply:

- Class inheritance requires a non-generic base class. Interface inheritance and implementation can name a generic interface instance.
- A generic method cannot override or be overridden.
- Constructors and destructors cannot have their own type parameters.
- A type parameter grants no construct or destroy permission; `new T()` is not valid.

`Box<Alert>` is not a subtype of `Box<Renderable>`; mutable boxes would make such a conversion unsafe. These rules do not specify ownership, nullability, or lifetime behavior.

For constructor and destructor behavior, including cleanup through a base-typed reference, see [Object lifecycle](ObjectLifecycle.md).

## Interfaces

An interface declares method signatures and can give methods default bodies. An interface extends other interfaces after its colon. A class lists implemented interfaces after `implements` and can also name one class base after its colon.

<!-- example: check -->
```neura
let Comparable = interface<T> (
    let compare_to = func(other: T) -> int;
);

let Rank = class implements Comparable<Rank> (
    export let value: int;
    export let compare_to = func(other: Rank) -> int {
        return value - other.value;
    };
);
```

Interfaces can be used in bindings, parameters, return types, and bounds. A call on an interface reference dispatches using the receiver's concrete class. Interface requirements with identical signatures are shared. A class method with a matching signature takes precedence over interface defaults. A more specific declaration takes precedence over its ancestor, and independent inherited defaults for the same method require an explicit override. A class must declare a method for each remaining requirement, and can leave that declaration bodyless to make the class abstract.
