# Neura language overview

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)


Neura is a statically typed language organized around modules, classes, and explicit interfaces. The [toolchain](https://github.com/Kayyo321/neura/wiki/Tooling) compiles Neura programs to standalone `.nvr` images and runs them. [Status](https://github.com/Kayyo321/neura/wiki/Status) separates what this release supports from proposed behavior that still requires a language decision.

Start with [Core language](https://github.com/Kayyo321/neura/wiki/CoreLanguage), [Numbers](https://github.com/Kayyo321/neura/wiki/Numbers), [Types](https://github.com/Kayyo321/neura/wiki/Types), [Modules](https://github.com/Kayyo321/neura/wiki/Modules), and [Collections](https://github.com/Kayyo321/neura/wiki/Collections). Then read [Errors](https://github.com/Kayyo321/neura/wiki/Errors), [Testing](https://github.com/Kayyo321/neura/wiki/Testing), [Standard library](https://github.com/Kayyo321/neura/wiki/StandardLibrary) and its [implementation plan](https://github.com/Kayyo321/neura/wiki/Stdlib), [Resources and concurrency](https://github.com/Kayyo321/neura/wiki/ResourcesConcurrency), and [Tooling](https://github.com/Kayyo321/neura/wiki/Tooling). [Status](https://github.com/Kayyo321/neura/wiki/Status) records the boundaries of this design. [Tutorial](https://github.com/Kayyo321/neura/wiki/Tutorial) walks through a small project.

The initial implementation should favor deterministic diagnostics, memory-safe collection operations, and explicit interfaces. This is a design direction, not a claim that every feature in these pages is ready to implement without further decisions.
