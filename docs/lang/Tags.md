# Tags

A tag tells the compiler how to treat the declaration below it: that a function is deprecated, that a class may not be extended, that a call's result must be used, or that the optimizer should inline a function. Tags are written with `#` on the lines before the declaration.

<!-- example: check -->
```neura
let Shapes = module (
    #Deprecated(as_of: "0.2.0", use_instead: area, reason: "it rounds the result")
    export let rough_area = func(width: int32) -> int32 {
        return width * width;
    };

    #[Inline(always: true), Since("0.2.0")]
    export let area = func(width: int32) -> int32 {
        return width * width;
    };

    #Internal
    export let cache_size: int32 = 64;
);
```

`neura tags` prints every tag with its arguments, the declarations it applies to, and its status; `neura tags <Name>` prints one. The registry holds 85 tags: 11 implemented, 17 informational, and 57 reserved. The [catalogue](#catalogue) below lists them all.

## Syntax

```text
tags := ( '#' tag | '#' '[' tag (',' tag)* [','] ']' )+
tag  := Name [ '(' [ arg (',' arg)* [','] ] ')' ]
arg  := [ident ':'] literal
```

A literal is a string, an integer or floating-point number (with an optional `-`), `true` or `false`, a qualified name such as `area` or `Shapes.area`, or a bracketed list of those. Tag names are PascalCase. `#[A, B]` is a group of tags on one line; several tag lines and groups may stack, and their tags apply together. Tags belong to the declaration that follows them:

- a `let`, `const`, or `last` declaration, with or without `export` and `static`, including functions, overload groups (the tags apply to every alternative), classes, interfaces, modules, enums, and singletons, at the top of a file, inside a module, class, interface, or [anonymous class](Types.md#anonymous-classes) body, or as a local binding inside a function body;
- an enum variant, written before the variant's name: `#Likely Ready,`;
- a parameter, written before its name: `func(#Unit("ms") timeout: int64)`.

A field's [accessor clauses](Types.md#accessors) are part of its declaration: tags go above the field and apply to it, and a clause takes no tags of its own. A bodyless function bound to a host service or a [kernel](../runtime/Kernels.md) takes tags like any other function.

<!-- example: check -->
```neura
let Ticket = class (
    /// The code printed on the ticket.
    #Deprecated(as_of: "0.2.0", reason: "print the label")
    export let code = "T-1";
        | get: code;

    #Note("set once by the constructor")
    export last label: string;

    export let init = func(text: string) -> void {
        label = text;
    };
);
```

[download.nra](../../examples/tags/download.nra) shows tags with `last` fields, accessors, and anonymous classes in one program.

A tag on a member of an anonymous class makes that body a class of its own where it is written, as a method or an accessor does, so two bodies whose fields differ only in their tags are never the same type ([Types](Types.md#anonymous-classes)).

Tags cannot precede a statement such as a loop, a destructuring declaration, or the discard binding `_`; each is error `E0909`. A documentation comment may stand before the tags, among them, or between them and the declaration; the comment closest to the declaration documents it.

An argument is positional, in the order of the tag's signature, or named, `name: value`; a positional argument cannot follow a named one. An argument without a default must be given. A parameter written `...` in the signature takes any number of positional values, and also accepts a list, so `#Allow(deprecated, experimental)` and `#Allow([deprecated, experimental])` are the same. A name argument (`name` in a signature) accepts a qualified name or a string. A warning argument accepts a warning's name, with `_` for `-` (`unused_result`), or its code (`W0003`), as a name or a string.

## How the compiler treats a tag

Each tag has one of three statuses:

- **Implemented.** The compiler checks or does what the tag says, as described [below](#implemented-tags).
- **Informational.** The compiler validates the tag, records it, and shows it in `neura docs`, but the program behaves as if it were not there. `#Hot`, `#Cold`, `#Likely`, and `#Unlikely` are hints that no pass reads yet.
- **Reserved.** The tag names a promise, such as purity or memoization, that the compiler does not keep or check yet. Its targets and arguments are validated, and then it is error `E0908`, which names what to write instead. A reserved tag is rejected rather than ignored so that no program, and no reader of one, relies on a guarantee that does not hold.

No tag that the optimizer or the runtime relies on is merely trusted: such a property is checked or derived by the compiler, and the tag is reserved until that check exists. There is deliberately no tag that disables bounds checks or overflow checks.

The compiler validates the tags of every declaration while it lowers the program to NeuraIR:

| Rule | Code |
| --- | --- |
| The tag is registered; a close name is suggested, and applied by `neura check --fix` when it is within two edits. | `E0900` |
| The tag applies to this kind of declaration. | `E0901` |
| Each argument exists and is in its place; each required argument is given; none is given twice. | `E0902`, `E0903`, `E0904` |
| Each argument has the type of its parameter. | `E0905` |
| A tag that does not repeat is given once. | `E0906` |
| No two tags contradict each other: `Inline` and `NoInline`, `Final` and `Sealed`, `Hot` and `Cold`, `Likely` and `Unlikely`, `Stable` and `Experimental`, `ThreadSafe` and `NotThreadSafe`, `Blocking` and `NonBlocking`, `Unroll` and `NoUnroll`, or an `Allow` and a `Deny` of the same warning. | `E0907` |
| The tag is not reserved. | `E0908` |
| An `Allow` or `Deny` names a warning that exists. | `E0915` |

Every accepted tag is stored on its symbol as a NeuraIR `tag` attribute ([NeuraIR format, revision 1.10](../NirFormat.md#revision-110-symbol-attributes-and-deferred-initialization)), with the tag's registry id, its name, its arguments, and its source span. A program that imports a prebuilt `.nir` module therefore sees the tags of its declarations, and the use-site checks below apply to them as to source.

## Implemented tags

### Deprecated

`#Deprecated(as_of: string = "", use_instead: name = "", reason: string = "")` marks a declaration that should no longer be used. Every use of it warns with `W0001`, "'f' is deprecated since <as_of>: <reason>", with a label at the tag: a call, a read, a reference to a type in an annotation, a `new`, a base class, or a match arm, an import of a deprecated module, and an override of a deprecated method. Uses inside the deprecated declaration itself, and inside other deprecated code, do not warn. A field is part of its class's representation, so the class's own methods, constructors, initializers, and accessor bodies use a deprecated or experimental field without a warning; a use from outside the class warns once, whether it reads or writes the storage or goes through the field's accessors. When `use_instead` names a declaration beside the deprecated one that the use would reach the same way, the warning suggests the replacement, and `neura check --fix` applies it; otherwise it says what to use in a help line.

<!-- example: warning: W0001 -->
```neura
let Shapes = module (
    #Deprecated(as_of: "0.2.0", use_instead: area)
    export let rough_area = func(width: int32) -> int32 {
        return width * width;
    };

    export let area = func(width: int32) -> int32 {
        return width * width;
    };
);

let total: int32 = Shapes.rough_area(3);
```

A use of a deprecated declaration of the standard library warns in a user program like any other. Warnings that arise in the standard library itself are hidden unless `--warn-library` is given ([Tooling](Tooling.md#diagnostics)).

### Experimental

`#Experimental(feature: string = "")` marks a declaration that may change or disappear without a deprecation period. Every use outside experimental code warns with `W0002`; `#Allow(experimental)` on the code that opts in, or `-Wno-experimental`, silences it.

### Internal

`#Internal` limits a declaration to its library. In the standard library that is the whole standard library, as for modules with an `Internal` path component ([Modules](Modules.md#standard-library-imports)). Elsewhere, the library is the directory of the declaring file with its subdirectories: in the project layout of [Project structure](ProjectStructure.md), the declaring module, its sibling modules, and their children. A use from a file outside that directory tree is error `E0911`, with a label at the tag. `#Internal` composes with `export`: an exported internal member is visible to the other modules of its library and hidden from the programs that use the library. Inside one file the tag has no effect, since a file is always inside its own library.

### Hidden

`#Hidden` leaves a declaration, and everything declared inside it, out of `neura docs` and of the generated standard library API listings. It has no other effect.

### NoDiscard

`#NoDiscard(reason: string = "")` on a function or method says that its result must be used. On a class, interface, or enum, it says the same of every call that returns that type, and on a class also of `new`. An expression statement that calls such a function, or that is a `new` of such a class (`new Lease();`), discards the result and warns with `W0003`, suggesting `let _ = ...;`, which discards a value on purpose and does not warn. `#NoDiscard` on a function that returns `void` is error `E0916`.

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

### Final and Sealed

`#Final` on a class forbids every subclass (`E0912`); on a method it forbids every override (`E0913`). `#Sealed` on a class or interface allows subclasses and implementations only inside the module that declares it, or inside its file when it is declared at the top of a file (`E0914`), so that every subtype is known where the type is declared. A class cannot be both `#Final` and `#Sealed`. [Polymorphism](Polymorphism.md) describes inheritance and overriding.

### Inline and NoInline

`#Inline(always: bool = false)` and `#NoInline` give a function or method the inlining directive that the `inline-calls` optimization pass reads ([IR optimization](../IrOptimization.md)). `#NoInline` sets the directive to never: the pass leaves every call of the function in place. `#Inline(always: true)` sets it to always, which lifts the pass's size budget. The pass still refuses a function that may trap, calls another function, is recursive, allocates, touches the heap or module state, or is part of a program that uses tasks, because inlining would change a trap's location, the frames, or the task schedule. When it refuses an `always` function, the optimizer warns once with `W0004` and the reason. `#Inline` without `always: true` records the request and leaves the decision to the pass's ordinary rules. The directive changes no output or trap: an optimized and an unoptimized build of the same program print the same thing, and only `--stats` step counts differ.

### Allow and Deny

`#Allow(warnings...)` silences the named warnings (`W0001` to `W0005`, among them the accessors' `setter-never-stores`, [W0005](../diagnostics/W0005.md)) inside the tagged declaration and everything declared inside it, and `#Deny(warnings...)` makes them errors there, with a label at the `#Deny`. The innermost tag that names a warning decides. Both apply to warnings that are reported inside the declaration, whatever declaration they are about. Command-line options do not override them: `-Wno-<name>` does not hide a denied warning, and `-Werror` makes the remaining warnings errors.

<!-- example: check -->
```neura
let Api = module (
    #Deprecated
    export let old = func() -> int32 {
        return 1;
    };
);

#Allow(deprecated)
let Compatibility = module (
    export let read = func() -> int32 {
        return Api.old();
    };
);
```

## Catalogue

Each table gives a tag's signature, the declarations it applies to, its status, and its meaning; a reserved tag also gives what to write instead. "Declarations" are modules, classes, interfaces, enums, variants, functions, methods, fields, and file and module bindings, but not locals or parameters.

### API lifecycle

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Deprecated(as_of: string = "", use_instead: name = "", reason: string = "")` | declarations | implemented | Every use outside deprecated code warns (W0001), naming the replacement. |
| `#Experimental(feature: string = "")` | declarations | implemented | Every use outside experimental code warns (W0002). |
| `#Internal` | declarations except variants | implemented | Usable only inside the declaring library: its directory tree, or the whole standard library. |
| `#Hidden` | declarations | implemented | Left out of `neura docs` and the generated API listings; no other effect. |
| `#Since(version: string)` | declarations | informational | The release that introduced the declaration. |
| `#Stable` | declarations | informational | The declaration's interface will not change incompatibly. |
| `#Removed(version: string, reason: string = "")` | declarations | informational | The release in which the declaration will be or was removed. |
| `#PreviewFeature(name: string)` | declarations | informational | The preview feature the declaration belongs to. |

### Correctness

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#NoDiscard(reason: string = "")` | classes, interfaces, enums, functions, methods | implemented | Discarding the result of a call warns (W0003); on a type, any call that returns it. |
| `#Final` | classes, methods | implemented | A class that no class may extend, or a method that no method may override. |
| `#Sealed` | classes, interfaces | implemented | Extended or implemented only inside the module or file that declares it. |
| `#Immutable` | classes, fields, file and module bindings | reserved | No field of the value changes after construction. Instead: declare the fields with const; the compiler checks #Immutable in a later release. |
| `#Pure` | functions, methods | reserved | No observable effects, and the same result for the same arguments. Instead: the optimizer already infers purity; the compiler checks #Pure in a later release. |
| `#Total` | functions, methods | reserved | Returns normally for every argument: never traps or loops forever. Instead: document the guarantee and test it; no analysis proves it yet. |
| `#NoTrap` | functions, methods | reserved | Never traps. Instead: document the guarantee and test the boundary values; no analysis proves it yet. |
| `#Idempotent` | functions, methods | reserved | Calling it twice has the effect of calling it once. Instead: document the property in the doc comment. |
| `#Commutative` | functions, methods | reserved | The two arguments may be swapped. Instead: document the property in the doc comment. |
| `#Associative` | functions, methods | reserved | Grouping of repeated calls does not matter. Instead: document the property in the doc comment. |
| `#Unsafe(reason: string = "")` | modules, classes, functions, methods | reserved | Callers must uphold a rule the compiler cannot check. Instead: Neura has no unchecked operations; state the rule in the doc comment. |
| `#Requires(condition: string)` | functions, methods | reserved, repeatable | A precondition checked on entry. Instead: check the condition at the start of the body and call trap_with_message. |
| `#Ensures(condition: string)` | functions, methods | reserved, repeatable | A postcondition checked on return. Instead: check the condition before each return. |
| `#Invariant(condition: string)` | classes | reserved, repeatable | A condition that holds between method calls. Instead: write a validate() method and call it in tests. |

### Optimization

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Inline(always: bool = false)` | functions, methods | implemented | Asks inline-calls to inline the function; always: true lifts the size budget (W0004 when refused). |
| `#NoInline` | functions, methods | implemented | Inline-calls never inlines the function. |
| `#Hot` | functions, methods | informational | A hint that the function runs often; no effect today. |
| `#Cold` | functions, methods | informational | A hint that the function runs rarely; no effect today. |
| `#Likely` | variants, functions, methods | informational | A hint that calls or this variant are common; no effect today. |
| `#Unlikely` | variants, functions, methods | informational | A hint that calls or this variant are rare; no effect today. |
| `#Unroll(count: integer?)` | functions, methods | reserved | Unroll the loops of the function. Instead: unroll the loop by hand where it matters. |
| `#NoUnroll` | functions, methods | reserved | Never unroll the loops of the function. Instead: no pass unrolls loops today; omit the tag. |
| `#Vectorize` | functions, methods | reserved | Evaluate the loops of the function several items at a time. Instead: use Parallel.map or the GPU graph device for data-parallel work. |
| `#Memoize(capacity: integer?)` | functions, methods | reserved | Cache results by argument. Instead: keep a Std.Collections.HashMap of results. |
| `#Specialize(types: name...?)` | classes, functions, methods | reserved, repeatable | Compile separate copies for the named type arguments. Instead: every generic is already specialized for each instantiation; omit the tag. |
| `#TailCall` | functions, methods | reserved | Require that the self-calls in tail position reuse the frame. Instead: rewrite the recursion as a loop. |
| `#Flatten` | functions, methods | reserved | Inline every call made by the function. Instead: mark the callees #Inline(always: true). |
| `#CompileTime` | functions, methods, fields, file and module bindings | reserved | Evaluate during compilation. Instead: a const binding with a literal initializer is already folded; use const. |

### Concurrency

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#ThreadSafe` | modules, classes, functions, methods | reserved | Safe to use from several tasks at once. Instead: the compiler checks #ThreadSafe in a later release; `neura check --task-safety` reports it today. |
| `#NotThreadSafe` | modules, classes, functions, methods | reserved | Must not be shared between tasks. Instead: the compiler checks #NotThreadSafe in a later release; document the restriction. |
| `#Synchronized` | methods | reserved | The method runs under its instance's guard. Instead: keep the state in a Std.Concurrent.Guarded and call with(); #Synchronized lands in a later release. |
| `#Isolated` | functions, methods | reserved | Touches no state shared with other tasks. Instead: `neura check --task-safety` reports which functions can be a parallel worker. |
| `#Atomic` | fields, file and module bindings | reserved | Reads and writes are indivisible across tasks. Instead: use Std.Concurrent.AtomicInt64 or a Guarded. |
| `#Blocking` | functions, methods | reserved | May wait for another task. Instead: document it; the scheduler already suspends a task that waits. |
| `#NonBlocking` | functions, methods | reserved | Never waits for another task. Instead: document it; no analysis proves it yet. |
| `#Cancellable` | functions, methods | reserved | Returns early when its task is cancelled. Instead: poll the cancellation of the task scope explicitly. |
| `#GuardedBy(guard: name)` | fields, file and module bindings | reserved | Accessed only while the named guard is held. Instead: keep the value inside the Guarded itself. |

### Testing

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Test(name: string = "")` | functions, methods | reserved | A test case. Instead: declare tests with Std.Test as docs/lang/Testing.md describes. |
| `#Ignore(reason: string = "")` | functions, methods | reserved | A test that is skipped. Instead: leave the test out of the suite and say why in a comment. |
| `#Benchmark` | functions, methods | reserved | A benchmark. Instead: measure with `neura run --stats` or the tests/bench-wall harness. |
| `#Fixture` | classes, functions, methods | reserved | Shared test setup. Instead: call a setup function at the start of each test. |
| `#Timeout(milliseconds: integer)` | functions, methods | reserved | Fail the test after the given time. Instead: run the test with `neura test --max-steps`. |
| `#ExpectTrap(message: string = "")` | functions, methods | reserved | A test that passes when it traps. Instead: write a trap program with its expected diagnostic. |
| `#Property(cases: integer?)` | functions, methods | reserved | A property-based test over generated inputs. Instead: write a seeded loop over random inputs. |
| `#BeforeEach` | functions, methods | reserved | Runs before each test. Instead: call the function at the start of each test. |
| `#AfterEach` | functions, methods | reserved | Runs after each test. Instead: call the function at the end of each test. |
| `#Repeat(count: integer)` | functions, methods | reserved | Runs a test several times. Instead: loop inside the test. |

### Documentation

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Example(text: string)` | declarations | informational, repeatable | A short usage example. |
| `#SeeAlso(target: name)` | declarations | informational, repeatable | A related declaration or document. |
| `#Author(name: string)` | declarations | informational, repeatable | An author of the declaration. |
| `#Todo(text: string)` | declarations, locals, parameters | informational, repeatable | Work still to do. |
| `#Fixme(text: string)` | declarations, locals, parameters | informational, repeatable | A known defect to fix. |
| `#Note(text: string)` | declarations, locals, parameters | informational, repeatable | A remark for readers. |
| `#License(spdx: string)` | declarations | informational | The SPDX license expression of the declaration. |
| `#Owner(team: string)` | declarations | informational, repeatable | The team that maintains the declaration. |

### Interop and layout

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Export(symbol: string?)` | functions, methods | reserved | Make the function callable from C. Instead: Neura calls C through import:c; C cannot call into an image yet. |
| `#Callback` | functions, methods | reserved | A function C code may call back. Instead: pass a Neura function value to Std.DynamicForeign, which supports upcalls. |
| `#NativeName(name: string)` | functions, methods, fields, file and module bindings | reserved | The C symbol name of a declaration. Instead: import:c names declarations as the header does. |
| `#Repr(layout: name)` | classes, enums | reserved | A fixed memory layout. Instead: object layout is the runtime's; marshal through Std.DynamicForeign. |
| `#Align(bytes: integer)` | classes, fields | reserved | A minimum alignment. Instead: object layout is the runtime's; marshal through Std.DynamicForeign. |
| `#Packed` | classes | reserved | No padding between fields. Instead: object layout is the runtime's; marshal through Std.DynamicForeign. |

### Diagnostics control

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Allow(warnings: warning...)` | declarations except variants | implemented, repeatable | Silences the named warnings inside the declaration. |
| `#Deny(warnings: warning...)` | declarations except variants | implemented, repeatable | Turns the named warnings into errors inside the declaration. |
| `#Expect(warnings: warning...)` | declarations except variants | reserved, repeatable | Silences the named warnings and reports when none occurs. Instead: use #Allow. |

### Security

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#RequiresCapability(capability: name...)` | modules, functions, methods | reserved, repeatable | Callers need the named runtime capability. Instead: capabilities are granted per run (`neura run --deny-*`); document the requirement. |
| `#Sensitive` | classes, fields, file and module bindings, locals, parameters | reserved | A secret kept out of traces, dumps, and to_string. Instead: avoid printing the value; --record and --task-dump may contain it. |
| `#Untrusted` | functions, methods, fields, file and module bindings, parameters | reserved | Data from outside the program that must be validated before use. Instead: validate the value where it enters the program. |
| `#ConstantTime` | functions, methods | reserved | Runs in time independent of secret inputs. Instead: write the comparison without early exits; no analysis proves it yet. |

### Serialization

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Serializable` | classes, enums | reserved | Can be written and read by Std serializers. Instead: write to_json and from_json with Std.Encoding.Json explicitly. |
| `#SerialName(name: string)` | classes, variants, fields | reserved | The name used when serialized. Instead: choose the key in the explicit to_json. |
| `#Skip` | fields | reserved | Left out when serialized. Instead: leave the field out of the explicit to_json. |
| `#SerialDefault(value: literal)` | fields | reserved | The value used when the input has no entry. Instead: supply the default in the explicit from_json. |

### Resource management

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Resource` | classes | reserved | Instances must be destroyed before they become unreachable. Instead: destroy the instance explicitly or bind it in a scope block. |
| `#NoAlloc` | functions, methods | reserved | Never allocates heap cells. Instead: measure peak_heap_cells with `neura run --stats`. |
| `#MaxSteps(steps: integer)` | functions, methods | reserved | A step budget for each call. Instead: bound the whole run with --max-steps. |

### Numerics

| Tag | Applies to | Status | Meaning |
| --- | --- | --- | --- |
| `#Unit(name: string)` | functions, methods, fields, file and module bindings, locals, parameters | informational | The unit of measure of the value or result, such as "ms". |
| `#Range(min: number, max: number)` | functions, methods, fields, file and module bindings, locals, parameters | reserved | The value always lies within [min, max]. Instead: check the bounds where the value is produced and trap_with_message. |

## Adding or enabling a tag

The registry is `src/neura-ir/tags.def`: one line per tag with its stable id, targets, argument schema, and status. A new tag appends a line with the next free id. A reserved tag is enabled by changing its status to `implemented` and adding the check or behavior it promises; the validation of its targets and arguments already runs. A tag that a later release retires stays registered as reserved, so that its id is never reused.
