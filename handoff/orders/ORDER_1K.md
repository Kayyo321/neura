# Work order 1K — the frontend's use-after-free in accessor lowering, and every reference like it

Read `handoff/WORK_RULES.md` first; it binds you. Then, in your worktree: `git reset --hard ccr-e433a631-4hzo4n` (the branch head, commit 6334d44; your worktree may start from another commit).

## The defect

Stage: NeuraIR lowering (`src/neura-ir`). Not the backend, not the runtime.

The Tier 2 sanitizers job (`.github/workflows/tier2.yml`, job `sanitizers`) has failed in `ui-accessors` since before this branch, with an AddressSanitizer heap-use-after-free. The cause, read from the code:

`src/neura-ir/lowering_accessors.inc`, function `lower_accessors`: `const auto& name = symbol(field).name;` is a reference into the lowering's symbol container. The function then calls `context(function)`, `value(...)`, `body(...)`, which add symbols; the container reallocates; `name` dangles; it is then read to build diagnostics (E0405 "the getter of …", W0005 "the setter of … never stores …").

Without sanitizers the freed memory usually still holds the old bytes, so the diagnostics look right. It is still a read of freed memory in the compiler.

## What to do

A. Reproduce first. Build the sanitizer configuration (below) and run `ctest --test-dir build-san -R '^ui-accessors$' --output-on-failure`. Quote the report's first frames.

B. Fix it in `lower_accessors` by the smallest change that cannot dangle (a copy of the name, taken once).

C. **Audit the whole of `src/neura-ir`** (every `.cpp`, `.hpp`, `.inc`) for the same class of defect: a reference, pointer, iterator, `std::string_view` or `std::span` into a container that can grow or be erased from (`symbols_` behind `symbol(...)`, `entries_`, type tables, scope maps, module lists, instruction vectors being appended to, …), held across any call that can grow or shrink that container, and read or written afterwards. There are about 67 sites of the form `auto& x = symbol(` alone; references to members (`symbol(x).parameters`, `.name`, `.type`), range-for loops over a member vector whose body lowers code, and `entries_[i].declaration`-style pointers count too. For each site decide: safe (say why in a word: "no call between", "callee cannot add symbols", "container is stable: std::deque / node-based / filled before lowering starts") or defective. Establish first what the containers are and whether their element addresses are stable; if `symbols_` is a `std::vector`, say whether making its storage stable (for example `std::deque`) is a smaller and safer change than fixing sites one by one, and what it would cost. **Do not make that container change without a measured compile-time comparison** on `tests/bench-wall/neura/*.nra` and the standard library tests (`neura test` over the largest directory under `tests/stdlib`): at most 2% slower.
   Fix every defective site. Do the same audit, more briefly, for `src/parser` and `src/tooling` only if they hold references into the same containers (list what you looked at).

D. Nothing observable changes: the same diagnostics (text, code, spans, labels, notes, order), the same NeuraIR, the same images byte for byte. Check images: build every program of `tests/bench-wall/neura` and `tests/backend/programs` with the binary of the branch head and with yours and compare the `.nvr` files with `cmp`.

## Tests

`ui-accessors` under the sanitizers is the test for B. For every other defective site you find, give the smallest source file that makes AddressSanitizer report it before your fix (add it as a UI test case the way the neighbouring `tests/ui/**` cases are written, expected output included, only if it shows a diagnostic or behaviour not already covered; otherwise name the existing test that reaches the site). A site you believe defective but cannot reach with any input: fix it and say so.

## Sanitizer build and run (required)

```
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined"
cmake --build build-san --parallel 2
export ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
ulimit -s 1048576
ctest --test-dir build-san --parallel 2 --timeout 3600 --output-on-failure \
  -R '^(frontend|concurrency-frontend|source-docs|doc-examples|nir-link|diagnostics-unit-.*|ui-.*|ir-optimization-.*|backend-nir|backend-generics|backend-stdlib|stdlib-install|stdlib-accessors|stdlib-core|stdlib-json|stdlib-hash-map|c-import)$'
```

(If the configuration needs other options, use what the `sanitizers` job of `.github/workflows/tier2.yml` uses.) Zero sanitizer reports in these suites. A report in code outside `src/neura-ir`: do not fix it; report it with the first frames.

Use `--parallel 2` for builds and tests: other work shares this 4-core machine.

## Release build

```
cmake --build build --parallel 2 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"
ctest --test-dir build --parallel 2 --output-on-failure \
  -R '^(frontend|concurrency-frontend|source-docs|doc-examples|nir-link|diagnostics-unit-.*|ui-.*|ir-optimization-.*|backend-nir|backend-generics|backend-stdlib|stdlib-install|stdlib-accessors|c-import)$'
```

## Files

`src/neura-ir/**`, and `tests/ui/**` for new cases. Nothing else; if a fix seems to need another file, stop and report.

## Report

As WORK_RULES.md says, plus: the container facts of C; the audit table (file:line, what is held, across which call, verdict); for each fixed site whether a test reaches it; the result of D.
