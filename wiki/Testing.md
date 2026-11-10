# Testing

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)


`neura test` builds and runs tests. A test is an exported, zero-argument function returning `void` whose name starts with `test_`, declared in a module of one of the files listed on the command line. Imported files contribute no tests. Functions that are not exported, take arguments, or return a value are not tests.

```text
neura test -I examples/guide examples/testing/math-tests.nra
```

Tests import [`Std.Test`](https://github.com/Kayyo321/neura/wiki/Stdlib#142-stdtest), whose assertions are ordinary Neura functions: `assert`, `assert_with`, `fail`, the typed `assert_equal_int32`, `assert_equal_int64`, `assert_equal_uint64`, `assert_equal_bool`, `assert_equal_string`, `assert_equal_float64`, and `assert_float_near`, the generic `assert_equal<T>` and `assert_not_equal<T>`, and `assert_some<T>`, `assert_none<T>`, `assert_ok<T>`, and `assert_err<T>` for `Optional` and `Result` values. A failed assertion builds its message and ends the test with a message trap (reason 3). The runner reports that message at the innermost frame that belongs to one of the listed test files, which is the assertion's call site, and continues with the next test. A test that returns normally passes. A test that traps or faults otherwise fails with the runtime diagnostic.

```text
PASS Suite.test_counter_starts_fresh
before the failing assertion
FAIL Suite.test_unequal: expected <3>, got <4> at suite.nra:25:9
FAIL Suite.test_trap: index out of bounds: index 3 for length 1 at suite.nra:35:21
3 test(s), 1 passed, 2 failed
```

Tests run serially in declaration order. Each test runs in a fresh execution of one image: module globals, the heap, and every object start from the program's initial state, so a test cannot observe another's effects and order does not matter. Before each test, the program's initialization runs, as it does before `main` ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)). Output from `Std.Io.Console` appears interleaved with the report.

The runner exits with status 0 when every test passes and 1 when any test fails, when no test is found, or when the build fails. `--no-optimize` builds without optimization, and `--max-steps`, `--max-frames`, and `--max-heap-cells` limit each test as they limit `neura run`. `--stats` appends each passing test's step count and peak heap cells. The capability options of `neura run` apply to every test ([Tooling](https://github.com/Kayyo321/neura/wiki/Tooling#build-and-run-executables)).

There is no assertion that expects a trap: a trap cannot be caught. Code that must trap is tested by a separate program with an expected diagnostic, as the standard library's trap programs under `tests/stdlib/<area>/traps` are.

The built-in `Test` module and its host services `neura.test.assert` and `neura.test.assert_equal_int32` are retired; importing `Test` is an error that names `Std.Test`.

Test timeouts beyond the step limit, fixtures, mocking, parameterized and property tests, coverage, and parallel execution are future work. See [the example tests](https://github.com/Kayyo321/neura/blob/main/examples/testing/math-tests.nra), which import [Core](https://github.com/Kayyo321/neura/blob/main/examples/guide/core.nra).
