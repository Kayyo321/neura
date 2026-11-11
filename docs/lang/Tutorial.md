# From hello world to a tested project

This walkthrough introduces the source examples and the [frontend commands](Tooling.md) for checking them.

1. Read [hello-world.nra](../../examples/hello-world/hello-world.nra) for an entry module and console output.
2. Read [core.nra](../../examples/guide/core.nra) for functions, branching and loops.
3. Put reusable exported functions in a module under your source root, using the mapping in [Project structure](ProjectStructure.md). Import it explicitly in the application and test modules.
4. Add exported `test_` functions under `tests/`, as in [math-tests.nra](../../examples/testing/math-tests.nra). Run it with `neura test`, naming the test file on the command line ([Testing](Testing.md)).
5. Check your source with `neura check` or inspect its typed intermediate representation with `neura emit-ir`. `neura build` and `neura run` compile and execute it; see [Tooling](Tooling.md) for the commands.

For a concrete multi-file import, see [the existing example](../../examples/modules/multi-file/main.nra). For failure handling, follow [Errors](Errors.md) as its result and I/O contracts are specified.
