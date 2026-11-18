# Neura 0.3.0: application platform, targets and budgets

This note freezes the contracts of the 0.3.0 application platform before its first implementation change, as [the 0.3.0 baseline](Release030.md) requires: one Neura program model for a browser frontend, a packaged desktop application and a backend server. It is the overview of six notes and the single owner of the target matrix, the version axes, the diagnostic codes, the finite limits, the measured budgets, the code inventory and the statement of what the development machine can qualify.

| Note | Owns |
| --- | --- |
| This note | Targets, processes, versions, codes, limits, budgets, inventory, qualification |
| [HTML factories and CSS](Html030.md) | The `html` and `css` forms, bindings, conditions, keyed lists, evaluation and commit |
| [State, components and contracts](Components030.md) | `Std.Ui` state and components, events, commands, `FrozenList`, contract generation, HTTP clients and bridges |
| [Backend](Backend030.md) | `Std.Net.Http`, routes, the provider ABI, SQL, migrations, request workers |
| [Desktop](Desktop030.md) | The window host, the native child, the bridge, packages |
| [Projects and tools](Projects030.md) | `neura.toml`, `neura.lock`, bundles, and the `new`, `dev`, `contracts`, `build` and `package` commands |

The owner approved the A0 design decisions on 2026-11-17. Rows marked *proposed* specify work scheduled for A1–A6. Each milestone records implementation and qualification evidence as its checks pass. A row that changes is changed here, with the reason. Rows move to the pages named under [Owning pages](#owning-pages) in the change that implements them.

A1–A6 remain pending. The [target matrix](#targets) identifies where each platform needs a recorded run. Provisional budgets await measurements of the reference application.

## Principles

| Rule | Status |
| --- | --- |
| An application is Neura for behavior, HTML for structure, CSS for presentation, and immutable shared contracts between its parts. The author writes no JavaScript, and no deployed application interprets Neura source. | proposed |
| Every source rule is decided in the frontend and in `Std` library code written in Neura. A host adapter (browser, desktop, server) applies operations it is given; it chooses no evaluation order, no key identity, no coercion and no failure rule. | proposed |
| The compiler gains three contextual forms, `html`, `css` and `from`, and nothing else. Routing, state, components, persistence and composition are library code. | proposed |
| No NeuraIR operation and no NVR section is added. A template lowers to ordinary NeuraIR that builds a view object and to one constant of descriptor bytes; a stylesheet is a bundle file named by its digest. NeuraIR and NVR keep the versions [the baseline](Release030.md#format-revisions) gives them. | proposed |
| Everything outside the circuit is a named, versioned host service with a capability, a size limit and a record in a replay trace, as today's services are ([NVM](../runtime/NVM.md#host-services)). | proposed |
| A program that uses none of this builds the image it builds today, byte for byte. | proposed |

## Targets

| Target | `kind` | What is shipped | Runs with |
| --- | --- | --- | --- |
| Browser frontend | `web` | `index.html`, the frontend image, `nvm.wasm`, the adapter `nvm.js`, stylesheets, assets, `bundle.json` | Any static file host. No SDK, no service, no server-side code. |
| Desktop application | `desktop` | The web target's bundle unchanged, the window host, a native image and its runner, selected providers | Windows, macOS and Linux, with neither SDK nor `neurad` installed, and with no listening socket. |
| Backend | `server` | The server image, the server host, the TLS and SQL providers the manifest selects | Foreground, or under a service manager the operator chooses. |
| Shared package | none | Nothing by itself: its sources or NeuraIR are compiled into each target that imports it | Wherever its imports are available. |

Outside 0.3.0: a native-widget renderer, mobile, server-side rendering and hydration, replacing live Neura state while it runs, HTTP/3 termination (HTTP/2 is in 0.3.0 by the owner's amendment of decision 9; see [Backend030](Backend030.md#http2-and-decision-9)), ORM proxies, SQL syntax in the language, a generic IPC or `invoke(any)` bridge, and an application updater.

### Browsers

| Browser | Target versions | Qualification environment |
| --- | --- | --- |
| Chromium (Chrome, Edge) | 156 and later | This machine (headless and under Xvfb) and hosted Linux, macOS and Windows runners |
| Firefox | 153 ESR and later | This machine and hosted runners |
| Safari | The current major version and the one before it at the first release candidate; the two numbers are recorded in the A2 qualification record | A hosted macOS runner only |

The adapter needs WebAssembly with exception handling and 64-bit integers as `BigInt`, ES modules, and nothing that needs cross-origin isolation: `nvm.wasm` is single-threaded and uses no `SharedArrayBuffer`, because a static host cannot be assumed to send the isolation headers. A browser below the matrix gets a static message from `index.html`, not a partial application.

### Desktop

| System | Architecture | Package | Qualification environment |
| --- | --- | --- | --- |
| Windows 11 and Windows Server 2025 | x86-64 | `.zip` and `.msix` | A hosted Windows runner (new for the repository) |
| macOS 15 | arm64 and x86-64 | `.app` in a `.dmg` | The hosted `macos-15` and `macos-15-intel` runners the release workflow already uses |
| Ubuntu 24.04 and Debian 13 | x86-64 | `.tar.gz` and `.deb` | The hosted `ubuntu-24.04` runner and this machine |
| Ubuntu 24.04 | arm64 | `.tar.gz` and `.deb` | The hosted `ubuntu-24.04-arm` runner |

Windows on arm64 and every older system are unqualified in 0.3.0 and `neura package` says so. The Windows row is for packaged desktop applications only; it adds no Windows SDK and no Windows `neurad`.

### Pinned third-party versions

Looked up on 2026-11-17. Each is pinned exactly, with its license and digest, in the lock file of the component that carries it, in the step named.

| Dependency | Version | Used by | Pinned in |
| --- | --- | --- | --- |
| Electron | 45.3.3 (Chromium 156.0.8078.100, Node 24.22.3) | Desktop window host | [Desktop](Desktop030.md#order-of-work) step 1 |
| Emscripten | 7.0.0 (compiler commit `777e6e5`, emsdk tag `7.0.0` at `1693ce7`) | Building `nvm.wasm` | A2 step 1, below |
| OpenSSL | 3.5 LTS (3.5.9 at lookup) | TLS provider of the server host | [Backend](Backend030.md#order-of-work) step 3 |
| SQLite | 3.54.1 amalgamation | SQLite provider | Backend step 5 |
| libpq | 18.7; PostgreSQL servers 16, 17 and 18 are qualified | PostgreSQL provider | Backend step 6 |

Emscripten was chosen over the WASI SDK (release 34 at lookup) because the NVM reports traps with C++ exceptions and needs no file system; the choice is revisited only if the size budget below fails.

## Processes

| Owner | Owns | Never does |
| --- | --- | --- |
| `neurad` (optional) | The outer application it launched, as one process tree | Restart an application, run routing, or reuse a worker |
| Desktop host | Its windows, renderers and one native child | Listen on a socket, or restart the native child after a trap |
| Server host | Its listeners, provider pools and request workers | Replay a request, or keep application state between requests |
| `neura dev` | Its watcher, its development HTTP server and the children it started | Register a service, or outlive its terminal session |

Closing an application ends its whole tree. No child becomes a resident service.

## Execution in the browser (A2)

`nvm.wasm` is the NVM of `src/nvm` built for `wasm32` without `src/jit`, `ffi_provider.cpp`, `fs_provider.cpp`, `net_provider.cpp`, `fleet.cpp` and the GPU device. It has no native tier 3.

| Rule | Status |
| --- | --- |
| The browser NVM runs tier 0, tier 1 and tier 2, and tier 2 runs **function programs on the portable interpreter**: `TierOptions::Programs::On`, `TierOptions::Tier3::Off`, native code off. That is today's `NEURA_TIER_PROGRAMS=1 NEURA_TIER3=off --jit=off`. | proposed |
| Tier 0 stays in the build as the oracle and the fallback, exactly as on every host: a block that is not proven, and every step a program declines, runs by reference. | proposed |
| The web image keeps its `LIFT` section. It is optional: a bundle built with `--no-lift-hints` runs correctly, more slowly, and is not what the budgets measure. | proposed |
| A proof is one uninterruptible piece of work: most take well under a millisecond, and a block with a `float64` multiplication or division takes tens ([Benchmarks](../Benchmarks.md)). In the browser a block that is due for proving is queued (a new `TierOptions` switch) and proven only at a yield where no event is waiting; until then it runs at tier 0. Which blocks are proven, and when, still changes nothing a run does. Budget B-W7 measures the result. A proof found to exceed 50 ms was to be made resumable in the step that found it; step 3 found one and bounded each attempt instead (below). | changed 2026-11-17, awaiting owner confirmation |
| The browser's results, traps, steps and peak cells equal a native `--tier=0` run of the same image under the same recorded host responses. | proposed |

The reason is measured. On this machine (8 cores, x86-64 Linux, the integration build of 2026-11-17, `scripts/bench-wall.py --quick`, 17 programs, medians of 3 runs, 1 run for tier 0), with native code off:

| Configuration | Geometric mean | Against function programs |
| --- | ---: | ---: |
| Tier 0, the reference evaluator (`--tier=0`) | 1.143 s | 16.5 times slower (4.4 to 38.7 by program) |
| Tier 1 (`--tier=1`) | 0.111 s | 1.60 |
| Tier 2, regions on the region interpreter (`NEURA_TIER3=off`) | 0.082 s | 1.18 |
| Tier 2, function programs on the portable interpreter (`NEURA_TIER_PROGRAMS=1`) | 0.069 s | 1.00 |
| Tier 3 IR on its interpreter (`NEURA_TIER3=interp`) | 0.113 s | 1.63 |

The reference evaluator alone would spend a 16 ms frame on what function programs do in 1 ms, so it cannot meet the input budget. Function programs beat regions on 14 of 17 programs and lose by at most 14% (`fannkuch`). The portable interpreter is already exercised by the program fuzzer and the `NEURA_TIER_PROGRAMS` suites, so the browser adds a host, not an execution mode. The cost is that `neura_lift` and `neura_circuit` are linked into `nvm.wasm` for the structural proof; the size budget W-1 includes them.

Other builds were running on the machine during the measurement, so the absolute times are indicative; the decision rests on the ratios, which step 3 repeats on an idle machine. These are native timings. The WebAssembly timings are first taken at A2 step 3; if function programs are slower there than regions by more than 5% in geometric mean, the default becomes regions and this table is replaced.

A2's order of work:

| Step | Content | Changes images |
| --- | --- | --- |
| 1 | Record the pinned toolchain in the component lock file; a CMake preset that builds `neura_nvr`, `neura_lift`, `neura_circuit` and the portable part of `neura_nvm` for `wasm32`; `TierOptions` selectable without the environment | no |
| 2 | `src/wasm/entry.cpp` and `hosts/web/nvm.js`: load, bind, run in slices with queued proofs, console and clock services; the conformance programs under Node | no |
| 3 | The `bench-wall` set in `nvm.wasm` under Node and headless Chromium; the table above repeated | no |
| 4 | The `neura.ui.*`, history and timer services ([HTML](Html030.md#host-services)); the same commits as the model host | no |
| 5 | `neura.http.fetch` ([Components](Components030.md#http-clients)); exact 64-bit transport vectors | no |
| 6 | Web bundles, the content-security policy, deep links, the static-host checks | no |

The `wasm-runtime` CMake preset takes `NEURA_EMSDK` as the path to the installed SDK checkout and verifies it against the component lock at configuration. It builds the image format, circuit, lift, kernels and portable NVM archives. `TierOptions::browser()` selects function programs and disables tier 3. The wasm build ignores native tier environment settings and executes parallel items in order on one thread.

```sh
NEURA_EMSDK=/path/to/emsdk cmake --preset wasm-runtime
cmake --build --preset wasm-runtime
```

For the standalone Node smoke test, configure the preset with `-DNEURA_WASM_SMOKE=ON`. Run `node tests/wasm/smoke.cjs build/wasm-runtime/neura_wasm_smoke.js /path/to/image.nvr 2` with a smoke image that uses only `neura.console.write_line`. The modes `0`, `1` and `2` exercise tier 0, normal tier 2 and eager portable function programs respectively.

Configure with `-DNEURA_WASM_HOST=ON` to link `nvm.wasm` and its ES module loader. The [browser adapter](../../hosts/web/README.md#nvm-execution) verifies and binds an image before starting it, then resumes the same execution across step slices. A due proof remains queued while the embedder processes events. The embedder can attempt one proof on an idle callback.

Step 2's production interface and step 4's services are implemented (2026-11-17, autonomous agents, awaiting owner confirmation): version 1 of the [browser adapter interface](../runtime/Providers.md#browser-adapter-interface) carries any host request, `run()` drives overlapping and cancellable requests, sleeps and scope deadlines without polling, and the [UI and history services](../runtime/Providers.md#ui-services) serve commits and events through the DOM adapter. Their checks are `tests/wasm/check-host.py` and `tests/apps/web/check.py`, under Node, headless Chromium 156 and headless Firefox 153 ESR. Step 6's bundles are assembled by `scripts/build-web-bundle.py` until `neura build` of a web target exists ([Web bundles](../runtime/Providers.md#web-bundles)); the script refuses an image that imports a service outside the web groups, and the page checks every module by its digest. Step 5's `neura.http.fetch` is implemented over the browser's `fetch` with the binding `Std.Net.Http.Fetch` ([HTTP fetch](../runtime/Providers.md#http-fetch)); its check is `tests/apps/web/fetch-check.py`, which runs a bundle against the page's origin and a second, CORS-enabled origin in headless Chromium and Firefox. The exact 64-bit transport vectors of step 5 apply to contract messages (A3), which carry integers as JSON text written by Neura code; the fetch service itself carries bytes. Recording and replay in the browser, gesture actions, and the B-W7 qualification remain open.

### Browser traces (design, not implemented)

*Proposed 2026-11-17 by autonomous agents, awaiting owner confirmation.* A browser run records the trace file of [NVM](../runtime/NVM.md#traces-of-images-with-tasks), byte for byte the format a native `neura run --record` writes, so that `neura run --replay` reproduces a browser run on a developer's machine and a browser can replay a native trace.

| Rule | Status |
| --- | --- |
| The trace is written by `nvm.wasm`, not by the adapter: the runtime answers `neura.process.exit` and every denied service itself, and only it knows each request's task and tick, which version 3 records. `neura_wasm_open` takes a record flag (`0x200` with the tier) and a new export, `neura_wasm_trace(run)`, returns the text with `nvm::write_trace`; both are additions to interface version 1 that a version 1 adapter never uses. | proposed |
| Requests, completions, deliveries with their ticks, clock readings that expire a timer or deadline, the settings and the outcome are recorded exactly as natively. A request the adapter answers carries the completion bytes it delivered; a cancelled request carries error 15 or the answer that arrived first. | proposed |
| Replay in the browser: `new NvmExecution(module, image, { replay: text })` answers every request from the trace in the runtime, as `nvm::replay` does, and reports the first difference; no service of the page is called. | proposed |
| The UI and history services are ordinary requests: a commit's bytes and an event batch's bytes are in the trace, so a replay reproduces what the program saw and did without a document. A trace contains what the user typed; recording is off unless the embedder asks for it. | proposed |
| A fetch is recorded with its request and response bytes, which can hold credentials and personal data. The adapter records only when the embedder asks, and never sends a trace anywhere. | proposed |

Step 3 was measured on 2026-11-17 with `tests/wasm/bench.py --runs 1 --browser chromium` on the loaded machine (other builds running), every output matching the native runner. The first measurement found `nvm.wasm` about 40 times slower than native function programs, slower than native tier 0: the structural proof rejected hints that the native runtime accepted. The cause was the C++ compiler, not WebAssembly. Gadgets whose neurons were built by two unsequenced calls came out in another order under Clang, which Emscripten uses, than under GCC, which built the images, so a Clang-built runtime regenerated different graphs (a native Clang build rejected the same hints). Fixing the order made GCC and Clang images identical, and the test `lift-portable` pins it. Then `nvm.wasm` was still refusing every float operation, because the lowering allowed floats only on x86-64 and AArch64. WebAssembly's float instructions are IEEE operations with one environment, so they are now lowered too. Geometric means of the 17 programs at their quick sizes, in milliseconds:

| Build | Native, function programs | Native, regions | Node, function programs | Node, regions | Chromium, function programs | Chromium, regions |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Before (round 1) | 70.6 | 82.4 | 2,920 | 2,986 | 2,795 | 2,838 |
| Same graphs from every compiler | 71.1 | 84.4 | 650 | 635 | 642 | 624 |
| Late idle proofs picked up | 72.7 | 81.0 | 626 | 625 | 644 | 640 |
| Floats lowered in WebAssembly | 76.0 | 86.7 | 550 | 544 | 555 | 544 |

The last row is 7.3 times native function programs in Chromium. The ratio of regions to function programs is 0.98 to 1.0 in WebAssembly, below the 5% that would change the default, so function programs stay the default. What remains slow is `nbody` (5.5 s against 87 ms): 10 of its 11 attempted hints lift and none is rejected, but the program of its main function keeps 74 of 116 blocks as stubs. The other float programs fell from about 6 s to about 1.5 s. The longest idle proof over the set was 26.2 ms in Chromium and 27.4 ms (27.6 ms of CPU) under Node. B-W5 and B-W7 need the 1,000-row reference list of A3 and `scripts/bench-web.mjs`, which do not exist yet, so neither was measured. `tests/wasm/check-host.py` under Chromium reported explicit idle proofs of up to 71.7 ms on the clock fixture at tier 1 on the loaded machine, and 43 ms in another run, so the 50 ms bound of B-W7 is not yet shown to hold.

The first measurement also found the over-50 ms proofs of step 2: with proofs deferred to idle callbacks a block accumulates runs, and each attempt's work allowance grew with them, up to an 88.3 ms attempt (80.2 ms of main-thread CPU) under Node. Browser attempts are now bounded instead of made resumable ([Idle proofs](../runtime/Providers.md#idle-proofs)); the longest attempt over the same set was then 25.7 ms (26.3 ms CPU) under Node and 27.6 ms in Chromium. This changes the proof row above, which asked for resumable proofs: the overflow analysis restarts on every attempt, and making it resumable is a change to `src/lift` that this step did not make. The bound of 2^19 work units is half the minimum `TierOptions` gives a binary64 multiply or divide graph, so such blocks may stay on tier 0 in a browser; what that costs is not measured, because lifting does not yet work in `nvm.wasm`. The decision was taken without the owner.

## Version axes

Each is independent of the language version, of NeuraIR and of NVR, starts at 1, and is negotiated or checked before anything that depends on it runs.

| Axis | Carried in | Checked by |
| --- | --- | --- |
| Project manifest schema | `neura.toml`, `schema = 1` | Every project command |
| Lock schema | `neura.lock` | Every project command |
| Bundle schema | `bundle.json` | `neura run`, the hosts, the adapter |
| Template descriptor encoding | First byte of each descriptor | The UI host at registration |
| UI host interface | Version of each `neura.ui.*` service | Image binding |
| Provider interface | Version of each `neura.http.*`, `neura.sql.*`, `neura.app.*` service | Image binding |
| Contract schema | `contracts.toml`, and `schema` in each generated registry | `neura contracts`, clients, servers, bridges |
| Bridge protocol | First frame between renderer, window host and native child | Each end |
| Request-worker protocol | First frame between server host and worker | Each end |
| Browser adapter interface | An export of `nvm.wasm` | `nvm.js` |

A mismatch on any axis is a rejection before the affected target or operation runs, with the axis, both versions and the component to replace. Nothing falls back to source, to script, or to a wider capability.

## Capability groups

A host grants a group only when the target's manifest lists it and the target's kind allows it ([Projects](Projects030.md#the-project-manifest)). A service of a group that is not granted still binds and answers error 14, as today.

| Group | Services | `web` | `server` | `desktop` (native child) |
| --- | --- | --- | --- | --- |
| console, process exit | `neura.console.*`, `neura.process.exit` | always | always | always |
| `clock`, `entropy` | `neura.clock.*`, `neura.entropy.fill` | always | always | always |
| `ui` | `neura.ui.*`, `neura.history.*` | may list | no | no |
| `http_client` | `neura.http.fetch` | may list | may list | may list |
| `bridge` | `neura.bridge.call` | may list; a browser answers 14 | no | no |
| `http_server` | `neura.http.server.*` | no | may list | no |
| `sql` | `neura.sql.*` | no | may list | may list |
| `app` | `neura.app.*` | no | may list | may list |
| `desktop` | `neura.desktop.*` | no | no | may list |
| `fs`, `net`, `env` | The existing file, socket and environment services | no | may list | may list |
| `native` | `neura.ffi.*` | no | no | no |

"No" is `E1027` at build when the service is reachable from the entry. In a browser the console group writes to the developer console, the clock is the browser's, and entropy is its cryptographic generator. `native` is excluded from every application target in 0.3.0 because native calls cannot be combined with tasks.

## Standard library modules

| Module | Source | Responsibility |
| --- | --- | --- |
| `Std.Ui` | `stdlib/std/ui.nra`, `stdlib/std/ui/` | Views, roots, state, components, events, forms, commands, routing |
| `Std.Web` | `stdlib/std/web.nra` | Mounting in a document, history, fetch policy, downloads |
| `Std.Desktop` | `stdlib/std/desktop.nra` | Windows, menus, dialogs, granted files, the bridge dispatcher |
| `Std.Net.Http` | `stdlib/std/net/http.nra`, `stdlib/std/net/http/` | Requests, responses, the client, routers, middleware |
| `Std.Data.Sql` | `stdlib/std/data/sql.nra`, `stdlib/std/data/sql/` | Pools, statements, rows, transactions, migrations |
| `Std.App` | `stdlib/std/app.nra` | Composition, configuration, health, logs, metrics, shutdown |
| `Std.Collections.Frozen` | `stdlib/std/collections/frozen.nra` | `FrozenList<T>` |
| `Std.Encoding.Json` | `stdlib/std/encoding/json.nra` (exists) | Manual codecs, and the derived codecs of the records milestone |

JSON keeps its existing name, `Std.Encoding.Json`. A second module, or a move to `Std.Data`, would break every 0.2 program that imports it for no gain.

## Inventory of reusable code

What exists at the integration base, by owning path, and what each application piece does with it. A search of `src`, `stdlib`, `tests` and `examples` found no HTTP, TLS, SQL, WebAssembly or TOML code, and no project manifest.

| Area | Owning paths today | Use |
| --- | --- | --- |
| Tokens and syntax | `src/lexer/lexer.cpp`, `src/parser/parser.cpp`, `src/parser/syntax.hpp` | Extended for islands; new `src/parser/html_template.cpp`, `css_stylesheet.cpp` |
| Lowering, summaries | `src/neura-ir/lowering.cpp`, `storage.hpp`, `purity.hpp`, `attributes.hpp`, `task_analysis.cpp`; `src/ir-optimization/effects.cpp` | Template lowering in a new `src/neura-ir/template_lowering.cpp`; render compatibility from the existing effect and retention facts |
| Host service interface | `src/nvm/host.hpp` (`HostService`, `HostSignature`, `PendingOperation`), `src/nvm/driver.cpp`, `src/nvm/providers.hpp` (`Capabilities`, `host_error`) | The provider ABI is this interface; see [Backend](Backend030.md#provider-abi) for the one change to `PendingOperation` |
| Sockets | `src/nvm/net_provider.cpp`; `stdlib/std/net/tcp-stream.nra`, `tcp-listener.nra`, `internal/socket.nra` | The server host's listener reuses the address and non-blocking code; `Std.Net.Http` does not sit on `TcpStream` |
| Record and replay | `src/nvm/trace.cpp`, `trace_file.cpp` | Every new service is recorded and replayed by the same code |
| JSON in Neura | `stdlib/std/encoding/json.nra` (1,183 lines: `JsonValue`, parser, writer) | Codecs, HTTP bodies, bridge payloads |
| JSON in C++ | `src/tooling/json.hpp` | `bundle.json`, lock files and registries in the tools; moved to `src/util/json.hpp` when the first tool outside `src/tooling` needs it |
| Digests | `src/util/sha256.cpp`; `stdlib/std/digest/sha256.nra` | Every content identity in manifests, locks and bundles |
| Collections | `stdlib/std/collections/immutable-list.nra`, `immutable-set.nra`, `immutable-map.nra` (read-only views over a shared list), `array-list.nra`, `sequence.nra` | Not reused for `FrozenList`: the views alias their source. `Sequence` is not a render source. |
| Immutability and transfer | `#Immutable` (`E0918` to `E0920`), transferable types (`E0602`) in `src/neura-ir/` | The definition of deep immutability, unchanged |
| Results and errors | `stdlib/std/core/result.nra`, `error.nra`, `optional.nra` | Local failure of every client and provider call |
| Text | `stdlib/std/text/strings.nra`, `stdlib/std/core/string-builder.nra`, `stdlib/std/core/internal/float-text.nra`, `integer-text.nra` | Text holes, header and path handling |
| Time, entropy, logging | `stdlib/std/time/`, `stdlib/std/random/`, `stdlib/std/log/logger.nra` | Deadlines, request identifiers, structured logs |
| Tasks | `stdlib/std/concurrent/` (`scope`, `channel`, `cancellation`, `worker-group`) | Commands, the UI event loop, request cancellation |
| Runner and containers | `src/runner/runner.cpp`, `src/launcher/container.cpp`, `descriptor.cpp`, `main.cpp` | The native child and the request worker are the runner with a different host registry; bundles are not containers |
| Install layout | `src/cli/install_layout.cpp`, `lifecycle.cpp`, `packaging/` | Target components are installed and found the way the SDK's are |
| Editor | `src/tooling/analysis.cpp`, `lsp.cpp`, `formatter.cpp`, `refactor.cpp` | Islands in the same document model |
| Command line | `src/cli/main.cpp`, `argument_lists.cpp` | Nothing is added to `main.cpp`: each new tool has its own entry file under `src/app/` and joins the tool registry of the split-product milestone |

The runtime sources carry `_WIN32` guards under which console input, files, sockets and native calls answer `unsupported` (`src/nvm/providers.cpp`, `net_provider.cpp`, `fs_provider.cpp`), but no workflow builds or tests the runtime on Windows, so whether it compiles there today is unknown. A native Windows child is therefore new porting work, a step of its own in [Desktop](Desktop030.md#order-of-work).

## Diagnostics

The codes below are reserved for the application platform. Each is added to `src/util/diagnostic_codes.def`, with its page and its fixture, in the change that implements it; the range line `E10xx  applications` is added to that file's header with the first of them.

| Range | Stage | Owner | Assigned | Free |
| --- | --- | --- | --- | --- |
| `E0240` to `E0259` | Parser | [HTML](Html030.md#diagnostics) | `E0240` to `E0256` | `E0257` to `E0259` |
| `E0580` to `E0589` | Classes | [Components](Components030.md#diagnostics) | `E0580` to `E0584` | `E0585` to `E0589` |
| `E1001` to `E1019` | Applications: templates and styles | [HTML](Html030.md#diagnostics) | `E1001` to `E1015` | `E1016` to `E1019` |
| `E1020` to `E1039` | Applications: projects, targets, bundles | [Projects](Projects030.md#diagnostics) | `E1020` to `E1033` | `E1034` to `E1039` |
| `E1040` to `E1059` | Applications: contracts and providers | [Components](Components030.md#diagnostics), [Backend](Backend030.md#diagnostics) | `E1040` to `E1045` | `E1046` to `E1059` |

A rule that an existing check already enforces keeps that check's code: a capturing handler that is retained without `new func`, a value that is not transferable (`E0602`), an unresolved import (`E0701`).

A failure at run time is not a compiler diagnostic. It is a returned `Result`, a `ui.Diagnostic`, a host error code, or a trap, as each note says.

## Finite limits

Every limit has a default, a manifest key under `[limits]` of the target ([Projects](Projects030.md#the-project-manifest)), and a hard maximum the tools reject above (`E1028`). Exceeding one at run time is the outcome named, never an unbounded queue.

| Id | Limit | Default | Maximum | When exceeded |
| --- | --- | ---: | ---: | --- |
| U-1 | Nodes in one template descriptor | 4,096 | 65,535 | `E1015` at build |
| U-2 | Nesting depth of elements and control blocks | 64 | 255 | `E1015` at build |
| U-3 | Dynamic slots in one template | 1,024 | 65,535 | `E1015` at build |
| U-4 | Items committed by one `@for` | 10,000 | 1,000,000 | `Invalid(LimitExceeded)` at commit |
| U-5 | Nodes in one root | 100,000 | 4,000,000 | `Invalid(LimitExceeded)` at commit |
| U-6 | Bytes of one text or attribute value | 1 MiB | 16 MiB | `Invalid(LimitExceeded)` at commit |
| U-7 | Queued events for one root | 1,024 | 65,536 | Input overflow ([Components](Components030.md#events)) |
| U-8 | Stylesheets registered | 256 | 4,096 | `E1015` at build |
| U-9 | Live roots in one execution | 16 | 256 | `Err` from `mount` |
| U-10 | Commands in flight for one root | 32 | 1,024 | `Rejected` from `Command.run` |
| W-1 | Bytes of one bundle file | 64 MiB | 1 GiB | `E1029` at build |
| W-2 | NVM work between yields to the browser | 8 ms, tested every 4,096 steps | 50 ms | The NVM yields and resumes; no observable effect |
| W-3 | Bytes of one fetch request or response body | 8 MiB | 256 MiB | `Err(TooLarge)` |
| W-4 | Fetches in flight | 16 | 256 | `Err(Busy)` |
| C-1 | Operations in one contract | 256 | 4,096 | `E1041` |
| C-2 | Bytes of one encoded contract message | 1 MiB | 64 MiB | `Err(TooLarge)` before sending; status 413 or `Rejected` on receipt |
| C-3 | Elements of one `FrozenList` in a message | 10,000 | 1,000,000 | Decode error |
| C-4 | Nesting of a message | 32 | 128 | Decode error |
| S-1 | Request workers running | 8 | 1,024 | The request waits (S-3) |
| S-2 | Ready workers that have run nothing | 2 | 64 | A worker is started on demand |
| S-3 | Requests waiting for a worker | 64 | 65,536 | Status 503 with `Retry-After: 1`, before any application code |
| S-4 | Connections open | 1,024 | 1,000,000 | The connection is refused after `accept` |
| S-5 | Bytes of request head (line and headers) | 16 KiB | 1 MiB | Status 431 |
| S-6 | Bytes of a request body | 1 MiB | 1 GiB | Status 413 |
| S-7 | Bytes of a buffered response body | 8 MiB | 1 GiB | The worker's send fails with `TooLarge`; status 500 if nothing was sent |
| S-8 | Seconds for one request, from head to last byte | 30 | 3,600 | The worker is cancelled, then killed after S-12; status 504 if nothing was sent |
| S-9 | Connections in one SQL pool | 8 | 1,024 | The acquire waits (S-10) |
| S-10 | Acquires waiting on one pool, and seconds each waits | 64; 5 | 65,536; 600 | `Err(PoolExhausted)`; `Err(TimedOut)` |
| S-11 | Rows of one materialized page | 1,000 | 1,000,000 | `Err(TooLarge)`; nothing is returned |
| S-12 | Seconds to drain at shutdown, and of grace before a worker is killed | 30; 5 | 3,600; 60 | Remaining workers are killed and reported |
| S-13 | Bytes of one log record, and records buffered | 16 KiB; 4,096 | 1 MiB; 1,000,000 | The record is truncated with a marker; the oldest is dropped and counted |
| S-14 | Concurrent HTTP/2 streams on one connection (`SETTINGS_MAX_CONCURRENT_STREAMS`) | 100 | 1,000 | The stream is refused with `RST_STREAM(REFUSED_STREAM)`; each admitted stream still counts against S-1 and S-3 |
| D-1 | Windows open | 8 | 64 | `Err` from `open_window` |
| D-2 | Bridge calls in flight for one window | 32 | 1,024 | `Err(Busy)` |
| D-3 | Bytes of one bridge frame | C-2 | C-2 | The frame is refused and the call fails with `TooLarge` |
| D-4 | Seconds for one bridge call | 30 | 3,600 | `Err(TimedOut)`; a late reply is discarded |

Decision taken without the owner (apps integration round 2, 2026-11-18): the boot module of a web bundle runs its image with the largest step limit, because an application's event loop lasts as long as the page and the runtime's default of 100,000,000 steps for a whole run stopped the 1,000-row page of B-W5 before its 200 clicks. A handler that never ends is therefore not stopped by a step trap in a page; W-2 still yields to the browser, so the page stays responsive. A page that constructs `NvmExecution` itself keeps the default unless it passes `maxSteps`. The desktop host's child is not covered by this change.

## Budgets

A budget is a number the release must meet, with the workload, the statistic, the command that measures it and where it can be measured. The reference application is the stock-list sample of [Projects](Projects030.md#the-reference-application). Numbers marked *provisional* are estimates derived from the baselines named; the step named measures them, and a miss is resolved there by a recorded change to the budget or to the design, never silently.

| Id | Budget | Number | Workload and statistic | Measured by | Where |
| --- | --- | ---: | --- | --- | --- |
| B-W1 | `nvm.wasm` | 4 MiB; 1.25 MiB as Brotli level 11 | The release build. Basis: the static native executable, 4.86 MB with the code generators and native calls the browser build omits. *Provisional.* | `scripts/check-web-size.py dist/web` (new), in CI | Here, A2 step 2 |
| B-W2 | `nvm.js` | 64 KiB; 20 KiB as Brotli | The shipped adapter. *Provisional.* | The same script | Here; A2 step 2 |
| B-W3 | First load of the reference web bundle | 2 MiB transferred as Brotli | Every file `index.html` needs before its first commit. *Provisional.* | The same script, summing `bundle.json` entries marked `initial` | Here; A6 |
| B-W4 | First usable UI | 1.0 s median, 1.5 s 95th percentile; 3.0 s median at 4 times CPU throttling | Navigation start to the mark `neura:first-commit`, cold HTTP cache, 30 loads from a local static host. *Provisional.* | `scripts/bench-web.mjs` (new; Node and the DevTools protocol, no package dependency) | Here in headless Chromium and Firefox; Safari on a hosted runner |
| B-W5 | Input to commit | 8 ms median, 16 ms 95th percentile | A dispatched click that changes one row of a 1,000-row keyed list, to `neura:commit-end`; 200 clicks | The same script | Here in headless Chromium; A3 |
| B-W6 | Keyed reorder | 50 ms 95th percentile | Reversing the 1,000 rows; 50 times | The same script | Here; A3 |
| B-W7 | Main-thread slice | No task over 50 ms after first commit | The B-W5 and B-W6 runs, from the browser's long-task entries | The same script | Here in headless Chromium; A3 |
| B-W8 | View and state allocation | The recorded count, then no growth without an explanation | Heap cells and steps of one update of the counter component and of one row change of the 1,000-row list, on the model host | `tests/benchmarks/baseline.txt` rows `ui-counter`, `ui-list` (new) | Here; recorded at A3, exact and deterministic |
| B-W9 | Retained growth | Zero | After 10,000 mount and unmount cycles and 100,000 updates: listeners, subscriptions, handles and live heap cells equal their values after cycle 100 | A CTest program on the model host; the same journey in headless Chromium, reading `neura.ui.stats` | Here; A3 |
| B-D1 | Desktop cold launch | 1.5 s median, 2.5 s 95th percentile | Process start to first commit of the reference application, 10 launches, warm file cache. *Provisional.* | `scripts/bench-desktop.py` (new), reading the host's `--timing` output | Linux here under Xvfb; Windows and macOS on hosted runners only |
| B-D2 | Desktop memory | 350 MiB for the whole tree; 40 MiB for the native child | Proportional set size (PSS) 5 s after first commit, summed over the process tree, so that a page the processes share counts once (owner decision 2026-11-18; the child is measured as RSS). Basis for the child: `neura run` peaks near 19 MiB on the benchmark programs. *Provisional.* | The same script | As B-D1 |
| B-D3 | Desktop package | 130 MiB per package | Each package of the matrix, as built. Dominated by the engine; reported beside the SDK archive, never inside it. *Provisional.* | `neura package --report` | Linux here; Windows and macOS on hosted runners only |
| B-S1 | Request latency, fresh worker | 5 ms median, 15 ms 95th, 40 ms 99th percentile | `GET` of a JSON operation that touches no database, HTTP/1.1 keep-alive on loopback, 32 connections, 8 workers, 60 s after 10 s of warm-up. Basis: a whole `neura run` of a hello-world image takes 0.68 ms. *Provisional.* | `scripts/bench-server.py` (new; a fixed-rate loader in Python with no dependency) | Here; A4 |
| B-S2 | Throughput | 1,500 requests per second | The B-S1 workload at saturation with no 503. *Provisional.* | The same script | Here; A4 |
| B-S3 | Latency with SQL | 10 ms median, 30 ms 95th percentile | One page of 50 rows from SQLite, and from PostgreSQL 18 in a container on loopback. *Provisional.* | The same script | Here (SQLite; PostgreSQL through Docker); A4 |
| B-S4 | Saturation | 503 within 5 ms at the 95th percentile; processes never above S-1 plus S-2 plus 1 | Twice the B-S2 rate for 30 s | The same script, sampling the process tree | Here; A4 |
| B-S5 | Drain | Exit within S-12 plus 1 s; every request in flight completes or is reported | A termination signal under the B-S1 load | The same script | Here; A4 |
| B-S6 | Sustained load | Resident size slope under 1% per minute after minute 2; no descriptor or process left | 10 minutes at half the B-S2 rate, with one request in 1,000 trapping | The same script | Here; A6 |

Measured in the apps integration, round 2 (2026-11-18, `tests/apps/web/bench-web.py`; Linux x86-64, 8 CPUs, a shared machine at load average 2.1 to 5.3; headless Chromium 156 and Firefox 153 ESR; `nvm.wasm` of the A2 round-2 build; 1,000 rows of `tests/apps/web/bench/rows.nra`, each row a component, and `bench/list.nra`, one `Std.Ui.Controls.list`). B-W5 and B-W6 are misses, recorded rather than resolved:

| Budget | Chromium | Firefox |
| --- | --- | --- |
| B-W5, a row component changes (only that row renders) | median 131.5 ms, 95th 265.7 ms | median 137.0 ms, 95th 256.0 ms |
| B-W5, a row of the list control changes (the list renders) | median 231.2 ms, 95th 258.6 ms | median 289.0 ms, 95th 365.0 ms |
| B-W6, reversing the row components | 95th 288.6 ms | 95th 309.0 ms |
| B-W7, tasks over 50 ms after the first commit | none; the observer reported an 80 ms control task of the page first (the run yields between slices) | not measured: Firefox reports no long-task entries |
| B-W9, browser half, after cycle 100 and cycle 1,000 (10,000 clicks; the budget names 10,000 cycles) | `neura.ui.stats` 4 nodes, 2 listeners, 1 template, 0 queued; 1 open root; 1 subscriber: equal; `nvm.wasm` linear memory did not grow (17,891,328 bytes) | the same |

The B-W5 time is the reconciler and the turn in `nvm.wasm`: about 2.8 million steps for a row component's change and 4.7 million for the list control's (model host, `Components030`), which at the measured times is roughly 20 million steps a second in the browser (an inference from the model-host counts). The B-W9 figures are host counts; linear memory grows in pages and only shows that the runtime asked for no more, not that its live cells stayed equal, and the counter that the cycles mount is a root's top component, which the reconciler's reuse does not memoize. The model-host run of B-W9 takes 54.2 million steps for 10,000 cycles (5,417 a cycle), under its limit of 100 million. Chromium's JavaScript heap read 6.2 and 8.4 MB at the two points without a forced collection, which is not evidence either way.

Published results name the hardware, system, browser, engine, driver and workload. A language benchmark is not evidence for B-W4 to B-W7, and the size of the core is not evidence for B-D3.

## What this machine can qualify

Probed on 2026-11-17: x86-64, 8 cores, Debian 13, Chromium 156.0.8078.79, Firefox 153.7.0 ESR, Node 26.13.1, clang 19.1.7 (which lists a `wasm32` target but has no WebAssembly linker and no system root), Xvfb and a display, Docker 29.9.3, SQLite 3.46.1 and OpenSSL 3.5.7 development files. The local Emscripten installation reports `emcc 7.0.0` at compiler commit `777e6e5`. A2 still needs to build and run the browser NVM. Both browsers were run headless, and Chromium ran a 64-bit WebAssembly addition whose result passed through `BigInt` exactly.

| Can be qualified here | Cannot be qualified here |
| --- | --- |
| The frontend, lowering, images and every compiler diagnostic | Safari, on any system |
| Templates, commits and events on the model UI host, with replay | Windows and macOS desktop packages, their launch, menus, dialogs and uninstall |
| `nvm.wasm` under Node, headless Chromium and headless Firefox after the A2 build | Signing and notarization on any system: there are no credentials |
| Browser journeys by injected events: clicks, keys, focus, controlled inputs, history, fetch | Input method editors, screen readers and a physical keyboard: injected events do not prove them |
| The server host, HTTPS on loopback, SQLite, and PostgreSQL 16 to 18 in containers | PostgreSQL on a remote network, and load beyond 8 cores |
| The Linux desktop package under Xvfb: launch, window, bridge, dialogs through the test driver, process-tree cleanup | The appearance of native menus and dialogs to a person; arm64 Linux |
| Every budget marked "Here" | B-D1 to B-D3 for Windows and macOS; B-W4 for Safari |

Evidence from this machine is recorded as "Linux x86-64, headless" and is never the evidence for another row of the matrix. The rows it cannot reach are qualified by hosted runners where a runner exists, and by a person where the right column says so; until then they are reported as unqualified, not as passed.

## Order of work

| Milestone | Note | Depends on |
| --- | --- | --- |
| A0 | These six notes | The baseline |
| A1 HTML and CSS compilation | [HTML](Html030.md#order-of-work) | Named arguments and inference; `new func` and retention summaries |
| A2 Browser execution | [Above](#execution-in-the-browser-a2) | A1; regions and inline values in the runtime |
| A3 Components, state, contracts | [Components](Components030.md#order-of-work) | A1, A2; records, derived codecs, `using`, result propagation |
| A4 Backend | [Backend](Backend030.md#order-of-work) | A3; `using`; the split tools |
| A5 Desktop | [Desktop](Desktop030.md#order-of-work) | A2, A3 |
| A6 Integration | [Projects](Projects030.md#order-of-work) | A1 to A5; the split products |

The notes depend on features of other 0.3.0 milestones by name: records and `new with`, derived codecs, named arguments, interpolated strings, `using`, and `?` on a `Result`. Their grammar and rules are fixed by their own notes; nothing here defines them.

## Gate

The platform is accepted when:

- every program of the identity and extended sets builds the image bytes it built before the first application change;
- each note's acceptance list holds, with the fixtures named there;
- every budget above is met and recorded with its environment, or its row is changed with the reason;
- every row of the browser and desktop matrices is qualified by a recorded run, or the release notes say which is not;
- the reference application builds for `web`, `server` and `desktop` from one project, runs from its packaged bundles with sources, names and maps stripped, and no bundle of the `web` target contains a server secret, a native provider or a development endpoint.

## Owning pages

The rows of these notes move to tracked pages as they are implemented, and the pages are added to `site/contents.toml` and to the wiki mapping in the same change.

| Page | Content |
| --- | --- |
| `docs/apps/Overview.md` | Targets, processes, what is portable |
| `docs/apps/Html.md`, `docs/apps/Css.md` | The template and stylesheet languages |
| `docs/apps/Components.md` | State, components, events, commands, routing |
| `docs/apps/Contracts.md` | Contracts, codecs, clients, bridges, `FrozenList` |
| `docs/apps/Backend.md` | HTTP, SQL, the server host |
| `docs/apps/Desktop.md` | Windows, the bridge, packages |
| `docs/apps/Projects.md` | Manifests, locks, bundles, commands |
| `docs/runtime/Providers.md` | The host services and protocols of every host |

## Migration

For the 0.3.0 migration guide:

- No 0.2 program changes meaning. `html`, `css` and `from` stay ordinary names except in the positions [HTML](Html030.md#grammar) lists, none of which is valid 0.2 syntax.
- A program that builds markup from strings moves to `html` factories; a string in a hole is text, never markup.
- A program that shares an `ImmutableList` between tasks or puts one in a message moves to `frozen.copy_of`.
- A server written on `TcpListener` keeps working. To gain routes, TLS and request isolation it moves to `Std.Net.Http` and a `server` target.
- Single-file `neura build` and `neura run` are unchanged; a project is opt-in.

## Approved design decisions

1. No NeuraIR or NVR change: templates are ordinary NeuraIR plus a descriptor constant. A format change would bind the image format to a UI design that has not shipped once.
2. The browser NVM runs function programs on the portable interpreter, by the measurement above.
3. `nvm.wasm` is single-threaded and built with Emscripten, so that a static host needs no special headers and traps keep their C++ exceptions.
4. JSON stays `Std.Encoding.Json`; SQL is `Std.Data.Sql`.
5. The target matrix covers what a runner or this machine can run: Chromium 156, Firefox 153 ESR, Safari by rule; Windows 11 x86-64, macOS 15, Ubuntu 24.04 and Debian 13.
6. Electron 45.3.3, OpenSSL 3.5, SQLite 3.54.1 and libpq 18.7 are the pinned lines, from a lookup on the date above.
7. The diagnostic ranges are split as the table shows, with free codes left in each.
8. Every budget number marked provisional is an estimate; the rule for resolving a miss is fixed, the number is not.
9. Application tests live under `tests/apps/`, because `tests/ui/` already holds the compiler's diagnostic fixtures.
