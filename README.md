# Neura

Neura is a statically typed programming language and a toolchain for compiling its programs into deterministic neuron circuits and decompiling those circuits into readable representations. The compiler derives the topology and weights from the program. The resulting circuits can be run, inspected, tested, and optimized against the behavior defined by the language.

## Compilation and execution

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

The compiler checks source semantics before producing typed NeuraIR. Optimization preserves the program's observable behavior, and the conversion stage maps the optimized IR to neuron primitives. The output describes the circuit, its weights, and the inputs, state, and runtime capabilities it needs.

Pure computations can use feedforward circuits. Stateful execution carries mutable data and control state across steps. Classes share method circuits while each instance holds its own fields. Decompilation recovers behavior from supported artifacts and can use optional metadata to improve readability. Verification compares the recovered representation with the circuit over a defined input and state domain.

## Documentation

- [Project design](docs/Project.md) covers the pipeline, execution model, decompilation, and verification.
- [Language overview](docs/lang/Overview.md) and [tutorial](docs/lang/Tutorial.md) cover Neura syntax and semantics. See [examples](examples/) for sample `.nra` programs.
- [Tooling](docs/lang/Tooling.md) covers building the compiler, checking source, inspecting NeuraIR, and building and running `.nvr` images.
- [Neura Virtual Machine](docs/runtime/NVM.md) and [NVR format](docs/runtime/NVR.md) define execution and the executable image. [Neuron conversion and execution](docs/Backend.md) describes the implementation.
- [Contributing](CONTRIBUTING.md) explains how to propose changes and verify them.

## Rules for agents

- Read [AGENTS.md](AGENTS.md), the relevant language spec, and [Coding style](docs/style/CodingStyle.md) before changing compiler behavior or `.nra` examples.
- Preserve the pipeline boundaries. Keep source semantics in typed NeuraIR, preserve observable behavior during optimization, and lower program logic into executable neuron primitives.
- Check semantic changes with a representative program and a failure or boundary case. Report which checks actually ran and their scope. Identify unresolved language behavior before relying on it.
- Keep language rules, examples, and affected stage contracts in sync when a change alters behavior.

## License

[Apache License 2.0](LICENSE)
