// The browser journey of tests/apps/web/events/events.nra, run by app-check.py: the mount errors of
// neura.ui.root.open (1, 3 and 100), each event kind beyond click, input and submit, the input-overflow record
// after a burst of clicks, and Root.reset. The page adds #spare-1 to #spare-16 before the image runs, so the image
// can open roots up to U-9. Events are dispatched as the browser would send them; scroll, toggle and close come
// from the elements themselves. The image never closes its root; the page reports once the journey is done.
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

const one = id => document.getElementById(id);
const token = location.search;

async function until(predicate, label, lines) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > 10000) {
            throw new Error(`timed out waiting for ${label}; DOM ${one('app').innerHTML}; output ${JSON.stringify(lines)}`);
        }
        await new Promise(resolve => setTimeout(resolve, 2));
    }
}

// Waits for the next printed line after `seen` lines that starts with `prefix`, and returns it.
async function line(lines, seen, prefix) {
    let found;
    await until(() => (found = lines.slice(seen).find(text => text.startsWith(prefix))) !== undefined, prefix, lines);
    return found;
}

async function journey(lines, steps) {
    await until(() => lines.includes('ready'), 'the mount', lines);
    const missing = lines.find(text => text.startsWith('missing: '));
    const nested = lines.find(text => text.startsWith('nested: '));
    const refused = lines.filter(text => /^spare \d+: /.test(text));
    check(/\(1\)$/.test(missing), `no element: ${missing}`);
    check(/\(3\)$/.test(nested), `an element inside another root: ${nested}`);
    check(refused.length === 1 && /^spare 16: .*\(100\)$/.test(refused[0]), `U-9: ${JSON.stringify(refused)}`);
    check(lines.includes('spares opened 15'), `the spare roots: ${JSON.stringify(lines)}`);
    for (let i = 1; i <= 15; i += 1) {
        check(one(`spare-${i}`).childNodes.length === 0, `closing spare root ${i} emptied its element`);
    }
    steps.push({ name: 'mount errors 1, 3 and 100', lines: [missing, nested, refused[0]] });

    let seen = lines.length;
    one('double').dispatchEvent(new MouseEvent('dblclick', { bubbles: true, cancelable: true, detail: 2, button: 0 }));
    const double = await line(lines, seen, 'dblclick');
    check(double === 'dblclick detail 2 button 0', double);

    seen = lines.length;
    one('check').click();
    const change = await line(lines, seen, 'change');
    check(change === 'change checked true value on', change);

    seen = lines.length;
    const keys = one('keys');
    keys.dispatchEvent(new FocusEvent('focus'));
    keys.dispatchEvent(new KeyboardEvent('keydown', { bubbles: true, cancelable: true, key: 'A', code: 'KeyA',
                                                      shiftKey: true, repeat: true }));
    keys.dispatchEvent(new KeyboardEvent('keyup', { bubbles: true, cancelable: true, key: 'A', code: 'KeyA',
                                                    shiftKey: true }));
    keys.dispatchEvent(new FocusEvent('blur'));
    const keyLines = [await line(lines, seen, 'focus'), await line(lines, seen, 'keydown'),
                      await line(lines, seen, 'keyup'), await line(lines, seen, 'blur')];
    check(JSON.stringify(keyLines) === JSON.stringify(['focus', 'keydown A KeyA shift repeat true',
                                                       'keyup A KeyA shift repeat false', 'blur']),
          `keys and focus: ${JSON.stringify(keyLines)}`);
    const order = keyLines.map(text => lines.indexOf(text, seen));
    check(order.every((at, i) => i === 0 || at > order[i - 1]), `key and focus order: ${JSON.stringify(lines.slice(seen))}`);

    seen = lines.length;
    const pad = one('pad');
    const box = pad.getBoundingClientRect();
    const at = (x, y) => ({ bubbles: true, cancelable: true, clientX: box.left + x, clientY: box.top + y,
                            pointerType: 'mouse' });
    pad.dispatchEvent(new PointerEvent('pointerdown', { ...at(10, 20), button: 0, altKey: true }));
    // Moves while nobody takes them coalesce to the latest.
    for (let x = 11; x <= 30; x += 1) pad.dispatchEvent(new PointerEvent('pointermove', at(x, 20)));
    pad.dispatchEvent(new PointerEvent('pointerup', { ...at(30, 20), button: 0, pointerType: 'pen' }));
    const pointerLines = [await line(lines, seen, 'pointerdown'), await line(lines, seen, 'pointermove'),
                          await line(lines, seen, 'pointerup')];
    check(/^pointerdown 10(\.0)?,20(\.0)? button 0 mouse alt $/.test(pointerLines[0]), pointerLines[0]);
    check(/^pointermove \d+(\.\d+)?,20(\.0)? mouse$/.test(pointerLines[1]), pointerLines[1]);
    check(/^pointerup 30(\.0)?,20(\.0)? button 0 pen$/.test(pointerLines[2]), pointerLines[2]);
    const moves = lines.slice(seen).filter(text => text.startsWith('pointermove'));
    check(moves.length < 20, `pointermove coalesces: ${moves.length} delivered`);

    seen = lines.length;
    one('scroller').scrollTop = 100;
    const scroll = await line(lines, seen, 'scroll');
    check(/^scroll top 100(\.0)? left 0(\.0)?$/.test(scroll), scroll);

    seen = lines.length;
    one('more').open = true;
    const toggle = await line(lines, seen, 'toggle');
    check(toggle === 'toggle open true', toggle);

    seen = lines.length;
    one('dialog').show();
    one('dialog').close('done');
    const close = await line(lines, seen, 'close');
    check(close === 'close done', close);
    steps.push({ name: 'dblclick, change, focus, keydown, keyup, blur, pointerdown, pointermove, pointerup, scroll, toggle, close', lines: [double, change, ...keyLines, ...pointerLines, scroll, toggle, close] });

    // A burst of clicks while the image cannot take them: past U-7 (1,024 queued records) they are dropped and
    // counted, the root's element is marked, and the image is told before the next event it takes.
    seen = lines.length;
    const flood = one('flood');
    for (let i = 0; i < 1100; i += 1) flood.click();
    const marked = one('app').hasAttribute('data-neura-overloaded');
    const overflow = await line(lines, seen, 'overflow');
    const dropped = Number(overflow.split(' ')[1]);
    await until(() => Number(one('count').textContent) + dropped === 1100, 'the clicks that were kept', lines);
    check(marked && dropped > 0 && !one('app').hasAttribute('data-neura-overloaded'),
          `overflow: marked ${marked}, ${overflow}, count ${one('count').textContent}`);
    steps.push({ name: `overflow (${dropped} of 1,100 clicks dropped, ${one('count').textContent} kept)` });

    // A reset empties the root and renders every component again into new nodes; events still arrive.
    const before = one('flood');
    const count = one('count').textContent;
    seen = lines.length;
    one('reset').click();
    await line(lines, seen, 'reset');
    await until(() => one('flood') && one('flood') !== before && one('count').textContent === count, 'the reset', lines);
    one('flood').click();
    await until(() => Number(one('count').textContent) === Number(count) + 1, 'a click after the reset', lines);
    steps.push({ name: 'reset into new nodes, then a click' });
}

async function main() {
    for (let i = 1; i <= 16; i += 1) {
        const spare = document.createElement('div');
        spare.id = `spare-${i}`;
        document.body.append(spare);
    }
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const lines = [];
    const ui = new UiHost();
    const routes = new HistoryHost();
    const steps = [];
    const execution = new NvmExecution(module, await read('app.nvr'), {
        host: { ...browserHost({ ui, history: routes }), console: line => void lines.push(line.replace(/\n$/, '')) },
    });
    try {
        const running = execution.run().then(outcome => ({ outcome }));
        const driven = journey(lines, steps).then(() => ({ driven: true }), error => ({ error }));
        const first = await Promise.race([running, driven]);
        if (first.error) throw first.error;
        if (first.outcome) {
            throw new Error(`the image ended: ${first.outcome.kind} ${first.outcome.status} `
                            + `${first.outcome.diagnostic ?? ''}; ${JSON.stringify(lines)}`);
        }
        return { steps, commits: 0, lines };
    } finally {
        routes.close();
        execution.close();
    }
}

const report = await main().catch(error => ({ error: error.stack || String(error) }));
await fetch(`/result${token}`, { method: 'POST', body: JSON.stringify(report) });
