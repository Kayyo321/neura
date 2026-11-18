// Added by backend-check.py to a copy of the bundle of examples/apps/web-backend/app.nra, beside its boot module:
// drives the page as a user would (a greeting, a renamed greeting, an echoed message, then a call while the backend
// is down) and posts what the status line showed after each step. The page's content-security policy allows it,
// since it is a module of the page's own origin.
const page = globalThis.document;
const status = () => page.getElementById('status')?.textContent;

async function until(predicate, label) {
    const started = performance.now();
    while (!predicate()) {
        if (performance.now() - started > 20000) {
            throw new Error(`timed out waiting for ${label}: state ${page.documentElement.dataset.neuraState}, `
                            + `failure ${page.getElementById('neura-failure')?.textContent}, `
                            + `DOM ${page.getElementById('app').innerHTML}`);
        }
        await new Promise(resolve => setTimeout(resolve, 5));
    }
}

function type(id, value) {
    const input = page.getElementById(id);
    input.focus();
    input.value = value;
    input.dispatchEvent(new Event('input', { bubbles: true }));
}

// Clicks `id` and waits until the status line leaves `Waiting for the server` with a text other than `before`.
async function call(id, label) {
    const before = status();
    page.getElementById(id).click();
    await until(() => status() !== before && status() !== 'Waiting for the server' && status() !== 'Ready', label);
    return status();
}

async function probe() {
    await until(() => status() === 'Ready', 'the first commit');
    const steps = [];
    steps.push(await call('greet', 'the greeting'));
    type('name', 'Grace');
    steps.push(await call('greet', 'the second greeting'));
    type('message', 'ping é ✓');
    steps.push(await call('send', 'the echo'));
    // From this request on, the check's proxy answers 502 for the backend, as a proxy does when the server is down.
    await fetch(`/backend-down${location.search}`, { method: 'POST' });
    steps.push(await call('greet', 'the failed call'));
    return { state: page.documentElement.dataset.neuraState, steps };
}

const report = await probe().catch(error => ({ error: error.stack || String(error) }));
await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
