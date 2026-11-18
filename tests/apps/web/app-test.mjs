// The browser journey of app-check.py: runs app.nvr (tests/apps/web/app/app.nra) on the page's #app element
// through Std.Web.mount, drives it with DOM events, and checks the DOM, the commits and the render lines after
// each step. Posts a JSON report to the serving script.
import createNvm from './nvm.mjs';
import { NvmExecution, browserHost } from './nvm.js';
import { HistoryHost, UiHost } from './ui.mjs';
import { decodeCommit } from './ui-wire.mjs';

async function read(relative) {
    const response = await fetch(new URL(relative, import.meta.url));
    if (!response.ok) throw new Error(`${relative}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

// Records every commit's decoded operations before applying it.
class RecordingUiHost extends UiHost {
    commits = [];
    rootHandles = [];

    openRoot(selector) {
        const handle = super.openRoot(selector);
        this.rootHandles.push(handle);
        return handle;
    }

    commit(root, bytes) {
        this.commits.push(decodeCommit(bytes));
        return super.commit(root, bytes);
    }
}

function check(condition, message) {
    if (!condition) throw new Error(message);
}

const text = id => document.getElementById(id)?.textContent;
const items = () => [...document.querySelectorAll('#parts li')];
const itemTexts = () => items().map(item => item.textContent);

async function until(predicate, label, lines) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > 10000) {
            throw new Error(`timed out waiting for ${label}; DOM ${document.getElementById('app').innerHTML}; `
                            + `output ${JSON.stringify(lines)}`);
        }
        await new Promise(resolve => setTimeout(resolve, 2));
    }
}

async function journey(ui, lines, steps) {
    const app = document.getElementById('app');
    const linesSince = mark => lines.slice(mark);
    const step = (name, detail) => steps.push({ name, ...detail });

    // Mount: every component rendered once, in order, in one commit; the stylesheet applies.
    await until(() => text('count') === '0' && items().length === 3, 'the first commit', lines);
    check(JSON.stringify(lines) === JSON.stringify(
        ['render app', 'render counter 0', 'render parts [bolt, nut, gear]', "render form ''"]),
          `first renders: ${JSON.stringify(lines)}`);
    check(ui.commits.length === 1, `one first commit, saw ${ui.commits.length}`);
    const color = getComputedStyle(document.getElementById('count')).color;
    check(color === 'rgb(200, 30, 30)', `the stylesheet colors the count: ${color}`);
    check(getComputedStyle(document.getElementById('count')).fontWeight === '700', 'the count is bold');
    const title = document.getElementById('title');
    title.dataset.mark = 'kept';
    step('mount', { commits: ui.commits.length, operations: ui.commits[0].length, color });

    // A click on the child: only the counter renders, and the commit changes only the count's text.
    for (const expected of ['1', '2']) {
        const before = lines.length;
        const commitsBefore = ui.commits.length;
        document.getElementById('add').click();
        await until(() => text('count') === expected, `count ${expected}`, lines);
        check(JSON.stringify(linesSince(before)) === JSON.stringify([`render counter ${expected}`]),
              `only the counter renders: ${JSON.stringify(linesSince(before))}`);
        check(ui.commits.length === commitsBefore + 1, 'one commit per click');
        const operations = ui.commits.at(-1);
        check(operations.length === 1 && operations[0].op === 'setText' && operations[0].value === expected,
              `the commit sets one text: ${JSON.stringify(operations, (_, v) => typeof v === 'bigint' ? `${v}` : v)}`);
        check(document.getElementById('title') === title && title.dataset.mark === 'kept', 'the parent is untouched');
        step(`click ${expected}`, { operations: operations.length });
    }

    // The keyed list: add, remove, reverse; surviving items keep their elements.
    for (const item of items()) item.dataset.mark = item.textContent;
    let before = lines.length;
    document.getElementById('part-add').click();
    await until(() => items().length === 4, 'an added part', lines);
    check(JSON.stringify(itemTexts()) === JSON.stringify(['bolt', 'nut', 'gear', 'part 4']), `add: ${itemTexts()}`);
    check(items().slice(0, 3).every(item => item.dataset.mark === item.textContent), 'add keeps the old items');
    check(linesSince(before).every(line => line.startsWith('render parts')), `add renders: ${linesSince(before)}`);
    step('add', { items: itemTexts(), operations: ui.commits.at(-1).length });
    for (const item of items()) item.dataset.mark = item.textContent;
    const nut = items()[1];
    document.getElementById('part-remove').click();
    await until(() => items().length === 3, 'a removed part', lines);
    check(JSON.stringify(itemTexts()) === JSON.stringify(['nut', 'gear', 'part 4']), `remove: ${itemTexts()}`);
    check(items()[0] === nut, 'remove keeps the next item');
    step('remove', { items: itemTexts(), operations: ui.commits.at(-1).length });
    const kept = items();
    document.getElementById('part-reverse').click();
    await until(() => items()[0]?.textContent === 'part 4', 'reversed parts', lines);
    check(JSON.stringify(itemTexts()) === JSON.stringify(['part 4', 'gear', 'nut']), `reverse: ${itemTexts()}`);
    check(items().every((item, at) => item === kept[2 - at]), 'reverse moves the same elements');
    const moves = ui.commits.at(-1).filter(operation => operation.op === 'move').length;
    const creations = ui.commits.at(-1).filter(operation => operation.op === 'element').length;
    check(moves > 0 && creations === 0, `reverse moves and creates nothing: ${moves} moves, ${creations} creations`);
    check(document.getElementById('title') === title, 'the parent is untouched by the list');
    step('reverse', { items: itemTexts(), moves });

    // Two-way form state: typing updates the state and the echo; Clear writes the state back to the input.
    const input = document.getElementById('name');
    input.focus();
    input.value = 'washer';
    input.setSelectionRange(3, 3);
    before = lines.length;
    input.dispatchEvent(new Event('input', { bubbles: true }));
    await until(() => text('echo') === 'washer', 'the echo of typed text', lines);
    check(JSON.stringify(linesSince(before)) === JSON.stringify(["render form 'washer'"]),
          `typing renders the form only: ${linesSince(before)}`);
    check(document.getElementById('name') === input && input.value === 'washer', 'the input keeps its value');
    check(document.activeElement === input && input.selectionStart === 3, 'the input keeps focus and selection');
    step('type', { echo: text('echo') });
    document.getElementById('add').click();
    await until(() => text('count') === '3', 'count 3 while a draft is typed', lines);
    check(input.value === 'washer', 'a commit of another component leaves the draft alone');
    document.getElementById('clear').click();
    await until(() => text('echo') === '' && input.value === '', 'a cleared input', lines);
    check(document.getElementById('name') === input, 'clearing keeps the input element');
    step('clear', { value: input.value });

    // Unmount: the root closes, the element is empty and every listener is gone.
    const handle = ui.rootHandles[0];
    const stats = ui.stats(handle);
    check(stats[1] > 0n, 'listeners are installed before closing');
    document.getElementById('close').click();
    return { app, handle, statsBeforeClose: stats.map(Number) };
}

async function main() {
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const lines = [];
    const ui = new RecordingUiHost();
    const history = new HistoryHost();
    const steps = [];
    const execution = new NvmExecution(module, await read('app.nvr'), {
        host: { ...browserHost({ ui, history }), console: line => void lines.push(line.replace(/\n$/, '')) },
    });
    try {
        const running = execution.run().then(outcome => ({ outcome }));
        const driven = journey(ui, lines, steps).then(() => ({ driven: true }), error => ({ error }));
        const first = await Promise.race([running, driven]);
        if (first.error) throw first.error;
        if (first.outcome) {
            throw new Error(`the image ended before the journey: ${first.outcome.kind} ${first.outcome.status}; `
                            + `${JSON.stringify(lines)}`);
        }
        const { outcome } = await running;
        check(outcome.kind === 'completed' && outcome.status === 0,
              `the image completes: ${outcome.kind} ${outcome.status}; ${JSON.stringify(lines)}`);
        const app = document.getElementById('app');
        check(app.childNodes.length === 0, 'a closed root is empty');
        check(JSON.stringify(ui.stats(ui.rootHandles[0])) === JSON.stringify({ error: 4 }), 'the handle is closed');
        check(lines.some(line => line.startsWith('before close: nodes ')), 'the app read the host stats');
        check(lines.at(-1).startsWith('closed: components 0, subscriptions 0'), `after close: ${lines.at(-1)}`);
        steps.push({ name: 'close', lastLine: lines.at(-1) });
        return { steps, commits: ui.commits.length, lines };
    } finally {
        history.close();
        execution.close();
    }
}

const report = await main().catch(error => ({ error: error.stack || String(error) }));
await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
