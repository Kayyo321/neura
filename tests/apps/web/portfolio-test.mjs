// The browser journey of the portfolio (tests/apps/web/portfolio/portfolio.nra), run by app-check.py: a deep
// link, a link to the contact page, an empty submission refused by validation, typing, a sent message, and a
// link back home. The image never closes its root; the page reports once the journey is done.
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
const link = label => [...document.querySelectorAll('nav a')].find(a => a.textContent === label);

async function until(predicate, label, lines) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > 10000) {
            throw new Error(`timed out waiting for ${label}; DOM ${one('#app').innerHTML}; output ${JSON.stringify(lines)}`);
        }
        await new Promise(resolve => setTimeout(resolve, 2));
    }
}

function type(element, value) {
    element.focus();
    element.value = value;
    element.dispatchEvent(new Event('input', { bubbles: true }));
}

async function journey(lines, steps) {
    await until(() => text('main h1') === 'Project', 'the deep link', lines);
    check(text('main p') === 'This is neura.', `deep link body: ${text('main p')}`);
    check(link('Contact').getAttribute('href') === '#/contact', `link href: ${link('Contact').getAttribute('href')}`);
    steps.push({ name: 'deep link', at: lines[0] });

    link('Contact').click();
    await until(() => text('main h1') === 'Contact', 'the contact page', lines);
    check(location.hash === '#/contact', `the address follows the router: ${location.hash}`);
    check(one('label[for="name"]').textContent === 'Name' && one('textarea#message'), 'the controls render');
    steps.push({ name: 'link', hash: location.hash });

    one('button[type="submit"]').click();
    await until(() => document.querySelectorAll('[role="alert"]').length === 3, 'three validation errors', lines);
    check(one('#name').getAttribute('aria-invalid') === 'true', 'the name is marked invalid');
    check(one('#name').getAttribute('aria-describedby') === one('[role="alert"]').id, 'the error describes the input');
    check(!lines.some(line => line.startsWith('sent ')), 'an invalid form sends nothing');
    steps.push({ name: 'refused', alerts: document.querySelectorAll('[role="alert"]').length });

    const name = one('#name');
    type(name, 'Ada');
    type(one('#email'), 'ada@example.com');
    type(one('#message'), 'Hello!');
    await until(() => one('#message').value === 'Hello!' && one('#name') === name, 'typed drafts', lines);
    one('button[type="submit"]').click();
    await until(() => one('[role="status"]') !== null, 'the thank-you note', lines);
    check(lines.includes('sent Ada <ada@example.com>: Hello!'), `the message is sent: ${JSON.stringify(lines)}`);
    check(document.querySelectorAll('[role="alert"]').length === 0, 'no error remains');
    check(one('#name') === name && name.value === 'Ada', 'the input keeps its element and value');
    steps.push({ name: 'sent', status: text('[role="status"]') });

    link('Home').click();
    await until(() => text('main h1') === 'Hello', 'the home page', lines);
    check(location.hash === '#/', `home address: ${location.hash}`);
    steps.push({ name: 'home', hash: location.hash });
}

async function main() {
    history.replaceState(null, '', '#/projects/neura');
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
await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
