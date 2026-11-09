# Polymorphism

Polymorphism lets one piece of code work with values of different types. This document develops the language design illustrated by [polymorphism.nra](../../examples/types/polymorphism.nra). The frontend represents inheritance, overrides, generic types, and dynamic calls in NeuraIR. Runtime representation and dispatch behavior remain part of the execution design.

## Subtypes and dynamic dispatch

A derived class is a subtype of its base. A value of a derived class can be passed to a parameter of the base type. The parameter exposes the base interface, while calls to overridden methods use the actual object's implementation. For example, `display(Renderable item)` calls `item.framed()`. The inherited `framed` method calls `render()` on that same object, so passing an `Alert` produces `[Alert: Attention]`, even though `framed` is defined on `Renderable`.

An override uses `let.override` or `const.override` and must match the inherited method's parameter and return types. Calls through the base and calls from inherited methods dispatch to the most-derived override. Fields are inherited; this proposal does not define field overriding, overload resolution, or a way to call a base implementation.

A bodyless class method declares a requirement. A class with an unimplemented requirement is abstract and cannot be instantiated; a concrete subclass must implement every inherited requirement. An intermediate subclass may remain abstract. The example's `Renderable` is abstract, `Text` implements `render`, and `Alert` overrides it again. This extends the existing use of bodyless function signatures in module interfaces to classes.

## Generic functions and classes

A generic declaration introduces type parameters in angle brackets after `func` or `class`. A call or type application supplies arguments in angle brackets:

```neura
let identity = func<T>(T value) -> T {
    return value;
};
let name: string = identity<string>("Neura");
```

`T` is one type throughout an application. An unconstrained type parameter supports only operations valid for every type; it does not promise arbitrary members. A generic class such as `Box<T>` uses its type argument for its fields and methods, so `Box<Text>.get()` returns `Text`. The example initializes `value` before reading it; general initialization rules remain open.

A bound, written `T:Renderable`, requires the supplied type to be `Renderable` or a subtype and makes the bound's exported interface available inside the generic body. Thus `render_twice<Alert>(alert)` can call `render` while retaining `Alert` as the type argument. The bound is checked at compile time; overridden methods still dispatch using the actual object. Unlike `display`, a function returning `T` can preserve the caller's chosen type rather than returning only the base type.

The examples supply type arguments explicitly. Type inference, multiple bounds, generic inheritance, variance, specialization, and runtime representation are not specified. In particular, this proposal does not assume that `Box<Alert>` is a subtype of `Box<Renderable>`; mutable boxes would make such a conversion unsafe. These rules do not specify ownership, nullability, or lifetime behavior.

For proposed constructor and destructor behavior, including cleanup through a base-typed reference, see [Object lifecycle](ObjectLifecycle.md).
