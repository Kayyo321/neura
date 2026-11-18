// Starts a web bundle's image in its page (docs/runtime/Providers.md, "Web bundles"). index.html holds the
// configuration as a JSON data block, which a content-security policy without inline scripts allows.
import createNvm from './nvm.mjs';
import { NvmExecution, browserHost } from './nvm.js';
import { HistoryHost, UiHost } from './ui.mjs';

const page = globalThis.document;

function fail(message) {
    const banner = page.getElementById('neura-failure');
    if (banner) {
        banner.textContent = message;
        banner.hidden = false;
    }
    page.documentElement.dataset.neuraState = 'failed';
}

// The adapter needs WebAssembly exception handling and 64-bit integers as BigInt (Apps030.md, "Browsers").
function supported() {
    return typeof WebAssembly === 'object' && typeof WebAssembly.Tag === 'function' && typeof BigInt === 'function' &&
        typeof BigInt64Array === 'function';
}

async function fetchVerified(path, digest) {
    const integrity = `sha256-${btoa(String.fromCharCode(...digest.match(/../g).map(hex => parseInt(hex, 16))))}`;
    // Relative to this module, which lies in the bundle's directory: under history routing the page's own URL
    // may be a deep application path such as /parts/7.
    const response = await fetch(new URL(path, import.meta.url), { integrity, cache: 'force-cache' });
    if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
    return response;
}

async function start() {
    const config = JSON.parse(page.getElementById('neura-boot').textContent);
    if (!supported()) {
        page.getElementById('neura-unsupported').hidden = false;
        page.documentElement.dataset.neuraState = 'unsupported';
        return;
    }
    const [image, module] = await Promise.all([
        fetchVerified(config.image.path, config.image.digest).then(response => response.arrayBuffer()),
        createNvm({
            // Streaming when the host sends application/wasm, from bytes otherwise (Projects030.md, "Bundles").
            instantiateWasm(imports, done) {
                const fetched = fetchVerified(config.wasm.path, config.wasm.digest);
                const fromBytes = () => fetched.then(response => response.clone().arrayBuffer())
                    .then(bytes => WebAssembly.instantiate(bytes, imports));
                fetched.then(response => (response.headers.get('Content-Type') === 'application/wasm'
                    ? WebAssembly.instantiateStreaming(response.clone(), imports).catch(fromBytes) : fromBytes()))
                    .then(result => done(result.instance, result.module), error => fail(String(error)));
                return {};
            },
        }),
    ]);
    // Bundled media resolve against the bundle's directory, whatever the page's path.
    const ui = new UiHost({ assetBase: new URL('./', import.meta.url).href });
    const history = new HistoryHost({ mode: config.routing, base: config.base ?? '/' });
    const execution = new NvmExecution(module, new Uint8Array(image), { host: browserHost({ ui, history }) });
    page.documentElement.dataset.neuraState = 'running';
    try {
        const event = await execution.run({ hasPendingEvents: () => ui.queued > 0 });
        page.documentElement.dataset.neuraStatus = String(event.status);
        if (event.kind === 'trapped') {
            // A trap ends the execution; no component can catch it (Html030.md, "Commit").
            fail(`The application stopped: ${event.diagnostic}`);
        } else {
            page.documentElement.dataset.neuraState = 'completed';
        }
    } finally {
        execution.close();
        history.close();
    }
}

start().catch(error => fail(`The application could not start: ${error.message}`));
