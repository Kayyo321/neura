# Contributing to Neura

Read the [project design](docs/Project.md) for the compiler and circuit model, the [language overview](docs/lang/Overview.md) for source-level behavior, and the [coding style guide](docs/style/CodingStyle.md) when writing `.nra` files. Check [language design decisions](docs/lang/Status.md) before treating a proposed feature or illustrative example as a conformance requirement. Follow the [repository rules](README.md#rules-for-agents) when using a coding agent.

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md). Report a vulnerability with the public security report form, as the [security policy](SECURITY.md) describes. [Governance](GOVERNANCE.md) states who reviews and merges changes and who publishes releases.

## Set up a checkout

Neura has one submodule, [`third_party/neura-libclang`](https://github.com/Kayyo321/neura-libclang). It contains the pinned libclang C API headers and a loader that opens the system libclang at run time, so the build never links libclang. CMake configuration fails, printing the commands below, when the submodule has not been fetched.

| Situation | Command |
| --- | --- |
| New clone | `git clone --recursive https://github.com/Kayyo321/neura.git` |
| After a plain `git clone` | `git submodule update --init --recursive` |
| Shallow clone | `git clone --depth 1 --recursive --shallow-submodules https://github.com/Kayyo321/neura.git`, or `git submodule update --init --recursive --depth 1` in an existing shallow checkout |
| After pulling a commit that moves the submodule | `git submodule update --init --recursive` |
| After the submodule URL changes | `git submodule sync --recursive`, then the update command |

`.gitmodules` marks the submodule `shallow`, so `git submodule update` fetches only the pinned commit by default.

Building needs the libffi development package (found with pkg-config), which the runner uses for native calls. C header imports need the libclang shared library from LLVM 14 or newer (19 or newer recommended) and the Clang builtin headers of the same version, such as `stddef.h`. On Debian and Ubuntu, install them with `sudo apt install libclang1-19 libclang-common-19-dev libffi-dev`. The libclang development package, `libclang-19-dev`, is not needed. Set `NEURA_LIBCLANG` to a library's path to use it instead of the one found by the default search. The `neura-libclang-load` test loads libclang and reads a small header through it; CTest reports it as skipped when no libclang is installed, and `NEURA_LIBCLANG_REQUIRED=1` turns that into a failure.

Build and test with CMake 3.20 or newer and C and C++20 compilers:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The build puts the tool at `build/neura`. It reports its version as `<version>-dev`, and it is not an installation that `neura update` manages ([Install and update](docs/Install.md)).

To move Neura to a newer neura-libclang commit, update the submodule in its own commit and describe the header or loader change it brings in:

```sh
git -C third_party/neura-libclang fetch --depth 1 origin main
git -C third_party/neura-libclang checkout FETCH_HEAD
git add third_party/neura-libclang
```

Change the headers and loader in the neura-libclang repository, following its README, rather than in the submodule checkout.

## Define the change

- For a bug, provide the smallest input that reproduces it, the expected behavior, and the actual behavior. Include the first stage where the result diverges if you can identify it. For a runtime issue, include the circuit artifact or its build inputs, the initial state, and any external inputs needed to replay it.
- For a language feature, define the syntax, static rules, observable behavior, and failure cases. Show at least one valid example and one rejected or failing example. Record any unresolved rule that the implementation would otherwise have to decide implicitly.
- For a circuit or artifact change, describe which programs it applies to, how callers or tools observe the change, and how readers handle artifacts produced under earlier supported formats.
- For a documentation change, update the page that owns the behavior. Keep illustrative examples and verified test fixtures distinct. When a change establishes new semantics, keep the language documentation, examples, and applicable tests in agreement.
- For a website change, follow [website maintenance](docs/Site.md). Name the specification page that supports each new statement about Neura, and preview the change before you open the pull request.

## Locate the change in the pipeline

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

| Stage | What to review |
| --- | --- |
| Lexer | Token boundaries, literal spelling, comments, source spans, and diagnostics for invalid characters or malformed tokens. |
| Parser | Grammar, precedence, associativity, and the location and quality of syntax errors. |
| NeuraIR | Name and type resolution, explicit control flow and effects, well-formed typed operations, and the source context needed by later diagnostics. |
| IR optimization | Preconditions for each rewrite, the resulting IR invariants, and preservation of observable behavior, including failures and effects. |
| IRtoNeuronConversion | Exact lowering of supported operations, circuit state and connections, and a diagnostic for unsupported operations or target constraints. |
| Output | Artifact validation, compatibility, stable serialization under the same build conditions, and execution from the serialized artifact. |

Place checks at the first stage with enough information to diagnose the issue. A parser can reject malformed syntax. Type checking and lowering must enforce semantic rules that require resolved names or types. Keep malformed input distinguishable from valid source that a target cannot lower.

When changing multiple stages, explain which invariant crosses each boundary and where it is checked. Keep backend layout choices out of the grammar and preserve enough source information for diagnostics after lowering.

## Verify behavior

Use the repository's applicable checks and report the exact commands and results in the pull request. Identify any behavior you assessed by review alone. For a fix, prefer a small fixture that isolates the failure and a check through the normal entry point when the issue crosses stages.

- **Front end.** Exercise accepted and rejected inputs. Check diagnostic spans and actionable error information. Match full diagnostic text only where the text itself is part of the contract. Tests that match diagnostics pass `--error-format=short`, the stable one-line form. For the rendered form, add a UI test: `tests/ui/<area>/<name>.nra` with the expected standard error in `<name>.stderr` and, when a suggestion applies automatically, the fixed source in `<name>.fixed`. `scripts/ui-test.py` (the `ui-<area>` CTest entries) compares them; run it with `--bless` to rewrite the expectations, then review the diff.
- **IR and optimization.** Validate the resulting IR and compare original and rewritten behavior for the same inputs and initial state. Cover reachable branches, numeric boundaries, and effect or failure ordering where relevant. For rewrites with narrow preconditions, include cases that must bypass the rewrite.
- **Circuit execution.** Run the serialized circuit with known inputs. Compare results, relevant state changes, external events, and defined failures with expected source behavior. Use an independent reference evaluator or hand-derived expectation where feasible. For recurrent circuits, cover multiple steps, termination or bounded execution, and state reset. When testing object semantics, cover independent instances and dispatch through a base-typed reference if supported by the tested subset.
- **Standard library.** Put tests for a module in `tests/stdlib/<area>/` (layout in `tests/stdlib/area.cmake`): a normal result for each public member, each documented `Err` or `None`, and a trap program with its expected diagnostic for each documented trap. Collections need seeded model-based tests and their `validate()` checks; numeric code needs independent reference vectors, with the generator checked in. Host-backed code needs a test with the capability denied and a recorded trace replayed with `neura run --replay`. Run `ctest -R 'stdlib-|backend-reference'`, which also runs each library test under the reference evaluator.
- **Documentation examples.** Every fenced `neura` block in `README.md`, `docs/`, and the agent skill in `prompts/` carries an HTML comment directive on the line above it that says how it is checked, and `scripts/check-doc-examples.py` (the `doc-examples` CTest entry) compiles or runs it with the built `neura`. `<!-- example: check -->` means `neura check` accepts the block as a complete file. `<!-- example: run -->` means the block is a program whose exact output is the `text` block after it. `<!-- example: test -->` means `neura test` passes it. `<!-- example: rejected: <text> -->` means `neura check` fails with a diagnostic containing `<text>`. `<!-- example: fragment: <reason> -->` marks a block that is not a complete file, with a specific reason ("calls fetch_user, which the page does not define"), and is not compiled. Prefer making an illustration complete over marking it a fragment. A block that does not compile because the page documents something the compiler does not implement is a documentation bug to fix, not a fragment.
- **Determinism and reproducibility.** Hold inputs, initial state, runtime limits, and external input traces fixed when comparing executions. Separately check serialized bytes when reproducible output is part of the change. Include the target and toolchain configuration in any reproducibility claim.
- **Decompilation.** Check recovered behavior by executing the recovered representation or by recompiling and comparing it with the original artifact within the supported domain. Include a circuit without optional debug information if the change claims to recover behavior from weights and topology alone. Report unsupported structures and distinguish evidence from sampled tests, exhaustive checks over a finite domain, and formal proofs. Source spelling and names are only meaningful comparison targets when the artifact explicitly preserves them.

Exercise failure paths that the change can affect. Examples include malformed artifacts, arithmetic boundaries, invalid memory access, exhausted execution limits, and unsupported circuit structures. Use the documented behavior of the affected type or feature to decide the expected result. Keep language-level tests independent of test order and follow the conventions in [Language testing](docs/lang/Testing.md).

Every pull request runs the [Tier 1 checks](docs/ContinuousIntegration.md): the documentation and workflow checks, a Release build on Linux x86-64, and the whole CTest suite. A test that is skipped or not run fails the pull request. Run the same commands locally before opening one; that page lists them.

## Review performance changes

Specify the workload, artifact configuration, execution environment, and baseline. For the standard library, `tests/benchmarks` records NVM steps, peak heap cells, neuron count, and image bytes for representative workloads in `tests/benchmarks/baseline.txt`; the `benchmarks` test fails when a figure rises more than 10%, and a change that raises one updates the baseline with the reason. `neura run --profile` shows the steps each function takes. Compare the same inputs and confirm equivalent results before measuring. Record the cost the change targets, such as compile time, circuit size or depth, serialized size, execution steps, runtime, or memory. Include the cost of any work moved to another stage or to the runtime. Explain which workloads benefit and which regress in the measurements.

## Submit a pull request

Every change reaches `main` through a pull request. Create a branch from the current `main`, name it for its purpose (for example `feature/enum-payloads`, `fix/shift-count-trap`, or `docs/numbers-rounding`), and keep it to one topic so it can be reviewed and merged quickly. Open the pull request against `main` and fill in its template. [Governance](GOVERNANCE.md#how-a-change-reaches-main) lists what a pull request needs before it is merged.

- Keep the patch focused and describe the affected stage and contract. Link the relevant design decision when behavior changes.
- Add a line to the **Unreleased** section of the [changelog](CHANGELOG.md) when the change alters behavior a user can observe.
- Include a reproducer or example, test evidence, and any compatibility or migration details a reviewer needs.
- For changes to circuit encoding or runtime behavior, state the input domain, execution limits, and assumptions behind the verification. Include a small artifact or reproducible way to generate it when reviewers need one.
- For dependency, build, or runtime changes, describe how they affect repeatability and what a reviewer needs to reproduce the results.
- Follow the [Apache License 2.0](LICENSE) and preserve applicable notices when incorporating third-party material. Identify its origin and license in the pull request.
