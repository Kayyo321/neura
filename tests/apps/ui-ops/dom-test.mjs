import { DomRoot, InvalidBatch, RootFault } from "/dom.mjs";

function equal(actual, expected, label) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(label + "\nexpected " + JSON.stringify(expected) + "\nreceived " + JSON.stringify(actual));
    }
}

function assert(condition, label) {
    if (!condition) throw new Error(label);
}

function throws(callback, type, label) {
    try {
        callback();
    } catch (error) {
        if (!(error instanceof type)) throw new Error(label, { cause: error });
        return;
    }
    throw new Error(label + ": expected rejection");
}

function fixture(callback, onEvent = () => {}) {
    const parent = document.createElement("div");
    const root = document.createElement("main");
    parent.append(root);
    document.body.append(parent);
    const adapter = new DomRoot(root, onEvent);
    try {
        return callback(root, adapter, parent);
    } finally {
        adapter.close();
        parent.remove();
    }
}

function element(id, name, parent = 0, before = null) {
    return { op: "element", id, name, svg: false, parent, before };
}

function text(id, value, parent = 0) {
    return { op: "text", id, value, parent, before: null };
}

function attr(id, name, value) {
    return { op: "attribute", id, name, value };
}

function property(id, name, value) {
    return { op: "property", id, name, value };
}

function handler(id, event, target, preventDefault = false, stopPropagation = false) {
    return { op: "handler", id, event, handler: target, preventDefault, stopPropagation };
}

function canonical(node) {
    if (node.nodeType === Node.TEXT_NODE) return node.data;
    const result = { tag: node.localName };
    if (node.namespaceURI === "http://www.w3.org/2000/svg") result.svg = true;
    if (node.attributes.length) {
        const names = node.getAttributeNames().sort();
        result.attrs = Object.fromEntries(names.map(name => [name, node.getAttribute(name)]));
    }
    if (node.childNodes.length) result.children = Array.from(node.childNodes, canonical);
    return result;
}

async function batches() {
    const cases = await fetch("/batches.json").then(response => response.json());
    for (const sample of cases) {
        fixture((root, adapter) => {
            for (const { operations, tree } of sample.commits) {
                adapter.apply(operations);
                equal(Array.from(root.childNodes, canonical), tree, sample.name);
            }
        });
    }
    fixture((root, adapter) => {
        const [first, second] = cases[0].commits;
        adapter.apply(first.operations);
        const original = root.firstElementChild.children[1];
        const moved = root.firstElementChild.children[2];
        const originalText = original.firstChild;
        adapter.apply(second.operations);
        assert(moved === root.firstElementChild.firstChild, "a move must keep the original element");
        assert(original === root.firstElementChild.lastChild, "reordering siblings preserves existing elements");
        assert(originalText === original.firstChild, "a text update must keep the original text node");
    });
}

function validation() {
    fixture((root, adapter, parent) => {
        adapter.apply([element(1, "div"), element(2, "span", 1), text(3, "Unchanged", 2)]);
        const observer = new MutationObserver(() => {});
        observer.observe(root, { subtree: true, attributes: true, childList: true, characterData: true });
        const inputs = [
            [{ op: "setText", id: 3, value: "Changed" }, property(2, "value", "wrong element")],
            [{ op: "move", id: 1, parent: 2, before: null }],
            [element(4, "script")],
            [attr(2, "onclick", "alert(1)")],
            [attr(2, "href", "ja\nvascript:alert(1)")],
            [attr(2, "title", "🙂".repeat(262145))],
            [{ op: "variable", id: 2, name: "--color", value: { number: 1, unit: "url(x)" } }],
            [{ op: "setText", id: 3, value: "Changed" }, element(4, "div")],
            [{ op: "move", id: 2, parent: 0, before: 3 }],
            [{ op: "remove", id: 1 }, { op: "remove", id: 2 }],
        ];
        for (const input of inputs) {
            throws(() => adapter.apply(input), InvalidBatch, "reject an invalid batch");
            equal(root.textContent, "Unchanged", "rejected batch keeps the old text");
            equal(observer.takeRecords().length, 0, "rejected batch did not mutate the DOM");
        }
        throws(() => new DomRoot(root.firstElementChild, () => {}), RootFault, "reject a nested root");
        throws(() => new DomRoot(parent, () => {}), RootFault, "reject an enclosing root");
        adapter.apply([{ op: "setText", id: 3, value: "Still usable" }]);
        equal(root.textContent, "Still usable", "invalid batch leaves the root live");
        observer.disconnect();

        const moved = root.querySelector("span");
        parent.append(moved);
        throws(() => adapter.apply([attr(2, "title", "outside")]), InvalidBatch, "reject a node moved outside the root");
        assert(!moved.hasAttribute("title"), "ownership failure does not mutate external DOM");
    });
}

function handlers() {
    const events = [];
    fixture((root, adapter, parent) => {
        adapter.apply([element(1, "button")]);
        const button = root.firstElementChild;
        const add = button.addEventListener;
        const remove = button.removeEventListener;
        let additions = 0;
        let removals = 0;
        let bubbled = 0;
        button.addEventListener = function (...args) {
            additions += 1;
            return add.apply(this, args);
        };
        button.removeEventListener = function (...args) {
            removals += 1;
            return remove.apply(this, args);
        };
        parent.addEventListener("click", () => bubbled += 1);
        adapter.apply([handler(1, "click", 7, true, true)]);
        adapter.apply([handler(1, "click", 8, true, true)]);
        equal([additions, removals], [1, 0], "new handler reuses the listener");
        const event = new MouseEvent("click", { bubbles: true, cancelable: true, detail: 2, shiftKey: true });
        button.dispatchEvent(event);
        assert(event.defaultPrevented, "prevent_default is applied before delivery");
        equal(bubbled, 0, "stop_propagation keeps the event from ancestors");
        equal(events.length, 1, "one click is delivered");
        equal([events[0].node, events[0].handler, events[0].event, events[0].fields.detail,
               events[0].fields.modifiers.shift], [1, 8, "click", 2, true], "listener uses the new handler");
        assert(Object.isFrozen(events[0].fields) && Object.isFrozen(events[0].fields.modifiers),
               "event records contain immutable snapshots");
        adapter.apply([handler(1, "click", null)]);
        button.dispatchEvent(new MouseEvent("click", { bubbles: true }));
        equal([events.length, bubbled, removals], [1, 1, 1], "removing a handler removes its behavior");
    }, event => events.push(event));
}

function controls() {
    fixture((root, adapter) => {
        adapter.apply([
            element(1, "div"), element(2, "input", 1), element(3, "input", 1),
            property(2, "value", "one two"),
        ]);
        const parent = root.firstElementChild;
        const input = parent.firstElementChild;
        const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        let writes = 0;
        Object.defineProperty(input, "value", {
            get: () => descriptor.get.call(input),
            set: value => {
                writes += 1;
                descriptor.set.call(input, value);
            },
        });
        input.focus();
        input.setSelectionRange(3, 7, "backward");
        adapter.apply([property(2, "value", "one two")]);
        equal(writes, 0, "equal property write is skipped");
        adapter.apply([property(2, "value", "four")]);
        equal([input.selectionStart, input.selectionEnd, input.selectionDirection], [3, 4, "backward"],
              "a shorter value clamps the focused selection");
        adapter.apply([{ op: "move", id: 2, parent: 1, before: null }]);
        assert(document.activeElement === input && parent.lastChild === input, "moving a focused input keeps its identity and focus");
        equal([input.selectionStart, input.selectionEnd], [3, 4], "moving the input preserves its selection");
        Object.defineProperty(parent, "moveBefore", { value: undefined });
        adapter.apply([{ op: "move", id: 2, parent: 1, before: 3 }]);
        assert(document.activeElement === input && parent.firstChild === input, "fallback retains focus and node identity");
        equal([input.selectionStart, input.selectionEnd], [3, 4], "fallback restores the selection");

        input.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
        descriptor.set.call(input, "draft");
        adapter.apply([property(2, "value", "old")]);
        adapter.apply([property(2, "value", "new")]);
        equal(input.value, "draft", "an injected composition defers property writes");
        input.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
        equal(input.value, "new", "composition ends with the latest requested value");

        input.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
        descriptor.set.call(input, "local");
        adapter.apply([property(2, "value", "discard")]);
        adapter.apply([property(2, "value", "local")]);
        const before = writes;
        input.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
        equal([input.value, writes], ["local", before], "the latest equal value supersedes a queued older write");

        adapter.apply([{ op: "variable", id: 2, name: "--gap", value: { number: 2, unit: "rem" } }]);
        equal(input.style.getPropertyValue("--gap"), "2rem", "typed numeric custom property");
        equal(getComputedStyle(input).getPropertyValue("--gap").trim(), "2rem", "custom property applies under CSP");
        adapter.apply([{ op: "variable", id: 2, name: "--gap", value: null }]);
        equal(input.style.getPropertyValue("--gap"), "", "remove a custom property");
    });
}

function hostFailure() {
    const events = [];
    fixture((root, adapter) => {
        adapter.apply([
            element(1, "p"), text(2, "Before", 1), element(3, "input"),
            attr(3, "type", "file"), handler(3, "input", 7),
        ]);
        const old = root.lastElementChild;
        throws(() => adapter.apply([{ op: "setText", id: 2, value: "After" }, property(3, "value", "file")]),
               RootFault, "a real property setter failure faults the root");
        equal(root.textContent, "After", "host failure does not roll back earlier mutations");
        throws(() => adapter.apply([]), RootFault, "a faulted root rejects commits");
        old.dispatchEvent(new InputEvent("input", { bubbles: true }));
        equal(events.length, 0, "fault suppresses old callbacks");
        adapter.reset();
        equal(root.childNodes.length, 0, "reset empties the root");
        adapter.apply([element(1, "button"), handler(1, "click", 9)]);
        root.firstElementChild.dispatchEvent(new MouseEvent("click", { bubbles: true }));
        equal(events[0].handler, 9, "a fresh commit after reset can deliver events");
        old.dispatchEvent(new InputEvent("input", { bubbles: true }));
        equal(events.length, 1, "reset detaches old listeners");
    }, event => events.push(event));
}

async function postCommit() {
    const parent = document.createElement("div");
    document.body.append(parent);
    const events = [];
    const adapter = new DomRoot(parent, event => events.push([event.fields.value, parent.firstChild.data]));
    try {
        adapter.apply([text(1, "Before"), element(2, "input"), handler(2, "input", 5)]);
        const input = parent.lastElementChild;
        const value = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(input, "value", {
            get: () => value.get.call(input),
            set: text => {
                value.set.call(input, text);
                input.dispatchEvent(new InputEvent("input", { bubbles: true }));
            },
        });
        adapter.apply([{ op: "setText", id: 1, value: "After" }, property(2, "value", "input")]);
        equal(events, [], "callbacks raised during DOM mutations are queued");
        await Promise.resolve();
        equal(events, [["input", "After"]], "callback runs after the whole batch");
        adapter.apply([property(2, "value", "unused after reset")]);
        adapter.reset();
        await Promise.resolve();
        equal(events.length, 1, "reset invalidates pending callbacks");
    } finally {
        adapter.close();
        parent.remove();
    }
}

const tests = [batches, validation, handlers, controls, hostFailure, postCommit];
const token = new URL(location.href).searchParams.get("token");
let report;
try {
    for (const test of tests) await test();
    report = { passed: tests.map(test => test.name), preservingMove: typeof Element.prototype.moveBefore === "function" };
} catch (error) {
    report = { error: error.stack + (error.cause ? "\n" + error.cause.stack : "") };
}
await fetch("/result?token=" + encodeURIComponent(token), {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(report),
});
