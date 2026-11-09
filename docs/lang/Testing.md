# Testing

`neura test` builds and runs tests. A test is an exported, zero-argument function returning `void` whose name starts with `test_`, declared in a module of one of the files listed on the command line. Imported files contribute no tests. Functions that are not exported, take arguments, or return a value are not tests.

```text
neura test -I examples/guide examples/testing/math-tests.nra
```

The built-in `Test` module provides `assert(bool)` and `assert_equal_int32(int32 expected, int32 actual)`. A failed assertion ends its test; the runner reports the expected and actual values and the assertion's source location and continues with the next test. A test that returns normally passes. A test that traps or faults fails with the runtime diagnostic.

```text
PASS Suite.test_counter_starts_fresh
before the failing assertion
FAIL Suite.test_unequal: expected 3, got 4 at suite.nra:25:9
FAIL Suite.test_trap: index out of bounds: index 3 for length 1 at suite.nra:35:21
3 test(s), 1 passed, 2 failed
```

Tests run serially in declaration order. Each test runs in a fresh execution of one image: module globals, the heap, and every object start from the program's initial state, so a test cannot observe another's effects and order does not matter. Before each test, the program's initialization runs, as it does before `main` ([Modules](Modules.md#initialization)). Output from `io.println` appears interleaved with the report.

The runner exits with status 0 when every test passes and 1 when any test fails, when no test is found, or when the build fails. `--no-optimize` builds without optimization, and `--max-steps`, `--max-frames`, and `--max-heap-cells` limit each test as they limit `neura run`.

Assertions bind to the host services `neura.test.assert` and `neura.test.assert_equal_int32`, version 1 ([NVM](../runtime/NVM.md#host-services)). `neura run` offers them too, so a program that calls `Test` directly traps at its first failed assertion.

Test timeouts beyond the step limit, fixtures, mocking, parameterized and property tests, coverage, and parallel execution are future work. See [the example tests](../../examples/testing/math-tests.nra), which import [Core](../../examples/guide/core.nra).
