# From hello world to a tested project

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)


This walkthrough introduces the source examples and the [frontend commands](https://github.com/Kayyo321/neura/wiki/Tooling) for checking them.

1. Read [hello-world.nra](https://github.com/Kayyo321/neura/blob/main/examples/hello-world/hello-world.nra) for an entry module and console output.
2. Read [core.nra](https://github.com/Kayyo321/neura/blob/main/examples/guide/core.nra) for functions, branching and loops.
3. Put reusable exported functions in a module under your source root, using the mapping in [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure). Import it explicitly in the application and test modules.
4. Add exported `test_` functions under `tests/`, as in [math-tests.nra](https://github.com/Kayyo321/neura/blob/main/examples/testing/math-tests.nra). Supply the test module explicitly to the future runner.
5. Check your source with `neura check` or inspect its typed intermediate representation with `neura emit-ir`. See [Tooling](https://github.com/Kayyo321/neura/wiki/Tooling) for build instructions and the proposed execution workflow.

For a concrete multi-file import, see [the existing example](https://github.com/Kayyo321/neura/blob/main/examples/modules/multi-file/main.nra). For failure handling, follow [Errors](https://github.com/Kayyo321/neura/wiki/Errors) as its result and I/O contracts are specified.
