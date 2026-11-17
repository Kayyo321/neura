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
