// The browser journey of tests/apps/web/tasks/tasks.nra, run by app-check.py: while a command waits, clicks render
// and the browser's back and forward buttons reach the router at once (Std.Web.Host as a ui.TaskHost); then the
// command completes, a second run is cancelled, and back and forward work with no command in flight.
import createNvm from './nvm.mjs';
import { NvmExecution, browserHost } from './nvm.js';
import { HistoryHost, UiHost } from './ui.mjs';

async function read(relative) {
    const response = await fetch(new URL(relative, import.meta.url));
    if (!response.ok) throw new Error(`${relative}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

function check(condition, message) {
    if (!condition) throw new Error(message);
}

const one = selector => document.querySelector(selector);
const text = selector => one(selector)?.textContent;

async function until(predicate, label, lines, limit = 15000) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > limit) {
            throw new Error(`timed out waiting for ${label}; DOM ${one('#app').innerHTML}; output ${JSON.stringify(lines)}`);
        }
        await new Promise(resolve => setTimeout(resolve, 2));
    }
    return performance.now() - started;
}

async function journey(lines, steps) {
    await until(() => text('#status') === 'Not loaded' && text('main h1') === 'Home', 'the first render', lines);
    steps.push({ name: 'mount' });

    one('#load').click();
    await until(() => text('#status') === 'Loading', 'the running command', lines);
    const started = performance.now();
    for (let i = 1; i <= 3; i++) {
        one('#add').click();
        const waited = await until(() => text('#count') === String(i), `click ${i} while loading`, lines);
        check(text('#status') === 'Loading', `click ${i} waited for the command: ${text('#status')}`);
        steps.push({ name: `click ${i} while loading`, ms: Math.round(waited) });
    }

    one('#other').click();
    await until(() => text('main h1') === 'Other', 'the link while loading', lines);
    check(location.hash === '#/other', `the address follows the router: ${location.hash}`);
    history.back();
    let waited = await until(() => text('main h1') === 'Home', 'back while loading', lines);
    check(text('#status') === 'Loading', `back waited for the command: ${text('#status')}`);
    steps.push({ name: 'back while loading', ms: Math.round(waited) });
    history.forward();
    waited = await until(() => text('main h1') === 'Other', 'forward while loading', lines);
    check(text('#status') === 'Loading', `forward waited for the command: ${text('#status')}`);
    steps.push({ name: 'forward while loading', ms: Math.round(waited) });

    await until(() => text('#status') === 'Loaded 42', 'the completion', lines);
    const elapsed = performance.now() - started;
    check(elapsed > 2000, `the load ended early: ${Math.round(elapsed)} ms`);
    const loading = lines.indexOf('status Loading');
    const loaded = lines.indexOf('status Loaded 42');
    check(loading >= 0 && loaded > loading, `status renders out of order: ${JSON.stringify(lines)}`);
    check(lines.slice(loading, loaded).includes('page Home'), `back did not render during the load: ${JSON.stringify(lines)}`);
    steps.push({ name: 'completed', ms: Math.round(elapsed) });

    one('#load').click();
    await until(() => text('#status') === 'Loading', 'the second run', lines);
    one('#cancel').click();
    waited = await until(() => text('#status') === 'Cancelled', 'the cancellation', lines);
    check(waited < 2000, `the cancellation waited for the work: ${Math.round(waited)} ms`);
    steps.push({ name: 'cancelled', ms: Math.round(waited) });

    history.back();
    await until(() => text('main h1') === 'Home', 'back with no command', lines);
    history.forward();
    await until(() => text('main h1') === 'Other', 'forward with no command', lines);
    one('#add').click();
    await until(() => text('#count') === '4', 'a click after the commands', lines);
    // Neither a completion of the cancelled run nor anything else follows.
    await new Promise(resolve => setTimeout(resolve, 300));
    check(text('#status') === 'Cancelled', `the cancelled run completed: ${text('#status')}`);
    steps.push({ name: 'back and forward idle' });
}

async function main() {
    history.replaceState(null, '', '#/');
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const lines = [];
    const ui = new UiHost();
    const routes = new HistoryHost();
    const steps = [];
    const execution = new NvmExecution(module, await read('app.nvr'), {
        host: { ...browserHost({ ui, history: routes }), console: line => void lines.push(line.replace(/\n$/, '')) },
    });
    try {
        const running = execution.run({ hasPendingEvents: () => ui.queued > 0 }).then(outcome => ({ outcome }));
        const driven = journey(lines, steps).then(() => ({ driven: true }), error => ({ error }));
        const first = await Promise.race([running, driven]);
        if (first.error) throw first.error;
        if (first.outcome) {
            throw new Error(`the image ended: ${first.outcome.kind} ${first.outcome.status} `
                            + `${first.outcome.diagnostic ?? ''}; ${JSON.stringify(lines)}`);
        }
        return { steps, lines };
    } finally {
        routes.close();
        execution.close();
    }
}

const report = await main().catch(error => ({ error: error.stack || String(error) }));
await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
