# Neura language overview

This documentation describes Neura 0.2.0. `neura --version` prints the installed version ([Install](../Install.md), [Tooling](Tooling.md#version-install-and-update)); a build from a checkout reports the version in the repository's `VERSION` file with `-dev` added. The [changelog](../../CHANGELOG.md) lists what changed in each version.

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](Tooling.md) compiles Neura programs to standalone `.nvr` images and runs them in the Neura Virtual Machine. It can also package the image and its runtime in a native executable. [Status](Status.md) lists what this release supports and the language decisions that remain open.

## New in 0.2.0

- **Every declaration ends with `;`**, including a function, class, or module whose body ends at `}` or `)`. Statement blocks take none. `neura check --fix` adds a missing one ([Core language](CoreLanguage.md#functions-and-control-flow)).
- **`last`** declares a binding that is assigned exactly once, at its declaration or later on every path before it is read ([Core language](CoreLanguage.md#source-and-bindings), [`last` fields](ObjectLifecycle.md#last-fields)).
- **Target-typed `new`**: `let origin: Point = new;` constructs the type that the annotation, the assignment, the result type, or the parameter names ([Object lifecycle](ObjectLifecycle.md#target-typed-new)). A destructuring declaration can annotate its names and give each its own value ([Types](Types.md#typed-and-listed-destructuring)).
- **Anonymous classes**: a class body can be written where a type is expected ([Types](Types.md#anonymous-classes)).
- **Accessors**: an exported field can have `| get:` and `| set:` clauses, which `object.field` and `object.field = value` call from outside the class ([Types](Types.md#accessors)).
- **Tags**: `#Deprecated`, `#NoDiscard`, `#Final`, `#Pure`, `#Synchronized`, and the other tags of the [catalogue](Tags.md) tell the compiler how to treat a declaration, and the compiler checks the ones it implements.
- **Monitors**: a class whose methods are `#Synchronized` can be shared by tasks ([Concurrency](Concurrency.md#81-monitors)).
- **Diagnostics** have codes, source snippets, labels, and suggested fixes; `neura explain <CODE>` prints the page of a code ([Tooling](Tooling.md#diagnostics), [Diagnostic codes](../diagnostics/README.md)).
- **Kernels and execution tiers**: parts of the standard library run on built-in C [kernels](../runtime/Kernels.md), and the runtime runs proven blocks natively ([execution tiers](../runtime/NVM.md#execution-tiers)). Neither changes what a program computes.

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

Start with [Core language](CoreLanguage.md), [Numbers](Numbers.md), [Types](Types.md), [Object lifecycle](ObjectLifecycle.md), [Modules](Modules.md), and [Collections](Collections.md). Then read [Polymorphism](Polymorphism.md), [Tags](Tags.md), [Errors](Errors.md), [Testing](Testing.md), the [standard library overview](StandardLibrary.md) and its [reference](../../wiki/Stdlib.md), [Resources and concurrency](ResourcesConcurrency.md), [Foreign interop](ForeignInterop.md), and [Tooling](Tooling.md). [Concurrency](Concurrency.md) specifies tasks, channels, cancellation, monitors, and the tools for concurrent programs. [Tutorial](Tutorial.md) walks through a small project, and the [coding style](../style/CodingStyle.md) states how Neura code is written. [Status](Status.md) records the boundaries of this design.

The language favors deterministic diagnostics, memory-safe collection operations, and explicit interfaces.
