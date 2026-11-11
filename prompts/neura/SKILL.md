---
name: neura
description: Write, read, debug, and review Neura (.nra) source code and drive the `neura` command-line toolchain. Use whenever a task involves a .nra file, a .nvr or .nir artifact, the `neura` CLI (check, build, run, test, explain, tags, emit-ir, decompile, build --native), the Neura standard library (Std.*), or the Neura language itself, including modules, imports, `let.promote`, `last` bindings, target-typed `new`, accessors (`| get:` / `| set:`), anonymous classes, `#` tags (`#NoDiscard`, `#Final`, `#Pure`, `#Synchronized`, ...), interfaces, classes, generics, enums and `match`, `===`, comparison chains, ranges, tasks and monitors (`scope`, `spawn`, `select`), error codes such as E0201, and foreign interop.
---

# Neura

Neura is a statically typed language whose compiler turns programs into deterministic neuron circuits (`.nvr` images) that the Neura Virtual Machine runs. It looks a little like TypeScript and a little like Rust, but most of its rules are its own. **Do not guess syntax from other languages.** Follow this file, then the references below, and check every file you write with `neura check`.

This skill describes Neura 0.2.0. Every Neura block in this skill is compiled or run by the repository's `doc-examples` test (the `<!-- example: ... -->` comment above each block says how), so the snippets are known to be accepted by the compiler.

## The rules agents most often get wrong

1. **Every declaration ends with `;`**, whatever its body: `let f = func { ... };`, `let C = class ( ... );`, `let M = module ( ... );`, an overload group after its last `or func`. Missing: `E0201` (`neura check --fix` inserts it).
2. **Statement blocks take no `;`**: `if (x) { ... }`, `while`, `for`, `match` statements, `scope { }`, `select { }`. A `;` after one is `E0204`.
3. **`last` for anything assigned once.** `last total = a + b;` for a local that never changes; `last label: string;` then one assignment in every branch instead of a placeholder `let`; `export last owner: string;` for a field the constructor sets once. `let` is for values that change, `const` for named module-level constants (`PascalCase`).
4. **`= new;` when the type is already named**: `let origin: Point = new;`, `let p: Point = new(1, 2);`, `return new(x, y);` from a function returning `Point`, `draw(new(3, 4))` for a `Point` parameter. `let a = new;` with no type is `E0420`.
5. **Accessors, not getter/setter methods.** A field the class changes and callers only read: `export let size: int = 0;` with `| get: size;` on the next line. A checked field: add `| set: { ...; level = value; };`. Callers write `obj.size` and `obj.level = 5`: **the field access is the call**; `obj.size()` is `E0423`. Inside the class a bare field name is the storage.
6. **Tags go on their own lines above the declaration**, after its `///` comment: `#Final`, `#NoDiscard`, `#[Final, Since("0.2.0")]`. A tag before a statement is `E0909`; reserved tags such as `#Memoize` or `#Test` are `E0908`.
7. **Use `#NoDiscard` results.** I/O results, resources, `Result`/`Optional` queries, and value classes of the standard library are `#NoDiscard`: `file.write(bytes);` warns `W0003`. Use the value, or `let _ = file.write(bytes);`.
8. **These standard library members are now fields**, read without `()`: `ByteBuffer.size`, `BufferedWriter.buffered`, `Path.is_absolute`, `FileMetadata.kind`/`size`/`modified`, `TcpListener.local_port`, `TcpStream.read_timeout` (assign it; no `set_read_timeout`), `Logger.level` (assign it; no `set_level`), `StringBuilder.byte_length`, `IntRange.start`/`end`/`step`, `Regex.pattern`/`group_count`, `RegexMatch.start`/`end`, `Instant.epoch_seconds`/`nanos`, `LocalDate.year`/`month`/`day`, `LocalDateTime.date`/`time`, `ZoneOffset.total_seconds`, `BigDecimal.unscaled`/`scale`, `Stopwatch.is_running`, `ItemLimits.max_steps`/`max_heap_cells`. Collections keep `size()` and `is_empty()` as methods.

## Reference files

Read the reference that matches the task before writing code. They are in `references/` next to this file.

| File | Read it when |
| --- | --- |
| [references/syntax.md](references/syntax.md) | Writing any code: bindings and `last`, functions, overloads, variadics, closures, control flow, loops, operators, `===`, comparison chains, ranges, tuples and destructuring, enums, `match`, classes, statics, singletons, interfaces, generics, casts, primitive references, `nil`, `destroy`, tags, doc comments. |
| [references/classes.md](references/classes.md) | Writing a class: choosing a field's form, `last` fields, accessors, target-typed `new`, typed destructuring, anonymous classes, and their error codes. |
| [references/tags.md](references/tags.md) | Any `#` tag: syntax and placement, what each implemented tag checks, informational and reserved tags, and which tags a public API carries. |
| [references/modules.md](references/modules.md) | Imports, `let.promote` and wildcards, module interfaces and inheritance, multi-file projects, `#Internal`, the program entry point and exit codes, initialization order, prebuilt `.nir` modules. |
| [references/stdlib.md](references/stdlib.md) | Using `Std.*`: the module-per-type pattern, which members are fields, `#NoDiscard`, console output and formatting, strings, `Optional`/`Result`/`Error`, collections, files, JSON, time, randomness, environment, logging, tests. |
| [references/stdlib-api.md](references/stdlib-api.md) | Looking up the exact exported signatures and tags of every standard library module (generated from `stdlib/`). Search it with grep rather than reading it whole. |
| [references/concurrency.md](references/concurrency.md) | Tasks: `scope`, `spawn`, `join`, `select`, channels, cancellation, timers, `Guarded`, monitors (`#Synchronized`), `#Immutable` sharing, `Parallel.map`, and the isolation rules. |
| [references/cli.md](references/cli.md) | Running the toolchain: every `neura` command and option, `explain`, `tags`, `check --fix`, diagnostic formats and warnings, capabilities, limits, tiers, native executables, tests, exit statuses. |
| [references/errors-and-diagnostics.md](references/errors-and-diagnostics.md) | A compile error or warning you do not understand, a runtime trap, or an exit status. Explains the error codes and `--error-format=json`, and lists real compiler messages with their codes and fixes. |
| [references/foreign.md](references/foreign.md) | Calling C: `Std.DynamicForeign` and `import:c("header.h")`. |
| [references/style.md](references/style.md) | Naming, layout, and the modern idioms for `.nra` files. |

## A modern program

A runnable program is a module that inherits `Std.Program.Entry` and overrides `main`. A file with only top-level statements and no entry module does not build.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let lists = import:Std.Collections.ArrayList;

/// A to-do item; its title is fixed and its state changes only through `finish`.
#Final
let Task = class (
    export last title: string;
    export last priority: int;

    export let done: bool = false;
        | get: done;

    export let init = func(text: string, rank: int) {
        title = text;
        priority = rank;
    };

    export let finish = func {
        done = true;
    };
);

/// A list of tasks with a checked limit.
#Final
let Board = class (
    last tasks: lists.ArrayList<Task> = lists.create<Task>();

    export let limit: int = 3;
        | get: limit;
        | set: {
            if (value < 1) {
                trap_with_message("the limit must be positive");
            }
            limit = value;
        };

    #NoDiscard(reason: "a full board refuses the task")
    export let add = func(task: Task) -> bool {
        if (tasks.size() >= limit) {
            return false;
        }
        tasks.add(task);
        return true;
    };

    export let open_count = func -> int {
        let count = 0;
        for (let task in tasks) {
            if (!task.done) {
                count++;
            }
        }
        return count;
    };
);

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let board: Board = new;
        board.limit = 2;

        let [write, review]: [Task, Task] = new("write the parser", 1), new("review the tests", 2);
        last added = board.add(write) && board.add(review);
        let _ = board.add(new("deploy", 3));            // dropped on purpose: the board is full
        write.finish();

        last summary: string;
        if (board.open_count() == 0) {
            summary = "all done";
        } else {
            summary = board.open_count().to_string() + " open";
        }

        let stats: class (
            export let shown: int = 0;
            export let label: string = "board";
        ) = new;
        stats.shown = board.limit;

        console.println("{}: {} of {}, added both: {}", stats.label, summary, stats.shown, added);
        return program.ExitCode.Ok;
    };
);
```

```text
board: 1 open of 2, added both: true
```

```sh
neura check app.nra                    # type-check only, silent on success
neura check --fix app.nra              # apply certain fixes (missing ';', tag spellings) in place
neura build app.nra -o app.nvr         # compile to an image
neura run app.nvr -- first second      # run; arguments after -- reach main
neura test tests.nra                   # run every exported test_ function
neura explain E0441                    # what an error code means, with a fixed example
```

## 0.1.0 habits and 0.2.0 idioms

| Don't (0.1.0 habit) | Do (0.2.0) |
| --- | --- |
| `let f = func { ... }` with no `;` | `let f = func { ... };` |
| `if (ready) { ... };` | `if (ready) { ... }` |
| `let total = a + b;` that never changes | `last total = a + b;` |
| `let label = "";` overwritten in each branch | `last label: string;` assigned once in each branch |
| `export let owner: string;` set only by `init` | `export last owner: string;` |
| `let origin: Point = new Point();` | `let origin: Point = new;` |
| `return new Point(x, y);` in a function returning `Point` | `return new(x, y);` |
| `export let size = func -> int => count;` | `export let size: int = 0;` + `\| get: size;` |
| `get_level()` / `set_level(v)` methods | `export let level ...` + `\| get:` + `\| set:`; callers write `obj.level = v` |
| `buffer.size()`, `logger.set_level(l)`, `date.year()` | `buffer.size`, `logger.level = l`, `date.year` |
| A one-off class declared only to carry two fields | `let s: class (export let a: int = 0; export let b: int = 0;) = new;` |
| `// deprecated, use area` comment | `#Deprecated(use_instead: area)` |
| A comment saying "the result must be checked" | `#NoDiscard` |
| `file.write(bytes);` | `let _ = file.write(bytes);` or handle the `Result` |
| A mutable object passed to `spawn` (rejected) | A `#Synchronized` monitor, an `#Immutable` object, a `Guarded<T>`, or a channel |
| Matching `path:line:col: error: message` in a script | `--error-format=short` (`error[E0201]: ...`) or `--error-format=json` |

## Other rules that trip up newcomers

**Declarations**

1. Everything is a binding: `let name = func(...) -> T { ... };`, `let Name = class ( ... );`, `let Name = module ( ... );`, `let Name = enum (A, B);`, `let Name = interface ( ... );`. There is no `fn`, `function`, `def`, `struct`, or `type` keyword.
2. Class, module, and interface bodies use **parentheses**, not braces: `class ( ... )`. Function and control-flow bodies use braces.
3. Types follow names: `let count: int32 = 0;`, `func(value: int) -> int`. A `void` function may omit `-> void`; a function with no parameters may omit `()`: `let reset = func { ... };`, `let answer = func -> int => 42;`.
4. Local `let` and `const` bindings require an initializer (`E0404`); a deferred `last x: T;` is the only form without one.
5. Members are private unless marked `export`. A class that implements an interface method must `export` the implementing member, or the compiler reports `member '...' does not match the interface requirement`. An accessor cannot implement an interface member.
6. Overriding uses `let.override` (or `const.override`), including when you give a body to an inherited bodyless signature such as `main`.
7. Constructors are `init` methods; `new Class(args)` or a target-typed `new(args)` calls them. In a derived class, `super.init(...)` must be the first statement. `instance` names the receiver (like `this`). Name `init`'s parameters differently from the fields they set.

**Types and expressions**

8. There are **no implicit conversions**. `int64 + int32` is an error; write `a + (b as int64)`. Literals adapt to the other operand: `a + 3` is fine when `a` is `int64`.
9. `"x" + 5` is an error. Convert explicitly: `"x" + 5.to_string()`. `console.print_line` takes a `string`; use `console.println("{}", 5)` to format.
10. `int` is `int32`, `float` is `float32`, `byte` is `uint8`. An unannotated integer literal is `int32`; an unannotated float literal is `float64`.
11. Integer literals are decimal only: no `0xFF`, no `1_000`. No character literals: `'a'` is an error.
12. Integer overflow, division by zero, and out-of-range `as` conversions **trap** (end the run). Nothing wraps unless you call a wrapping function such as `Int32.wrapping_add`.
13. Strings are UTF-8. Use `text.byte_length`, not `.length`; `text[0]` is an error, use `text.byte_at(0)` or `text.slice(start, end)`.
14. Conditions must be `bool`; there is no truthiness. `if` and loop bodies always need braces; `else if` is fine.
15. `==` on class references compares identity; `a === b` calls `a.equals(b)`.
16. `x == 1 or 2 or 3` is a comparison chain. `&&` and `||` are the Boolean operators; `and` and `or` are only chain connectors.
17. `a .. b` is a half-open range (`{0 .. 5}` is `[0, 1, 2, 3, 4]`); `a .. b : step` adds a step.
18. Array and list literals do not allow a trailing comma. An empty literal needs an expected type: `let names: string[list] = [];`.
19. `T[]` is a fixed-length array; `T[list]` is a growable list with `append` and `remove_at`. Both have `length`. There is no `new int[5]`; use a literal, a range, or `Std.Collections.Lists.filled<T>(count, value)`.

**Generics, enums, and errors**

20. Generic calls **always** need explicit type arguments: `identity<int>(7)`, `maps.create<string, int>()`. A target-typed `new` takes them from the annotation: `let box: Box<int> = new;`.
21. Generic enum variants need type arguments when constructed: `optional.Optional<int>.Some(4)`, `result.Result<int>.Err(e)`. Patterns do not: `optional.Optional.Some(value) => ...`.
22. `match` must be exhaustive (or end with `_ =>`). A match expression's arms are expressions separated by commas; a match statement's arms are blocks. Patterns are one level deep, with no guards.
23. There are no exceptions. Expected failures are returned as `Result<T>` or `Optional<T>` (from `Std.Core`) or your own enum; traps cannot be caught.
24. A lambda cannot call itself by name, and cannot reassign a captured binding. Use a named declaration for recursion and a `Std.Core.Cell` for shared counters.

**Imports**

25. Every module you use must be imported; there is no prelude. Each `Std` type lives in its own module named after the type, and the module exports the type plus factory functions: `let maps = import:Std.Collections.HashMap;` then `maps.HashMap<string, int>` for the type and `maps.create<string, int>()` for a new map.
26. An import alias shares the file's scope with its declarations. Pick an alias no member uses (`hashing` for `Std.Core.Hash` if the file exports `hash`).
27. `let.promote _ = import:Std.Core.Optional;` brings a module's exports into scope unqualified (`Optional<int>.Some(1)`). See [references/modules.md](references/modules.md).

## Working method

1. Read the relevant reference page before writing unfamiliar constructs. When a standard library signature matters, grep [references/stdlib-api.md](references/stdlib-api.md) or the installed source below `share/neura/stdlib/std/` (a checkout keeps it in `stdlib/std/`).
2. Write the file in the modern style above, then run `neura check file.nra`. Fix the **first** error and check again: after parsing, the compiler stops at the first error. For machine-readable output use `--error-format=json` (see [errors-and-diagnostics.md](references/errors-and-diagnostics.md#diagnostics-for-agents---error-format)); `neura explain <CODE>` explains any code, and `neura check --fix` applies the fixes that are certain.
3. Treat warnings as work: `W0003` means a result was dropped, `W0001` a deprecated use with its replacement named.
4. For behavior, build and run: `neura build file.nra -o file.nvr && neura run file.nvr`. For library code, write tests with `Std.Test` and run `neura test`.
5. When a construct is not documented here or in the references, assume it does not exist and check with the compiler rather than inventing it. `neura tags` lists every tag and its status. [Status](https://github.com/Kayyo321/neura/blob/main/docs/lang/Status.md) lists open language decisions.

The language specification lives in the repository's [docs/lang/](https://github.com/Kayyo321/neura/tree/main/docs/lang) and is authoritative when it and this skill disagree.
