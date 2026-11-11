# Neura documentation

This documentation describes Neura 0.2.0. `neura --version` prints the installed version ([Install](Install), [Tooling](Tooling#version-install-and-update)); a build from a checkout reports the version in the repository's `VERSION` file with `-dev` added. The [changelog](https://github.com/Kayyo321/neura/blob/main/CHANGELOG.md) lists what changed in each version.

Neura is a statically typed language whose compiler translates programs into deterministic neuron circuits. Its toolchain builds executable images, runs them in the Neura Virtual Machine, and provides tools to inspect and decompile those images.

## Start here

1. [Build and use the toolchain](Tooling) to compile and run a program.
   [Install, update, and uninstall](Install) covers a release archive and the `neura update` command.
2. Follow the [tutorial](Tutorial) and read the [language overview](Overview), which starts with what is new in 0.2.0: `last` bindings, target-typed `new`, anonymous classes, accessors, and [tags](Tags).
3. Explore [neurons and circuits](Neurons-and-Circuits) for the mathematical model behind the execution target.

## Language reference

The reference covers [core syntax and control flow](CoreLanguage), [numbers](Numbers), [types](Types), [modules and imports](Modules), [collections](Collections), [object lifecycle](ObjectLifecycle), [polymorphism](Polymorphism), [tags](Tags), [errors](Errors), [testing](Testing), [resources and concurrency](ResourcesConcurrency), [concurrency](Concurrency), [GPU execution](GpuExecution), [foreign interop](ForeignInterop), and [project structure](ProjectStructure). [Native executables](Standalone) covers packaging a program as a native executable, and [diagnostic codes](Diagnostics) lists every error and warning the compiler reports, which `neura explain <CODE>` explains.

## Standard library

Start with the [standard library overview](StandardLibrary), then use the [library reference and contents](Stdlib) to find individual libraries, their APIs, and their contracts.

## Compiler and execution

The [project design](Project) describes stage boundaries and the execution model. The [backend](Backend) explains circuit conversion, while [NeuraIR optimization](IrOptimization) describes transformations and their correctness requirements. For artifact and runtime details, see [NeuraIR format](NirFormat), [NVR format](NVR), the [Neura Virtual Machine](NVM) and its execution tiers, the built-in [kernels](Kernels) behind parts of the standard library, and [wall-clock benchmarks](Benchmarks). The [decompiler](Decompiler) documentation explains how artifacts can be inspected and reconstructed. Contributors should read the [coding style](CodingStyle), which states how this release writes Neura, and [continuous integration](ContinuousIntegration) pages.

## Reading this documentation

The repository's [language specification](https://github.com/Kayyo321/neura/tree/main/docs/lang) and [project design](https://github.com/Kayyo321/neura/blob/main/docs/Project.md) are the source of truth. This wiki presents those documents for navigation and includes a circuit introduction. When the wiki and repository differ, consult the repository files. The [feature status](Status) page lists what is supported, what is outside this release, and the open language decisions; examples illustrate the language and are not conformance guarantees unless backed by tests. The [release checklist](Release) maps features to verification.
