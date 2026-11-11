<!-- Generated from docs/lang/Overview.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Neura language overview

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](Tooling) compiles Neura programs to standalone `.nvr` images and runs them. It can also package the image and its runtime in a native executable. [Status](Status) separates what this release supports from proposed behavior that still requires a language decision.

Start with [Core language](CoreLanguage), [Numbers](Numbers), [Types](Types), [Modules](Modules), and [Collections](Collections). Then read [Errors](Errors), [Testing](Testing), the [standard library overview](StandardLibrary) and its [reference](Stdlib), [Resources and concurrency](ResourcesConcurrency), [Foreign interop](ForeignInterop), and [Tooling](Tooling). [Status](Status) records the boundaries of this design. [Tutorial](Tutorial) walks through a small project. [Concurrency](Concurrency) specifies tasks, channels, cancellation, and the tools for concurrent programs.

The initial implementation should favor deterministic diagnostics, memory-safe collection operations, and explicit interfaces. This is a design direction, not a claim that every feature in these pages is ready to implement without further decisions.
