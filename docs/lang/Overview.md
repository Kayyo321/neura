# Neura language overview

Neura is a proposed statically typed language organized around modules, classes, and explicit interfaces. This repository contains design documents and illustrative `.nra` programs, not a compiler. No example has been compiler-verified. The documents describe an initial design target; “proposed” means a decision for implementation, not an existing feature.

Start with [Core language](CoreLanguage.md), [Types](Types.md), [Modules](Modules.md), and [Collections](Collections.md). Then read [Errors](Errors.md), [Testing](Testing.md), [Standard library](StandardLibrary.md), [Resources and concurrency](ResourcesConcurrency.md), and [Tooling](Tooling.md). [Status](Status.md) records the boundaries of this design. [Tutorial](Tutorial.md) walks through a small project.

The initial implementation should favor deterministic diagnostics, memory-safe collection operations, and explicit interfaces. This is a design direction, not a claim that every feature in these pages is ready to implement without further decisions.
