// Loads a page in headless Chromium (DevTools protocol) or Firefox (WebDriver BiDi), waits until a condition
// holds in it, and prints {"value": <the condition's last value>, "html": <the document>} as JSON. bundle-check.py
// uses it to load web bundles under load: the wait is real time with a deadline, not a virtual-time budget, and a
// browser that does not come up is started again with a back-off.
//
//   node load-page.mjs <browser> <url> <seconds> '<expression>'
//
// The expression is evaluated in the page every 200 ms; the wait ends when it returns something other than null,
// undefined or false, or at the deadline, which is not an error: the value is then the last one seen.
import { spawn } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { basename, join } from 'node:path';

const [browser, url, seconds = '60', expression = 'document.documentElement.dataset.neuraState'] =
    process.argv.slice(2);
const firefox = basename(browser).includes('firefox');
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

function freePort() {
    return new Promise((resolve, reject) => {
        const server = createServer();
        server.listen(0, '127.0.0.1', () => {
            const { port } = server.address();
            server.close(() => resolve(port));
        });
        server.on('error', reject);
    });
}

// One JSON-message session over a WebSocket: send() resolves with the reply to its id.
function session(socket) {
    let next = 0;
    const waiting = new Map();
    socket.addEventListener('message', event => {
        const message = JSON.parse(event.data);
        const entry = waiting.get(message.id);
        if (!entry) return;
        waiting.delete(message.id);
        if (message.error) entry.reject(new Error(`${message.error.message ?? message.error}`));
        else entry.resolve(message.result);
    });
    return (method, params = {}, extra = {}) => new Promise((resolve, reject) => {
        const id = ++next;
        waiting.set(id, { resolve, reject });
        socket.send(JSON.stringify({ id, method, params, ...extra }));
    });
}

function open(address) {
    return new Promise((resolve, reject) => {
        const socket = new WebSocket(address);
        socket.addEventListener('open', () => resolve(socket), { once: true });
        socket.addEventListener('error', () => reject(new Error(`cannot connect to ${address}`)), { once: true });
    });
}

async function connect(port, deadline) {
    for (;;) {
        try {
            if (firefox) return await open(`ws://127.0.0.1:${port}/session`);
            const pages = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
            const page = pages.find(target => target.type === 'page');
            if (page) return await open(page.webSocketDebuggerUrl);
        } catch {
            // The browser is still starting.
        }
        if (Date.now() > deadline) throw new Error('the browser did not open its remote protocol');
        await sleep(200);
    }
}

async function attempt(budget) {
    const profile = await mkdtemp(join(tmpdir(), 'neura-load-page-'));
    const port = await freePort();
    const flags = firefox
        ? ['--headless', '--no-remote', '--profile', profile, `--remote-debugging-port=${port}`, 'about:blank']
        : ['--headless', '--disable-gpu', '--no-first-run', '--disable-background-networking', '--disable-sync',
           `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`, 'about:blank'];
    const child = spawn(browser, flags, { stdio: 'ignore', detached: true });
    const started = Date.now();
    try {
        const socket = await connect(port, started + Math.min(budget, 60_000));
        const send = session(socket);
        let evaluate;
        if (firefox) {
            await send('session.new', { capabilities: {} });
            const { contexts } = await send('browsingContext.getTree', {});
            const context = contexts[0].context;
            await send('browsingContext.navigate', { context, url, wait: 'none' });
            evaluate = async text => (await send('script.evaluate', {
                expression: `JSON.stringify(${text})`, target: { context }, awaitPromise: false,
            })).result?.value;
        } else {
            await send('Page.navigate', { url });
            evaluate = async text => (await send('Runtime.evaluate', {
                expression: `JSON.stringify(${text})`, returnByValue: true,
            })).result?.value;
        }
        let value = null;
        while (Date.now() - started < budget) {
            try {
                const text = await evaluate(`(() => { try { return ${expression}; } catch { return null; } })()`);
                value = text === undefined ? null : JSON.parse(text);
            } catch {
                // A navigation in progress replaces the context.
            }
            if (value !== null && value !== undefined && value !== false) break;
            await sleep(200);
        }
        const html = JSON.parse(await evaluate('document.documentElement.outerHTML') ?? '""');
        socket.close();
        return { value, html };
    } finally {
        try {
            process.kill(-child.pid, 'SIGKILL');
        } catch {
            // Already gone.
        }
        await rm(profile, { recursive: true, force: true }).catch(() => {});
    }
}

const budget = Number(seconds) * 1000;
let failure;
for (let tries = 0; tries < 3; ++tries) {
    try {
        console.log(JSON.stringify(await attempt(budget)));
        process.exit(0);
    } catch (error) {
        failure = error;
        await sleep(2000 * 2 ** tries);
    }
}
console.error(`load-page: ${failure?.message}`);
process.exit(1);
