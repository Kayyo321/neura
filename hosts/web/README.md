# Browser host

This directory holds the browser host of Neura web applications: the NVM adapter `nvm.js`, the UI and history services `ui.mjs` and their encodings `ui-wire.mjs`, the DOM adapter `dom.mjs`, the HTTP client `fetch.mjs`, and the bundle's start module `boot.mjs`. [Application host providers](../../docs/runtime/Providers.md) owns their contracts.

## DOM adapter

`dom.mjs` applies a decoded commit to a browser element owned by a `DomRoot`. The caller supplies operation order and identity decisions. The root keeps its managed elements, text nodes, and installed listeners between commits.

`new DomRoot(element, queueEvent)` opens a root. `queueEvent` receives an immutable event snapshot with the decoded node and handler IDs. Modifier handling occurs during native dispatch, before event delivery. Call `close()` to release the root and detach its listeners.

### Decoded operations

Use node ID 0 for the container when an operation names its parent. Insertion uses a sibling ID in `before` or `null` to append. A subtree removal releases all its descendant IDs. Each following phase consumes the node IDs that the earlier phases leave.

| Phase | Operation | Fields after `op` |
| --- | --- | --- |
| Remove | `remove` | `id` |
| Create or move | `element` | `id`, `name`, Boolean `svg`, `parent`, `before` |
| Create or move | `text` | `id`, `value`, `parent`, `before` |
| Create or move | `move` | `id`, `parent`, `before` |
| Content | `setText` | `id`, `value` |
| Content | `attribute` | `id`, `name`, string `value` or `null` to remove |
| Content | `variable` | `id`, `name`, `value` with `number` and `unit`, or `null` to remove |
| Handler | `handler` | `id`, `event`, `handler` or `null` to detach, Boolean `preventDefault` and `stopPropagation` |
| Property | `property` | `id`, `name`, string or Boolean `value` |

`apply(operations)` validates the whole batch before changing the DOM. It throws `InvalidBatch` for a bad batch and accepts a subsequent valid one. If a browser operation fails while applying a valid batch, the root throws `RootFault` and suppresses event delivery. Earlier mutations stay in place. `reset()` empties the root, detaches its listeners, and clears the fault so the caller can commit a fresh tree.

Property writes skip equal values and clamp the existing selection when a focused text input or textarea gets shorter. During composition, the adapter retains the latest requested value until `compositionend`. A listener raised while applying a commit queues its snapshot for delivery after the commit.

The provider's binary encoding, service handles, and event queue sit outside this decoded API. The [operation corpus](../../tests/apps/ui-ops/batches.json) can be read by another host independently of browser event fixtures.

### Verification

Run `python3 tests/apps/ui-ops/browser.py` using local Chromium and Firefox. The tests exercise native DOM mutation, user-facing selection state, event modifiers, listener identity, rejected batches, fault and reset behavior, and injected composition events. Physical IME behavior requires a manual platform run.

## NVM execution

Configure the `wasm-runtime` preset with `-DNEURA_WASM_HOST=ON`. The `neura_wasm` target produces `nvm.mjs` and `nvm.wasm` in the preset's build directory. Instantiate the generated module, then pass it and the NVR bytes to `new NvmExecution(module, image, { args, host, tier })` from `nvm.js`. The constructor checks the [browser adapter interface](../../docs/runtime/Providers.md#browser-adapter-interface) version, verifies the image, and binds every import before execution starts. Each instance owns its frames, heap, scheduler, and proof queue. Call `close()` when finished.

The members of `host` grant [the browser services](../../docs/runtime/Providers.md#browser-services). A service the host does not grant still binds and answers error 14. `browserHost({ output, ui, history, http })` returns the defaults of a page: console lines on `console.log`, the page's clocks, timers for sleeps, `crypto.getRandomValues` for entropy, the UI and history services when given a `UiHost` and a `HistoryHost` from `ui.mjs`, and [`neura.http.fetch`](../../docs/runtime/Providers.md#http-fetch) when given a `FetchHost` from `fetch.mjs`.

| Member | Called as | Answer |
| --- | --- | --- |
| `console` | `console(line, context)`; the line includes its LF | `false` for a failed write, or `true` or `undefined` |
| `consoleWrite` | `consoleWrite(stream, bytes, context)`; stream 1 is stdout and 2 stderr | The number of bytes written |
| `clockWall` | `clockWall(context)` | `{ seconds, nanos }` with `seconds` a signed 64-bit `BigInt` and `nanos` an integer in `[0, 1000000000)` |
| `clockMonotonic` | `clockMonotonic(context)` | A signed 64-bit `BigInt` in nanoseconds |
| `sleep` | `sleep(nanos, context)` | `undefined` when the time has passed |
| `entropy` | `entropy(count, context)` | A `Uint8Array` of `count` bytes |
| `ui` | A `UiHost`: `openRoot`, `closeRoot`, `template`, `commit`, `nextEvents`, `stats` | As [UI services](../../docs/runtime/Providers.md#ui-services) |
| `history` | A `HistoryHost`: `current`, `push`, `replace`, `next` | As [History services](../../docs/runtime/Providers.md#history-services) |

Every answer may instead be `{ error }` with a positive uint32 host error code, or a promise of either. `context.sequence` is the request's `BigInt` sequence number, from zero in each execution. A cancellable request also has `context.signal`, an `AbortSignal`. A service that throws, or whose promise rejects, closes the execution.

`run()` drives the execution until it completes or traps, as [Scheduling](../../docs/runtime/Providers.md#scheduling) describes: it answers requests, delivers overlapping answers at scheduling points, gives the task clock to timers, and waits for an answer or a timer without polling. It checks the browser's work budget after each 4,096-step slice or intervening event. It defaults to 8 ms with a configurable ceiling of 50 ms. A charged kernel or a parallel join runs atomically, so that operation can pass the next step sample. An idle callback may attempt one queued proof after the page has had an event turn, and while the run waits. Pass `hasPendingEvents` when a host has queued events of its own; a `UiHost` reports its count as `queued`. `onProof` receives the duration of an idle attempt in milliseconds. Embedders can also drive the instance through `advanceSlice()`, `serveRequest()`, `cancelRequest()`, `deliverReady()`, `advanceClock()` and `proveOneIdle()`.

## UI services

`ui.mjs` serves the UI and history groups over `DomRoot`, and `ui-wire.mjs` decodes [commits](../../docs/runtime/Providers.md#commit-encoding) and encodes [events](../../docs/runtime/Providers.md#event-encoding). Each root queues its events, coalesces `pointermove` and `scroll`, and reports overflow at the limit U-7. A commit marks `neura:first-commit` once and `neura:commit-end` after each, for the browser budgets.

## Runtime checks

Run `python3 tests/wasm/check-host.py --neura /path/to/neura --browser chromium --browser firefox` after building `neura_wasm`. The check builds the fixtures once, records native tier-0 output and counters, and compares the same images under Node and the selected local browsers.

Run `python3 tests/apps/web/check.py --neura /path/to/neura --browser chromium --browser firefox` for the services. It round-trips the operation corpus and every operation and event kind through the encodings, runs sleeping and cancelled tasks against native output, sends the corpus through `neura.ui.commit`, checks the event queue, and runs a counter image that mounts a button, receives injected clicks and commits a new label for each.

Run `python3 tests/apps/web/bundle-check.py --neura /path/to/neura` to assemble a [web bundle](../../docs/runtime/Providers.md#web-bundles) with `scripts/build-web-bundle.py` and load it from a static file host in headless Chromium: under its content-security policy, from a deep path, with `nvm.wasm` served as `application/wasm` and as `application/octet-stream`, and with a changed image, which must not start.

Run `python3 tests/wasm/bench.py --neura /path/to/neura --browser chromium` to time the bench-wall programs in `nvm.wasm` against the native runner, with function programs and with regions, and to report the longest idle proof.
