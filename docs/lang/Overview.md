# Neura language overview

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](Tooling.md) compiles Neura programs to standalone `.nvr` images and runs them. [Status](Status.md) separates what this release supports from proposed behavior that still requires a language decision.

Start with [Core language](CoreLanguage.md), [Numbers](Numbers.md), [Types](Types.md), [Modules](Modules.md), and [Collections](Collections.md). Then read [Errors](Errors.md), [Testing](Testing.md), [Standard library](StandardLibrary.md) and its [implementation plan](Stdlib.md), [Resources and concurrency](ResourcesConcurrency.md), [Foreign interop](ForeignInterop.md), and [Tooling](Tooling.md). [Status](Status.md) records the boundaries of this design. [Tutorial](Tutorial.md) walks through a small project. Two plans describe work that is not implemented yet: [Concurrency](Concurrency.md) and [Native executables](Standalone.md).

The initial implementation should favor deterministic diagnostics, memory-safe collection operations, and explicit interfaces. This is a design direction, not a claim that every feature in these pages is ready to implement without further decisions.
