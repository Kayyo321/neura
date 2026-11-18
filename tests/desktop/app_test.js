'use strict';

// The desktop application runner under the pinned Electron: a package
// directory with desktop.json, a verified web bundle, registry.json and the
// real Neura native child (tests/desktop/fixtures/app-child.nra). The child
// opens the windows; pages call the bridge through the preload object only.
// run_app_test.py runs one case per process under Xvfb.

const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { promises: fs } = require('node:fs');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');
const { app, BrowserWindow } = require('electron');
const { DesktopStatus, loadDesktopPackage, registerDesktopScheme, runDesktopApplication } = require('../../hosts/desktop/main');

const scratch = process.env.NEURA_DESKTOP_TEST_SCRATCH;
const caseName = process.env.NEURA_DESKTOP_TEST_CASE;
app.setPath('userData', path.join(scratch, 'profile'));
registerDesktopScheme();
const applicationId = 'example.neura.fixture';
const home = `app://${applicationId}/`;
const csp = "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src 'self'; " +
    "base-uri 'none'; form-action 'none'";
const operations = ['fixture.echo', 'fixture.fail', 'fixture.open', 'fixture.quit', 'fixture.title', 'fixture.trap',
    'fixture.unserved'];

function digest(bytes) { return createHash('sha256').update(bytes).digest('hex'); }

// A package laid out as Desktop030 describes, with spaces and non-ASCII
// characters in its path.
async function makePackage() {
    const root = path.join(scratch, 'Fixture app é');
    const web = path.join(root, 'web');
    await fs.mkdir(web, { recursive: true });
    await fs.mkdir(path.join(root, 'bin'));
    const files = [];
    async function file(relative, value, media_type, initial = true) {
        const bytes = Buffer.from(value);
        await fs.writeFile(path.join(web, relative), bytes);
        files.push({ path: relative, sha256: digest(bytes), bytes: bytes.length, media_type, initial });
        return { path: relative, digest: digest(bytes) };
    }
    const image = await file('app.fixture.nvr', 'synthetic image', 'application/octet-stream');
    // The page script stands in for the renderer's NVM, which this branch does not build (A2).
    await file('page.fixture.js', `
        globalThis.bridgeCall = async (operation, text) => {
            const { status, bytes } = await neuraDesktop.call(operation, new TextEncoder().encode(text));
            return { status, text: new TextDecoder().decode(bytes) };
        };
        document.addEventListener('DOMContentLoaded', () => {
            document.querySelector('button').addEventListener('click', async () => {
                const reply = await bridgeCall('fixture.echo', 'clicked');
                document.querySelector('output').textContent = reply.status + ':' + reply.text;
            });
        });`, 'text/javascript');
    await file('index.html', '<!doctype html><title>Page title</title><script src="page.fixture.js"></script>' +
        '<button>Echo</button><output></output>', 'text/html');
    await file('second.html', '<!doctype html><title>Second page</title><script src="page.fixture.js"></script>' +
        '<button>Echo</button><output></output>', 'text/html', false);
    const manifest = Buffer.from(JSON.stringify({
        schema: 1, kind: 'web', id: applicationId, name: 'fixture', version: '0.1.0', language: '0.3', files, image, csp,
        requires: { browserAdapter: 1 }, origins: {}, limits: {}, capabilities: ['bridge'], contracts: [],
    }));
    await fs.writeFile(path.join(web, 'bundle.json'), manifest);
    const registry = Buffer.from(JSON.stringify({ operations, schema: 1 }));
    await fs.writeFile(path.join(root, 'registry.json'), registry);
    await fs.copyFile(process.env.NEURA_DESKTOP_TEST_CHILD, path.join(root, 'bin', 'app-child'));
    await fs.chmod(path.join(root, 'bin', 'app-child'), 0o755);
    await fs.writeFile(path.join(root, 'desktop.json'), JSON.stringify({
        schema: 1, kind: 'desktop', id: applicationId,
        web: { directory: 'web', manifestDigest: digest(manifest) },
        registry: { path: 'registry.json', sha256: digest(registry) },
        child: { executable: 'bin/app-child', args: [] },
        supportedRequirements: { browserAdapter: 1 }, externalOrigins: [], capabilities: [], limits: {},
    }));
    return root;
}

async function waitFor(check, message, timeout = 10000) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
        const value = await check();
        if (value) return value;
        await delay(20);
    }
    throw new Error(message);
}

function processGone(pid) {
    try {
        process.kill(pid, 0);
        return false;
    } catch (error) {
        return error.code === 'ESRCH';
    }
}

async function run() {
    assert.equal(process.versions.electron, '45.3.3');
    const configuration = await loadDesktopPackage(await makePackage());
    const reported = [];
    const logged = [];
    const running = await runDesktopApplication(configuration, {
        log: message => logged.push(message),
        onNativeFailure: (error, result) => reported.push({ error, result }),
    });
    const pid = running.child.pid;
    // Case 1: the child opens the first window after Hello.
    const [first] = await waitFor(() => {
        const open = running.windows();
        return open.length === 1 && open[0].window.isVisible() && open;
    }, 'the child did not open its window');
    assert.equal(first.handle, 1);
    const contents = first.window.webContents;
    assert.equal(contents.getURL(), home);
    assert.equal(first.window.getTitle(), 'Fixture', 'the child owns the title, not the page');
    const evaluate = script => contents.executeJavaScript(script, true);

    // A bridge round trip with a 64-bit integer text, exact (case 15).
    const big = '{"value":9007199254740993,"max":18446744073709551615}';
    assert.deepEqual(await evaluate(`bridgeCall('fixture.echo', ${JSON.stringify(big)})`), { status: 0, text: big });
    // An operation outside registry.json never reaches the child (case 18).
    assert.deepEqual(await evaluate("bridgeCall('fixture.missing', '')"), { status: DesktopStatus.NotFound, text: '' });
    assert.ok(logged.some(line => /outside registry/.test(line)));
    // A frame of D-3 plus one byte is TooLarge (case 23).
    assert.equal(await evaluate("neuraDesktop.call('fixture.echo', new Uint8Array(1048576 - 37 + 1)).then(r => r.status)"),
        DesktopStatus.TooLarge);
    assert.equal(await evaluate("neuraDesktop.call('fixture.echo', new Uint8Array(1048576 - 37)).then(r => r.status)"), 0);
    // Sixteen calls in flight from one window, each answered with its own reply (case 5).
    assert.equal(await evaluate(`Promise.all(Array.from({ length: 16 }, (_, i) => bridgeCall('fixture.echo', 'n' + i)))
        .then(replies => replies.every((r, i) => r.status === 0 && r.text === 'n' + i))`), true);
    // An implementation error and an operation with no implementation.
    assert.deepEqual(await evaluate("bridgeCall('fixture.fail', '')"),
        { status: DesktopStatus.Other, text: 'PermissionDenied: refused by the fixture' });
    assert.equal(await evaluate("bridgeCall('fixture.unserved', '').then(r => r.status)"), DesktopStatus.Unsupported);
    // A bridge operation that calls a host service: set_title from the child.
    assert.deepEqual(await evaluate("bridgeCall('fixture.title', 'Zoë ✓')"), { status: 0, text: 'titled' });
    assert.equal(first.window.getTitle(), 'Zoë ✓');
    // A DOM event in the page reaches the child and back.
    await evaluate("document.querySelector('button').click()");
    await waitFor(() => evaluate("document.querySelector('output').textContent === '0:clicked'"), 'the click did not round-trip');
    // The page sees only the two preload functions.
    assert.deepEqual(await evaluate('Object.keys(neuraDesktop).sort()'), ['call', 'next_host_event']);

    // Case 2: a second window on another route, its own renderer.
    assert.deepEqual(await evaluate("bridgeCall('fixture.open', '')"), { status: 0, text: '2' });
    const second = running.windows().find(entry => entry.handle === 2).window;
    await waitFor(() => second.isVisible(), 'the second window did not show');
    assert.equal(second.webContents.getURL(), `${home}second.html`);
    assert.equal(second.getTitle(), 'Second');
    assert.notEqual(second.webContents.getOSProcessId(), contents.getOSProcessId());
    assert.equal(await second.webContents.executeJavaScript("bridgeCall('fixture.echo', 'two').then(r => r.text)"), 'two');
    second.close();
    await waitFor(() => running.windows().length === 1, 'the second window did not close');
    assert.equal(processGone(pid), false, 'closing one window ended the child');
    assert.equal(await evaluate("bridgeCall('fixture.echo', 'still').then(r => r.text)"), 'still');

    const expected = { 'app-close': 0, 'app-quit': 7, 'app-trap': 1 }[caseName];
    app.once('quit', (_event, code) => {
        try {
            assert.equal(code, expected);
            assert.equal(processGone(pid), true, 'the native child survived the application');
            assert.equal(BrowserWindow.getAllWindows().length, 0);
            if (caseName === 'app-trap') {
                assert.equal(reported.length, 1);
                assert.equal(reported[0].error.code, 'ChildExited');
                assert.notEqual(reported[0].result.code, 0);
            } else {
                assert.equal(reported.length, 0);
            }
            console.log(`Verified desktop ${caseName}`);
        } catch (error) {
            console.error(error);
        }
    });
    if (caseName === 'app-close') {
        // Closing the last window ends the child and the application with 0 (case 1).
        first.window.close();
    } else if (caseName === 'app-quit') {
        // quit(7) from a bridge operation: every window closes and the status is 7.
        evaluate("bridgeCall('fixture.quit', '')").catch(() => {});
    } else {
        // A trap in the child: every window closes and the application exits 1 (case 32).
        evaluate("bridgeCall('fixture.trap', '')").catch(() => {});
    }
}

run().catch(error => {
    console.error(error);
    app.exit(1);
});
