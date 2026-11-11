<!-- Generated from docs/lang/Overview.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Neura language overview

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This documentation describes Neura 0.2.0. `neura --version` prints the installed version ([Install](Install), [Tooling](Tooling#version-install-and-update)); a build from a checkout reports the version in the repository's `VERSION` file with `-dev` added. The [changelog](https://github.com/Kayyo321/neura/blob/main/CHANGELOG.md) lists what changed in each version.

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](Tooling) compiles Neura programs to standalone `.nvr` images and runs them in the Neura Virtual Machine. It can also package the image and its runtime in a native executable. [Status](Status) lists what this release supports and the language decisions that remain open.

## New in 0.2.0

- **Every declaration ends with `;`**, including a function, class, or module whose body ends at `}` or `)`. Statement blocks take none. `neura check --fix` adds a missing one ([Core language](CoreLanguage#functions-and-control-flow)).
- **`last`** declares a binding that is assigned exactly once, at its declaration or later on every path before it is read ([Core language](CoreLanguage#source-and-bindings), [`last` fields](ObjectLifecycle#last-fields)).
- **Target-typed `new`**: `let origin: Point = new;` constructs the type that the annotation, the assignment, the result type, or the parameter names ([Object lifecycle](ObjectLifecycle#target-typed-new)). A destructuring declaration can annotate its names and give each its own value ([Types](Types#typed-and-listed-destructuring)).
- **Anonymous classes**: a class body can be written where a type is expected ([Types](Types#anonymous-classes)).
- **Accessors**: an exported field can have `| get:` and `| set:` clauses, which `object.field` and `object.field = value` call from outside the class ([Types](Types#accessors)).
- **Tags**: `#Deprecated`, `#NoDiscard`, `#Final`, `#Pure`, `#Synchronized`, and the other tags of the [catalogue](Tags) tell the compiler how to treat a declaration, and the compiler checks the ones it implements.
- **Monitors**: a class whose methods are `#Synchronized` can be shared by tasks ([Concurrency](Concurrency#81-monitors)).
- **Diagnostics** have codes, source snippets, labels, and suggested fixes; `neura explain <CODE>` prints the page of a code ([Tooling](Tooling#diagnostics), [Diagnostic codes](Diagnostics)).
- **Kernels and execution tiers**: parts of the standard library run on built-in C [kernels](Kernels), and the runtime runs proven blocks natively ([execution tiers](NVM#execution-tiers)). Neither changes what a program computes.

## Neura at a glance

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

/// An account whose balance only its own methods change.
#Final
let Account = class (
    export last owner: string;

    /// The balance in cents: read-only outside the class.
    #Unit("cents")
    export let balance: int64 = 0;
        | get: balance;

    export let init = func(name: string) {
        owner = name;
    };

    export let deposit = func(amount: int64) {
        balance += amount;
    };

    #NoDiscard(reason: "the result says whether the money moved")
    export let withdraw = func(amount: int64) -> bool {
        if (amount > balance) {
            return false;
        }
        balance -= amount;
        return true;
    };
);

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let account: Account = new("Ada");
        account.deposit(250);
        last paid = account.withdraw(100);

        let limits: class (
            export let daily: int64 = 500;
            export let currency: string = "EUR";
        ) = new;

        console.println("{} has {} {}, paid: {}, daily limit {}",
            account.owner, account.balance, limits.currency, paid, limits.daily);
        return program.ExitCode.Ok;
    };
);
```

```text
Ada has 150 EUR, paid: true, daily limit 500
```

`Account` is `#Final`, so no class extends it. `owner` is a `last` field that the constructor assigns once, and `balance` has only a getter, so code outside the class reads `account.balance` and cannot assign it. `withdraw` is `#NoDiscard`: a call that drops its result warns. `new("Ada")` constructs the `Account` that the annotation names, and `limits` is an object of an anonymous class built from its field initializers.

## Reading order

Start with [Core language](CoreLanguage), [Numbers](Numbers), [Types](Types), [Object lifecycle](ObjectLifecycle), [Modules](Modules), and [Collections](Collections). Then read [Polymorphism](Polymorphism), [Tags](Tags), [Errors](Errors), [Testing](Testing), the [standard library overview](StandardLibrary) and its [reference](Stdlib), [Resources and concurrency](ResourcesConcurrency), [Foreign interop](ForeignInterop), and [Tooling](Tooling). [Concurrency](Concurrency) specifies tasks, channels, cancellation, monitors, and the tools for concurrent programs. [Tutorial](Tutorial) walks through a small project, and the [coding style](CodingStyle) states how Neura code is written. [Status](Status) records the boundaries of this design.

The language favors deterministic diagnostics, memory-safe collection operations, and explicit interfaces.
