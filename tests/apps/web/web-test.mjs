// Checks of the browser runtime's host services, run by check.py under Node and in headless browsers.
import createNvm from './nvm.mjs';
import { NvmExecution, browserHost } from './nvm.js';
import { HistoryHost, UiHost } from './ui.mjs';
import { WireError, decodeCommit, decodeEvents, encodeCommit, encodeEvents } from './ui-wire.mjs';

const browser = typeof document !== 'undefined';

// Compares values by content: object keys in sorted order, BigInts by value.
const stable = value => JSON.stringify(value, (_, v) => {
    if (typeof v === 'bigint') return `${v}n`;
    if (v && typeof v === 'object' && !Array.isArray(v)) {
        return Object.fromEntries(Object.keys(v).sort().map(key => [key, v[key]]));
    }
    return v;
});

function equal(actual, expected, label) {
    const a = stable(actual);
    const e = stable(expected);
    if (a !== e) throw new Error(`${label}: expected ${e}, received ${a}`);
}

function throws(call, type, label) {
    try {
        call();
    } catch (error) {
        if (error instanceof type) return;
        throw error;
    }
    throw new Error(`${label}: expected ${type.name}`);
}

async function read(relative) {
    const url = new URL(relative, import.meta.url);
    if (url.protocol === 'file:') {
        const { readFile } = await import('node:fs/promises');
        return new Uint8Array(await readFile(url));
    }
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${relative}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

const json = async relative => JSON.parse(new TextDecoder().decode(await read(relative)));

// Operations that the corpus lacks: handlers, properties, custom properties and removals of attributes.
const extraOperations = [
    { op: 'element', id: 1, parent: 0, before: null, name: 'input', svg: false },
    { op: 'element', id: 2, parent: 0, before: 1, name: 'details', svg: false },
    { op: 'attribute', id: 1, name: 'aria-label', value: null },
    { op: 'variable', id: 2, name: '--gap', value: { number: 1.5, unit: 'rem' } },
    { op: 'variable', id: 2, name: '--tone', value: null },
    { op: 'handler', id: 1, event: 'input', handler: 18446744073709551615n, preventDefault: false, stopPropagation: true },
    { op: 'handler', id: 2, event: 'toggle', handler: null, preventDefault: false, stopPropagation: false },
    { op: 'property', id: 1, name: 'value', value: 'é ✓' },
    { op: 'property', id: 2, name: 'open', value: true },
];

async function wire() {
    const corpus = await json('batches.json');
    let commits = 0;
    for (const sample of corpus) {
        for (const { operations } of sample.commits) {
            equal(decodeCommit(encodeCommit(operations)), operations, sample.name);
            commits += 1;
        }
    }
    equal(decodeCommit(encodeCommit(extraOperations)), extraOperations, 'extra operations');
    const bytes = encodeCommit(extraOperations);
    throws(() => decodeCommit(bytes.subarray(0, bytes.length - 1)), WireError, 'truncated commit');
    throws(() => decodeCommit(Uint8Array.of(...bytes, 0)), WireError, 'trailing byte');
    throws(() => decodeCommit(Uint8Array.of(2, 0, 0, 0, 0)), WireError, 'unknown encoding');
    throws(() => decodeCommit(Uint8Array.of(1, 1, 0, 0, 0, 10, 1, 0, 0, 0)), WireError, 'unknown operation');
    throws(() => decodeCommit(Uint8Array.of(1, 1, 0, 0, 0, 5, 1, 0, 0, 0, 1, 0, 0, 0, 0xff)), WireError, 'bad UTF-8');
    throws(() => decodeCommit(Uint8Array.of(1, 0xff, 0xff, 0xff, 0xff)), WireError, 'oversized count');
    const modifiers = { alt: true, ctrl: false, meta: true, shift: false };
    const events = [
        { event: 'click', node: 4, handler: 9n, fields: { button: 0, x: 1.5, y: -2, modifiers, detail: 2 } },
        { event: 'input', node: 5, handler: 1n, fields: { value: 'héllo', checked: false, composing: true } },
        { event: 'change', node: 5, handler: 2n, fields: { value: '', checked: true } },
        { event: 'submit', node: 6, handler: 3n, fields: { submitter: 'save' } },
        { event: 'keydown', node: 7, handler: 4n, fields: { key: 'Enter', code: 'Enter', modifiers, repeat: true } },
        { event: 'focus', node: 7, handler: 5n, fields: {} },
        { event: 'pointermove', node: 8, handler: 6n, fields: { x: 3, y: 4, button: 255, kind: 'pen', modifiers } },
        { event: 'scroll', node: 9, handler: 7n, fields: { top: 120, left: 0 } },
        { event: 'toggle', node: 10, handler: 8n, fields: { open: true } },
        { event: 'close', node: 11, handler: 9n, fields: { return_value: 'ok' } },
        { event: 'overflow', node: 0, handler: 0n, fields: { dropped: 3 } },
    ];
    const encoded = encodeEvents(events.map(e => (e.event === 'overflow' ? { ...e, dropped: 3 } : e)));
    equal(decodeEvents(encoded), events, 'event round trip');
    throws(() => decodeEvents(encoded.subarray(0, encoded.length - 2)), WireError, 'truncated events');
    // The vectors of docs/runtime/Providers.md, "Vectors".
    const hex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
    equal(hex(encodeCommit([
        { op: 'element', id: 1, parent: 0, before: null, svg: false, name: 'p' },
        { op: 'text', id: 2, parent: 1, before: null, value: 'Hi' },
        { op: 'handler', id: 1, event: 'click', handler: 7n, preventDefault: true, stopPropagation: false },
    ])), '01030000000201000000000000000000000000010000007003020000000100000000000000020000004869080100000005000000' +
        '636c69636b070000000000000001', 'commit vector');
    equal(hex(encodeEvents([{ event: 'click', node: 1, handler: 7n, fields: { button: 0, x: 2.5, y: 4,
        modifiers: { alt: false, ctrl: true, meta: false, shift: true }, detail: 1 } }])),
        '01010000000101000000070000000000000000000000000000044000000000000010400a01000000', 'click vector');
    equal(hex(encodeEvents([{ event: 'overflow', node: 0, handler: 0n, dropped: 3 }])),
        '01010000001000000000000000000000000003000000', 'overflow vector');
    return { commits };
}

// Counts slices, so that a wait that spun would show.
class CountingExecution extends NvmExecution {
    slices = 0;

    advanceSlice(budget) {
        this.slices += 1;
        return super.advanceSlice(budget);
    }
}

async function runImage(module, image, host, label) {
    const lines = [];
    const execution = new CountingExecution(module, image, {
        host: { ...browserHost(host), console: line => void lines.push(line) },
    });
    const started = performance.now();
    try {
        const event = await execution.run();
        return { kind: event.kind, status: event.status, stdout: lines.join(''), slices: execution.slices,
                 milliseconds: performance.now() - started, label };
    } finally {
        execution.close();
    }
}

async function waits(module, oracles) {
    const results = [];
    for (const name of ['timers', 'serialSleep']) {
        const outcome = await runImage(module, await read(`${name}.nvr`), {}, name);
        equal([outcome.kind, outcome.status, outcome.stdout], [oracles[name].kind, oracles[name].status,
              oracles[name].stdout], name);
        // About ten scheduling points and a dozen requests; a polling wait would take thousands of slices.
        if (outcome.slices > 200) throw new Error(`${name}: ${outcome.slices} slices for a waiting program`);
        results.push(outcome);
    }
    return results;
}

function canonical(node) {
    if (node.nodeType === Node.TEXT_NODE) return node.data;
    const result = { tag: node.localName };
    if (node.namespaceURI === 'http://www.w3.org/2000/svg') result.svg = true;
    if (node.attributes.length) {
        const names = node.getAttributeNames().sort();
        result.attrs = Object.fromEntries(names.map(name => [name, node.getAttribute(name)]));
    }
    if (node.childNodes.length) result.children = Array.from(node.childNodes, canonical);
    return result;
}

// The operation corpus, sent through the binary commit service.
async function corpus() {
    const cases = await json('batches.json');
    let commits = 0;
    for (const sample of cases) {
        const element = document.createElement('div');
        element.id = 'corpus';
        document.body.append(element);
        const ui = new UiHost();
        const root = ui.openRoot('#corpus');
        try {
            for (const { operations, tree } of sample.commits) {
                equal(ui.commit(root, encodeCommit(operations)), undefined, sample.name);
                equal(Array.from(element.childNodes, canonical), tree, sample.name);
                commits += 1;
            }
            equal(ui.commit(root, Uint8Array.of(1, 1, 0, 0, 0, 1, 99, 0, 0, 0)), { error: 4 }, 'unknown node');
            equal(ui.commit(root, Uint8Array.of(7)), { error: 4 }, 'unknown encoding');
            equal(ui.openRoot('#corpus'), { error: 3 }, 'second root on one element');
            equal(ui.openRoot('#missing'), { error: 1 }, 'missing element');
            equal(ui.openRoot('['), { error: 4 }, 'invalid selector');
        } finally {
            ui.closeRoot(root);
            element.remove();
        }
    }
    return commits;
}

// Events: arrival order, coalescing in place, overflow at U-7 with the latest input kept, cancellation.
async function queue() {
    const element = document.createElement('div');
    element.id = 'queue';
    document.body.append(element);
    const ui = new UiHost({ limits: { queuedEvents: 3 } });
    const root = ui.openRoot('#queue');
    try {
        ui.commit(root, encodeCommit([
            { op: 'element', id: 1, parent: 0, before: null, name: 'input', svg: false },
            { op: 'element', id: 2, parent: 0, before: null, name: 'div', svg: false },
            { op: 'handler', id: 1, event: 'input', handler: 1n, preventDefault: false, stopPropagation: false },
            { op: 'handler', id: 2, event: 'scroll', handler: 2n, preventDefault: false, stopPropagation: false },
            { op: 'handler', id: 2, event: 'click', handler: 3n, preventDefault: false, stopPropagation: false },
        ]));
        const [input, box] = element.children;
        box.dispatchEvent(new Event('scroll'));
        input.value = 'a';
        input.dispatchEvent(new Event('input'));
        box.dispatchEvent(new Event('scroll'));
        equal(ui.stats(root)[3], 2n, 'a scroll coalesces in place');
        box.click();
        input.value = 'ab';
        input.dispatchEvent(new Event('input'));
        box.click();
        equal(element.hasAttribute('data-neura-overloaded'), true, 'overloaded root is marked');
        const first = decodeEvents(ui.nextEvents(root, 16));
        equal(first.map(e => e.event), ['scroll', 'input', 'click', 'overflow', 'input'], 'queue order');
        equal(first[3].fields.dropped, 2, 'dropped count');
        equal(first[4].fields.value, 'ab', 'the latest dropped input is delivered');
        equal(element.hasAttribute('data-neura-overloaded'), false, 'overflow delivered');
        const controller = new AbortController();
        const waiting = ui.nextEvents(root, 1, { signal: controller.signal });
        controller.abort();
        equal(await waiting, { error: 15 }, 'a cancelled wait');
        const later = ui.nextEvents(root, 1);
        box.click();
        equal(decodeEvents(await later).map(e => e.event), ['click'], 'a waiting request takes the next event');
        const closing = ui.nextEvents(root, 1);
        ui.closeRoot(root);
        equal(decodeEvents(await closing), [], 'closing answers an empty batch');
        equal(ui.nextEvents(root, 1), { error: 4 }, 'a closed handle');
    } finally {
        element.remove();
    }
}

// The counter image: mounted by the NVM, clicked by injected events, re-rendered after each.
async function counter(module, oracles) {
    const element = document.createElement('div');
    element.id = 'app';
    document.body.append(element);
    history.replaceState(null, '', '#/');
    const ui = new UiHost();
    const routes = new HistoryHost();
    const clicker = (async () => {
        for (let wanted = 1; wanted <= 3; ++wanted) {
            for (;;) {
                const button = document.getElementById('counter');
                if (button && button.textContent === `Clicked ${wanted - 1}`) {
                    button.click();
                    break;
                }
                await new Promise(resolve => setTimeout(resolve, 5));
            }
        }
    })();
    try {
        const outcome = await runImage(module, await read('counter.nvr'), { ui, history: routes }, 'counter');
        await clicker;
        equal([outcome.kind, outcome.status, outcome.stdout], ['completed', 0, oracles.counter], 'counter');
        equal(location.hash, '#/parts/7', 'pushed path');
        equal(element.childNodes.length, 0, 'a closed root is empty');
        return outcome;
    } finally {
        routes.close();
        element.remove();
    }
}

async function main() {
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const oracles = await json('oracles.json');
    const report = { wire: await wire(), waits: await waits(module, oracles) };
    if (browser) {
        report.corpus = await corpus();
        await queue();
        report.counter = await counter(module, oracles);
    }
    return report;
}

if (!browser) {
    console.log(JSON.stringify(await main()));
} else {
    const report = await main().catch(error => ({ error: error.stack || String(error) }));
    await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
}
