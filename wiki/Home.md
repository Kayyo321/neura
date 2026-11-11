# Neura documentation

Neura is a statically typed language whose compiler translates programs into deterministic neuron circuits. Its toolchain builds executable images, runs them in the Neura Virtual Machine, and provides tools to inspect and decompile those images.

## Start here

1. [Build and use the toolchain](Tooling) to compile and run a program.
   [Install, update, and uninstall](Install) covers a release archive and the `neura update` command.
2. Follow the [tutorial](Tutorial) and read the [language overview](Overview).
3. Explore [neurons and circuits](Neurons-and-Circuits) for the mathematical model behind the execution target.

## Language reference

The reference covers [core syntax and control flow](CoreLanguage), [numbers](Numbers), [types](Types), [modules and imports](Modules), [collections](Collections), [object lifecycle](ObjectLifecycle), [polymorphism](Polymorphism), [errors](Errors), [testing](Testing), [resources and concurrency](ResourcesConcurrency), [concurrency](Concurrency), [GPU execution](GpuExecution), [foreign interop](ForeignInterop), and [project structure](ProjectStructure). [Native executables](Standalone) covers packaging a program as a native executable.

## Standard library

Start with the [standard library overview](StandardLibrary), then use the [library reference and contents](Stdlib) to find individual libraries, their APIs, and their contracts.

## Compiler and execution

The [project design](Project) describes stage boundaries and the execution model. The [backend](Backend) explains circuit conversion, while [NeuraIR optimization](IrOptimization) describes transformations and their correctness requirements. For artifact and runtime details, see [NeuraIR format](NirFormat), [NVR format](NVR), and the [Neura Virtual Machine](NVM). The [decompiler](Decompiler) documentation explains how artifacts can be inspected and reconstructed. Contributors should read the [coding style](CodingStyle) and [continuous integration](ContinuousIntegration) pages.

## Reading this documentation

The repository's [language specification](https://github.com/Kayyo321/neura/tree/main/docs/lang) and [project design](https://github.com/Kayyo321/neura/blob/main/docs/Project.md) are the source of truth. This wiki presents those documents for navigation and includes a circuit introduction. When the wiki and repository differ, consult the repository files. The [feature status and decisions](Status) page distinguishes implemented behavior from proposals and unresolved rules; examples illustrate the language and are not conformance guarantees unless backed by tests. The [release checklist](Release) maps features to verification.
