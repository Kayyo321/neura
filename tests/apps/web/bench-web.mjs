// Budgets B-W5, B-W6 and B-W7 (docs/design/Apps030.md, "Budgets") in a headless browser, run by bench-web.py: the
// image (tests/apps/web/bench/rows.nra or list.nra) mounts a 1,000-row keyed list; 200 clicks each change one row
// (B-W5, from the click's dispatch to the next `neura:commit-end` mark), 50 clicks on Reverse reverse the rows when
// the page has the button (B-W6, the same span), and the browser's long-task entries after the first commit give
// B-W7. The image tests/apps/web/bench/growth.nra (a page with #close) is the browser half of B-W9 instead: mount
// and unmount cycles of ten clicks each, with neura.ui.stats, the open roots, the state's subscribers and the
// runtime's memory read after cycle 100 and after the last. Posts the samples and the page's conditions.
import createNvm from './nvm.mjs';
import { NvmExecution, browserHost } from './nvm.js';
import { HistoryHost, UiHost } from './ui.mjs';

async function read(relative) {
    const response = await fetch(new URL(relative, import.meta.url));
    if (!response.ok) throw new Error(`${relative}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

const token = location.search;
const commits = () => performance.getEntriesByName('neura:commit-end', 'mark');

async function until(predicate, label, seconds = 60) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > seconds * 1000) throw new Error(`timed out waiting for ${label}`);
        await new Promise(resolve => setTimeout(resolve, 1));
    }
}

// Dispatches a click on `element` and answers the time from the dispatch to the commit it caused.
async function timedClick(element) {
    await new Promise(resolve => setTimeout(resolve, 5));
    const seen = commits().length;
    const start = performance.now();
    element.click();
    await until(() => commits().length > seen, 'the commit of a click');
    return commits()[seen].startTime - start;
}

function summary(samples) {
    const sorted = [...samples].sort((a, b) => a - b);
    const at = q => sorted[Math.min(sorted.length - 1, Math.ceil(q * sorted.length) - 1)];
    return { count: sorted.length, median: at(0.5), p95: at(0.95), max: sorted[sorted.length - 1] };
}

async function measure() {
    const buttons = () => document.querySelectorAll('#app li button, #app ul button');
    await until(() => buttons().length === 1000, 'the first commit', 120);
    const longTasks = [];
    const observer = new PerformanceObserver(list => {
        for (const entry of list.getEntries()) longTasks.push(entry.duration);
    });
    // A browser without long-task entries (Firefox) ignores the type instead of refusing it.
    const longTaskSupported = PerformanceObserver.supportedEntryTypes?.includes('longtask') ?? false;
    let controlSeen = null;
    if (longTaskSupported) {
        observer.observe({ type: 'longtask' });
        // A positive control: an 80 ms task of the page itself must be reported before the measured clicks.
        await new Promise(resolve => setTimeout(resolve, 0));
        const busy = performance.now();
        while (performance.now() - busy < 80) { /* block the main thread */ }
        const waited = performance.now();
        while (!longTasks.some(duration => duration >= 70) && performance.now() - waited < 3000) {
            await new Promise(resolve => setTimeout(resolve, 10));
        }
        controlSeen = longTasks.some(duration => duration >= 70);
        longTasks.length = 0;
    }
    const clicks = [];
    for (let i = 0; i < 200; i += 1) {
        const button = buttons()[(i * 37) % 1000];
        const before = Number(button.textContent);
        clicks.push(await timedClick(button));
        if (Number(button.textContent) !== before + 1) throw new Error(`click ${i} did not change its row`);
    }
    const reversals = [];
    const reverse = document.getElementById('reverse');
    if (reverse) {
        for (let i = 0; i < 50; i += 1) {
            const first = buttons()[0].dataset.key;
            reversals.push(await timedClick(reverse));
            const expected = first === 'r0' ? 'r999' : 'r0';
            if (buttons()[0].dataset.key !== expected) throw new Error(`reversal ${i} left ${buttons()[0].dataset.key} first`);
        }
    }
    await new Promise(resolve => setTimeout(resolve, 100));
    observer.disconnect();
    return {
        w5: summary(clicks), w6: reversals.length ? summary(reversals) : null,
        w7: { supported: longTaskSupported, control: controlSeen, over50: longTasks.filter(d => d > 50).length,
              longest: longTasks.length ? Math.max(...longTasks) : 0 },
        cores: navigator.hardwareConcurrency, agent: navigator.userAgent,
    };
}

const CYCLES = 1000;

async function growth(ui, lines, module) {
    const snapshots = [];
    let previous = null;
    for (let cycle = 1; cycle <= CYCLES; cycle += 1) {
        await until(() => document.getElementById('add') && document.getElementById('add') !== previous, `mount ${cycle}`);
        const add = document.getElementById('add');
        for (let k = 0; k < 10; k += 1) {
            const before = add.textContent;
            add.click();
            await until(() => add.textContent !== before, `click ${k} of cycle ${cycle}`);
        }
        if (cycle === 100 || cycle === CYCLES) {
            const mounted = lines.filter(line => line.startsWith('mounted, '));
            const [nodes, listeners, templates, queued] = ui.stats(ui.lastHandle).map(Number);
            snapshots.push({ cycle, nodes, listeners, templates, queued, openRoots: ui.opened - ui.closed,
                             subscribers: mounted[mounted.length - 1], mounts: mounted.length,
                             wasmBytes: module.HEAPU8.byteLength,
                             jsHeap: performance.memory?.usedJSHeapSize ?? null });
        }
        previous = add;
        document.getElementById('close').click();
    }
    return { growth: snapshots, cores: navigator.hardwareConcurrency, agent: navigator.userAgent };
}

// Counts the roots opened and closed, and keeps the latest handle.
class CountingUiHost extends UiHost {
    opened = 0;
    closed = 0;
    lastHandle = null;

    openRoot(selector) {
        const handle = super.openRoot(selector);
        if (typeof handle !== 'object') {
            this.opened += 1;
            this.lastHandle = handle;
        }
        return handle;
    }

    closeRoot(handle) {
        this.closed += 1;
        return super.closeRoot(handle);
    }
}

async function main() {
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const lines = [];
    const ui = new CountingUiHost();
    const routes = new HistoryHost();
    // As a bundle's boot module does: no step limit of the run's own.
    const execution = new NvmExecution(module, await read('app.nvr'), {
        host: { ...browserHost({ ui, history: routes }), console: line => void lines.push(line.replace(/\n$/, '')) },
        maxSteps: 2n ** 64n - 1n,
    });
    try {
        const running = execution.run().then(outcome => ({ outcome }));
        const work = document.querySelector('meta[name="neura-bench"]')?.content === 'growth' ? growth(ui, lines, module) : measure();
        const measured = work.then(report => ({ report }), error => ({ error }));
        const first = await Promise.race([running, measured]);
        if (first.error) throw first.error;
        if (first.outcome) {
            throw new Error(`the image ended: ${first.outcome.kind} ${first.outcome.status} ${first.outcome.diagnostic ?? ''}; `
                            + JSON.stringify(lines));
        }
        return first.report;
    } finally {
        routes.close();
        execution.close();
    }
}

const report = await main().catch(error => ({ error: error.stack || String(error) }));
await fetch(`/result${token}`, { method: 'POST', body: JSON.stringify(report) });
