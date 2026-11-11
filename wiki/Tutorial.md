<!-- Generated from docs/lang/Tutorial.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# From hello world to a tested project

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This walkthrough introduces the source examples and the [frontend commands](Tooling) for checking them.

1. Read [hello-world.nra](https://github.com/Kayyo321/neura/blob/main/examples/hello-world/hello-world.nra) for an entry module and console output.
2. Read [core.nra](https://github.com/Kayyo321/neura/blob/main/examples/guide/core.nra) for functions, branching and loops.
3. Put reusable exported functions in a module under your source root, using the mapping in [Project structure](ProjectStructure). Import it explicitly in the application and test modules.
4. Add exported `test_` functions under `tests/`, as in [math-tests.nra](https://github.com/Kayyo321/neura/blob/main/examples/testing/math-tests.nra). Run it with `neura test`, naming the test file on the command line ([Testing](Testing)).
5. Check your source with `neura check` or inspect its typed intermediate representation with `neura emit-ir`. Each error names a code such as `E0201`; `neura explain E0201` prints the rule with a failing and a fixed example, and `neura check --fix` applies the fixes that are certain ([Diagnostics](Tooling#diagnostics)). `neura build` and `neura run` compile and execute it; see [Tooling](Tooling) for the commands.
6. Read the [overview](Overview#neura-at-a-glance) and the [coding style](CodingStyle) for how this release writes classes: `last` fields, target-typed `new`, accessors, and tags.

For a concrete multi-file import, see [the existing example](https://github.com/Kayyo321/neura/blob/main/examples/modules/multi-file/main.nra). For failure handling, follow [Errors](Errors) as its result and I/O contracts are specified.
