# Tags

A tag, written with `#` on the lines above a declaration, tells the compiler how to treat it: warn at its uses, forbid subclasses, require that its result be used, check that it is pure, or guard it for tasks. Specification: [Tags](https://github.com/Kayyo321/neura/blob/main/docs/lang/Tags.md). `neura tags` prints the whole catalogue and `neura tags <Name>` one tag; trust that output over memory.

## Contents

- [Syntax and placement](#syntax-and-placement)
- [What the compiler does with a tag](#what-the-compiler-does-with-a-tag)
- [Implemented tags](#implemented-tags)
- [Informational tags](#informational-tags)
- [Reserved tags are errors](#reserved-tags-are-errors)
- [Which tags to write](#which-tags-to-write)

## Syntax and placement

```text
tags := ( '#' tag | '#' '[' tag (',' tag)* [','] ']' )+
tag  := Name [ '(' [ arg (',' arg)* ] ')' ]
arg  := [name ':'] literal        -- string, number, true/false, a qualified name, or [a list]
```

<!-- example: check -->
```neura
let Shapes = module (
    /// The area of a square. The doc comment goes above the tags.
    #[Since("0.2.0"), Stable]
    #Deprecated(as_of: "0.2.0", use_instead: area, reason: "it rounds the result")
    export let rough_area = func(width: int32) -> int32 {
        return width * width;
    };

    export let area = func(width: int32) -> int32 {
        return width * width;
    };

    export let Status = enum (#Likely Ready, #Unlikely Failed);

    export let wait = func(#Unit("ms") timeout: int64) -> int64 => timeout;
);

let Meter = class (
    #Unit("mm")
    export let reading: int = 0;
        | get: reading;

    #Note("set once by the constructor")
    export last label: string;

    export let init = func(text: string) {
        label = text;
    };
);
```

- A tag goes on its own line directly above a `let`, `const`, or `last` declaration (with or without `export`/`static`): a function, overload group, class, interface, module, enum, singleton, field, module binding, or local. Several tag lines stack; `#[A, B]` groups tags on one line.
- An enum variant or a parameter takes its tags on the same line, before its name.
- A field's accessor clauses belong to the field: tags go above the field, never between the field and its `| get:`/`| set:` lines.
- **Not allowed** before a statement (`while`, `if`, an expression), a destructuring declaration, or `_`: `E0909` `expected a declaration after tags`.
- Arguments are positional in the signature's order or named (`name: value`); a positional argument cannot follow a named one. A warning name accepts `unused_result` or `"unused-result"` or its code `W0003`.
- Tag validation: unknown tag `E0900` (the fix suggests the right name), wrong target `E0901`, bad arguments `E0902`–`E0905`, repeated `E0906`, contradictory pair such as `Inline` with `NoInline` or `Final` with `Sealed` `E0907`, reserved `E0908`.
- A tag on a member of an anonymous class makes that body a class of its own (it no longer matches the same fields written elsewhere).

## What the compiler does with a tag

- **Implemented** tags are checked or acted on.
- **Informational** tags are validated, recorded, and shown by `neura docs`; the program behaves as without them.
- **Reserved** tags name a promise the compiler cannot keep yet; using one is error `E0908`, whose message says what to write instead.

Tags persist in `.nir` files, so a program that imports a prebuilt module sees and checks them.

## Implemented tags

| Tag | Applies to | What the compiler does |
| --- | --- | --- |
| `#Deprecated(as_of: string = "", use_instead: name = "", reason: string = "")` | declarations | Every use outside deprecated code warns `W0001`; with `use_instead`, `neura check --fix` rewrites the use. |
| `#Experimental(feature: string = "")` | declarations | Every use outside experimental code warns `W0002`. |
| `#Internal` | declarations except variants | Usable only inside its library: the declaring file's directory tree, or the whole standard library (`E0911`). |
| `#Hidden` | declarations | Left out of `neura docs` and the generated API listings. |
| `#NoDiscard(reason: string = "")` | classes, interfaces, enums, functions, methods | A statement that drops the result warns `W0003`; on a type, every call returning it and every `new` of it. `let _ = f();` drops on purpose. On a `void` function: `E0916`. |
| `#Final` | classes, methods | No subclass (`E0912`) or no override (`E0913`). |
| `#Sealed` | classes, interfaces | Subtypes only inside the declaring module, or file at top level (`E0914`). |
| `#Inline(always: bool = false)`, `#NoInline` | functions, methods | Directs the optimizer's inlining; an `always` function it cannot inline warns `W0004`. Changes no output. |
| `#Allow(warnings...)`, `#Deny(warnings...)` | declarations except variants | Silence the named warnings inside the declaration, or make them errors there. The innermost tag wins; `-Wno-` does not hide a denied warning. |
| `#Pure` | functions, methods | Checked (`E0917`): writes no global, static, or foreign object state, reads no reassignable binding, makes no host request or scheduler operation, uses no object identity, calls no function value and only pure functions. May allocate, trap, and loop. |
| `#Immutable` | classes | Checked: every instance field `last` or `const` (`E0918`) of an immutable type (`E0919`), bases and subclasses `#Immutable` (`E0920`). Tasks may share the instances. |
| `#ThreadSafe` | classes, functions, methods | Checked: the function is task-safe (`E0608`, with the call chain), or the class is `#Immutable` or a monitor (`E0609`) with task-safe exported methods. |
| `#NotThreadSafe` | classes, functions, methods | No spawned task or parallel worker may call it (`E0601`) or receive the class's instances (`E0602`). |
| `#Synchronized` | classes, methods | The method runs with exclusive access to its receiver, first come, first served, re-entrant for calls on its own receiver; on a class, every exported method with a body. Not on constructors, destructors, statics, bodyless methods, or kernel or host bindings (`E0921`); an override of a synchronized method must be synchronized (`E0922`). See [concurrency.md](concurrency.md#monitors). |

<!-- example: warning: W0003 -->
```neura
#NoDiscard(reason: "the result says whether the write happened")
let write = func(text: string) -> bool {
    return text.byte_length > 0;
};

let save = func() -> void {
    write("data");
};
```

The fix is `let _ = write("data");`, or using the result.

<!-- example: check -->
```neura
#Pure
let squares = func(count: int32) -> int64[list] {
    let result: int64[list] = [];
    for (let i = 0; i < count; i++) {
        result.append((i as int64) * (i as int64));
    }
    return result;
};

/// A value tasks can share without a guard.
#[Immutable, Final]
let Point = class (
    export last x: int32;
    export last y: int32;

    export let init = func(left: int32, top: int32) {
        x = left;
        y = top;
    };
);
```

## Informational tags

`#Since(version)`, `#Stable`, `#Removed(version, reason)`, `#PreviewFeature(name)`, `#Hot`, `#Cold`, `#Likely`, `#Unlikely` (hints no pass reads yet), `#Example(text)`, `#SeeAlso(target)`, `#Author(name)`, `#Todo(text)`, `#Fixme(text)`, `#Note(text)`, `#License(spdx)`, `#Owner(team)`, and `#Unit(name)`. `#Todo`, `#Fixme`, `#Note`, and `#Unit` also apply to locals and parameters.

## Reserved tags are errors

These parse but fail with `E0908`; do not write them. The message names the alternative.

| Group | Reserved tags | Write instead |
| --- | --- | --- |
| Correctness | `Total`, `NoTrap`, `Idempotent`, `Commutative`, `Associative`, `Unsafe`, `Requires`, `Ensures`, `Invariant` | A doc comment and tests; check a precondition at the top of the body and `trap_with_message`. |
| Optimization | `Unroll`, `NoUnroll`, `Vectorize`, `Memoize`, `Specialize`, `TailCall`, `Flatten`, `CompileTime` | Nothing (generics are already specialized, `const` literals already folded), a `HashMap` cache, a loop instead of recursion, `#Inline(always: true)` on callees, or `Parallel.map`. |
| Concurrency | `Isolated`, `Atomic`, `Blocking`, `NonBlocking`, `Cancellable`, `GuardedBy` | `neura check --task-safety`, `AtomicInt64`, `Guarded<T>`, or polling cancellation. |
| Testing | `Test`, `Ignore`, `Benchmark`, `Fixture`, `Timeout`, `ExpectTrap`, `Property`, `BeforeEach`, `AfterEach`, `Repeat` | `Std.Test` with exported `test_` functions ([cli.md](cli.md#test)). |
| Interop and layout | `Export`, `Callback`, `NativeName`, `Repr`, `Align`, `Packed` | `import:c` and `Std.DynamicForeign` ([foreign.md](foreign.md)). |
| Diagnostics | `Expect` | `#Allow`. |
| Security | `RequiresCapability`, `Sensitive`, `Untrusted`, `ConstantTime` | Document it; deny capabilities per run with `--deny-*`. |
| Serialization | `Serializable`, `SerialName`, `Skip`, `SerialDefault` | Explicit `to_json` and `from_json` with `Std.Encoding.Json`. |
| Resources and numerics | `Resource`, `NoAlloc`, `MaxSteps`, `Range` | `destroy` or `close()`, `--stats`, `--max-steps`, or a bounds check that traps. |

## Which tags to write

- On a library's public API, as the standard library does: `#Since("x.y.z")` on modules, exported types, and new members; `#Final` on classes not designed for extension (value classes, resource handles); `#NoDiscard` on functions returning a `Result`, an `Optional`, a computed value, or a resource that must be closed; `#Deprecated(use_instead: ...)` before removing something.
- `#Pure`, `#Immutable`, `#ThreadSafe`, `#Synchronized` where the property is part of the contract. The optimizer infers purity on its own, so do not add `#Pure` only for speed.
- `#Internal` on helpers that sibling modules share and users must not call.
- Put the doc comment first, then the tags, then the declaration.
