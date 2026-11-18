// The browser journey of the portfolio (tests/apps/web/portfolio/portfolio.nra), run by app-check.py: a deep
// link, a link to the contact page, an empty submission refused by validation that focuses the first invalid
// field, typing, a sent message, a link back home, and the browser's back and forward buttons. It runs with hash
// routing and, when index.html says `<meta name="neura-routing" content="history">`, with history routing, where
// the page was opened at the deep link's path. The image never closes its root; the page reports once the
// journey is done.
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
const mode = document.querySelector('meta[name="neura-routing"]')?.content ?? 'hash';
// The token of the result URL; history routing replaces the address's query.
const token = location.search;
// The application path the address shows, in the page's routing mode.
const at = () => (mode === 'hash' ? location.hash.slice(1) : location.pathname);
const href = path => (mode === 'hash' ? `#${path}` : path);

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
    check(link('Contact').getAttribute('href') === href('/contact'), `link href: ${link('Contact').getAttribute('href')}`);
    check(link('Home').getAttribute('href') === href('/'), `link href: ${link('Home').getAttribute('href')}`);
    steps.push({ name: 'deep link', at: lines[0] });

    link('Contact').click();
    await until(() => text('main h1') === 'Contact', 'the contact page', lines);
    check(at() === '/contact', `the address follows the router: ${location.href}`);
    check(one('label[for="name"]').textContent === 'Name' && one('textarea#message'), 'the controls render');
    steps.push({ name: 'link', at: at() });

    one('button[type="submit"]').click();
    await until(() => document.querySelectorAll('[role="alert"]').length === 3, 'three validation errors', lines);
    // The refused form asks its root to focus the first invalid field (ui.FocusHost over neura.ui.focus).
    await until(() => document.activeElement === one('#name'), 'the focus on the first invalid field', lines);
    check(one('#name').getAttribute('aria-invalid') === 'true', 'the name is marked invalid');
    check(one('#name').getAttribute('aria-describedby') === one('[role="alert"]').id, 'the error describes the input');
    check(!lines.some(line => line.startsWith('sent ')), 'an invalid form sends nothing');
    steps.push({ name: 'refused and focused', alerts: document.querySelectorAll('[role="alert"]').length });

    // Fill in the name only: the e-mail is now the first invalid field.
    type(one('#name'), 'Ada');
    one('button[type="submit"]').click();
    await until(() => document.activeElement === one('#email'), 'the focus on the e-mail field', lines);
    check(document.querySelectorAll('[role="alert"]').length === 2, 'two errors remain');
    steps.push({ name: 'focus follows the first invalid field' });

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
    check(at() === '/', `home address: ${location.href}`);
    steps.push({ name: 'home', at: at() });

    // The browser's buttons reach the router: back to the contact page, back to the deep link, and forward.
    history.back();
    await until(() => text('main h1') === 'Contact', 'back to the contact page', lines);
    check(at() === '/contact', `back address: ${location.href}`);
    history.back();
    await until(() => text('main h1') === 'Project' && text('main p') === 'This is neura.', 'back to the deep link',
                lines);
    history.forward();
    await until(() => text('main h1') === 'Contact', 'forward to the contact page', lines);
    check(one('#name').value === 'Ada', 'the form keeps its drafts');
    steps.push({ name: 'back and forward', at: at() });
}

async function main() {
    if (mode === 'hash') history.replaceState(null, '', `${token}#/projects/neura`);
    else check(location.pathname === '/projects/neura', `history routing opens the deep link: ${location.href}`);
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const lines = [];
    const ui = new UiHost();
    const routes = new HistoryHost({ mode });
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
