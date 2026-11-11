---
name: neura
description: Write, read, debug, and review Neura (.nra) source code and drive the `neura` command-line toolchain. Use whenever a task involves a .nra file, a .nvr or .nir artifact, the `neura` CLI (check, build, run, test, emit-ir, decompile, build --native), the Neura standard library (Std.*), or the Neura language itself, including modules, imports, `let.promote`, interfaces, module interfaces, classes, generics, enums and `match`, `===`, comparison chains, ranges, tasks (`scope`, `spawn`, `select`), and foreign interop.
---

# Neura

Neura is a statically typed language whose compiler turns programs into deterministic neuron circuits (`.nvr` images) that the Neura Virtual Machine runs. It looks a little like TypeScript and a little like Rust, but most of its rules are its own. **Do not guess syntax from other languages.** Follow this file, then the references below, and check every file you write with `neura check`.

This skill describes Neura 0.1.0. Every Neura block in this skill is compiled or run by the repository's `doc-examples` test (the `<!-- example: ... -->` comment above each block says how), so the snippets are known to be accepted by the compiler.

## Reference files

Read the reference that matches the task before writing code. They are in `references/` next to this file.

| File | Read it when |
| --- | --- |
| [references/syntax.md](references/syntax.md) | Writing any code: bindings, functions, overloads, variadics, closures, control flow, loops, operators and precedence, `===`, comparison chains, ranges, tuples, enums, `match`, classes, static members, singletons, interfaces, generics, object casts, primitive references, `nil`, construction and `destroy`, doc comments. |
| [references/modules.md](references/modules.md) | Imports, simplifying imports with `let.promote` and wildcards, module interfaces and module inheritance, multi-file projects and the file layout, the program entry point and exit codes, initialization order, prebuilt `.nir` modules. |
| [references/stdlib.md](references/stdlib.md) | Using `Std.*`: the module-per-type pattern, console output and formatting, strings, `Optional`/`Result`/`Error`, collections, files, JSON, time, randomness, environment, logging, tests. |
| [references/stdlib-api.md](references/stdlib-api.md) | Looking up the exact exported signatures of every standard library module (generated from `stdlib/`). Search it with grep rather than reading it whole. |
| [references/concurrency.md](references/concurrency.md) | Tasks: `scope`, `spawn`, `join`, `select`, channels, cancellation, timers, `Guarded`, `Parallel.map`, and the isolation rules the compiler enforces. |
| [references/cli.md](references/cli.md) | Running the toolchain: every `neura` command and option, capabilities, limits, native executables, tests, exit statuses. |
| [references/errors-and-diagnostics.md](references/errors-and-diagnostics.md) | A compile error you do not understand, a runtime trap, or an exit status to interpret. Lists real compiler messages and their fixes. |
| [references/foreign.md](references/foreign.md) | Calling C: `Std.DynamicForeign` and `import:c("header.h")`. |
| [references/style.md](references/style.md) | Naming and layout conventions for `.nra` files. |

## The program skeleton

A runnable program is a module that inherits `Std.Program.Entry` and overrides `main`. A file with only top-level statements and no entry module does not build.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let name = args.length > 0 ? args[0] : "Neura";
        console.print_line("Hello, " + name + "!");
        return program.ExitCode.Ok;
    }
);
```

```text
Hello, Neura!
```

```sh
neura check app.nra                    # type-check only, silent on success
neura build app.nra -o app.nvr         # compile to an image
neura run app.nvr -- first second      # run; arguments after -- reach main
neura test tests.nra                   # run every exported test_ function
```

## Rules that trip up newcomers

These are the mistakes an LLM is most likely to make. Each was confirmed against the compiler.

**Declarations**

1. Everything is a binding: `let name = func(...) -> T { ... };`, `let Name = class ( ... );`, `let Name = module ( ... );`, `let Name = enum (A, B);`, `let Name = interface ( ... );`. There is no `fn`, `function`, `def`, `struct`, or `type` keyword.
2. Class, module, and interface bodies use **parentheses**, not braces: `class ( ... )`. Function and control-flow bodies use braces.
3. Types follow names: `let count: int32 = 0;`, `func(value: int) -> int`. A `void` function may omit `-> void`; a function with no parameters may omit `()`: `let reset = func { ... };`, `let answer = func -> int => 42;`.
4. `=> expression` is an expression body. A named declaration with an expression body ends with `;`.
5. Local `let` bindings require an initializer. `const` prevents reassignment of the binding, not mutation through it.
6. Members are private unless marked `export`. A class that implements an interface method must `export` the implementing member, or the compiler reports `member '...' does not match the interface requirement`.
7. Overriding uses `let.override` (or `const.override`), including when you give a body to an inherited bodyless signature such as `main`.
8. Constructors are `init` methods; `new Class(args)` calls them. In a derived class, `super.init(...)` must be the first statement. `instance` names the receiver (like `this`).

**Types and expressions**

9. There are **no implicit conversions**. `int64 + int32` is an error; write `a + (b as int64)`. Literals adapt to the other operand: `a + 3` is fine when `a` is `int64`.
10. `"x" + 5` is an error. Convert explicitly: `"x" + 5.to_string()`. `console.print_line` takes a `string`, so `print_line(5)` is an error too; use `console.println("{}", 5)` to format.
11. `int` is `int32`, `float` is `float32`, `byte` is `uint8`. An unannotated integer literal is `int32`; an unannotated float literal is `float64`.
12. Integer literals are decimal only: no `0xFF`, no `1_000`. No character literals: `'a'` is an error.
13. Integer overflow, division by zero, and out-of-range `as` conversions **trap** (end the run). Nothing wraps unless you call a wrapping function such as `Int32.wrapping_add`.
14. Strings are UTF-8. Use `text.byte_length`, not `.length`; `text[0]` is an error, use `text.byte_at(0)` or `text.slice(start, end)`.
15. Conditions must be `bool`; there is no truthiness. `if` and loop bodies always need braces; `else if` is fine.
16. `==` on class references compares identity; `a === b` calls `a.equals(b)`. Use `===` for value equality of objects and wherever you want `equals` semantics.
17. `x == 1 or 2 or 3` is a comparison chain (the subject is evaluated once). `&&` and `||` are the Boolean operators. `and` and `or` are only chain connectors.
18. `a .. b` is a half-open range (`{0 .. 5}` is `[0, 1, 2, 3, 4]`); `a .. b : step` adds a step.
19. Array and list literals do not allow a trailing comma. An empty literal needs an expected type: `let names: string[list] = [];`.
20. `T[]` is a fixed-length array; `T[list]` is a growable list with `append` and `remove_at`. Both have `length`. There is no `new int[5]`; build one from a literal, a range, or `Std.Collections.Lists.filled<T>(count, value)` (a list).

**Generics, enums, and errors**

21. Generic calls **always** need explicit type arguments: `identity<int>(7)`, `maps.create<string, int>()`. `identity(7)` fails with `expected 1 type argument(s)`.
22. Generic enum variants need type arguments when constructed: `optional.Optional<int>.Some(4)`, `result.Result<int>.Err(e)`. Patterns do not: `optional.Optional.Some(value) => ...`.
23. `match` must be exhaustive (or end with `_ =>`). A match expression's arms are expressions separated by commas; a match statement's arms are blocks. Patterns are one level deep, with no guards.
24. There are no exceptions. Expected failures are returned as `Result<T>` or `Optional<T>` (from `Std.Core`) or your own enum; traps cannot be caught.
25. A lambda cannot call itself by name. Use a named declaration for recursion.

**Imports**

26. Every module you use must be imported; there is no prelude. Each `Std` type lives in its own module named after the type, and the module exports the type plus factory functions: `let maps = import:Std.Collections.HashMap;` then `maps.HashMap<string, int>` for the type and `maps.create<string, int>()` for a new map.
27. An import alias shares the file's scope with its declarations. Pick an alias no member uses (`hashing` for `Std.Core.Hash` if the file exports `hash`).
28. `let.promote _ = import:Std.Core.Optional;` brings a module's exports into scope unqualified (`Optional<int>.Some(1)`). See [references/modules.md](references/modules.md).

## Quick syntax tour

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let.promote _ = import:Std.Core.Optional;

/// A documented constant.
const MaxItems = 3;

let Shape = interface (
    let area = func -> float64;
    let describe = func -> string => "area " + area().to_string();
);

let Square = class implements Shape (
    export let side: float64 = 1.0;

    export let init = func(side_length: float64) {
        side = side_length;
    }

    export let area = func -> float64 => side * side;
);

let Color = enum (Red, Green, Custom { name: string });

let label = func(color: Color) -> string {
    return match (color) {
        Color.Red => "red",
        Color.Green => "green",
        Color.Custom { name } => name,
    };
};

let first_even = func(values: int[]) -> Optional<int> {
    for (let value in values) {
        if (value % 2 == 0) {
            return Optional<int>.Some(value);
        }
    }
    return Optional<int>.None;
};

let report = func {
    let shapes: Shape[list] = [];
    shapes.append(new Square(2.0));
    for (let i: int = 0; i < MaxItems; i++) {
        if (i == 1 or 2) {
            continue;
        }
        console.println("{} {}", i, shapes[0].describe());
    }
    let [count, text] = [shapes.length, label(Color.Custom { name: "teal" })];
    match (first_even({1 .. 10})) {
        Optional.Some(value) => { console.print_line("even " + value.to_string()); },
        Optional.None => { console.print_line("none"); },
    }
};
```

## Working method

1. Read the relevant reference page before writing unfamiliar constructs. When a standard library signature matters, grep [references/stdlib-api.md](references/stdlib-api.md) or the installed source below `share/neura/stdlib/std/` (a checkout keeps it in `stdlib/std/`).
2. Write the file, then run `neura check file.nra` and fix every diagnostic. Diagnostics name `path:line:column`.
3. For behavior, build and run: `neura build file.nra -o file.nvr && neura run file.nvr`. For library code, write tests with `Std.Test` and run `neura test`.
4. When a construct is not documented here or in the references, assume it does not exist and check with the compiler rather than inventing it. [Status](https://github.com/Kayyo321/neura/blob/main/docs/lang/Status.md) lists open language decisions.

The language specification lives in the repository's [docs/lang/](https://github.com/Kayyo321/neura/tree/main/docs/lang) and is authoritative when it and this skill disagree.
