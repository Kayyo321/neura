<!-- Generated from docs/lang/Overview.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Neura language overview

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This documentation describes Neura 0.1.0, the version in the repository's `VERSION` file. `neura --version` prints the installed version ([Install](Install), [Tooling](Tooling#version-install-and-update)); a build from a checkout reports `0.1.0-dev`.

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](Tooling) compiles Neura programs to standalone `.nvr` images and runs them. It can also package the image and its runtime in a native executable. [Status](Status) lists what this release supports and the language decisions that remain open.

Start with [Core language](CoreLanguage), [Numbers](Numbers), [Types](Types), [Modules](Modules), and [Collections](Collections). Then read [Errors](Errors), [Testing](Testing), the [standard library overview](StandardLibrary) and its [reference](Stdlib), [Resources and concurrency](ResourcesConcurrency), [Foreign interop](ForeignInterop), and [Tooling](Tooling). [Status](Status) records the boundaries of this design. [Tutorial](Tutorial) walks through a small project. [Concurrency](Concurrency) specifies tasks, channels, cancellation, and the tools for concurrent programs.

The language favors deterministic diagnostics, memory-safe collection operations, and explicit interfaces.
