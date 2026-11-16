# Neura 0.3.0: grouped parameters and constructor shorthand

This note fixes the grammar and rules of two 0.3.0 features before they are implemented, as [the 0.3.0 baseline](Release030.md) requires: grouped parameter types, and the constructor shorthand `=> set`. The rows move to [Core language](../lang/CoreLanguage.md) and [Object lifecycle](../lang/ObjectLifecycle.md) in the change that implements them.

Status: *proposed* on 2026-11-16, and *implemented* as written in the change that moved the rows to the language pages. The owner was away and had asked for autonomous work toward 0.3.0, so the rows await the owner's confirmation; "proposed" stays until then. A row the owner changes is changed here and on the language page in one later change.

Neither feature adds a NeuraIR operation or changes an image. A program written with them lowers to the IR of the same program written out in full.

## Grouped parameter types

<!-- example: check -->
```neura
let distance = func(x, y: float64, label: string) -> string { return label; };
// is the function
let spread = func(x: float64, y: float64, label: string) -> string { return label; };
```

| Rule | Status |
| --- | --- |
| A parameter list is a sequence of groups. A group is one or more names separated by commas, followed by `:` and one type, which every name in the group takes. A name without a type belongs to the next group that has one. | proposed |
| The type is parsed once. Every parameter of the group refers to that one type, so an anonymous class type written in a group is one class, not one per name. | proposed |
| Each parameter keeps its own name span and its own tags; a tag before a name applies to that name only. The group's type keeps its own span, which every parameter of the group shares. | proposed |
| Grouping applies wherever a parameter list is written: functions, methods, constructors, function values and lambdas, and each alternative of an overload set. | proposed |
| A variadic parameter is a group of one name, and it is last: `func(a, b: int, items: ...string)`. `func(a, items: ...string)` is an error, because `a` would take the variadic type. | proposed |
| A group never reaches back across a completed group: in `func(a: int, b, c: string)`, `a` is `int` and `b` and `c` are `string`. | proposed |
| A list that ends in names without a type is an error at the first untyped name, unless every parameter of the list is untyped and the function value has a callable expected type (a lambda passed where a function type is expected). A list that mixes typed groups with trailing untyped names is an error in 0.3.0. Contextual typing of lambdas is defined by the inference milestone; until then an untyped parameter is always an error. | proposed |
| Duplicate names, and overload alternatives with equal signatures, are diagnosed after expansion, with the existing codes. `int` and `int32` are the same type, so `func(a, b: int)` and `func(a: int32, b: int32)` are equal signatures. | proposed |
| Locals, fields and enum payloads are not grouped: `let a, b: int;` and `Point(x, y: int)` stay errors. | proposed |

Tooling follows the expansion. Signature help, hovers and the documentation index count and name each expanded parameter. `neura fmt` keeps the grouping and the comments the author wrote, and does not group or ungroup a list itself.

## Constructor shorthand

<!-- example: check -->
```neura
let Point = class(
    export last x: int;
    export last y: int;

    export let init = func(x, y: int) => set;
);
```

| Rule | Status |
| --- | --- |
| `=> set` is the body of an instance constructor `init` only. It assigns each parameter to the instance field of the same name, in parameter order, after the class's field initializers have run. It is the constructor written with those assignments; it adds no constructor and changes no visibility. | proposed |
| `set` is contextual. Elsewhere it is an ordinary name, and a constructor whose expression body was an identifier named `set` must be written as a block. | proposed |
| Each parameter must name an instance field declared by the class itself, of a type the parameter is assignable to. An inherited field, a static field, a method, a missing name, or an incompatible type is an error at the parameter. | proposed |
| The stores bind the class's own fields directly. A parameter or local named like the receiver cannot redirect them, and a field accessor is bypassed, as a store to a field inside the class is today. | proposed |
| The shorthand is expanded before reference binding and definite assignment. A `last` field that already has a value cannot be set again, and every `last` field without a value must still be assigned, by the shorthand or by an initializer. | proposed |
| In a derived class, the shorthand calls `super.init()` first when the direct base has an accessible constructor that takes no arguments, and is an error otherwise; the constructor must then be a block that calls `super.init(...)` itself. | proposed |
| Each generated store carries the span of its parameter and of the field, so a diagnostic or a debugger stop names both. | proposed |

Renaming a field in the editor renames the shorthand parameter with it, or expands the shorthand into explicit assignments; it never changes which field a parameter sets. The [coding style](../style/CodingStyle.md) advice to name constructor parameters differently from fields applies to constructors written with assignments, not to the shorthand.

## Acceptance

- Parser cases with spans for each rule: groups, tags on grouped names, anonymous class types shared by a group, nested generic, tuple and function types, variadics, comments inside a list, malformed commas, and incomplete input in the editor.
- Each grouped or shorthand program lowers to exactly the IR of its written-out form, and builds the same image bytes.
- Each rejection has a test with its code, span and message.
