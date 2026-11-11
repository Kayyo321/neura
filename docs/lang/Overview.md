# Neura language overview

This documentation describes Neura 0.1.0, the version in the repository's `VERSION` file. `neura --version` prints the installed version ([Install](../Install.md), [Tooling](Tooling.md#version-install-and-update)); a build from a checkout reports `0.1.0-dev`.

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](Tooling.md) compiles Neura programs to standalone `.nvr` images and runs them. It can also package the image and its runtime in a native executable. [Status](Status.md) lists what this release supports and the language decisions that remain open.

Start with [Core language](CoreLanguage.md), [Numbers](Numbers.md), [Types](Types.md), [Modules](Modules.md), and [Collections](Collections.md). Then read [Tags](Tags.md), [Errors](Errors.md), [Testing](Testing.md), the [standard library overview](StandardLibrary.md) and its [reference](../../wiki/Stdlib.md), [Resources and concurrency](ResourcesConcurrency.md), [Foreign interop](ForeignInterop.md), and [Tooling](Tooling.md). [Status](Status.md) records the boundaries of this design. [Tutorial](Tutorial.md) walks through a small project. [Concurrency](Concurrency.md) specifies tasks, channels, cancellation, and the tools for concurrent programs.

The language favors deterministic diagnostics, memory-safe collection operations, and explicit interfaces.
