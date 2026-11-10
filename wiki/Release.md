# Release checklist

Each feature of this release with the tests that establish it. A feature counts as complete only when it can be written in Neura, built into a standalone `.nvr` image, run under `neura run` or `neura test`, and produces its documented behavior and diagnostics. Every test listed is a CTest entry, named as `ctest -R` matches it; `suite.test` names refer to the `NEURA_TEST` cases inside it.

**Kind of check:** *exhaustive* covers every value of the stated domain; *sampled* covers chosen boundary and pseudo-random values; *structural* compares executable records exactly; *end to end* builds and runs programs through the CLI and compares stdout, status, and diagnostics exactly.

## Language

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| `int` and `byte` as `int32` and `uint8` | [Numbers](Numbers#integer-types) | `ir-optimization-structure` (`Folding.int_alias`, `byte_alias`); `frontend` (lowered IR spells sized names); `backend-generics` (`Box<int>` is `Box<int32>`); `backend-cli` (`programs/integers.nra`) | Structural, end to end |
| Integer overflow traps | [Numbers](Numbers#integer-arithmetic) | `backend-circuits`; `traps/overflow.nra`; `ir-optimization-structure.declines_folds_that_trap` | Exhaustive (8-bit), sampled (wider), end to end |
| Integer bitwise operations and checked shifts | [Numbers](Numbers#bitwise-operations) | `backend-circuits.shifts_cover_each_lane_and_trap_on_invalid_counts`, `programs/operators.nra`, `traps/shift-count.nra`, `frontend` (shift count type) | Exhaustive (8-bit words and valid counts), sampled (64-bit), end to end |
| Comparison chains | [Core language](CoreLanguage#comparison-chains) | `programs/operators.nra` (capture once, short circuit, both connectors), `frontend` (mixed connectors and repeated comparisons) | End to end |
| Truncating `/`, `%` (compatibility change) | [Numbers](Numbers#integer-arithmetic) | `backend-circuits.exhaustive_eight_bit_operations`, `wide_operations…`; `ir-optimization-behavior.integer_folding_agrees_with_oracles`; `backend-conversion.functions_agree…` (`rem_i8`, `rem_u8`); `programs/integers.nra`; `traps/divide-by-zero.nra`, `traps/remainder-by-zero.nra` | Exhaustive (8-bit), sampled, end to end |
| Boundary literals | [Numbers](Numbers#integer-literals) | `programs/integers.nra` (int32 and int64 minimums); `frontend` (range errors) | End to end |
| `as` conversions, integer | [Numbers](Numbers#conversions) | `backend-circuits.integer_conversions_between_every_format`; `backend-conversion` (`to_unsigned_i8`, `to_signed_u8`, `narrow_i16`, `widen_u8`); `traps/conversion-overflow.nra`; `frontend` (invalid conversions) | Exhaustive (8/16-bit sources), sampled, end to end |
| float16, bfloat16, float32, float64 arithmetic and comparisons | [Numbers](Numbers#floating-point-types) | `backend-floats.tiny_formats_exhaustively` (e3m2, e4m3 against a brute-force exact reference and a double reference), `sixteen_bit_binary_operations`, `wide_binary_operations`; `backend-conversion.floats_agree_with_the_reference_evaluator`; `programs/floats.nra` | Exhaustive (tiny formats), sampled (16/32/64-bit), end to end |
| Rounding, subnormals, signed zeros, infinities, canonical NaN | [Numbers](Numbers#rounding-and-exceptions) | the float tests above check bit patterns, including NaN encodings; `programs/floats.nra` | Exhaustive (tiny formats), sampled |
| Float conversions | [Numbers](Numbers#conversions) | `backend-floats.sixteen_bit_unary_operations_exhaustively`, `wide_conversions`; `traps/float-to-integer.nra` | Exhaustive (every 16-bit value), sampled |
| Float literals | [Numbers](Numbers#float-literals) | `backend-floats.literals_round_exactly` (against `strtod`/`strtof`, float16 ties); `frontend` (overflow and underflow errors) | Sampled |
| Generic functions, classes, methods, bounds, nested instances | [Polymorphism](Polymorphism) | `backend-generics`; `programs/generics.nra`; `examples/polymorphism` driver; `frontend` (unsatisfied bound, generic inheritance, generic override, missing type arguments); `ir-optimization-verifier.rejects_unsatisfied_bounds…` | Exhaustive (8-bit wrappers), end to end |
| Specialization limits | [Polymorphism](Polymorphism#type-arguments-and-specialization) | `backend-generics.limits_stop_unbounded_specialization`; `unsupported/unbounded-generic.nra` | End to end |
| Three-part `for`, for-each | [Core language](CoreLanguage#loops) | `programs/loops.nra` (empty collections, nested loops, early exits, continue, the collection evaluated once, element writes); `backend-conversion.structures_agree…` (`skip_and_stop`, `list_sum`); `frontend` (scopes, step restrictions, element types, non-iterable classes); `stdlib-for-each` (every standard-library `Iterable`) | Exhaustive (8-bit), end to end |
| For-each modification rule | [Core language](CoreLanguage#loops) | `traps/for-each-append.nra`, `for-each-alias.nra` (remove then append through an alias), `for-each-last.nra` (the final advance); `backend-vm.list_versions_count_structural_changes`; `backend-conversion` (`grow_while_iterating`) | End to end, exhaustive (8-bit) |
| Enums | [Types](Types#enums) | `programs/enums.nra`; `frontend` (unknown variant, empty enum, ordering, integer-to-enum, duplicate variant) | End to end |
| Enum payloads, matches, and tuples | [Types](Types) | `programs/aggregates.nra` (recursive payloads, named field evaluation order, exhaustive match, tuple returns, indexing, and destructuring), `frontend` (non-exhaustive match, wrong payload type and tuple arity) | End to end |
| Closures and first-class function values | [Core language](CoreLanguage) | `programs/closures.nra` (nested, escaping, immediately invoked, and bound method closures), `backend-cli` (NIR build), `frontend` (invalid capture assignment) | End to end |
| Function and constructor overloads, object casts, primitive companions | [Core language](CoreLanguage#function-overloads), [Types](Types#object-casts) | `programs/overloads.nra` through the CLI and reference evaluator, `frontend` (ambiguity and invalid declarations), `traps/argument-cast-trap.nra`, `stdlib-string` | End to end |
| Prebuilt NeuraIR imports | [Modules](Modules#prebuilt-neurair-modules), [NeuraIR format](NirFormat) | `nir-link` (shared dependencies, layout identity, initialization, visibility, malformed inputs, linked artifact round trip), `backend-nir` (remapping and format compatibility) | Structural, end to end |
| Non-null primitive references | [Types](Types#primitive-references) | `programs/primitive-references.nra` through the CLI and NeuraIR round trip, `nir-link` (exported reference returns), `frontend` (invalid referents and constructor paths) | End to end |
| Documentation comments | [Core language](CoreLanguage#documentation-comments) | `source-docs` (attachment, overloads, source ranges, JSON, Doxygen filter) | End to end |
| Initialization order, top-level statements | [Modules](Modules#initialization) | `initialization/main.nra` (two files, through `.nra`, `.nir`, and unoptimized builds); `backend-conversion.initialization_order_matches_the_evaluator`; `traps/uninitialized-global.nra`; `examples/resources` driver | End to end |
| Objects, construction, destruction | [Object lifecycle](ObjectLifecycle) | `programs/objects.nra`; `backend-conversion.structures_agree…` (`two_counters`, `construction_order`); `traps/use-after-destroy.nra`, `uninitialized-field.nra` | Exhaustive (8-bit), end to end |
| Collections and `length` | [Collections](Collections) | `programs/collections.nra`; `examples/collections` driver; `backend-conversion` (`element`, `remove`); `traps/index-out-of-bounds.nra`, `negative-index.nra` | Exhaustive (8-bit), end to end |
| Errors | [Errors](Errors) | Trap programs above; `backend-vm` faults | End to end |
| Test runner | [Testing](Testing) | `backend-cli` (`math-tests`, `testing/suite.nra` with passes, failures, a trap, isolation, and an optimized and an unoptimized build; `testing/exit-suite.nra`; no tests found); `stdlib-std-test` (every assertion's message) | End to end |
| Program entry and exit codes | [Modules](Modules#program-entry) | `stdlib-program` (Ok, Failure, Status bounds, invalid statuses); `stdlib-process`; `backend-stdlib` (trusted roots, two entries rejected) | End to end |
| Retired `SysIo` and `Test` | [Modules](Modules#program-entry) | `backend-stdlib` (both imports rejected with their replacements); `backend-cli` (legacy images: a SysIo Hello World runs, an image using the retired assertion services fails to bind) | End to end |
| Initialization with import cycles | [Modules](Modules#initialization) | `backend-cli` (`initialization-cycle` in both build modes; `initialization` with library imports); `backend-conversion.initialization_order_matches_the_evaluator` | End to end |
| Runnable examples | [Status](Status#examples) | `backend-cli`; `frontend`; `ir-optimization-cli` | End to end |

## Toolchain and formats

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| Build routes give identical bytes; builds are reproducible | [Tooling](Tooling) | `backend-nir.both_build_routes_emit_identical_images`; `backend-cli` (Hello World, initialization, programs built twice) | Structural |
| `.nir` 1.2 and minimal versions | [NeuraIR file format](NirFormat) | `backend-nir` (round trips, `minor_version_follows_the_features_used`, corrupt files, 1.3 rejected) | Structural |
| Tracing garbage collection | [NVM](NVM) | `backend-vm` (unreachable cycles, typed roots and parallel assignments under heap pressure) | Runtime |
| NVR 1.1: float types, `ReadVersion`, reason 5, minimal versions | [NVR](NVR#versions-10-through-13) | `backend-format.version_1_1_features`, `float_types_and_constants` (hand-assembled fixtures, 1.1 features in 1.0 images rejected, 1.2 rejected); `backend-cli` (the legacy 1.0 Hello World image; Hello World with `Std.Program` is 1.2); `backend-nir` (import-free modules in `nir-versions`) | Structural |
| Malformed image rejection | [NVR](NVR#writer-and-loader-requirements) | `backend-format` (every truncation and the validation cases) | Exhaustive (truncations), structural |
| Installed toolchain | [Tooling](Tooling) | `backend-install`; `stdlib-install` (the installed library matches `stdlib/`; `Std` imports resolve with no `-I`; images carry no installation path) | End to end |
| Host services, capabilities, and traces | [NVM](NVM#host-services) | `backend-host` (every provider, error code, denial, partial I/O, limits, cleanup at run end, trace files); `stdlib-console`, `-io`, `-file`, `-fs`, `-net`, `-environment`, `-process`, `-system-clock`, `-host` (denied and allowed runs, replayed traces); `backend-vm`; `decompiler-behavior` (record and replay) | End to end, replay |

## Standard library

Every module of the [standard library reference](Stdlib) has a test area `stdlib-<area>` that runs its tests optimized twice (identical output and step counts) and unoptimized, and builds and runs its trap and output programs in both modes; program images also round-trip through assembly and decompile.

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| Core values | [Stdlib 5–6](Stdlib-Design#5-core-contracts) | `stdlib-core`, `-comparators`, `-int-range` | End to end |
| C header imports (`import:c`) | [Foreign interop](ForeignInterop#c-imports) | `c-import` (functions, structs by value, macros, enumerators, a global, out-parameters, strings, a variadic call, and a `static inline` wrapper from `mathops.h` and `libmathops.a`; `qsort` from a header-only `stdlib.h` with an upcall; reproducible builds; `.nir` and assembly round trips; decompilation; a denied capability; another target; `c-bindings` against `tests/c-import/mathops.bindings`; and diagnostics for a function-like macro, a bit-field struct, a missing header, a shared library, a non-PIC archive, and a missing libclang) | End to end on Linux x86-64 |
| Native interop (`Std.DynamicForeign`) | [Foreign interop](ForeignInterop) | `stdlib-dynamic-foreign` (the Java example, upcalls through `qsort` and a library function, a trapping upcall, a stub called after its arena closed, scalars, strings, out-parameters, variadic calls, globals, structs by value, layouts, the default lookup, and traps for a denied capability, bounds, a closed arena, a mismatched argument, and an unpromoted variadic layout), `native-replay` (including upcalls), `backend-host.trace_files_round_trip` (nested callbacks), `format.callbacks_and_native_libraries_require_version_1_4`, `frontend` (callbacks outside the library), `backend-host.native_services_allocate_read_write_and_call`, `backend-host.library_bindings_match_the_providers` | End to end on Linux x86-64 |
| Program exit codes and trap statuses | [Modules](Modules#program-entry), [Errors](Errors) | `stdlib-program` (every `ExitCode` variant, `status_of`), `stdlib-process` (`exit(ExitCode)`), `backend-vm.traps_map_to_signal_exit_statuses`, each `traps/` header | End to end |
| Integer, Bool, and float companions | [Stdlib 7](Stdlib-Design#7-primitive-companion-modules) | `stdlib-integers` (every 7.2 recipe at its boundaries, Python vectors), `-bool`, `-floats` (an exact Fraction model, parse corpus), `-floats-exhaustive` (every `float16` and `bfloat16` round trip) | Exhaustive (16-bit floats), sampled |
| Unicode 17.0 case, locale tailoring, and character encodings | [String](StandardLibrary#unicode-case-and-locale) | `stdlib-string` (full mappings, locale contexts, UTF-8/16/32, legacy charsets, and malformed input) | End to end |
| Text | [Stdlib 8](Stdlib-Design#8-stdtext) | `stdlib-strings` (Python UTF-8 vectors; agreement with the NVM validator), `-scalars`, `-regex` (Python `re` and ECMAScript corpora), `-string-builder` | Sampled, exhaustive (2- and 3-byte UTF-8 prefixes) |
| Collections | [Stdlib 9](Stdlib-Design#9-stdcollections) | `stdlib-<collection>` for every collection: seeded model tests of at least 10,000 operations with `validate()`, fail-fast iterators through aliases, reclamation under a heap limit | Sampled (seeded), end to end |
| Math | [Stdlib 10](Stdlib-Design#10-stdmath) | `stdlib-math`, `-big-integer`, `-big-decimal` (Python reference vectors) | Sampled |
| Time and randomness | [Stdlib 12](Stdlib-Design#12-time-and-randomness) | `stdlib-duration`, `-instant`, `-clock`, `-local-date`, `-local-time`, `-local-date-time`, `-zone-offset`, `-random`, `-uuid` (Python models; replayed entropy) | Sampled |
| Data formats and digests | [Stdlib 13](Stdlib-Design#13-data-formats) | `stdlib-utf8`, `-hex`, `-base64`, `-csv`, `-json`, `-crc32`, `-sha256` (published vectors) | Published vectors, sampled |
| Logging | [Stdlib 14.1](Stdlib-Design#141-stdlog) | `stdlib-logger` | End to end |
| Reference evaluator agreement | [Stdlib 17](Stdlib-Design#17-verification) | `backend-reference` (every library test function under the NeuraIR reference evaluator and NVM; tests that need host responses or exceed the evaluator's budget are counted, not compared) | Differential |
| Cost baseline | [Stdlib 17](Stdlib-Design#17-verification) | `benchmarks` (steps, peak heap cells, neurons, and image bytes against `tests/benchmarks/baseline.txt`; step growth between two sizes) | Measured |

## Decompiler

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| Lossless assembly and reassembly | [Decompiler](Decompiler#assembly-the-lossless-form) | `decompiler-lossless` (every corpus image, edited images, assembly errors) | Structural, over the corpus |
| Lifting with structural proof | [Decompiler](Decompiler#lifting) | `decompiler-lift.claims_hold_and_coverage_is_reported` (every claim re-checked and minimum coverage enforced), `lifted_operations_match_the_source` | Structural, over the corpus |
| Edited weights and stripped metadata | [Decompiler](Decompiler#lifting) | `decompiler-lift.edited_weights_are_not_lifted_from_stale_structure`, `provenance_is_optional` | Structural |
| Behavior of the recovered form | [Decompiler](Decompiler#re-synthesis-and-behavior-comparison) | `decompiler-behavior` (trace replay of every compiled corpus image, with and without arguments, including conditional output and traps; fixture functions on every 8-bit input) | Sampled (programs), exhaustive (8-bit functions) |

## Not in this release

The deferred standard library areas of [Stdlib section 15.1](Stdlib-Design#151-deferred-and-excluded); concurrency; interop with managed runtimes; native interop on platforms other than Linux x86-64; and packages ([Status](Status#outside-this-release)). MSVC builds are not verified.
