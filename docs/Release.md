# Release checklist

Each feature of this release with the tests that establish it. A feature counts as complete only when it can be written in Neura, built into a standalone `.nvr` image, run under `neura run` or `neura test`, and produces its documented behavior and diagnostics. Every test listed is a CTest entry, named as `ctest -R` matches it; `suite.test` names refer to the `NEURA_TEST` cases inside it.

**Kind of check:** *exhaustive* covers every value of the stated domain; *sampled* covers chosen boundary and pseudo-random values; *structural* compares executable records exactly; *end to end* builds and runs programs through the CLI and compares stdout, status, and diagnostics exactly.

## Language

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| `int` and `byte` as `int32` and `uint8` | [Numbers](lang/Numbers.md#integer-types) | `ir-optimization-structure` (`Folding.int_alias`, `byte_alias`); `frontend` (lowered IR spells sized names); `backend-generics` (`Box<int>` is `Box<int32>`); `backend-cli` (`programs/integers.nra`) | Structural, end to end |
| Integer overflow traps | [Numbers](lang/Numbers.md#integer-arithmetic) | `backend-circuits`; `traps/overflow.nra`; `ir-optimization-structure.declines_folds_that_trap` | Exhaustive (8-bit), sampled (wider), end to end |
| Integer bitwise operations and checked shifts | [Numbers](lang/Numbers.md#bitwise-operations) | `backend-circuits.shifts_cover_each_lane_and_trap_on_invalid_counts`, `programs/operators.nra`, `traps/shift-count.nra`, `frontend` (shift count type) | Exhaustive (8-bit words and valid counts), sampled (64-bit), end to end |
| Comparison chains | [Core language](lang/CoreLanguage.md#comparison-chains) | `programs/operators.nra` (capture once, short circuit, both connectors), `frontend` (mixed connectors and repeated comparisons) | End to end |
| Truncating `/`, `%` (compatibility change) | [Numbers](lang/Numbers.md#integer-arithmetic) | `backend-circuits.exhaustive_eight_bit_operations`, `wide_operations…`; `ir-optimization-behavior.integer_folding_agrees_with_oracles`; `backend-conversion.functions_agree…` (`rem_i8`, `rem_u8`); `programs/integers.nra`; `traps/divide-by-zero.nra`, `traps/remainder-by-zero.nra` | Exhaustive (8-bit), sampled, end to end |
| Boundary literals | [Numbers](lang/Numbers.md#integer-literals) | `programs/integers.nra` (int32 and int64 minimums); `frontend` (range errors) | End to end |
| `as` conversions, integer | [Numbers](lang/Numbers.md#conversions) | `backend-circuits.integer_conversions_between_every_format`; `backend-conversion` (`to_unsigned_i8`, `to_signed_u8`, `narrow_i16`, `widen_u8`); `traps/conversion-overflow.nra`; `frontend` (invalid conversions) | Exhaustive (8/16-bit sources), sampled, end to end |
| float16, bfloat16, float32, float64 arithmetic and comparisons | [Numbers](lang/Numbers.md#floating-point-types) | `backend-floats.tiny_formats_exhaustively` (e3m2, e4m3 against a brute-force exact reference and a double reference), `sixteen_bit_binary_operations`, `wide_binary_operations`; `backend-conversion.floats_agree_with_the_reference_evaluator`; `programs/floats.nra` | Exhaustive (tiny formats), sampled (16/32/64-bit), end to end |
| Rounding, subnormals, signed zeros, infinities, canonical NaN | [Numbers](lang/Numbers.md#rounding-and-exceptions) | the float tests above check bit patterns, including NaN encodings; `programs/floats.nra` | Exhaustive (tiny formats), sampled |
| Float conversions | [Numbers](lang/Numbers.md#conversions) | `backend-floats.sixteen_bit_unary_operations_exhaustively`, `wide_conversions`; `traps/float-to-integer.nra` | Exhaustive (every 16-bit value), sampled |
| Float literals | [Numbers](lang/Numbers.md#float-literals) | `backend-floats.literals_round_exactly` (against `strtod`/`strtof`, float16 ties); `frontend` (overflow and underflow errors) | Sampled |
| Generic functions, classes, methods, bounds, nested instances | [Polymorphism](lang/Polymorphism.md) | `backend-generics`; `programs/generics.nra`; `examples/polymorphism` driver; `frontend` (unsatisfied bound, generic inheritance, generic override, missing type arguments); `ir-optimization-verifier.rejects_unsatisfied_bounds…` | Exhaustive (8-bit wrappers), end to end |
| Specialization limits | [Polymorphism](lang/Polymorphism.md#type-arguments-and-specialization) | `backend-generics.limits_stop_unbounded_specialization`; `unsupported/unbounded-generic.nra` | End to end |
| Three-part `for`, for-each | [Core language](lang/CoreLanguage.md#loops) | `programs/loops.nra` (empty collections, nested loops, early exits, continue, the collection evaluated once, element writes); `backend-conversion.structures_agree…` (`skip_and_stop`, `list_sum`); `frontend` (scopes, step restrictions, element types) | Exhaustive (8-bit), end to end |
| For-each modification rule | [Core language](lang/CoreLanguage.md#loops) | `traps/for-each-append.nra`, `for-each-alias.nra` (remove then append through an alias), `for-each-last.nra` (the final advance); `backend-vm.list_versions_count_structural_changes`; `backend-conversion` (`grow_while_iterating`) | End to end, exhaustive (8-bit) |
| Enums | [Types](lang/Types.md#enums) | `programs/enums.nra`; `frontend` (unknown variant, empty enum, ordering, integer-to-enum, duplicate variant) | End to end |
| Enum payloads, matches, and tuples | [Types](lang/Types.md) | `programs/aggregates.nra` (recursive payloads, named field evaluation order, exhaustive match, tuple returns, indexing, and destructuring), `frontend` (non-exhaustive match, wrong payload type and tuple arity) | End to end |
| Closures and first-class function values | [Core language](lang/CoreLanguage.md) | `programs/closures.nra` (nested, escaping, immediately invoked, and bound method closures), `backend-cli` (NIR build), `frontend` (invalid capture assignment) | End to end |
| Initialization order, top-level statements | [Modules](lang/Modules.md#initialization) | `initialization/main.nra` (two files, through `.nra`, `.nir`, and unoptimized builds); `backend-conversion.initialization_order_matches_the_evaluator`; `traps/uninitialized-global.nra`; `examples/resources` driver | End to end |
| Objects, construction, destruction | [Object lifecycle](lang/ObjectLifecycle.md) | `programs/objects.nra`; `backend-conversion.structures_agree…` (`two_counters`, `construction_order`); `traps/use-after-destroy.nra`, `uninitialized-field.nra` | Exhaustive (8-bit), end to end |
| Collections and `length` | [Collections](lang/Collections.md) | `programs/collections.nra`; `examples/collections` driver; `backend-conversion` (`element`, `remove`); `traps/index-out-of-bounds.nra`, `negative-index.nra` | Exhaustive (8-bit), end to end |
| Errors | [Errors](lang/Errors.md) | Trap programs above; `backend-vm` faults | End to end |
| Test runner | [Testing](lang/Testing.md) | `backend-cli` (`math-tests`, `testing/suite.nra` with passes, failures, a trap, isolation, and an optimized and an unoptimized build; no tests found) | End to end |
| Runnable examples | [Status](lang/Status.md#examples) | `backend-cli`; `frontend`; `ir-optimization-cli` | End to end |

## Toolchain and formats

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| Build routes give identical bytes; builds are reproducible | [Tooling](lang/Tooling.md) | `backend-nir.both_build_routes_emit_identical_images`; `backend-cli` (Hello World, initialization, programs built twice) | Structural |
| `.nir` 1.2 and minimal versions | [NeuraIR file format](NirFormat.md) | `backend-nir` (round trips, `minor_version_follows_the_features_used`, corrupt files, 1.3 rejected) | Structural |
| Tracing garbage collection | [NVM](runtime/NVM.md) | `backend-vm` (unreachable cycles, typed roots and parallel assignments under heap pressure) | Runtime |
| NVR 1.1: float types, `ReadVersion`, reason 5, minimal versions | [NVR](runtime/NVR.md#versions-10-and-11) | `backend-format.version_1_1_features`, `float_types_and_constants` (hand-assembled fixtures, 1.1 features in 1.0 images rejected, 1.2 rejected); `backend-cli` (Hello World still 1.0) | Structural |
| Malformed image rejection | [NVR](runtime/NVR.md#writer-and-loader-requirements) | `backend-format` (every truncation and the validation cases) | Exhaustive (truncations), structural |
| Installed toolchain | [Tooling](lang/Tooling.md) | `backend-install` | End to end |
| Host services and traces | [NVM](runtime/NVM.md#host-services) | `backend-vm`; `decompiler-behavior` (record and replay) | End to end |

## Decompiler

| Feature | Specification | Tests | Kind |
| --- | --- | --- | --- |
| Lossless assembly and reassembly | [Decompiler](Decompiler.md#assembly-the-lossless-form) | `decompiler-lossless` (every corpus image, edited images, assembly errors) | Structural, over the corpus |
| Lifting with structural proof | [Decompiler](Decompiler.md#lifting) | `decompiler-lift.claims_hold_and_coverage_is_reported` (every claim re-checked and minimum coverage enforced), `lifted_operations_match_the_source` | Structural, over the corpus |
| Edited weights and stripped metadata | [Decompiler](Decompiler.md#lifting) | `decompiler-lift.edited_weights_are_not_lifted_from_stale_structure`, `provenance_is_optional` | Structural |
| Behavior of the recovered form | [Decompiler](Decompiler.md#re-synthesis-and-behavior-comparison) | `decompiler-behavior` (trace replay of every compiled corpus image, with and without arguments, including conditional output and traps; fixture functions on every 8-bit input) | Sampled (programs), exhaustive (8-bit functions) |

## Not in this release

The standard library beyond `println`, `Entry`, and `Test`; concurrency; foreign interop; and packages ([Status](lang/Status.md#outside-this-release)). MSVC builds are not verified.
