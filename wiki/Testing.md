<!-- Generated from docs/lang/Testing.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Testing

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

`neura test` builds and runs tests. A test is an exported, zero-argument function returning `void` whose name starts with `test_`, declared in a module of one of the files listed on the command line. Imported files contribute no tests. Functions that are not exported, take arguments, or return a value are not tests.

```text
neura test -I examples/guide examples/testing/math-tests.nra
```

Tests import [`Std.Test`](Stdlib-Design#142-stdtest), whose assertions are ordinary Neura functions: `assert`, `assert_with`, `fail`, the typed `assert_equal_int32`, `assert_equal_int64`, `assert_equal_uint64`, `assert_equal_bool`, `assert_equal_string`, `assert_equal_float64`, and `assert_float_near`, the generic `assert_equal<T>` and `assert_not_equal<T>`, and `assert_some<T>`, `assert_none<T>`, `assert_ok<T>`, and `assert_err<T>` for `Optional` and `Result` values. A failed assertion builds its message and ends the test with a message trap (reason 3). The runner reports that message at the innermost frame that belongs to one of the listed test files, which is the assertion's call site, and continues with the next test. A test that returns normally passes. A test that traps or faults otherwise fails with the runtime diagnostic.

```text
PASS Suite.test_counter_starts_fresh
before the failing assertion
FAIL Suite.test_unequal: expected <3>, got <4> at suite.nra:25:9
FAIL Suite.test_trap: index out of bounds: index 3 for length 1 at suite.nra:35:21
3 test(s), 1 passed, 2 failed
```

Tests may be advanced together. Each test’s output, host effects, and report line appear in declaration order, exactly as when run one at a time. Each test runs in a fresh execution of one image: module globals, the heap, and every object start from the program's initial state, so a test cannot observe another's effects and order does not matter. Before each test, the program's initialization runs, as it does before `main` ([Modules](Modules#initialization)). Output from `Std.Io.Console` appears interleaved with the report. When a test ends, the runner closes every file and socket it left open.

The runner exits with status 0 when every test passes and 1 when any test fails, when no test is found, or when the build fails. `--no-optimize` builds without optimization, and `--max-steps`, `--max-frames`, and `--max-heap-cells` limit each test as they limit `neura run`. `--stats` appends each passing test's step count and peak heap cells. The capability options of `neura run` apply to every test, and so do its task options: the task limits, `--task-quantum`, `--parallel-workers`, and `--task-dump` ([Tooling](Tooling#run-programs-with-tasks)).

## Tests of concurrent code

A test may open scopes, spawn tasks, and use everything in `Std.Concurrent`. A test is a function body, so the rules for tasks apply to it unchanged, and a test that leaves every task parked fails with the deadlock diagnostic. Three modes make such tests fast and repeatable. [Concurrency, section 14.4](Concurrency#144-testing-concurrent-code) specifies them.

**Virtual time.** `neura test --virtual-time` gives each test a clock of its own that starts at zero and moves only when every task of the test is parked, to the earliest timer, deadline, or sleep. A test of a two-hour timeout finishes at once, and the time it observes is exact: after `clock.sleep(durations.of_hours(2))`, the monotonic clock has moved by 7,200,000,000,000 nanoseconds. The wall clock reads the virtual clock's nanoseconds after 2000-01-01T00:00:00Z. The clock capability still applies. Sockets stay real, and the clock does not wait for them, so a test that needs a timeout and a real peer together runs without the option.

**Schedule exploration.** The reference scheduler runs tasks in the order they became runnable, so a test that wrongly depends on that order passes every time. `neura test --schedules N` runs each test under up to `N` legal schedules: the reference schedule first, then schedules that pick among the runnable tasks at every decision, from `--schedule-seed S` (default 0). The same seed gives the same schedules in every run. A test stops at its first failing schedule, and each line reports how many schedules ran:

```text
PASS OrderTests.test_sum_is_order_free [8 schedules]
FAIL OrderTests.test_assumes_spawn_order [schedule 2 of 8, seed 1]: expected <1>, got <2> at order-tests.nra:39:9
PASS OrderTests.test_without_tasks [1 schedule]
explored 11 schedule(s) of 3 test(s), seed 0
3 test(s), 2 passed, 1 failed
```

A test in which no decision offered a choice has one schedule. The `explored` line counts the schedules that ran over all tests. Exploration is bounded: a pass says nothing about schedules that did not run. It varies which runnable task goes next, and not the moment a host request is answered, so a test with timers is explored under `--virtual-time` as well.

**Reproduction.** `--save-schedule <file>` writes the first failing schedule of the run, and `neura test --schedule <file>` with the same test files runs that one test under exactly that schedule:

```text
FAIL OrderTests.test_assumes_spawn_order [saved schedule]: expected <1>, got <2> at order-tests.nra:39:9
1 test(s), 0 passed, 1 failed
```

The file is text: a `neura-schedule 1` line, the test's name, the seed, the quantum, whether virtual time was on, and one `choose <tick> <position>` line for each decision that differed from the reference scheduler's. It is not a host trace, and `neura run --replay` does not take it. A schedule that names a test the files do not define, or that does not fit the program, is reported as such.

With `--schedules` above 1 or `--virtual-time`, tests run one at a time on the CPU. The tests named in these examples are those of `tests/concurrency/schedules/order-tests.nra`; which schedule fails first, and so the total, follow from the seed.

There is no assertion that expects a trap: a trap cannot be caught. Code that must trap is tested by a separate program with an expected diagnostic, as the standard library's trap programs under `tests/stdlib/<area>/traps` are.

The built-in `Test` module and its host services `neura.test.assert` and `neura.test.assert_equal_int32` are retired; importing `Test` is an error that names `Std.Test`.

Wall-clock test timeouts, fixtures, mocking, parameterized and property tests, coverage, and running tests in parallel are future work. The step limit bounds what a test computes. It does not bound a test parked on a real timer or socket; `--virtual-time` removes the wait for timers. See [the example tests](https://github.com/Kayyo321/neura/blob/main/examples/testing/math-tests.nra), which import [Core](https://github.com/Kayyo321/neura/blob/main/examples/guide/core.nra).
