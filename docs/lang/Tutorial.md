# From hello world to a tested project

This walkthrough introduces the source examples and the [frontend commands](Tooling.md) for checking them.

1. Read [hello-world.nra](../../examples/hello-world/hello-world.nra) for an entry module and console output.
2. Read [core.nra](../../examples/guide/core.nra) for functions, branching and loops.
3. Put reusable exported functions in a module under your source root, using the mapping in [Project structure](ProjectStructure.md). Import it explicitly in the application and test modules.
4. Add exported `test_` functions under `tests/`, as in [math-tests.nra](../../examples/testing/math-tests.nra). Run it with `neura test`, naming the test file on the command line ([Testing](Testing.md)).
5. Check your source with `neura check` or inspect its typed intermediate representation with `neura emit-ir`. Each error names a code such as `E0201`; `neura explain E0201` prints the rule with a failing and a fixed example, and `neura check --fix` applies the fixes that are certain ([Diagnostics](Tooling.md#diagnostics)). `neura build` and `neura run` compile and execute it; see [Tooling](Tooling.md) for the commands.
6. Read the [overview](Overview.md#neura-at-a-glance) and the [coding style](../style/CodingStyle.md) for how this release writes classes: `last` fields, target-typed `new`, accessors, and tags.

For a concrete multi-file import, see [the existing example](../../examples/modules/multi-file/main.nra). For failure handling, follow [Errors](Errors.md) as its result and I/O contracts are specified.
