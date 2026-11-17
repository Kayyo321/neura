'use strict';

const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { promises: fs } = require('node:fs');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');
const { app, BrowserWindow } = require('electron');
const { launchDesktopShell, registerDesktopScheme, verifyWebBundle } = require('../../hosts/desktop/main');

const scratch = process.env.NEURA_DESKTOP_TEST_SCRATCH;
app.setPath('userData', path.join(scratch, 'profile'));
registerDesktopScheme();
const applicationId = 'example.neura.shell';
const home = `app://${applicationId}/`;
const externalOrigin = 'https://allowed.neura.invalid';
const csp = "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; img-src 'self'; " +
    "font-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'";

function digest(bytes) { return createHash('sha256').update(bytes).digest('hex'); }

async function makeBundle() {
    const directory = path.join(scratch, 'Verified web é');
    await fs.mkdir(path.join(directory, 'assets'), { recursive: true });
    const fileTable = [];
    async function file(relative, value, media_type, initial = true) {
        const bytes = Buffer.from(value);
        const sha256 = digest(bytes);
        await fs.writeFile(path.join(directory, relative), bytes);
        fileTable.push({ path: relative, sha256, bytes: bytes.length, media_type, initial });
        return { path: relative, digest: sha256 };
    }
    const image = await file('app.fixture.nvr', 'synthetic image', 'application/octet-stream');
    await file('runtime.fixture.wasm', [0, 97, 115, 109, 1, 0, 0, 0], 'application/wasm');
    await file('boot.fixture.js', 'globalThis.booted = true;', 'text/javascript');
    await file('skin.fixture.css', 'body { color: rgb(20, 40, 60); }', 'text/css');
    await file('assets/café space.svg', '<svg xmlns="http://www.w3.org/2000/svg" width="2" height="2"/>', 'image/svg+xml');
    await file('404.html', '<!doctype html><title>Second document</title>', 'text/html', false);
    await file('index.html', '<!doctype html><title>Verified desktop</title><link rel="stylesheet" href="skin.fixture.css">' +
        '<script defer src="boot.fixture.js"></script><script>globalThis.inlineRan = true</script>', 'text/html');
    const manifest = {
        schema: 1, kind: 'web', id: applicationId, name: 'shell', version: '0.1.0', language: '0.3',
        files: fileTable, image, csp, requires: { browserAdapter: 1 }, origins: {}, limits: {}, capabilities: [], contracts: [],
    };
    const manifestFile = path.join(directory, 'bundle.json');
    async function commit(document = manifest) {
        const bytes = Buffer.from(JSON.stringify(document));
        await fs.writeFile(manifestFile, bytes);
        return {
            directory, applicationId, manifestDigest: digest(bytes), supportedRequirements: { browserAdapter: 1 },
            externalOrigins: [externalOrigin],
        };
    }
    return { directory, commit, manifest, settings: await commit() };
}

async function verifyStartup({ directory, commit, manifest, settings }) {
    await assert.rejects(launchDesktopShell({ ...settings, manifestDigest: '0'.repeat(64) }), /SHA-256 mismatch for bundle.json/);
    assert.equal(BrowserWindow.getAllWindows().length, 0);
    await assert.rejects(launchDesktopShell({ ...settings, externalOrigins: [externalOrigin + '/path'] }), /HTTPS origin/);
    await assert.rejects(verifyWebBundle({ ...settings, applicationId: 'example.neura.other' }), /application id/);
    await assert.rejects(verifyWebBundle({ ...settings, supportedRequirements: {} }), /browserAdapter/);

    const badName = path.join(directory, 'unlisted.txt');
    await fs.writeFile(badName, 'unlisted startup file');
    await assert.rejects(launchDesktopShell(settings), /unlisted file.*unlisted\.txt/);
    assert.equal(BrowserWindow.getAllWindows().length, 0);
    await fs.unlink(badName);

    const changed = path.join(directory, 'boot.fixture.js');
    const script = await fs.readFile(changed);
    const corrupted = Buffer.from(script);
    corrupted[0] ^= 1;
    await fs.writeFile(changed, corrupted);
    await assert.rejects(launchDesktopShell(settings), /SHA-256 mismatch for boot\.fixture\.js/);
    assert.equal(BrowserWindow.getAllWindows().length, 0);
    await fs.unlink(changed);
    await assert.rejects(verifyWebBundle(settings), /missing file: boot\.fixture\.js/);

    const outside = path.join(scratch, 'outside.js');
    await fs.writeFile(outside, script);
    await fs.symlink(outside, changed);
    await assert.rejects(verifyWebBundle(settings), /unsupported file type: boot\.fixture\.js/);
    await fs.unlink(changed);
    await fs.writeFile(changed, script);

    await assert.rejects(verifyWebBundle(await commit({ ...manifest, files: [...manifest.files, manifest.files[0]] })), /duplicate file/);
    const invalidHash = [{ ...manifest.files[0], sha256: manifest.files[0].sha256.toUpperCase() }, ...manifest.files.slice(1)];
    await assert.rejects(verifyWebBundle(await commit({ ...manifest, files: invalidHash })), /invalid bundle file row/);
    const invalidPath = [{ ...manifest.files[0], path: '../outside.js' }, ...manifest.files.slice(1)];
    await assert.rejects(verifyWebBundle(await commit({ ...manifest, files: invalidPath })), /invalid bundle file row/);
    const wrongMime = manifest.files.map(row => row.path.endsWith('.wasm') ? { ...row, media_type: 'text/plain' } : row);
    await assert.rejects(verifyWebBundle(await commit({ ...manifest, files: wrongMime })), /wrong media type for runtime/);
    assert.deepEqual(await commit(), settings);
}

async function waitFor(check, reason) {
    const deadline = Date.now() + 4000;
    while (Date.now() < deadline) {
        if (await check()) return;
        await delay(10);
    }
    assert.fail(reason);
}

async function verifyShell({ directory, settings }) {
    const opened = [];
    const logged = [];
    const window = await launchDesktopShell(settings, { openExternal: url => opened.push(url), log: message => logged.push(message) });
    try {
        const contents = window.webContents;
        contents.debugger.attach('1.3');
        async function evaluate(expression) {
            const result = await contents.debugger.sendCommand('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
            assert.equal(result.exceptionDetails, undefined, result.exceptionDetails?.exception?.description);
            return result.result.value;
        }
        const preferences = contents.getLastWebPreferences();
        for (const name of ['nodeIntegration', 'nodeIntegrationInSubFrames', 'nodeIntegrationInWorker', 'webviewTag']) {
            assert.equal(preferences[name], false, name);
        }
        for (const name of ['sandbox', 'contextIsolation', 'webSecurity']) assert.equal(preferences[name], true, name);
        contents.openDevTools({ mode: 'detach' });
        assert.equal(contents.isDevToolsOpened(), false);

        assert.deepEqual(await evaluate(`({ title: document.title, origin: location.origin, secure: isSecureContext,
            require: typeof require, process: typeof process, booted: booted, inline: typeof inlineRan,
            styles: getComputedStyle(document.body).color, bridge: Object.keys(neuraDesktop).sort() })`), {
            title: 'Verified desktop', origin: home.slice(0, -1), secure: true, require: 'undefined', process: 'undefined',
            booted: true, inline: 'undefined', styles: 'rgb(20, 40, 60)', bridge: ['call', 'next_host_event'],
        });
        assert.equal(await evaluate("neuraDesktop.call('unused', new Uint8Array()).then(() => false, () => true)"), true);
        assert.equal(await evaluate("neuraDesktop.next_host_event().then(() => false, () => true)"), true);

        const isolated = contents.session;
        const page = await isolated.fetch(home);
        assert.equal(page.headers.get('content-security-policy'), csp);
        assert.equal(page.headers.get('x-content-type-options'), 'nosniff');
        assert.equal(page.headers.get('content-type'), 'text/html');
        const head = await isolated.fetch(home, { method: 'HEAD' });
        assert.equal(head.status, 200);
        assert.equal(head.headers.get('content-length'), `${(await page.arrayBuffer()).byteLength}`);
        assert.equal(await head.text(), '');
        assert.equal((await isolated.fetch(home, { method: 'POST' })).status, 405);
        const manifest = await isolated.fetch(`${home}bundle.json`);
        assert.equal(digest(Buffer.from(await manifest.arrayBuffer())), settings.manifestDigest);
        assert.equal((await isolated.fetch('app://another.neura.shell/')).status, 403);
        assert.equal((await isolated.fetch(`${home}assets/%2foutside.js`)).status, 404);
        assert.equal((await isolated.fetch(`${home}assets/%5coutside.js`)).status, 404);

        await fs.writeFile(path.join(directory, 'late.txt'), 'unlisted after launch');
        assert.equal((await isolated.fetch(`${home}late.txt`)).status, 404);
        assert.deepEqual(await evaluate(`Promise.all([
            WebAssembly.instantiateStreaming(fetch('runtime.fixture.wasm')).then(({instance}) => !!instance),
            fetch('assets/caf%C3%A9%20space.svg').then(r => r.headers.get('content-type')),
            fetch('runtime.fixture.wasm').then(r => r.headers.get('content-type')),
        ])`), [true, 'image/svg+xml', 'application/wasm']);

        assert.deepEqual(await evaluate(`Promise.all([
            navigator.permissions.query({ name: 'geolocation' }).then(p => p.state),
            Notification.requestPermission(),
            new Promise(done => navigator.geolocation.getCurrentPosition(() => done(0), err => done(err.code))),
        ])`), ['denied', 'denied', 1]);
        assert.equal(await evaluate(`window.open(${JSON.stringify(externalOrigin)}) === null && window.open('/404.html') === null`), true);
        assert.equal(BrowserWindow.getAllWindows().length, 1);
        assert.deepEqual(opened, []);
        assert.equal(await evaluate(`new Promise(done => {
            const frame = document.createElement('iframe');
            frame.onload = () => {
                try { done(typeof frame.contentWindow.neuraDesktop); } catch { done('undefined'); }
            };
            frame.src = '/404.html';
            document.body.append(frame);
        })`), 'undefined');

        isolated.enableNetworkEmulation({ offline: true });
        await window.loadURL(home);
        assert.equal(await evaluate('booted && location.origin === ' + JSON.stringify(home.slice(0, -1))), true);
        const unfinished = await contents.debugger.sendCommand('Runtime.evaluate', { expression: `location.assign('/404.html')` });
        assert.equal(unfinished.exceptionDetails, undefined);
        await waitFor(() => contents.getURL() === `${home}404.html`, 'same-origin navigation was blocked');
        const retained = contents.getURL();
        for (const url of ['app://another.neura.shell/', 'https://unlisted.neura.invalid/', 'https://allowed.neura.invalid.other/',
                           'https://allowed.neura.invalid:8443/']) {
            const before = logged.length;
            await evaluate(`location.assign(${JSON.stringify(url)})`);
            await waitFor(() => logged.length > before, `blocked navigation was not recorded for ${url}`);
            assert.equal(contents.getURL(), retained);
        }
        const external = `${externalOrigin}/path?x=1`;
        await evaluate(`location.assign(${JSON.stringify(external)})`);
        await waitFor(() => opened.length === 1, 'listed HTTPS origin was not sent to the dispatch hook');
        assert.deepEqual(opened, [external]);
        assert.equal(contents.getURL(), retained);
        await assert.rejects(window.loadURL('https://unlisted.neura.invalid/'));
        await waitFor(() => contents.getURL() === retained, 'blocked direct navigation did not restore the local document');

        const changed = path.join(directory, 'boot.fixture.js');
        const original = await fs.readFile(changed);
        original[0] ^= 1;
        await fs.writeFile(changed, original);
        assert.equal((await isolated.fetch(`${home}boot.fixture.js`)).status, 500);
        await waitFor(() => contents.getTitle() === 'Application unavailable', 'integrity failure did not replace the page');
        assert.equal((await isolated.fetch(`${home}404.html`)).status, 503);
        assert.equal(opened.length, 1);
    } finally {
        window.destroy();
    }
}

async function run() {
    assert.equal(process.versions.electron, '45.3.3');
    const fixture = await makeBundle();
    await verifyStartup(fixture);
    await verifyShell(fixture);
    console.log('Verified Electron bundle, offline assets, isolation, navigation, and integrity failure');
}

run().then(() => app.exit(0), error => { console.error(error); app.exit(1); });
