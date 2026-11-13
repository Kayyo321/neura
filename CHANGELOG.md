# Changelog

This file records the changes in each version of Neura that a user of the language, the `neura` command, the standard library, or the `.nir` and `.nvr` formats can observe. Its layout follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versions are numbered `MAJOR.MINOR.PATCH`. Before `1.0.0`, a minor version may change or remove existing behavior, and its entry lists each such change under **Changed** or **Removed**. The `.nir` and `.nvr` formats carry their own version numbers, which the [NeuraIR format](docs/NirFormat.md) and [NVR format](docs/runtime/NVR.md) pages define.

A pull request that changes observable behavior adds a line to **Unreleased** under one of these headings:

| Heading | For |
| --- | --- |
| Added | New syntax, library members, commands, options, or format features. |
| Changed | Existing behavior that now differs, including a diagnostic or exit status that programs or scripts may depend on. |
| Deprecated | Behavior that still works and is scheduled for removal. |
| Removed | Behavior that no longer exists. |
| Fixed | Behavior corrected to match its specification. |
| Security | Fixes for vulnerabilities reported under the [security policy](SECURITY.md). |

## Unreleased

### Added

- Research-candidate editor tooling for the separate Neura VS Code extension: `neura lsp --stdio`, compiler-backed unsaved analysis and semantic editing, `neura fmt`, `neura debug --stdio`, and `neura capabilities --json`.
- Semantic test discovery (`test --list --format=json`), stable exact test filters, a versioned live JSON reporter, and mapped-block source-line coverage. Existing terminal test output remains the default.
- Read-only image/graph JSON inspection through `neura inspect-image --json`.
- `neura update --nightly` includes published prereleases when selecting the newest version. It also works with `--check`; ordinary updates continue to use stable releases.
- `neura run --tier-stats` (and `run --each` and `neura test` with it) ends with a line that divides the command's wall time among loading the image, proving, searching, tier 0, tier 1, regions, garbage collection, host services, and the rest ([controls](docs/runtime/NVM.md#controls)). The existing lines are unchanged.

### Changed

- Native code for tier 2 regions now computes `float32` and `float64` arithmetic, comparisons, and conversions with the processor's scalar instructions instead of calling the runtime for each operation (x86-64 and AArch64). Results are bit for bit those of the reference evaluator, including canonical NaNs; only speed changes ([native compilation](docs/runtime/NVM.md#native-compilation)).
- The overflow proof that lets a block run as native operations is about three to four times faster: the analysis keeps the results of enumerations it has already made, re-evaluates only the nodes an assignment changes, and no longer rebuilds its bookkeeping for every wire. It proves exactly what it proved before.
- Blocks of `float64` squaring, multiplication by a constant, subtraction of a constant, and division by a converted integer are now proven and run as native operations; in 0.2.0 their proof failed with "an output lane not proven 0 or 1" and they stayed on the reference evaluator, which held `nbody`, `mandelbrot`, and `spectral-norm` there for most of their time. The proof of a `float64` or `int64` multiplication or division also takes about a tenth of the time. The circuit library now records which gadget built each neuron while a block is regenerated, and the overflow analysis tries the cut that gadget's own argument uses first. The record is advice: every range is still established by evaluating over all assignments of a cut ([execution tiers](docs/runtime/NVM.md#execution-tiers)).
- The compiler records lift hints for the blocks it builds itself, which in 0.2.0 had none and so ran on the reference evaluator however often they ran: the narrowing of a length to `int32`, the loops of the functions generated for concatenation, text hashing, and value members, value equality of integers, `hash_code` of a Boolean, integer, or float, and the nil tests before a value member is dispatched. With them, 4% to 10% of the steps of the map, text, closure, and dispatch benchmarks move off the reference evaluator. The `LIFT` section gains two operator codes for this, 23 (`-~`, subtraction without an overflow check) and 24 (`hash`). The section stays optional and never trusted, and every other byte of an image is unchanged. A 0.2.0 runtime reads a section that uses either code as malformed and ignores it whole, as the format prescribes for hints it cannot read, so such an image runs there correctly and without hints ([lift hints](docs/runtime/NVR.md#lift-hints), [backend](docs/Backend.md)).
- A block with a lift hint is now proven after it has run 4 times instead of 256, and its proof may use about 16 ms of work at the first attempt. Proofs became cheap enough that waiting cost more than proving: the float benchmarks at their full sizes take about a third to a half of the time they took with the old thresholds. Which blocks run lifted, and when, changes; nothing a program can observe does ([execution tiers](docs/runtime/NVM.md#execution-tiers)).

### Fixed

- The website places one skill-copy button above its prompt with feedback that keeps the page in place, keeps the front page's frame fitted during mobile scrolling, and gives diagnostic codes a compact Reading Room overview.
- The compiler no longer reads freed memory while lowering three constructs, which 0.2.0 does: an accessor whose body declares names (the `W0005` and `E0405` messages were built from a freed field name), a generic enum applied to an anonymous class (`Optional<class (...)>.None` with many fields could crash the compiler), and a target-typed `new` of an anonymous class whose field initializer declares many names (it could be rejected with a wrong `E0403` or crash). Programs that compiled are compiled to the same image.
- Three defects of the overflow proof that 0.2.0 has. The proof accepted a graph in which a node reads one wire twice, counting the two edges as the two halves of a step pair; it divided the most negative 64-bit weight by -1, which is undefined; and when a wire's range was 2^63 wide or more it looped over that range without counting the work against its limit. The loader rejects the first kind of graph before any proof, and no graph that the circuit library builds was found to reach the other two, so no image is known to have run wrongly. The proof now refuses a graph whose edges are not in strictly ascending order of source itself, and its arithmetic and loops are bounded on every input ([execution tiers](docs/runtime/NVM.md#execution-tiers)).

## [0.2.0] - 2026-11-11

This version adds assign-once `last` bindings, target-typed `new`, anonymous classes in type positions, accessors on exported fields, and `#` tags to the language; requires a `;` after every declaration; gives every diagnostic a code, a rendered source snippet, and a page that `neura explain` prints; runs parts of the standard library on built-in C kernels; and adds two execution tiers above the reference evaluator: blocks proven to compute typed operations run as native operations, and regions of them run together, as machine code the runner generates on x86-64 Linux. The standard library is rewritten in the new style, and some of its zero-argument methods are now fields. The [release checklist](docs/Release.md) maps each feature to its tests.

### Migrating from 0.1.0

- **Semicolons.** Every declaration now ends with `;`, including a function, overload group, class, interface, or module whose body ends at `}` or `)`. A 0.1.0 source that omits one fails with `E0201`. `neura check --fix <file.nra>...` inserts every missing `;` in place and then checks the result. Statement blocks (`if`, `while`, `for`, `match`, `scope`, `select`, and a bare `{ }`) still take none.
- **Standard library fields.** Members that only returned stored state are now fields, read and assigned without parentheses: `buffer.size()` becomes `buffer.size`, `logger.set_level(LogLevel.Debug)` becomes `logger.level = LogLevel.Debug`, and `stream.set_read_timeout(d)` becomes `stream.read_timeout = d`. The full list is under [Changed](#changed). A call with `()` on one of them is `E0423` (`expression is not callable`), and an assignment to a read-only one is `E0510` or, for a `last` field, `E0445`.
- **Diagnostic format.** Errors are now printed with a code, the source lines, and labels, over several lines. A script or test that matched the 0.1.0 line `file:line:column: error: message` should pass `--error-format=short`, which prints that line with the code added (`file:line:column: error[E0201]: message`) and is the stable form; `--error-format=json` gives one JSON object per diagnostic.
- **Warnings.** The standard library marks I/O results, resource handles, and value-returning queries `#NoDiscard`, so a call whose result is dropped, such as `file.write(bytes);`, now warns with `W0003` (`unused-result`). Use the result, or discard it on purpose with `let _ = file.write(bytes);`. `-Wno-unused-result` turns the warning off, and `-Werror` turns warnings into errors.

### Added

**Language**

- `last` bindings: `last x = value;` binds `x` once, like a local `const`, and `last x: T;` declares it without a value; it is then assigned exactly once on every path before it is read, which the compiler checks with a definite-assignment analysis (`E0440` to `E0447`). An instance field declared `last` is assigned by its initializer or exactly once by every `init`. `last` is a contextual word: it declares only when an identifier follows it, so a binding named `last` keeps working ([Core language](docs/lang/CoreLanguage.md#source-and-bindings), [Object lifecycle](docs/lang/ObjectLifecycle.md#last-fields)).
- Target-typed `new`: `let a: Point = new;` and `= new(arguments);` construct the type that the context expects, in a binding or field annotation, an assignment, a `return`, a call argument, an element of a typed collection, and either arm of a conditional. `let a = new;` without an expected type is `E0420` ([Object lifecycle](docs/lang/ObjectLifecycle.md#target-typed-new)).
- Typed and listed destructuring: `let [a, b]: [K, V] = e1, e2;` gives each name its own value and its element of the annotation as the expected type, evaluated left to right (`E0436`, `E0437`) ([Types](docs/lang/Types.md#typed-and-listed-destructuring)).
- Anonymous classes in type positions: `let p: class (export let x: int = 0;) = new;`. A body of plain exported typed `let` fields is one class wherever it is written, identified by its fields; a body with a method, a base, a `const` or `last` field, an accessor clause, or a tagged member is a class of its own at that spot (`E0530` to `E0536`) ([Types](docs/lang/Types.md#anonymous-classes)).
- Accessors on exported instance fields: `| get: expression;` and `| set: statement;` (or a block) after the field. Code outside the class reads `object.field` and assigns `object.field = value` without call syntax and goes through the accessors; inside the class a bare name is the storage. A get-only field is read-only outside its class (`E0510`), a set-only field write-only (`E0515`); `E0516` to `E0524` give the other limits ([Types](docs/lang/Types.md#accessors)).
- `#` tags before declarations, class and module members, enum variants, and parameters: `#Name`, `#Name(arguments)`, and `#[A, B]`. The registry holds 85 tags. Implemented: `Deprecated`, `Experimental`, `Internal`, `Hidden`, `NoDiscard`, `Final`, `Sealed`, `Inline`, `NoInline`, `Allow`, and `Deny`, and the checked tags `Pure`, `Immutable`, `ThreadSafe`, `NotThreadSafe`, and `Synchronized`. Seventeen informational tags, such as `Since`, `Note`, and `Unit`, are recorded and shown by `neura docs`. The other 52 are reserved, and using one is `E0908`, which names what to write instead. Tag errors are `E0900` to `E0922` ([Tags](docs/lang/Tags.md)).
- Monitors: a class whose exported methods are all `#Synchronized` and whose state only its own methods reach may be shared between tasks; each synchronized method runs under a per-instance guard, first come, first served, re-entrant for calls on its own receiver. Instances of `#Immutable` classes may also be shared between tasks ([Concurrency, section 8.1](docs/lang/Concurrency.md#81-monitors)).
- `#Pure` is checked (`E0917`) in every specialization of a generic function and over every dispatch target, including interface implementations and the `to_string`, `hash_code`, and `equals` that value text, hashing, and equality call. A kernel call writes the lists it is given to fill or sort. A pure function may pass an object it allocated to a helper that writes only its parameters.
- `#Immutable` (`E0918` to `E0920`), `#ThreadSafe` (`E0608`, `E0609`), and `#Synchronized` (`E0921`, `E0922`) are checked by the compiler.
- `#NotThreadSafe` on a class keeps its instances in one task: a task may create and use its own, and giving one to another task is `E0602`. On a function or method it rejects a spawned task or parallel worker that can reach it (`E0601`). A `#NotThreadSafe` class cannot also be `#Immutable`, `#ThreadSafe`, or a monitor (`E0907`, `E0610`).

**Diagnostics**

- Every compiler error has a code, `E0101` to `E0916`, by area: `E01xx` lexer, `E02xx` parser, `E03xx` names, `E04xx` types and bindings, `E05xx` classes and lifecycle, `E06xx` concurrency, `E07xx` modules and imports, `E08xx` backend, and `E09xx` tags. Warnings are `W0001` `deprecated`, `W0002` `experimental`, `W0003` `unused-result`, `W0004` `inline`, and `W0005` `setter-never-stores`. There are 164 codes, each with a page in [`docs/diagnostics`](docs/diagnostics/README.md) that states the rule and shows a failing and a fixed program ([Tooling](docs/lang/Tooling.md#diagnostics)).
- `neura explain <CODE>`, `neura --explain <CODE>`, `neura explain <warning-name>`, and `neura explain --list` print those pages, which are built into the binary.
- Rendered diagnostics: the source lines with the primary span marked `^` and secondary labels marked `-`, across lines and files, then notes and a `help:` with the edited line. Common errors label the code that explains them and suggest a fix: the declaration of a constant that is assigned, the earlier of two duplicate declarations, the closest name to an unknown one, the origin of an expected type, the missing arms of a `match`, and a missing `.override`.
- `--error-format=human|short|json`, `--color=auto|always|never`, `--max-errors N` (default 20), `-W<name>`, `-Wno-<name>`, `-Werror`, and `--warn-library`.
- The lexer and the parser recover from an error and report several per run, in source order.
- `neura check --fix` applies every machine-applicable suggestion to the source files and checks the result.
- `neura tags` prints the tag catalogue and `neura tags <Name>` one tag; `neura docs` shows the tags, `last`, and accessors (`{ get; set }`) of each declaration and leaves `#Hidden` declarations out.

**Runtime and standard library**

- Built-in C kernels: 67 deterministic, pure C functions that the runtime calls in place of standard library code, each with the replaced Neura code kept as its test oracle. A trusted `Std` module binds one with `kernel("name", version)` ([Kernels](docs/runtime/Kernels.md), [Standard library, P2a](docs/lang/Stdlib.md#p2a-kernel-bindings-in-source)). A kernel call is charged steps in advance by a formula over its arguments, is not a host request, and needs no capability.
- Kernel-backed modules: `Std.Digest.Sha256` and `Crc32`; every function of `Std.Math.Math`, bit for bit as before; in `Std.Text.Strings` the searches, comparisons, trims, `split`, `split_limit`, `lines`, replacements, ASCII case mappings, `scalar_count`, and `hash`, and the `String` members built on them; `StringBuilder.hash_code`; the integer companions' `to_string_radix` and `parse_radix`; and `Std.Encoding.Utf8.validate`, `Base64`, and `Hex`. Their results, errors, and traps are unchanged; their step counts are lower.
- `Std.Collections.Sorting.sort_ascending` sorts a list of `int32`, `uint32`, `int64`, `uint64`, or `float64` values on a kernel.
- Execution tiers: tier 0 evaluates every block by reference; tier 1 runs a block as native operations once the runner has proven that its graph computes typed operations whose overflow checks and results match the reference, floating-point addition, subtraction, multiplication, and division of `float32` and `float64` included; and tier 2, the default, runs regions of proven blocks of one function together, without returning to the step dispatcher, and goes back to tier 1 at any block that traps, calls, allocates, or reaches a limit. The result, steps, traps, output, and traces are the same at every tier ([NVM](docs/runtime/NVM.md#execution-tiers)). `neura run` and `neura test` take `--tier=0|1|2` and `--tier-stats`; `NEURA_TIER` lowers the tier for every run, `NEURA_TIER_CHECK=1` checks every tier-1 step against the reference and every region against tier 1, and `NEURA_RUN_OPTIONS` may lower the tier of a native executable.
- Native code for tier 2 regions: the runner compiles each region of proven blocks to machine code, which runs in place of the region interpreter with the same exits, slots, traps, steps, output, and traces ([NVM](docs/runtime/NVM.md#native-compilation)). `neura run`, `neura test`, and `NEURA_RUN_OPTIONS` take `--jit=off|on|auto`, and `NEURA_JIT` sets it for every run; `auto`, the default, compiles on x86-64 Linux, and `on` also compiles on AArch64 Linux, AArch64 macOS, and x86-64 macOS, or fails the run at its start where it cannot. `--tier-stats` reports the regions compiled, their bytes of code, the entries that ran native code, and the regions left on the interpreter and why. Code is mapped writable, then executable, never both, and `--deny-native` does not turn it off.
- The optional `LIFT` section of an image records the typed operations of each block for tier 1. The runner proves each one and never trusts it. `neura build --no-lift-hints` leaves it out.
- IR optimization passes: copy propagation, common-subexpression elimination including reuse of pure calls, loop-invariant hoisting, dead-store removal, removal of unused pure calls, and inlining of small calls, which `#Inline` and `#NoInline` direct. An optimized program never takes more steps, heap cells, or frames than the unoptimized one ([IR optimization](docs/IrOptimization.md)).
- New standard library fields: see [Changed](#changed).

**Formats and tools**

- NVR 1.6: the `KERN` table of kernel records and the kernel state operation (24). Only an image that calls a kernel is written as 1.6 ([NVR](docs/runtime/NVR.md#kernels-16)).
- NeuraIR 1.10: symbol attributes (tags, accessors, `last`, anonymous classes, kernel bindings, inlining directives, synchronized methods, and shareable classes) and deferred initialization. A program that imports the standard library, whose declarations carry tags, is written as 1.10 ([NeuraIR format](docs/NirFormat.md#revision-110-symbol-attributes-and-deferred-initialization)).
- `scripts/bench-wall.py` and `tests/bench-wall/`: a wall-clock benchmark harness of twelve programs, each with a Java counterpart, and `--compare-tiers` ([Benchmarks](docs/Benchmarks.md)).
- `tests/ui/` and `scripts/ui-test.py` (the `ui-<area>` tests) compare rendered diagnostics and applied fixes with snapshots, and `scripts/check-diagnostics.py` checks that every diagnostic has a registered code, a page, and a test.
- The language skill for AI agents covers the new syntax, the tags, and the diagnostics, and its examples use the modern style.

### Changed

- Every declaration ends with `;` (`E0201`, fixed by `neura check --fix`). In 0.1.0 a declaration whose body ended at `}` or `)` could omit it.
- The default diagnostic output is the rendered `human` form described under Added. The 0.1.0 one-line form, with the code added, is `--error-format=short`.
- A task-safety error keeps its first clause and names the function that breaks the rule; each call on the way, the offending operation, and the place a library starts the task are labels instead of text in the message.
- Standard library members that are now fields (old → new):
  - `Std.Core`: `StringBuilder.byte_length()` → `byte_length`; `IntRange.start()`, `end()`, `step()` → `start`, `end`, `step`.
  - `Std.Text`: `Regex.pattern()`, `group_count()` → `pattern`, `group_count`; `RegexMatch.start()`, `end()` → `start`, `end`.
  - `Std.Io`: `ByteBuffer.size()` → `size`; `BufferedWriter.buffered()` → `buffered`.
  - `Std.Fs`: `Path.is_absolute()` → `is_absolute`; `FileMetadata.kind`, `size`, and `modified` are read-only (`last`) fields.
  - `Std.Net`: `TcpListener.local_port()` → `local_port`; `TcpStream.set_read_timeout(d)` → the field `read_timeout`, assigned `stream.read_timeout = d`, whose setter traps on a negative duration as before and whose getter returns the duration last set.
  - `Std.Log`: `Logger.set_level(level)` → the field `level`, read and assigned `logger.level = level`.
  - `Std.Time`: `Instant.epoch_seconds()`, `nanos()` → `epoch_seconds`, `nanos`; `LocalDate.year()`, `month()`, `day()` → `year`, `month`, `day`; `LocalDateTime.date()`, `time()` → `date`, `time`; `ZoneOffset.total_seconds()` → `total_seconds`; `Stopwatch.is_running()` → `is_running`.
  - `Std.Math`: `BigDecimal.unscaled()`, `scale()` → `unscaled`, `scale`.
  - `Std.Concurrent`: `Parallel.ItemLimits.max_steps` and `max_heap_cells` are read-only (`last`) fields.
- The standard library is written in the modern style: assign-once values are `last`, constructions whose type is named by an annotation use `new` alone, every module carries `#Since`, value and resource classes are `#Final`, and I/O results, resource handles, `Result` and `Optional` queries, and other value-returning calls are `#NoDiscard`, so dropping one warns `W0003`. The scheduler's `select` protocol members are `#Hidden`.
- The standard library marks the classes whose instances hold mutable or native state `#NotThreadSafe`: `StringBuilder`, `Regex`, `Random`, `Logger`, `Stopwatch`, `FixedClock`, `ByteBuffer`, `BufferedReader`, `BufferedWriter`, `Utf8Reader`, `Utf8Writer`, `File`, `TcpStream`, `TcpListener`, the mutable collections (`ArrayList`, `ArrayDeque`, `LinkedList`, `HashMap`, `HashSet`, `StringMap`, `IntMap`, `OrdinalMap`, `TreeMap`, `TreeSet`, `PriorityQueue`, `BitSet`, `Sequence`), and `Arena`, `MemorySegment`, `MethodHandle`, `Linker`, and `SymbolLookup`. Giving one to another task or sending one on a channel was already `E0602`, because none of them is transferable; the error now also points at the tag, and a task may still create and use its own and return one to the task that joins it. `#Pure` now also marks the functions that build their result in a `StringBuilder` or list of their own, such as `Json.parse` and `write`, `Csv.write`, the float `parse`, `to_fixed`, and `to_exponential`, `Regex.compile`, and the `BigInteger` arithmetic ([Stdlib](docs/lang/Stdlib.md#58-task-safety)).
- Text searches use a linear algorithm and are charged steps by the text they can reach, so a long needle no longer makes a search quadratic, and `replace_first` no longer charges for every possible occurrence.
- A program that imports the standard library is written as NeuraIR 1.10 by `neura emit-ir -o`; a 1.9 reader cannot read it.

### Removed

- `Logger.set_level` and `TcpStream.set_read_timeout`, replaced by the fields `level` and `read_timeout`, and the zero-argument methods listed under Changed, replaced by fields of the same name.

### Fixed

- `Std.Digest.Sha256` and `Crc32`, and the `Std.Encoding.Hex` and `Base64` decoders, keep no module state any more, so a spawned task or a parallel item may call them.

### Known issues

- Sustained computation is not yet near the speed of a compiled or JIT-compiled language. In one local measurement on x86-64 Linux at the quick sizes the tiers ran ten of the twelve benchmark programs 1.1 to 48 times as fast as tier 0 and `nbody` and `spectral-norm` slower (see the proving cost below), and at the full sizes `nbody` (2,500 steps) still took 6.3 s and `spectral-norm` (64) 11.7 s. The time goes to blocks that stay on the reference evaluator, to calls, allocations, and other exits that regions do not run, and to proving blocks ([Benchmarks](docs/Benchmarks.md)).
- There are no measured comparisons with Java yet. `tests/bench-wall/` has a Java counterpart for each program, but the machine where the harness was run had no JDK, and the Java programs have not been compiled.
- Proving a block that multiplies or divides `float64` values takes a few tenths of a second, and the runner spends that time while the program runs. A float-heavy program that ends within a second or two can therefore take longer than at `--tier=0` (in one local measurement, `nbody` and `spectral-norm` at their quick sizes took about 1.2 times as long); longer runs repay the proofs. Blocks that compute `float16` or `bfloat16` values stay on tier 0.
- A synchronized method re-enters its guard only for calls on its own receiver. Calling back into the same instance any other way, such as through a function value that captured it, while the guard is held traps with `invalid state: guard already held`.
- A monitor cannot hold a list or a map in a field directly, because every field of a shared class must have a transferable type; keep the collection in a `Guarded<T>` field or with an owner task.
- An accessor cannot satisfy an interface member, and accessors cannot be overridden, so the collections keep `size()` and `is_empty()` as methods.
- `LocalTime.hour()`, `minute()`, `second()`, and `nano()` remain methods, because they are computed rather than stored.
- Tags cannot precede a statement or a destructuring declaration (`E0909`).
- Native code for tier 2 regions is verified by execution only on x86-64 Linux. On AArch64 (Linux and macOS) its instruction encodings are checked against an assembler's, but the generated code has not been run, and the macOS memory mapping (`MAP_JIT` on Apple silicon) has not been run either, so `--jit=auto` interprets regions there and `--jit=on` is experimental. The `Native code` workflow runs the comparison on those platforms; it is not yet required.
- Lexical and syntax errors are reported several at a time, but name resolution, type checking, and the later checks stop at the first error.
- The two known issues of 0.1.0, `neura run --each` with `--gpu=always` on an image whose tasks wait on cancellable host I/O and the `neura-libclang-load` test on Linux arm64, are not addressed in this version.

## [0.1.0] - 2026-11-11

The first release. The [release checklist](docs/Release.md) lists the language, toolchain, standard library, and decompiler features in it, each with the tests that establish it. [Install and update](docs/Install.md) lists the supported platforms: Linux and macOS on x86-64 and arm64, and Windows through WSL 2.

### Added

- The Neura language skill for AI coding agents in [`prompts/neura`](https://github.com/Kayyo321/neura/tree/main/prompts/neura): the syntax, modules and imports, the standard library with a generated signature index, concurrency, the command line, diagnostics, and style, with every example compiled or run by the `doc-examples` test. The README and the website's front page carry a prompt that installs it ([Write Neura with an AI agent](README.md#write-neura-with-an-ai-agent)).
- `VERSION` is the one source of the tool's version. `neura --version` and `neura version` print `neura <version> (<target>, commit <commit>)`; `neura help` and `neura --help` print the usage. A local build reports `<version>-dev`, and the native launcher descriptor carries that version in place of `development` ([Install](docs/Install.md), [Tooling](docs/lang/Tooling.md#version-install-and-update)).
- `neura doctor` checks an install and its optional tools. `neura update [--check] [--source] [--require-attestation]` replaces an archive install with a newer release after verifying its size, SHA-256, and, with `gh`, its build provenance, and rolls back if activation fails. `neura uninstall [--dry-run] [--yes]` removes an archive install. They exit with status 3 for an install that `neura update` does not manage, and `update --check` exits with status 4 when a newer version exists ([Install](docs/Install.md)).
- macOS support: Neura builds and passes its tests on macOS 15, on Apple silicon and Intel. C imports find the SDK's headers and link with Apple's linker, and a C import also takes declarations from the underscore-named headers a system header is made of, such as `_stdlib.h` ([Foreign interop](docs/lang/ForeignInterop.md)). There is no native launcher for macOS.
- The release workflow (`.github/workflows/release.yml`): a `v*` tag builds, tests, packs, and qualifies an archive for Linux and macOS on x86-64 and arm64, and publishes it with `SHA256SUMS`, `neura-release.txt`, and build provenance attestations ([release process](docs/ReleaseProcess.md), [code signing policy](docs/CodeSigning.md)). `NEURA_PORTABLE_RELEASE=ON` links libffi and the C++ runtime into `neura`.
- A one-line install: each release publishes `install.sh`, which downloads the archive for the machine, verifies its size and SHA-256 (and its build provenance when `gh` is installed), and installs it ([Install](docs/Install.md#install-with-one-command)).
- Release archives: `packaging/make-archive.sh`, `make-release-metadata.sh`, and the `install.sh` that each archive carries, with the install layout, marker, and `neura-release.txt` format described in [Install](docs/Install.md). The installed tree gains `share/doc/neura/LICENSE`.
- An update notice on standard error, off unless the install is an archive install and standard input and standard error are terminals, at most one network check every 7 days; `NEURA_NO_UPDATE_CHECK=1` or `CI` turns it off ([Install](docs/Install.md#update-notices-and-privacy)).
- Scoped tasks: `scope { }`, `scope (name) { }`, `spawn`, `join`, and `select`, with compile-time checks that values given to a task are transferable, that spawned calls are task-safe, and that task and scope handles do not leave their scope ([Concurrency](docs/lang/Concurrency.md)).
- `Std.Concurrent`: `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Tasks`, `WorkerGroup`, `Parallel`, `AtomicInt64`, `Semaphore`, and `Guarded`.
- Cancellation and deadlines for scopes, and host I/O that overlaps other tasks and can be cancelled: `TcpStream` and `TcpListener` operations, `Console` input, and `SystemClock.sleep`. `IoErrorKind.Cancelled` reports a cancelled operation.
- The scoped resource helpers `File.with_open`, `TcpStream.with_connection`, and `Closeable.release`, and `Error.to_portable` and `from_portable` for sending an error over a channel.
- `neura run` and `neura test` options for concurrent programs: `--max-tasks`, `--task-quantum`, `--max-queued-messages`, `--max-pending-requests`, `--parallel-workers`, `--task-dump[=text|json]` (also on `SIGQUIT`), `--virtual-time`, and for `run`, `--trace-events` and `--trace-wall-time`.
- `neura test --schedules N`, `--schedule-seed S`, `--save-schedule <file>`, and `--schedule <file>`: run tests under several legal task schedules and reproduce a failing one.
- `neura check --task-safety`: report, for every function, whether a spawned task may call it.
- Record and replay of programs with tasks (trace file version 3).
- `neura build --native`: package an image as a native executable with a recorded policy of capabilities, limits, and task settings; `neura inspect` reads it back; `NEURA_RUN_OPTIONS` restricts or observes a run ([Native executables](docs/lang/Standalone.md)). The fully static flavor (`--static`) has no native interop and resolves no host names.
- GPU execution of eligible graphs for `neura run` and `neura test`, with `--gpu=auto|always|off`, `--require-gpu`, `--gpu-verify`, and `run --each` for batched runs ([GPU execution](docs/lang/GpuExecution.md)).
- NVR 1.5 (scheduler operations, poll bits, and the transferable-layout table) and `.nir` 1.9 (`task_scope`, `spawn`, `scheduler_call`, and polls).
- `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, and `--deny-all`.

### Changed

- `neura run`, `neura test`, and the policy that `neura build --native` records now grant every host service by default: files, network, environment, clock, entropy, and native code. Before, each needed its `--allow-*` option. Run an image you do not trust with `--deny-all` and the `--allow-*` options it needs. The capability options now apply in command-line order, so `--allow-fs --allow-fs=prefix` grants only the prefix.
- A collection's `to_string` prints `(this collection)` wherever its own text reaches it again, including through other collections. An indirect cycle used to end with the frame-limit fault.
- The trap for denied native access reads `native access denied: the native capability is not granted`.
- A program that uses a scope, a channel, or another scheduler operation cannot also use `Std.DynamicForeign` or `import:c(...)`.

### Fixed

- The install script and `neura update` treated a `gh` that is installed but not signed in as a failed provenance check and stopped. They now report that provenance was not verified and continue, as they do without `gh`; `--require-attestation` still stops. Found by the release workflow on the candidate `v0.1.0-rc.1`, whose published `install.sh` has this defect; `0.1.0` does not.

### Known issues

- `neura run --each <file> --gpu=always` does not finish for an image whose tasks wait on cancellable host I/O, such as `tests/concurrency/io/cancelled.nra`; the same fleet finishes with `--gpu=off` and with the default `--gpu=auto`. [GPU execution](docs/lang/GpuExecution.md) specifies that a fleet gives the results of running its members in order, so this is a defect, not a documented limit. The release archives are built without the GPU backend and are not affected; a build from source with the backend is.
- On Linux arm64, `neura-libclang-load`, a test of the `neura-libclang` submodule, fails because it expects `char` to be signed. C imports themselves map `char` as the platform does.

### Security

- A native executable finds its own file through `/proc/self/exe` or the path the kernel recorded, never through its program name, and writes traces through an exclusively created temporary file. `--trace-events` refuses a symbolic link at its path.
