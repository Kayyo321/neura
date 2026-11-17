# Browser DOM adapter

`dom.mjs` applies a decoded commit to a browser element owned by a `DomRoot`. The caller supplies operation order and identity decisions. The root keeps its managed elements, text nodes, and installed listeners between commits.

`new DomRoot(element, queueEvent)` opens a root. `queueEvent` receives an immutable event snapshot with the decoded node and handler IDs. Modifier handling occurs during native dispatch, before event delivery. Call `close()` to release the root and detach its listeners.

## Decoded operations

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

## Verification

Run `python3 tests/apps/ui-ops/browser.py` using local Chromium and Firefox. The tests exercise native DOM mutation, user-facing selection state, event modifiers, listener identity, rejected batches, fault and reset behavior, and injected composition events. Physical IME behavior requires a manual platform run.

## NVM execution

Configure the `wasm-runtime` preset with `-DNEURA_WASM_HOST=ON`. The `neura_wasm` target produces `nvm.mjs` and `nvm.wasm` in the preset's build directory. Instantiate the generated module, then pass it and the NVR bytes to `new NvmExecution(module, image, { args, host, tier })` from `nvm.js`. The constructor verifies the image and binds the exact service versions and signatures before execution starts. Each instance owns its frames, heap, scheduler, and proof queue. Call `close()` when finished.

The controlled host supplies synchronous callbacks. The sequence argument is a `BigInt` starting at zero in each execution. Clock readings enter the run only through the supplied callbacks.

| Callback | Reply |
| --- | --- |
| `console(line, sequence)` | The supplied line includes its LF. Return `false` for a failed write, or `true` or `undefined` for success. |
| `clockWall(sequence)` | `{ seconds, nanos }` with `seconds` a signed 64-bit `BigInt` and `nanos` an integer in `[0, 1000000000)`, or `{ error }` with a positive uint32. |
| `clockMonotonic(sequence)` | A signed 64-bit `BigInt` in nanoseconds, or `{ error }` with a positive uint32. |

`run()` serves those requests and checks the browser's work budget after each 4,096-step slice or intervening event. It defaults to 8 ms with a configurable ceiling of 50 ms. A charged kernel or a parallel join runs atomically, so that operation can pass the next step sample. An idle callback may attempt one queued proof after the page has had an event turn. Pass `hasPendingEvents` when a host has queued events of its own. `onProof` receives the duration of an idle attempt in milliseconds. Applications can also drive the instance through `advanceSlice()`, `serveRequest()`, and `proveOneIdle()`, with the caller responsible for running proofs only while idle. `run()` returns a wait to its caller so the embedder can supply a task clock reading with `advanceClock()` before resuming. Browser timer and UI providers are later integration steps.

Run `python3 tests/wasm/check-host.py --neura /path/to/neura --browser chromium --browser firefox` after building `neura_wasm`. The check builds the fixtures once, records native tier-0 output and counters, and compares the same images under Node and the selected local browsers.
