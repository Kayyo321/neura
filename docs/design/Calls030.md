# Neura 0.3.0: calls, inference, extensions, interpolation and `nameof`

This note fixes the design of the 0.3.0 calls milestone before it is implemented, as [the 0.3.0 baseline](Release030.md) requires: contextual inference of type arguments and lambda parameters, named arguments and constant defaults, explicit extension sets, interpolated strings, and `nameof`. It builds on [grouped parameters and the constructor shorthand](Parameters030.md) and on [storage and construction](Storage030.md), and it is a design note in their sense: rows move to [Core language](../lang/CoreLanguage.md), [Polymorphism](../lang/Polymorphism.md), [Modules](../lang/Modules.md) and [Types](../lang/Types.md) in the change that implements them.

Status: *proposed* on 2026-11-17, and *implemented* on the milestone branch the same day, steps 2 to 12 of the [order of work](#order-of-work). The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written by autonomous agents and await the owner's confirmation; a row marked "implemented, proposed" is in the compiler and still awaits it. Later changes to a row are made here, with the reason, under [Decisions taken in the implementation](#decisions-taken-in-the-implementation).

No feature of this note adds a NeuraIR operation, an NVR record or a runtime primitive. Each lowers to operations NeuraIR 1.10 already has, and a program that uses none of them builds the image it builds today, byte for byte.

## What exists at the baseline

Checked against the code of the milestone branch on 2026-11-17.

| Area | Baseline |
| --- | --- |
| Arguments | `expression_list` (`src/parser/parser.cpp`) reads expressions only. `f(a: 1)` is `E0203` at the `:`. `Lowering::arguments` (`src/neura-ir/lowering.cpp`) fills parameters by position and packs a variadic tail. |
| Defaults | `parameters()` reads `name: Type` groups. `func(a: int = 1)` is `E0203` at the `=`. `Parameter` (`src/parser/syntax.hpp`) has no default. |
| Overloads | `select_overload` keeps the alternatives whose parameter count equals the argument count, lowers ordinary arguments once, and ranks each argument (exact, assignable, reference binding, contextual, cast). |
| Type arguments | `Lowering::type_arguments` requires one written argument per type parameter. `id(7)` is `E0417`. |
| Lambdas | `Lowering::lambda` takes every parameter type from the syntax. An untyped parameter is `E0214` in the parser. |
| Member calls | `Lowering::select` finds members of the static receiver type, intrinsic members, and [primitive companions](../lang/Types.md#primitive-companions). There are no extensions. `let.promote` and `let.as` are the two forms of `let.` with a word. |
| Text | A string literal is one token. `$` is `E0101`. `Std.Io.Console.format` renders a [format string](../lang/StandardLibrary.md#console-formatting) at run time through `Std.Io.Internal.Formatting.try_vformat`, with arguments packed by `Lowering::pack_argument` into `Std.Core.Variadic.Argument`. |
| Names | `nameof`, `extension` and `extensions` are ordinary identifiers. The repository's sources use `extension` as a method name (`Std.Fs.Path`) and never use `nameof` or `extensions`. |
| Facts in NeuraIR | A parameter is a symbol with its name, so labels already survive a `.nir` file. Symbol attributes (`src/neura-ir/attributes.def`) can be attached to locals, fields, functions and classes, not to parameters, and a literal value is a Boolean, an integer or text. |

## Named arguments

<!-- example: check -->
```neura
let schedule = func(job: string, retries: int = 3, urgent: bool = false) -> string {
    return job;
};

let first = schedule("backup");
let second = schedule("backup", urgent: true);
let third = schedule(urgent: true, job: "backup", retries: 0);
```

| Rule | Status |
| --- | --- |
| An argument written `name: expression` is a named argument. It gives its value to the parameter of that name. Positional arguments come first and fill parameters from the left; every argument after the first named one is named. | implemented, proposed |
| Every parameter receives exactly one value: a positional argument, a named argument, or its default. A parameter without a default must be supplied. | implemented, proposed |
| Labels are resolved at compile time against the declaration the call names: a function, a method, a constructor (`new T(...)`, `super.init(...)`), an extension function, or the call of a `spawn`. The parameter names of an exported declaration are part of its interface: renaming one breaks callers that name it. | implemented, proposed |
| A call through a function value is positional, because a function type carries neither names nor defaults. So is a call of an intrinsic member (`append`, `slice`, `equals` and the others), of a primitive conversion such as `int64(x)`, and of a positional enum constructor. A named function read as a value loses its labels and defaults. | implemented, proposed |
| A call of a function with a variadic parameter is positional. | implemented, proposed |
| The parameter names of an override equal those of the method it overrides, position by position, and the names of an implementation equal those of the interface method. | implemented, proposed |

### Order of evaluation

A named call is evaluated in the order it is written, not in parameter order.

| Step | Event |
| --- | --- |
| 1 | The callee is evaluated: the receiver of a method call or of an extension call, the function value, or nothing for a named function. |
| 2 | Each written argument, left to right: its expression is evaluated, then the value is admitted to its parameter. Admission is the conversion the parameter requires: a selected [object cast](../lang/Types.md#object-casts), the binding of a [primitive reference](../lang/Types.md#primitive-references), and the parameter's linked class admission hook. An argument is admitted before the next written argument is evaluated. |
| 3 | Each omitted parameter, in declaration order: its default is materialized and admitted. |
| 4 | The admitted values are moved to their parameter positions. This is transport: it evaluates nothing and runs no hook. |
| 5 | The call is made. |

A trap in step 2 or 3 ends the run there, so no later argument is evaluated. For `third` above the trace is: evaluate `true`, admit `urgent`; evaluate `"backup"`, admit `job`; evaluate `0`, admit `retries`; call. For `second` it is: evaluate `"backup"`, admit `job`; evaluate `true`, admit `urgent`; materialize `3`, admit `retries`; call.

A positional call is unchanged by this table: its arguments are written in parameter order, so steps 2 and 4 coincide with today's left-to-right evaluation.

## Constant defaults

| Rule | Status |
| --- | --- |
| A default is written `name: Type = constant` and belongs to one parameter. A group of several names takes no default: `func(x, y: int = 1)` is an error. | implemented, proposed |
| A parameter with a default may be followed by one without. A caller reaches the later one by name, or supplies both by position. | implemented, proposed |
| A default is a literal constant of the parameter's type: a Boolean, integer, float or string literal, a numeric literal with unary `-`, `nil` for a class or interface type, or a variant without payload written with its enum (`Light.Red`, `Optional<int>.None`). It contains no call, no `new`, no name of a binding, and no other parameter. | implemented, proposed |
| A default is checked once, at the declaration, against the parameter's type with the rules of a literal initializer. | implemented, proposed |
| These parameters take no default: a variadic parameter and every parameter of a function that has one; a primitive reference parameter (`&T`); a parameter whose type mentions a type parameter; a parameter of a lambda, of a function type, of an enum payload, or of a declaration without a body (a host, kernel, scheduler or callback binding). | implemented, proposed |
| The default is inserted by the compiler in the caller. A caller built against an earlier default keeps it until it is rebuilt. | implemented, proposed |
| The defaults of an override equal those of the method it overrides, parameter by parameter: the same parameters have one, with the same value. The same holds between an interface method and its implementations. A call therefore means the same whatever the static type of its receiver. | implemented, proposed |

### Overloads

| Rule | Status |
| --- | --- |
| An alternative applies to a call when the call has no more positional arguments than the alternative has parameters, every label names a parameter the positional arguments did not fill, no parameter is named twice, and every parameter left without a value has a default. This replaces the filter on equal counts. | implemented, proposed |
| Applicable alternatives are ranked as today, argument by argument, where an argument is compared at the parameter it fills in each alternative. A parameter filled by a default takes no part. | implemented, proposed |
| No rule prefers the alternative with fewer defaults. If the ranking has no unique winner the call is ambiguous (`E0409`), and the error names the alternatives and the argument that would decide. | implemented, proposed |
| Two alternatives are equal signatures when their parameter types are equal, as today. Defaults and names do not distinguish them. | implemented, proposed |

## Contextual inference

<!-- example: check -->
```neura
let transform = func<T, U>(items: T[list], convert: func(T) -> U) -> U[list] {
    let result: U[list] = new;
    for (let item: T in items) {
        result.append(convert(item));
    }
    return result;
};

let lengths = transform(new string[list](["a", "bc"]), func(word) => word.byte_length);
```

Here `T` is `string` from the first argument, the lambda's parameter is then `string`, and `U` is `int32` from the lambda's result.

| Rule | Status |
| --- | --- |
| A call of a generic function or method without written type arguments infers them. A call with written type arguments supplies all of them, as today. | implemented, proposed |
| Inference has four steps, in this order and without going back. 1: the receiver and every argument that is not a lambda with untyped parameters give constraints. 2: each such lambda, in written order, takes its parameter types from its parameter's function type, which must by then mention no unsolved type parameter. 3: the result type of each lambda gives constraints. 4: a type parameter still unsolved is taken from the expected type of the call, when there is one. | implemented, proposed |
| A constraint comes from matching a parameter's type against the argument's static type, structure by structure: `T` against `A` gives `T = A`; `T[]` against `A[]` gives `T = A`; a generic instance is matched argument by argument. | implemented, proposed |
| Every constraint on one type parameter must give the same type. Two different types are an error, also when one is assignable to the other: no common base is computed. | implemented, proposed |
| A numeric literal, `nil`, an empty collection literal and a target-typed `new` give no constraint. They are typed from the solution, with today's contextual rules. When literals are the only source for a type parameter, each gives its default type, `int32` or `float64`, and those must agree. | implemented, proposed |
| Inference never chooses `any`, never widens a number, never inserts a constructor, a box, `new func` or `new bound`, and never uses the order of declarations. | implemented, proposed |
| The solution is then checked like written type arguments: bounds, `void`, primitive references, scope-bound types, assignability of each argument, and the specialization limits. | implemented, proposed |
| Overload groups stay free of function type parameters, so inference and overload selection never meet in one call. `new` and enum constructors keep written or target-typed arguments: `new Box<int>(1)`, `Optional<int>.Some(4)`. | implemented, proposed |
| An inferred call lowers to the NeuraIR of the same call with the type arguments written. | implemented, proposed |

### Lambdas with untyped parameters

| Rule | Status |
| --- | --- |
| A function expression may leave every parameter untyped: `func(a, b) => a + b`. It needs an expected function type with the same number of parameters, which gives the parameter types. Its result type may be written; otherwise it is the expected one. | implemented, proposed |
| The expected type comes from the parameter the lambda is passed to, the result type of the function it is returned from, the type of the place it is assigned to, or the element type of a typed collection. | implemented, proposed |
| A list that mixes typed and untyped parameters stays `E0214`, and so does an untyped list in a named declaration `let f = func(a) ...;`, which has no expected type. | implemented, proposed |
| With an overload group, an untyped lambda fits an alternative whose parameter is a function type with the same number of parameters. Its body is not used to choose. If more than one alternative remains, the call is ambiguous. | implemented, proposed |
| An untyped lambda is an ordinary lambda once typed. A capturing one that is retained is written `new func(a, b) => ...`, by the rule of [storage and construction](Storage030.md#construction). | implemented, proposed |

## Extension sets

<!-- example: fragment: two files; the second imports the first as App.Words -->
```neura
// In words.nra, the module App.Words:
export let.extension shout = func(text: string, marks: int = 1) -> string {
    return text + "!";
};

// In a file that uses it:
let.extensions words = import:App.Words;

let loud = "ready".shout();
let same = words.shout("ready", marks: 2);
```

| Rule | Status |
| --- | --- |
| `let.extension name = func(receiver: T, ...) ...;` declares an extension function in a file or a module. It is an ordinary function, callable as one, that may also be called as a member of its first parameter's type. It has at least one parameter. | implemented, proposed |
| `let.extensions alias = import:Module;` is an import that also enables the extension functions the module exports, in the file or module that contains the declaration. `alias` is an ordinary import alias. | implemented, proposed |
| A plain `import` enables nothing. Enabling is not inherited by importers, not exported, and not available in a function body. The declaring file or module has its own extension functions enabled. | implemented, proposed |
| The form does not combine with `let.promote`, a wildcard, `_` as the alias, a C header import, or `const`. | implemented, proposed |
| `value.name(arguments)` is resolved in this order, on the static type of `value`: an accessible member of the type or its bases and an intrinsic member; a function of the primitive companion; then the enabled extension functions named `name` whose first parameter accepts the receiver (an equal type, a base class or interface of it, or a type inference solves). The first step that finds something wins. | implemented, proposed |
| If more than one enabled extension function accepts the receiver, the call is an error, whatever the order of the imports and however the receiver types relate. The qualified call `alias.name(value, ...)` is the way out. Alternatives of one `or func` group are one function, chosen by the overload rules. | implemented, proposed |
| The call is the ordinary call `name(value, arguments)`. The receiver is evaluated first and once, and is admitted as the first argument. A nil receiver is passed, not trapped. | implemented, proposed |
| An extension function has the access of any function of its module: no private member of the receiver's class. It adds no storage, overrides nothing, and satisfies no interface requirement. | implemented, proposed |
| To change a primitive receiver, the first parameter is `&T`, and the receiver obeys the borrow rules of a `&T` argument. | implemented, proposed |
| `value.name` without a call is an error: an extension function is not a bound method. | implemented, proposed |

## Interpolated strings

<!-- example: check -->
```neura
let report = func(name: string, paid: float64, items: int) -> string {
    return $"{name}: {items:>4} items, {paid:.2f} paid, {{braces}}";
};
```

| Rule | Status |
| --- | --- |
| `$"..."` is an interpolated string. Its type is `string`. Outside holes it has the escapes of a string literal, and `{{` and `}}` are single braces. | implemented, proposed |
| A hole is `{expression}` or `{expression:specification}`. The specification is the text after the colon of a [format field](../lang/StandardLibrary.md#console-formatting): fill, alignment, sign, `#`, `0`, width, precision and presentation. | implemented, proposed |
| A width or precision may be a nested hole, `{value:>{width}}`. It holds an expression of an integer type and no specification. | implemented, proposed |
| In a hole, the first `:` outside parentheses, brackets and braces begins the specification. A conditional expression or a range with a step is written in parentheses. | implemented, proposed |
| The whole literal is on one line: a line end before its closing `"`, in text or in a hole, is the unterminated string literal of today (`E0103`). A hole contains no comment. A `"` inside a hole begins an ordinary nested literal, and an interpolated string may be nested to a depth of eight. | implemented, proposed |
| Every hole expression, nested ones included, is evaluated once, in the order written, and its value saved. Then the text is formatted from the saved values, in order. A saved object is a reference: its `to_string()` runs during formatting, after every hole is evaluated, and sees what earlier formatting did. A value with a built-in `to_string()` that is not a scalar, text or object is turned into text when it is evaluated, as `Console.format` does. | implemented, proposed |
| The specification is checked at compile time against the hole's type, with the rules the library applies at run time. A valid interpolated string therefore cannot fail to format; it can trap only where a hole expression or a `to_string()` traps. | implemented, proposed |
| The values are packed as the arguments of `Console.format` are. Packing runs no class hook; reading the expression and calling `to_string()` have their usual effects. | implemented, proposed |
| An interpolated string without holes is a string literal. | implemented, proposed |
| A format string that is computed at run time stays with `Console.format` and `vformat`. | implemented, proposed |

## `nameof`

<!-- example: check -->
```neura
let Invoice = class (
    export let total: int64 = 0;
);

let field = nameof(Invoice.total);   // "total"
let kind = nameof(Invoice);          // "Invoice"
```

| Rule | Status |
| --- | --- |
| `nameof(path)` is a string constant: the declared name of the declaration `path` resolves to, without qualification. | implemented, proposed |
| `path` is a name or a dotted path, with type arguments where a generic type is applied. It may name a local, a parameter, a function or overload group, a field, a method, a class, an interface, an enum, a variant, a module, a type parameter, or an import alias (which gives the alias). A member is found on the static type of what precedes it. | implemented, proposed |
| Nothing is evaluated: no receiver, getter, hook, initializer or allocation. A use in `nameof` is not a read for definite assignment or for the unused-binding warning. | implemented, proposed |
| The operand must resolve to one accessible declaration. A built-in type name, a literal, a call, an index or any other expression is an error. | implemented, proposed |
| The string is program data. It is in the image and stays when names and source maps are stripped. It changes when the declaration is renamed, so it is not a stable name for stored data. | implemented, proposed |
| `nameof` is recognized where an expression can start and `(` follows. From 0.3.0 no declaration may be named `nameof`. | implemented, proposed |

## Grammar

```text
argument       := [ identifier ":" ] expression
arguments      := "(" [ argument { "," argument } ] ")"

parameter      := { tag } identifier
group          := parameter { "," parameter } ":" [ "..." ] type [ "=" default ]
default        := literal | "-" numeric-literal | "nil" | qualified-name [ type-arguments ] "." identifier
lambda         := "func" "(" identifier { "," identifier } ")" [ "->" type ] body

extension      := [ "export" ] "let" "." "extension" identifier "=" function
extension-set  := "let" "." "extensions" identifier "=" "import" ":" module-path ";"

interpolated   := "$" '"' { text | "{{" | "}}" | hole } '"'
hole           := "{" expression [ ":" { spec-char | "{" expression "}" } ] "}"

nameof         := "nameof" "(" expression ")"
```

| Rule | Status |
| --- | --- |
| An argument is named when it starts with an identifier directly followed by `:`. No expression starts that way today, so no existing call changes meaning. After the label the value is a whole expression, including a range with a step and a conditional. | implemented, proposed |
| Labels are read wherever `expression_list(")")` reads call arguments: calls, `new`, `super.init`, and the call of a `spawn`. They are not read in a collection literal or an index. A trailing comma stays an error, as today. | implemented, proposed |
| `=` in a parameter list begins a default only after a type. The default is read as an expression and checked in lowering. | implemented, proposed |
| `extension` and `extensions` are contextual: they are words only directly after `let.`. `Path.extension()` and a binding named `extension` keep their meaning. | implemented, proposed |
| `$` directly before `"` begins an interpolated string. `$` anywhere else stays `E0101`. | implemented, proposed |
| The lexer produces one token for an interpolated string, with its parts as child tokens that carry their own spans: text, hole expression tokens, and the specification. It tracks nesting to find the end of a hole. | implemented, proposed |
| `neura fmt` writes a label as `name: value`, a default as `name: Type = value`, and copies an interpolated string byte for byte. It never adds, removes or reorders labels. | implemented, proposed |
| The language server offers parameter names as completions inside an argument list, marks the active parameter by label in signature help, shows defaults in hovers and signatures, renames a parameter together with its labels at call sites, lists extension functions among a receiver's members with their set, and resolves navigation and rename through `nameof` and through holes. | implemented, proposed |

## Lowering and artifacts

| Rule | Status |
| --- | --- |
| The parser records labels in a vector parallel to a call's operands (`Expr`), a default on `Parameter`, and the two import and declaration forms on `Declaration`. `Lowering::arguments` maps arguments to parameters; `select_overload` uses the same mapping per alternative. | implemented, proposed |
| A call whose written order is the parameter order, with defaults only after the last written argument or with no default that has an admission event, lowers to today's `Call`, `VirtualCall`, `ExternalCall`, `BaseInitialize` or `Allocate`, with the defaults as constant operands. | implemented, proposed |
| Any other named call lowers to a `ScopedValue` that initializes one hidden constant local per written argument, in written order, with the admitted value, and then makes the call with loads of those locals in parameter order. | implemented, proposed |
| A hidden local of a `&T` argument holds the bound reference. If the borrow rules of the storage milestone cannot express that, a named `&T` argument out of parameter order is rejected instead (`E0484`); it is never copied. | implemented, proposed |
| A default is recorded in NeuraIR as a symbol attribute on its parameter, `parameter.default`, with the payload `[kind, literal]`: kind 0 Boolean, 1 signed integer, 2 unsigned integer, 3 float (the literal is the encoding of the value at the parameter's width, as an unsigned integer), 4 text, 5 nil (the literal is `false`). A variant default is the attribute `parameter.default.variant`, a symbol reference. The registry gains the symbol kind `on_parameter`. | implemented, proposed |
| An extension function carries the attribute `function.extension`, so a prebuilt `.nir` module exports its set. | implemented, proposed |
| The three attributes take the next free identifiers when the change lands (12, 13 and 14 on the milestone's base). Attributes need no NeuraIR version. The `.nir` of a program without defaults and extensions keeps its bytes. | implemented, proposed |
| The verifier checks that a `parameter.default` literal fits its parameter's type, that no parameter of a variadic function has one, and that `function.extension` is on a function with a parameter. | implemented, proposed |
| An interpolated string lowers to a call of `Std.Io.Internal.Formatting.try_vformat` through a compiler-resolved import, as primitive companions are resolved, with a constant pattern that uses explicit indexes and a `Collection` of packed arguments, and to the trap of `Console.vformat` on an error, which a checked literal never reaches. | implemented, proposed |
| `nameof` lowers to `IrOp::String`. | implemented, proposed |
| Nothing changes in the converter, NVR, NVM, the tiers, the collector or the ABI. | implemented, proposed |
| A call with more than 255 argument words stays correct: tier 2 declines such an exit and the interpreter runs it. Named calls, defaults and holes add tests at 300 arguments, in parameter order and reversed. | implemented, proposed |
| The effect summary of a call counts the defaults and admissions it inserts. The optimizer may not move a later written argument before an earlier one that can trap, nor a default's admission before a written argument. | implemented, proposed |

## Diagnostics

The calls milestone and the matching milestone share one allocation. This note uses and reserves the codes below; the matching note has E0230 to E0239, E0317, E0490 to E0499 and E0565 to E0569.

| Code | Title | Span | Message and fix |
| --- | --- | --- | --- |
| `E0225` | positional argument after a named argument | the positional argument | "a positional argument cannot follow a named one"; labels the first named argument; fix: name it, when the callee is known. |
| `E0226` | default value on a parameter that takes none | the `=` through the default | Names the reason: a group of several names, a variadic function, `&T`, a type parameter, a lambda, a function type, an enum payload, a declaration without a body. Fix for a group: split it, `x: int, y: int = 1`. |
| `E0227` | malformed interpolated string | the offending part | An empty hole, a lone `}`, a bracket in a hole that closes nothing or is not closed before the hole's `}`, a comment in a hole, nesting deeper than eight, a specification on a nested hole. A literal or hole that the line ends is `E0103`, not this. Fix for a lone brace: double it. For a conditional at the top of a hole: parenthesize it. |
| `E0228` | malformed extension declaration or extension import | the word after `let.` | `let.extension` on something that is not a function, without a parameter, in a class or a block, or with `static`; `let.extensions` on something that is not a module import, in a block, or with `promote`, a wildcard, `_`, `const` or a C header. |
| `E0229` | reserved | | |
| `E0315` | ambiguous extension function | the member name | Lists each candidate with its set. Fix: the qualified call through one alias. |
| `E0316` | extension function used without a call | the member name | "an extension function is not a bound method"; help: call it, or pass `alias.name`. |
| `E0480` | conflicting inferred type arguments | the argument that gives the second type | "type parameter 'T' is string from this argument and int32 from that one"; labels both; fix: write the type arguments. |
| `E0481` | lambda does not fit its callable context | the parameter list | "this lambda takes 2 parameters; the expected function type takes 1", or a written result type that differs. |
| `E0482` | unknown argument label | the label | "no parameter named 'retrys'"; suggests the nearest unfilled parameter name. |
| `E0483` | argument supplied twice | the second argument | Labels the first, positional or named. |
| `E0484` | labels are not available for this call | the first label | Names the reason: a function value, an intrinsic, a conversion, a positional enum constructor, a variadic function, a `&T` argument out of order. Fix: remove the labels when the order is the parameter order. |
| `E0485` | default value is not a constant | the default | "a default is a literal, nil, or a variant without payload"; help: use an overload, or test for a marker value in the body. |
| `E0486` | invalid format specification in an interpolated string | the specification, or the character in it | The library's message for the same error, such as "precision is not allowed for an integer". |
| `E0487` | invalid `nameof` operand | the operand | "nameof takes the name of a declaration". |
| `E0488` | invalid extension function | the first parameter | The first parameter is variadic, or its type is a bare type parameter without a bound, `void`, or a function type. |
| `E0489` | reserved | | |
| `E0560` | parameter names differ from the overridden method | the parameter | Labels the base parameter. Fix: rename to the base name. |
| `E0561` | defaults differ from the overridden method | the parameter | Labels the base parameter. Fix: copy the base default. |
| `E0562` to `E0564` | reserved | | |

Existing codes keep their cases: a required parameter without a value is `E0407`, with the parameter named; type arguments that cannot be inferred are `E0419`, with the unsolved type parameter and the annotation to write; an untyped lambda without an expected type is `E0214`; a default of the wrong type is `E0403`; ambiguity is `E0409`; an unknown member is `E0305`; an unresolved or inaccessible `nameof` path is `E0304` or `E0305`; a declaration named `nameof` is `E0311`.

## Edge cases

Each case is a test. "Accept" gives the result, "reject" the code.

Named arguments and defaults:

1. `f(1, c: 3, b: 2)`: accept; evaluation order 1, 3, 2.
2. `f(a: 1, 2)`: reject, `E0225`.
3. `f(1, a: 2)` where `a` is the first parameter: reject, `E0483`.
4. `f(b: 1, b: 2)`: reject, `E0483`.
5. `f(z: 1)` with no parameter `z`: reject, `E0482`, with a suggestion.
6. `f()` with a required parameter: reject, `E0407`, naming it.
7. `f(a: 1,)`: reject, `E0204`, as a trailing comma is today.
8. `f(a : 1)`, `f(a:1)`, and a line break or comment between the label, the colon and the value: accept.
9. `f(a: x ? 1 : 2)` and `f(r: 0 .. 10 : 2)`: accept; the value is a whole expression.
10. `f(x ? a : b)`: accept as a positional conditional, as today.
11. `f(if: 1)`: reject, `E0204` at `if`. `f(1: 2)`: reject, `E0203` at the `:`. A label is an identifier.
12. A label equal to a local's name, `f(count: count)`: accept; the label is not a use of the local.
13. `g(v: 1)` where `g` is a function value: reject, `E0484`.
14. `let h = f; h(1)` where `f` has defaults: reject, `E0407`; the value has no defaults.
15. `items.append(element: x)`: reject, `E0484`.
16. `Shape.Circle(radius: 2)` for a positional payload: reject, `E0484`. `Shape.Circle { radius: 2 }` for a named payload: accept, unchanged.
17. `new Point(y: 2, x: 1)` and `super.init(y: 2, x: 1)`: accept; `y` is evaluated first.
18. `init = func(x: int, y: int = 0) => set;` and `new Point(1)`: accept.
19. `init = func(x, y: int = 0) => set;`: reject, `E0226`.
20. `spawn work(limit: 3, job: next())`: accept; the arguments are evaluated in written order by the parent.
21. `log(level: 2, "a", "b")` and any label in a call of a variadic function: reject, `E0484`.
22. `func(a: int = 1, items: ...string)`: reject, `E0226`.
23. `func(a: &int = 1)`: reject, `E0226`.
24. `func<T>(a: T = 1)`: reject, `E0226`.
25. `func(a: int = limit())`, `= other`, `= 1 + 2`, `= new T()`: reject, `E0485`.
26. `func(a: int = -1)`, `= 0x10`, `(a: float32 = 1.5)`, `(a: string = "")`, `(a: Light = Light.Red)`, `(a: Shape = nil)`: accept. (Neura has no hexadecimal literal, so `0x10` is `E0105` wherever it is written; the test uses `16`.)
27. `func(a: uint8 = 256)` and `(a: int = "x")`: reject, `E0424` and `E0403`.
28. `func(a: int = 1, b: int)`: accept; `f(b: 2)` and `f(1, 2)` are valid, `f(2)` is `E0407`.
29. A tag on a parameter with a default, `#Note("x") a: int = 1`: accept.
30. A lambda `func(a: int = 1) => a` and a function type `func(int = 1) -> int`: reject, `E0226`, and a syntax error in the type.
31. Alternatives `f(a: int)` and `f(a: int, b: int = 1)` called as `f(1)`: reject, `E0409`; `f(1, 2)` and `f(1, b: 2)`: accept.
32. Alternatives `f(a: int, b: string)` and `f(b: string, c: bool)` called as `f(b: "x", a: 1)`: accept the first; only it has `a`.
33. An override that renames a parameter: reject, `E0560`. One that changes or drops a default: reject, `E0561`.
34. A method called through a base reference with a default: accept; the value is the shared default.
35. A named call with a trap in the second written argument: accept; the third is not evaluated and the first was admitted.
36. A named argument that needs an object cast: accept; the cast runs at that argument's place in the written order.
37. A call with 300 parameters, all named in reverse, and one with 299 defaults: accept, in every execution mode.
38. A label in an incomplete buffer, `f(a: ` and `f(a:)`: the recovering parser keeps the call with the label; signature help marks `a`.

Inference and lambdas:

39. `identity(7)`: accept, `T = int32`. `identity<int64>(7)`: accept, unchanged.
40. `pair(1, "a")` for `func<A, B>(a: A, b: B)`: accept.
41. `same(dog, cat)` for `func<T>(a: T, b: T)` with two classes: reject, `E0480`, also when `Dog` extends `Animal` and the other argument is an `Animal`.
42. `same(1, big)` with `big: int64`: accept, `T = int64`; the literal takes it.
43. `same(1, 2.5)`: reject, `E0480`; with literals only, each gives its default type, and `int32` and `float64` differ. `same(1.0, 2.5)`: accept.
44. `empty()` for `func<T>() -> T[list]` without context: reject, `E0419`. With `let xs: int[list] = empty();`: accept.
45. `first(nil)` and `first([])`: reject, `E0419`.
46. `render(alert)` for `<T: Renderable>`: accept, `T = Alert`. An argument that fails the bound: reject, `E0418`, at the argument.
47. `transform(names, func(word) => word.byte_length)`: accept, as above.
48. `apply(func(x) => x)` for `func<T>(f: func(T) -> T)`: reject, `E0419`; nothing gives `T` before the lambda.
49. `func(x, y: int)` and `func(x: int, y)`: the first is a group; the second is `E0214`.
50. `let f = func(x) => x;`: reject, `E0214`.
51. `sort(items, func(a, b) => a < b)` where the parameter type has two parameters: accept. With three: reject, `E0481`.
52. An untyped lambda passed to an overload group with two function-typed alternatives of the same parameter count: reject, `E0409`.
53. An untyped lambda returned from a function whose result is a function type: accept.
54. A generic method on a generic class, `box.pair("x")`: accept; `T` from the receiver, `U` from the argument.
55. An inferred call that exceeds a specialization limit: reject with the existing diagnostic at the call.
56. `f<int>(func(x) => x)`: accept; written type arguments give the lambda its types.

Extensions:

57. `"a".shout()` with the set enabled: accept. Without `let.extensions`: reject, `E0305`, with a help naming the set when an imported module exports one.
58. A set enabled in a file is not enabled in a file that imports that file: reject, `E0305`.
59. Two enabled sets each with `shout(text: string)`: reject, `E0315`; `words.shout("a")`: accept.
60. An extension named like an accessible member: the member is called. Like a private member, from outside the class: the extension is called.
61. An extension named like a primitive companion function: the companion is called.
62. An extension on a base class called on a derived value, and one on an interface: accept.
63. A generic extension `func<T>(items: T[list]) -> int`: accept; `T` is inferred from the receiver.
64. `nil_reference.describe()` where `describe` is an extension: accept; the function receives nil.
65. `let m = "a".shout;`: reject, `E0316`.
66. `let.extension` in a class, a block, or on a binding that is not a function: reject, `E0228`.
67. `let.extensions _ = import:App.Words;`, `let.extensions words = import:App.*;`, `let.promote.extensions`: reject, `E0228`.
68. `let.extensions words = import:App.Words;` where the module exports no extension: accept; it is an import.
69. `let extension = 1;` and `path.extension()`: accept, unchanged.
70. An extension with a `&int` receiver called on a local: accept, and on a literal: reject with the existing reference diagnostic.
71. An extension set from a prebuilt `.nir` module: accept.

Interpolation and `nameof`:

72. `$""` and `$"plain"`: accept, a string constant.
73. `$"{a}{b}"`, `$"{ a }"`, `$"{a:>8}"`, `$"{a:{w}.{p}f}"`: accept.
74. `$"{{{a}}}"`: accept, the value in braces.
75. `$"{}"`, `$"a}"`, `$"{(a}"` and `$"{a // note}"`: reject, `E0227`. `$"{a"`: reject, `E0103`; the second `"` begins a nested literal that the line ends.
76. `$"{map.get("k")}"` and `$"{$"{a}"}"`: accept.
77. `$"{ok ? 1 : 2}"`: reject, `E0227`, with the fix `{(ok ? 1 : 2)}`. `$"{(ok ? 1 : 2)}"` and `$"{f(a: 1)}"`: accept.
78. `$"{a:d}"` for a string, `$"{a:.2q}"`: reject, `E0486`.
79. A line break inside the literal or a hole: reject, `E0103` at the line end.
80. `$ "x"` and `$x`: reject, `E0101`.
81. A hole of type `void`: reject, `E0416`. A hole of a type without a text form: reject with the diagnostic of `Console.format`.
82. Two holes that call functions with effects: accept; both calls happen before any formatting.
83. A hole holding an object whose `to_string()` changes another hole's object: accept; the later text shows the change.
84. An interpolated string with 300 holes: accept, in every execution mode.
85. An unterminated `$"{a` at the end of an editor buffer: one `E0103`; the tokens before it are kept.
86. `nameof(total)`, `nameof(Invoice.total)`, `nameof(invoice.total)`, `nameof(Box<int>.value)`, `nameof(console)` for an import alias: accept.
87. `nameof(missing)`: reject, `E0304`. `nameof(Invoice.hidden)` from outside: reject, `E0305`.
88. `nameof(int32)`, `nameof(1)`, `nameof(f())`, `nameof(a[0])`, `nameof()`: reject, `E0487`.
89. `nameof(overloaded)` for an overload group: accept, the shared name.
90. `nameof(local)` for a deferred `last` local before its assignment: accept; it is not a read.
91. `let nameof = 1;`: reject, `E0311`.
92. A program built with names and source maps stripped prints the same `nameof` strings.

## Order of work

Each step is one commit on the milestone branch with the tests named. No step changes the image of a program that does not use the step's feature, apart from the sources step 2 edits.

| Step | Content | Targeted tests | Changes images |
| --- | --- | --- | --- |
| 1 | This note | doc checks | no |
| 2 | Labels in the parser and `Expr`; mapping in `Lowering::arguments`; hidden locals for reordered calls; `E0225`, `E0482` to `E0484`; `E0560` with the migration of the repository's overrides | `tests/ui/calls`, a call-order program with its expected trace, `tests/parameters/equivalence` (a named call in parameter order against the positional call) | only the source maps of the programs whose parameters it renames, which it lists |
| 3 | Defaults: parser, `parameter.default`, caller insertion, overload applicability, `E0226`, `E0485`, `E0561`; `.nir` round trip and a build from `.nir` | `tests/ui/calls`, `nir` tests, the linker test for a default across modules | no |
| 4 | Inference of type arguments, `E0480`; the equivalence of an inferred and a written call | `tests/ui/types`, `generics` tests, `tests/parameters/equivalence` | no |
| 5 | Untyped lambdas, `E0481`; overload fitting | `tests/ui/types`, the sequence library's tests rewritten in part to the short form | no |
| 6 | Extension sets: `let.extension`, `let.extensions`, `function.extension`, `E0228`, `E0315`, `E0316`, `E0488` | `tests/ui/modules`, a two-module program, a `.nir` set | no |
| 7 | Interpolated strings: lexer token, parser, the compile-time specification checker, lowering, `E0227`, `E0486` | `tests/ui/lexer`, a corpus that runs every accepted specification through `try_vformat` and every rejected one against its error | no |
| 8 | `nameof`, `E0487`, the reserved name | `tests/ui/names`, a stripped build | no |
| 9 | Tooling: formatter, signature help, completion, hover, rename of parameters with labels, documentation index | `tests/tooling/formatter_tests.py`, `lsp_parameters.py`, `refactor_protocol.py`, `source-docs` | no |
| 10 | Wide calls: 300 named arguments, 299 defaults, 300 holes, in every execution mode | `tests/backend/tier/wide-operands.nra`, `tier_programs_tests.cpp` | no |
| 11 | Language pages, diagnostics pages, examples, changelog; this note's rows marked implemented | doc checks, `doc-examples` | no |
| 12 | After the class-hook milestone is merged: admission events at steps 2 and 3 of the order table, and the hook traces | the call-order programs with hooks, optimized and unoptimized | no |

## Gate and acceptance

- Every image of the identity set and the extended set is the image the milestone's base builds, byte for byte, except the programs whose parameters step 2 renames, which are listed and compared with `--no-source-map`, and `scripts/runtime-identity.py` gives identical captures in every mode of [the baseline's table](Release030.md#execution-modes).
- Call-order traces: for each row of the order table a program prints one line per event, and its output equals a hand-written expectation, from source, from `.nir`, optimized and unoptimized, at tier 0 and in the default mode. The set covers a reversed named call, defaults before and after written arguments, a trap in the middle, a cast at an argument, a constructor with `=> set`, a method through a base reference, and a spawn.
- A named call in parameter order, and an inferred call, build the bytes of the positional and the written call, compared with `--no-source-map`.
- Source to `.nir` to image gives the bytes of source to image for programs with defaults and extension sets, and a module built to `.nir` exports its defaults, labels and extensions to a source importer.
- Every row of the diagnostics table has a test with its code, span and message, and every numbered edge case has a test.
- The specification checker and the library agree on a generated corpus of specifications and argument types: the same accept or reject, and for rejects the same reason.
- The retention diagnostic of the storage milestone fires for an untyped capturing lambda passed to a retaining parameter, and not for one passed to a parameter that does not retain.
- Signature help, completion, hover, rename and formatting cases pass on real fixtures, including incomplete buffers.
- The full suite, the mode matrix, the fuzzer and the benchmark gate run once, at the milestone gate.

## Migration

For the migration guide of the release:

- **Parameter names are interface.** An override or an implementation whose parameter names differ from the base is `E0560`. Step 2 counts the sites in `stdlib/`, `examples/` and `tests/` with the compiler and renames them; the count goes in the guide.
- **`nameof` is reserved** as a declaration name. The repository has no use.
- **Type arguments may be dropped** where they are inferred. Nothing has to change; `Types.md` and `Polymorphism.md` lose the sentences that say a call must write them, and `E0419`'s page gains the inferred cases.
- **`Console.format` with a literal pattern** can become an interpolated string. Nothing has to change. A pattern that is computed stays as it is.
- **Diagnostic pages and fixtures whose rejected example becomes valid** get a new example: `id(7)` under `E0417` and in Types, the untyped lambda of `E0214`, and the UI fixtures that expect those errors.
- **`E0214`** no longer says that contextual typing is undefined; its page describes the callable context.
- **Standard library:** defaults replace pairs of overloads only where the short form is the long form with a constant; each such change is listed, because a default is compiled into callers. No library function becomes an extension in 0.3.0.
- Pages to update with the implementation: Core language (functions, overloads, lambdas, expressions), Polymorphism, Modules (imports), Types (inference), Standard library (console formatting), Tooling, Status, the NeuraIR format page (the three attributes), the coding style (labels at call sites with several arguments of one type), and the diagnostics catalogue.

## Decisions taken without the owner

| Decision | Reason |
| --- | --- |
| A call of a variadic function takes no labels, and such a function takes no defaults. | Which arguments are the variadic tail must not depend on labels. |
| A default is a literal, `nil` or a variant without payload; named constants are not accepted yet. | It keeps a default free of initialization order and lets `.nir` carry it as one attribute value. |
| An override and an implementation repeat the base's defaults exactly. | A call then has one meaning whatever the receiver's static type, with caller-side insertion. |
| Type parameters are solved by equal types only; no common base is computed. | Two sources that disagree are an ambiguity the author resolves by writing the type. |
| `new` and enum constructors are not inferred. | The milestone infers calls; a target type already serves both forms. |
| An untyped lambda's body is not used to choose an overload. | Choosing by body would lower the lambda once per alternative and make errors depend on the alternatives. |
| Two applicable extension functions are always an error, with no ranking by receiver type. | The result must not depend on import order or on a later, more specific declaration. |
| A file or module has its own extension functions enabled. | A module cannot import itself. |
| `let.extensions` takes no `_` alias. | The alias is the way to disambiguate. |
| An extension function is not a bound method value. | Bound methods carry a receiver and an allocation rule that extensions do not have yet. |
| A conditional or stepped range at the top of a hole needs parentheses; a hole is on one line and holds no comment. | The lexer finds the specification and the end of the literal without parsing the expression. |
| Nested width and precision holes are accepted. | They are the format grammar's nested fields with an expression in place of an index. |
| A specification is checked at compile time against the hole's type, not only for its syntax. | Otherwise a literal could still trap at run time for a reason visible at compile time. |
| `nameof` is a reserved declaration name, and a built-in type is not a `nameof` operand. | The operator is recognized without name lookup, and a built-in type has no declaration to rename. |
| Reordered arguments use hidden locals, and an in-order call keeps today's NeuraIR. | Operands of a call are evaluated left to right, and programs that do not reorder keep their images. |
| Defaults and extension marks are symbol attributes with the next free identifiers, not a NeuraIR revision. | The registry exists for facts of this kind; other milestones are allocating format revisions at the same time. |

## Decisions taken in the implementation

Choices the rows leave open, or where a row could not be implemented as written, made by the implementing agent without the owner. Each takes the stricter behavior where there was a choice.

| Decision | Reason |
| --- | --- |
| A named `&T` argument out of parameter order is `E0484`; it is not saved in a hidden local. | The borrow rules of the storage milestone do not yet describe a hidden local that holds a bound reference, which the row allows as the alternative. |
| `E0225` is reported in lowering, before `E0484`, and suggests the label of the parameter the argument would most likely fill. A call of a variadic function reports `E0484` first. | The fix needs the callee; which arguments are a variadic tail must not depend on labels. |
| An interpolated string lowers to a call of `Std.Io.Console.vformat`, which is the call of `try_vformat` followed by `Console.vformat`'s trap. | One call, already in the library, with the same result and the same trap. |
| The `c` presentation is `E0486` in an interpolated string. | The library checks it against the value when it runs, so a literal that uses it could fail to format, which the rows exclude. |
| A nested width or precision hole whose value is negative, or larger than the library takes, traps as `Console.vformat` does. | The note accepts nested holes (edge case 73); their value is known only when the program runs. This is the one way a checked literal fails to format. |
| A `"` in a specification is `E0227`. | In a hole a `"` begins a nested literal, and a specification is part of a hole. |
| `E0409` names both tied alternatives, with their parameters, only for a call with labels, defaults or an untyped lambda; it does not single out the argument that would decide, and says in its help what would. | Other calls keep their diagnostic text; with labels and defaults the tie is between which parameters the arguments fill, which the two signatures show. |
| The conversion `int64(x)` named in the rows on positional calls is not a Neura form (`E0304`), so it has no `E0484` case. | Conversions are written `x as int64`. |
| A type parameter left unsolved by the arguments is taken from the expected type of the call before the default types of numeric literals. | `let wide: int64 = identity(7);` then infers `int64`, as the expected type is a source and the literal rule applies when literals are the only source. |
| `E0488` names a variadic receiver, a bare type parameter without a bound, and a function type. A `void` receiver is `E0416` first, as for any parameter. | A parameter of type `void` is rejected before the extension check. |
| The effect summary of a call that inserts defaults and hidden locals is the summary of the NeuraIR it lowers to, which holds the constants and the locals explicitly; no test compares summaries. The call-order traces run optimized and unoptimized and from `.nir`. | Nothing in the effect analysis treats a named call specially. |
| A typed lambda passed to a generic function gives its type from its syntax and is lowered at its place among the arguments. | The inferred call then lowers to the NeuraIR of the written call, symbol for symbol. An argument written after an untyped lambda, whose own lowering declares a lambda, may still be numbered differently; the tests cover lambdas in every position the rows name. |
| A lambda with untyped parameters infers its result type only from an expression body. A block body without a written result type is `E0419` when the call waits on its result. | The body is lowered once; a block's result would need its returns before its type is known. |
| An extension function is called with the static qualifier of its module, as `alias.name(value, ...)` calls it. | It is the ordinary call the rows name. |
| Argument labels and `nameof` operands are recorded for the editor in `IrProgram::name_uses`, which no later stage reads and a `.nir` file does not hold. | Neither leaves a node in NeuraIR, and navigation and rename need them. |
| A linked class admission runs when its written argument is evaluated. An omitted nil with a setter runs after all written arguments. A spawn admits in its parent and the task reads the saved parameters directly. | Reordering, constructor field initialization, virtual and extension dispatch, casts, traps and spawned calls have source and prebuilt IR traces. Existing calls without hooks retain their executable form. |
