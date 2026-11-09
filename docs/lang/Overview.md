# Neura language overview

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [frontend](Tooling.md) checks and lowers the repository’s illustrative `.nra` programs to typed NeuraIR. The language pages also describe proposed behavior that requires further specification or an executor.

Start with [Core language](CoreLanguage.md), [Types](Types.md), [Modules](Modules.md), and [Collections](Collections.md). Then read [Errors](Errors.md), [Testing](Testing.md), [Standard library](StandardLibrary.md), [Resources and concurrency](ResourcesConcurrency.md), and [Tooling](Tooling.md). [Status](Status.md) records the boundaries of this design. [Tutorial](Tutorial.md) walks through a small project.

The initial implementation should favor deterministic diagnostics, memory-safe collection operations, and explicit interfaces. This is a design direction, not a claim that every feature in these pages is ready to implement without further decisions.
