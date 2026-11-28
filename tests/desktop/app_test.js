'use strict';

// The desktop application runner under the pinned Electron: a package
// directory with desktop.json, a verified web bundle, registry.json and the
// real Neura native child (tests/desktop/fixtures/app-child.nra). The child
// opens the windows; pages call the bridge through the preload object only.
// run_app_test.py runs one case per process.

const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { promises: fs } = require('node:fs');
const path = require('node:path');
const { StringDecoder } = require('node:string_decoder');
const { setTimeout: delay } = require('node:timers/promises');
const { app, BrowserWindow, clipboard, ipcMain, Menu } = require('electron');
const { DesktopStatus, loadDesktopPackage, registerDesktopScheme, runDesktopApplication } = require('../../hosts/desktop/main');

const scratch = process.env.NEURA_DESKTOP_TEST_SCRATCH;
const caseName = process.env.NEURA_DESKTOP_TEST_CASE;
const windowsHost = process.platform === 'win32';
app.setPath('userData', path.join(scratch, 'profile'));
registerDesktopScheme();
const applicationId = 'example.neura.fixture';
const home = `app://${applicationId}/`;
const csp = "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self'; connect-src 'self'; " +
    "frame-src 'self' https:; base-uri 'none'; form-action 'none'";
const operations = ['fixture.clipboard_read', 'fixture.clipboard_write', 'fixture.close', 'fixture.data_dir',
    'fixture.echo', 'fixture.fail', 'fixture.focus', 'fixture.menu', 'fixture.menu_bad', 'fixture.notify', 'fixture.open',
    'fixture.open_many', 'fixture.open_read', 'fixture.quit', 'fixture.save', 'fixture.spin', 'fixture.title',
    'fixture.trap', 'fixture.unserved', 'fixture.wait', 'fixture.work'];

function digest(bytes) { return createHash('sha256').update(bytes).digest('hex'); }

// A package laid out as Desktop030 describes, with spaces and non-ASCII
// characters in its path.
async function makePackage({ capabilities = [], limits = {} } = {}) {
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
        '<button>Echo</button><output></output><input id="source" value="copied by a role"><input id="target">',
    'text/html');
    await file('second.html', '<!doctype html><title>Second page</title><script src="page.fixture.js"></script>' +
        '<button>Echo</button><output></output>', 'text/html', false);
    await file('frame.html', '<!doctype html><title>Embedded</title><p>embedded</p>', 'text/html', false);
    const manifest = Buffer.from(JSON.stringify({
        schema: 1, kind: 'web', id: applicationId, name: 'fixture', version: '0.1.0', language: '0.3', files, image, csp,
        requires: { browserAdapter: 1 }, origins: {}, limits: {}, capabilities: ['bridge'], contracts: [],
    }));
    await fs.writeFile(path.join(web, 'bundle.json'), manifest);
    const registry = Buffer.from(JSON.stringify({ operations, schema: 1 }));
    await fs.writeFile(path.join(root, 'registry.json'), registry);
    const executable = windowsHost ? 'bin/app-child.exe' : 'bin/app-child';
    const childPath = path.join(root, executable);
    await fs.copyFile(process.env.NEURA_DESKTOP_TEST_CHILD, childPath);
    if (!windowsHost) await fs.chmod(childPath, 0o755);
    await fs.writeFile(path.join(root, 'desktop.json'), JSON.stringify({
        schema: 1, kind: 'desktop', id: applicationId,
        web: { directory: 'web', manifestDigest: digest(manifest) },
        registry: { path: 'registry.json', sha256: digest(registry) },
        child: { executable, args: [] },
        supportedRequirements: { browserAdapter: 1 }, externalOrigins: [], capabilities, limits,
    }));
    return root;
}

// A check that has not settled within the time left counts as false: executeJavaScript on a renderer that is
// being replaced may never settle.
async function waitFor(check, message, timeout = 10000) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
        const value = await Promise.race([Promise.resolve().then(check), delay(Math.max(deadline - Date.now(), 1), false)]);
        if (value) return value;
        await delay(20);
    }
    throw new Error(typeof message === 'function' ? message() : message);
}

// Progress on standard output, so that a case that times out shows where it stopped.
function step(text) {
    console.log(`step: ${text}`);
}

function processGone(pid) {
    try {
        process.kill(pid, 0);
        return false;
    } catch (error) {
        return error.code === 'ESRCH';
    }
}

// Starts the application and waits for the child's first window.
async function start(packageOptions = {}, runOptions = {}) {
    const configuration = await loadDesktopPackage(await makePackage(packageOptions));
    const reported = [];
    const logged = [];
    const childLog = [];
    const decoder = new StringDecoder('utf8');
    let pending = '';
    const running = await runDesktopApplication(configuration, {
        log: message => logged.push(message),
        onNativeFailure: runOptions.defaultFailure ? undefined : (error, result) => reported.push({ error, result }),
        nativeIO: {
            onStderr(chunk) {
                const lines = (pending + decoder.write(chunk)).split(/\r?\n/);
                pending = lines.pop();
                childLog.push(...lines.filter(Boolean));
            },
        },
        ...runOptions,
    });
    const [first] = await waitFor(() => {
        const open = running.windows();
        return open.length === 1 && open[0].window.isVisible() && open;
    }, 'the child did not open its window');
    const evaluate = (script, window = first.window) => window.webContents.executeJavaScript(script, true);
    return { running, first, evaluate, reported, logged, childLog, pid: running.child.pid };
}

function bridge(evaluate, operation, text = '', window) {
    return evaluate(`bridgeCall(${JSON.stringify(operation)}, ${JSON.stringify(text)})`, window);
}

// Checks the end of the application: its status, no child left, no window left.
function expectEnd({ pid, reported }, expected, check = () => {}) {
    app.once('quit', (_event, code) => {
        try {
            assert.equal(code, expected);
            assert.equal(processGone(pid), true, 'the native child survived the application');
            assert.equal(BrowserWindow.getAllWindows().length, 0);
            check(reported);
            console.log(`Verified desktop ${caseName}`);
        } catch (error) {
            console.error(error);
        }
    });
}

// The bridge, two windows and the three ways an application ends (cases 1, 2, 5, 15, 18, 23, 32).
async function lifecycle() {
    const session = await start();
    const { running, first, evaluate, logged, pid } = session;
    assert.equal(first.handle, 1);
    const contents = first.window.webContents;
    assert.equal(contents.getURL(), home);
    assert.equal(first.window.getTitle(), 'Fixture', 'the child owns the title, not the page');

    // A bridge round trip with a 64-bit integer text, exact (case 15).
    const big = '{"value":9007199254740993,"max":18446744073709551615}';
    assert.deepEqual(await bridge(evaluate, 'fixture.echo', big), { status: 0, text: big });
    // An operation outside registry.json never reaches the child (case 18).
    assert.deepEqual(await bridge(evaluate, 'fixture.missing'), { status: DesktopStatus.NotFound, text: '' });
    assert.ok(logged.some(line => /outside registry/.test(line)));
    // A frame of D-3 plus one byte is TooLarge (case 23).
    assert.equal(await evaluate("neuraDesktop.call('fixture.echo', new Uint8Array(1048576 - 37 + 1)).then(r => r.status)"),
        DesktopStatus.TooLarge);
    assert.equal(await evaluate("neuraDesktop.call('fixture.echo', new Uint8Array(1048576 - 37)).then(r => r.status)"), 0);
    // Sixteen calls in flight from one window, each answered with its own reply (case 5).
    assert.equal(await evaluate(`Promise.all(Array.from({ length: 16 }, (_, i) => bridgeCall('fixture.echo', 'n' + i)))
        .then(replies => replies.every((r, i) => r.status === 0 && r.text === 'n' + i))`), true);
    // An implementation error and an operation with no implementation.
    assert.deepEqual(await bridge(evaluate, 'fixture.fail'),
        { status: DesktopStatus.Other, text: 'PermissionDenied: refused by the fixture' });
    assert.equal((await bridge(evaluate, 'fixture.unserved')).status, DesktopStatus.Unsupported);
    // A bridge operation that calls a host service: set_title from the child.
    assert.deepEqual(await bridge(evaluate, 'fixture.title', 'Zoë ✓'), { status: 0, text: 'titled' });
    assert.equal(first.window.getTitle(), 'Zoë ✓');
    // A DOM event in the page reaches the child and back.
    await evaluate("document.querySelector('button').click()");
    await waitFor(() => evaluate("document.querySelector('output').textContent === '0:clicked'"), 'the click did not round-trip');
    // The page sees only the two preload functions.
    assert.deepEqual(await evaluate('Object.keys(neuraDesktop).sort()'), ['call', 'next_host_event']);

    // Case 2: a second window on another route, its own renderer.
    assert.deepEqual(await bridge(evaluate, 'fixture.open'), { status: 0, text: '2' });
    const second = running.windows().find(entry => entry.handle === 2).window;
    await waitFor(() => second.isVisible(), 'the second window did not show');
    assert.equal(second.webContents.getURL(), `${home}second.html`);
    assert.equal(second.getTitle(), 'Second');
    assert.notEqual(second.webContents.getOSProcessId(), contents.getOSProcessId());
    assert.equal((await bridge(evaluate, 'fixture.echo', 'two', second)).text, 'two');
    second.close();
    await waitFor(() => running.windows().length === 1, 'the second window did not close');
    assert.equal(processGone(pid), false, 'closing one window ended the child');
    assert.equal((await bridge(evaluate, 'fixture.echo', 'still')).text, 'still');

    const expected = { 'app-close': 0, 'app-quit': 7, 'app-trap': 1 }[caseName];
    expectEnd(session, expected, reported => {
        if (caseName === 'app-trap') {
            assert.equal(reported.length, 1);
            assert.equal(reported[0].error.code, 'ChildExited');
            assert.notEqual(reported[0].result.code, 0);
        } else {
            assert.equal(reported.length, 0);
        }
    });
    if (caseName === 'app-close') {
        // Closing the last window ends the child and the application with 0 (case 1).
        first.window.close();
    } else if (caseName === 'app-quit') {
        // quit(7) from a bridge operation: every window closes and the status is 7.
        bridge(evaluate, 'fixture.quit').catch(() => {});
    } else {
        // A trap in the child: every window closes and the application exits 1 (case 32).
        bridge(evaluate, 'fixture.trap').catch(() => {});
    }
}

// The child's services with the capabilities granted: notifications, the
// clipboard in both directions, and the data directory.
async function services() {
    const notified = [];
    const session = await start({ capabilities: ['desktop.clipboard_read', 'desktop.clipboard_write'] }, {
        // The scripted notification service: "off" stands for notifications turned off (case 16).
        notify: async (title, body) => {
            notified.push([title, body]);
            return body === 'off' ? 1 : 0;
        },
    });
    const { evaluate } = session;
    assert.deepEqual(await bridge(evaluate, 'fixture.notify', 'Saved ✓'), { status: 0, text: 'Shown' });
    assert.deepEqual(await bridge(evaluate, 'fixture.notify', 'off'), { status: 0, text: 'Denied' });
    assert.deepEqual(notified, [['Fixture', 'Saved ✓'], ['Fixture', 'off']]);

    step('clipboard');
    // The test desktop's system clipboard, written by the child and read by the host, then the reverse.
    assert.deepEqual(await bridge(evaluate, 'fixture.clipboard_write', 'copied by Neura ✓'), { status: 0, text: 'written' });
    assert.equal(await clipboard.readText(), 'copied by Neura ✓');
    await clipboard.writeText('pasted from the system é');
    assert.deepEqual(await bridge(evaluate, 'fixture.clipboard_read'), { status: 0, text: 'pasted from the system é' });

    step('data directory');
    const expected = path.join(windowsHost ? process.env.LOCALAPPDATA : process.env.XDG_DATA_HOME, applicationId);
    assert.deepEqual(await bridge(evaluate, 'fixture.data_dir'), { status: 0, text: expected });
    const entry = await fs.stat(expected);
    assert.ok(entry.isDirectory());
    if (!windowsHost) assert.equal(entry.mode & 0o777, 0o700);

    expectEnd(session, 0, reported => assert.equal(reported.length, 0));
    session.first.window.close();
}

// The same services without the capabilities (case 30), the real notification
// service, and a package that lowers D-1 to two windows.
async function denied() {
    const session = await start({ limits: { 'D-1': 2 } });
    const { evaluate, running } = session;
    assert.deepEqual(await bridge(evaluate, 'fixture.clipboard_read'),
        { status: DesktopStatus.Other, text: 'CapabilityDenied: neura.desktop.clipboard.read failed' });
    assert.deepEqual(await bridge(evaluate, 'fixture.clipboard_write', 'x'),
        { status: DesktopStatus.Other, text: 'CapabilityDenied: neura.desktop.clipboard.write failed' });
    // Electron's own notification service on the test desktop: either shown or unsupported.
    const outcome = await bridge(evaluate, 'fixture.notify', 'real');
    assert.equal(outcome.status, 0);
    assert.ok(['Shown', 'Unsupported'].includes(outcome.text), outcome.text);
    console.log(`notify on this display: ${outcome.text}`);
    // The host refuses the window past the package's D-1 (case 31, host side).
    assert.deepEqual(await bridge(evaluate, 'fixture.open_many'),
        { status: 0, text: '1 then Other: neura.desktop.window.open failed' });
    assert.equal(running.windows().length, 2);
    expectEnd(session, 0, reported => assert.equal(reported.length, 0));
    for (const { window } of running.windows()) window.close();
}

// Window services and events: the ninth window (case 31, child side),
// child-initiated close, focus, Focused, Blurred and Resized, a reopened
// window (case 3), embedded and remote frames (case 19), and a renderer that
// ends during the run (case 33, with a renderer crash standing in for a trap).
async function windows() {
    const session = await start();
    const { running, first, evaluate, childLog, logged, pid } = session;
    let mark = 0;
    const position = line => childLog.indexOf(`child: ${line}`, mark);
    const seen = line => position(line) >= 0;

    step('resized');
    // Resized: the host reports the content size it observed.
    first.window.setContentSize(500, 400);
    await waitFor(() => seen('event Resized 1 500x400'), 'the child did not see Resized');

    step('ninth window');
    // The ninth window under the default D-1 is refused by the child itself.
    assert.deepEqual(await bridge(evaluate, 'fixture.open_many'), { status: 0, text: '7 then Other: too many windows' });
    assert.equal(running.windows().length, 8);
    // Child-initiated close of windows 2 to 8: each is a Closed event, the first window stays.
    for (let handle = 2; handle <= 8; handle += 1) {
        assert.deepEqual(await bridge(evaluate, 'fixture.close', String(handle)), { status: 0, text: 'closed' });
        await waitFor(() => seen(`event Closed ${handle}`), `the child did not see Closed ${handle}`);
    }
    assert.deepEqual(running.windows().map(entry => entry.handle), [1]);
    // A window that is no longer open is refused by the host.
    assert.deepEqual(await bridge(evaluate, 'fixture.close', '2'),
        { status: DesktopStatus.Other, text: 'InvalidInput: neura.desktop.window.close failed' });

    step('focus');
    // Focus from the child: Focused for the new window, Blurred for the one that had it.
    assert.deepEqual(await bridge(evaluate, 'fixture.open'), { status: 0, text: '9' });
    const reopened = running.windows().find(entry => entry.handle === 9).window;
    await waitFor(() => reopened.isVisible(), 'window 9 did not show');
    await delay(200);
    mark = childLog.length;
    assert.deepEqual(await bridge(evaluate, 'fixture.focus', '1'), { status: 0, text: 'focused' });
    await waitFor(() => seen('event Focused 1'), 'the child did not see Focused 1');
    assert.deepEqual(await bridge(evaluate, 'fixture.focus', '9'), { status: 0, text: 'focused' });
    await waitFor(() => seen('event Focused 9') && seen('event Blurred 1'), 'the child did not see Focused 9 and Blurred 1');
    const order = ['event Focused 1', 'event Blurred 1', 'event Focused 9'].map(position);
    // X delivers the old window's focus-out and the new window's focus-in in either order, so only Focused 1
    // before both is fixed.
    assert.ok(order[0] < order[1] && order[0] < order[2], `events out of order: ${order}`);

    step('close with a call in flight');
    // Case 26: a window closed with a call in flight; the call is cancelled in the child.
    reopened.webContents.executeJavaScript("bridgeCall('fixture.work', '')").catch(() => {});
    await waitFor(() => running.inFlight(9) === 1, 'the working call did not reach the host');
    mark = childLog.length;
    await evaluate('globalThis.kept = 42', reopened);
    reopened.close();
    await waitFor(() => seen('event Closed 9'), 'the child did not see Closed 9');
    await waitFor(() => childLog.slice(mark).some(line => /^child: work \d+ stopped$/.test(line)), 'the child did not stop the call');

    step('reopen');
    // Case 3: a window on a route that was closed before opens with a fresh renderer and fresh state.
    assert.deepEqual(await bridge(evaluate, 'fixture.open'), { status: 0, text: '10' });
    const fresh = running.windows().find(entry => entry.handle === 10).window;
    await waitFor(() => fresh.isVisible(), 'window 10 did not show');
    assert.equal(await evaluate('typeof globalThis.kept', fresh), 'undefined');
    assert.equal((await bridge(evaluate, 'fixture.echo', 'fresh', fresh)).text, 'fresh');

    step('frames');
    // Case 19: an embedded document of the bundle gets no bridge object, and a remote document never loads.
    assert.equal(await evaluate(`new Promise(done => {
        const frame = document.createElement('iframe');
        frame.onload = () => done(typeof frame.contentWindow.neuraDesktop);
        frame.src = 'frame.html';
        document.body.append(frame);
    })`), 'undefined');
    const embedded = await waitFor(() => first.window.webContents.mainFrame.frames.find(frame => frame.url.endsWith('/frame.html')),
        'the embedded frame did not load');
    await evaluate(`new Promise(done => {
        const frame = document.createElement('iframe');
        frame.src = 'https://elsewhere.example/';
        frame.onload = done;
        setTimeout(done, 2000);
        document.body.append(frame);
    })`);
    const hostOf = url => { try { return new URL(url).hostname; } catch { return ''; } };
    assert.ok(!first.window.webContents.mainFrame.frames.some(frame => hostOf(frame.url) === 'elsewhere.example'));
    // The host's own sender check, exercised directly: a synthetic IPC event whose sender frame is the embedded
    // frame of a runner window (the preload never exposes the bridge there, so no page can send one).
    const handler = ipcMain._invokeHandlers?.get('neura:bridge-call');
    assert.equal(typeof handler, 'function', 'Electron no longer keeps invoke handlers where the test expects them');
    const refused = await handler({ sender: first.window.webContents, senderFrame: embedded }, 'fixture.echo', Buffer.from('x'));
    assert.deepEqual(refused, { status: DesktopStatus.PermissionDenied });
    assert.ok(logged.some(line => /frame outside the application/.test(line)));

    step('renderer crash');
    // Case 33: the second window's renderer ends. That window shows the failure page; the child and the
    // other windows continue, and Reload is a fresh execution.
    const crashed = fresh.webContents;
    // A call in flight from that page waits on a timer in the child; it is cancelled there, and its reply dropped.
    crashed.executeJavaScript("bridgeCall('fixture.wait', '60000')").catch(() => {});
    await waitFor(() => running.inFlight(10) === 1, 'the waiting call did not reach the host');
    const gone = new Promise(resolve => crashed.once('render-process-gone', (_event, details) => resolve(details.reason)));
    process.kill(crashed.getOSProcessId(), 'SIGKILL');
    step(`renderer gone: ${await Promise.race([gone, delay(10000, 'no event')])}`);
    await waitFor(() => !fresh.isDestroyed() && crashed.getURL() === `${home}second.html` && crashed.getTitle() === 'Window stopped',
        () => `no failure page: ${crashed.getURL()} "${crashed.getTitle()}"; ${logged.join('; ')}`);
    assert.ok(logged.some(line => /renderer ended/.test(line)));
    await waitFor(() => childLog.some(line => /^child: wait \d+ cancelled$/.test(line)), 'the child did not cancel the call');
    assert.equal(running.inFlight(10), 0);
    assert.equal((await bridge(evaluate, 'fixture.echo', 'alive')).text, 'alive');
    await crashed.executeJavaScript("document.getElementById('neura-reload').click()");
    await waitFor(() => crashed.getTitle() === 'Second page', 'Reload did not load the route again');
    assert.equal((await bridge(evaluate, 'fixture.echo', 'reloaded', fresh)).text, 'reloaded');
    assert.equal(processGone(pid), false);

    expectEnd(session, 0, reported => assert.equal(reported.length, 0));
    for (const { window } of running.windows()) window.close();
}

// Menus, dialogs and granted files (cases 6 to 10 and 29) with the scripted dialog driver; the page never
// receives a path.
async function dialogs() {
    const script = [];
    const asked = [];
    const session = await start({}, {
        dialogs: async (kind, options) => {
            asked.push([kind, options.handle, options.suggestedName, JSON.stringify(options.filters)]);
            return script.shift() ?? { canceled: true };
        },
    });
    const { evaluate, childLog, logged, first } = session;
    const files = path.join(scratch, 'files');
    await fs.mkdir(files);

    step('save cancelled');
    script.push({ canceled: true });
    assert.deepEqual(await bridge(evaluate, 'fixture.save', 'report.txt'), { status: 0, text: 'cancelled' });
    assert.deepEqual(asked.shift(), ['save', 1, 'report.txt', '[{"name":"Text","extensions":["txt"]}]']);
    assert.deepEqual(await fs.readdir(files), []);

    step('save chosen');
    const target = path.join(files, 'chosen report.txt');
    script.push({ canceled: false, filePath: target });
    assert.deepEqual(await bridge(evaluate, 'fixture.save', 'report.txt'),
        { status: 0, text: 'saved chosen report.txt 11+12 closed; InvalidInput: the granted file is closed' });
    assert.equal(await fs.readFile(target, 'utf8'), 'first line\nsecond line\n');

    step('save over an existing file');
    await fs.writeFile(target, 'old content that is longer than the new content\n');
    script.push({ canceled: false, filePath: target });
    assert.equal((await bridge(evaluate, 'fixture.save', 'report.txt')).text,
        'saved chosen report.txt 11+12 closed; InvalidInput: the granted file is closed');
    assert.equal(await fs.readFile(target, 'utf8'), 'first line\nsecond line\n');

    step('open and read');
    const source = path.join(files, 'notes é.txt');
    await fs.writeFile(source, 'read in chunks of five ✓');
    script.push({ canceled: false, filePath: source });
    assert.deepEqual(await bridge(evaluate, 'fixture.open_read'),
        { status: 0, text: 'notes é.txt: read in chunks of five ✓; PermissionDenied: neura.desktop.file.write failed' });
    assert.equal(await fs.readFile(source, 'utf8'), 'read in chunks of five ✓');
    script.push({ canceled: true });
    assert.deepEqual(await bridge(evaluate, 'fixture.open_read'), { status: 0, text: 'cancelled' });

    step('menu');
    assert.deepEqual(await bridge(evaluate, 'fixture.menu_bad'),
        { status: DesktopStatus.Other, text: 'InvalidInput: neura.desktop.menu.set failed' });
    assert.equal(Menu.getApplicationMenu(), null, 'a refused menu replaced the menu');
    assert.deepEqual(await bridge(evaluate, 'fixture.menu'), { status: 0, text: 'menu set' });
    const menu = Menu.getApplicationMenu();
    assert.equal(menu.getMenuItemById('file.export').accelerator, 'CmdOrCtrl+E');
    assert.equal(menu.getMenuItemById('file.never').enabled, false);
    assert.equal(menu.getMenuItemById('file.never').label, 'Unavailable "quoted"');
    // A native item: MenuActivated for the child, with the focused window.
    first.window.focus();
    await waitFor(() => first.window.isFocused(), 'the window did not take focus');
    menu.getMenuItemById('file.export').click(undefined, first.window, first.window.webContents);
    await waitFor(() => childLog.includes('child: event MenuActivated file.export 1'), 'no MenuActivated');
    // A window item reaches the focused window's page through next_host_event().
    const next = evaluate('neuraDesktop.next_host_event()');
    menu.getMenuItemById('view.refresh').click(undefined, first.window, first.window.webContents);
    assert.deepEqual(await next, { kind: 'menu', id: 'view.refresh' });
    // Case 10: a window item with no focused window is dropped.
    menu.getMenuItemById('view.refresh').click(undefined, undefined, undefined);
    assert.ok(logged.some(line => /no focused window/.test(line)));
    // Case 9: the copy and paste roles work in a text input with no Neura code involved.
    await evaluate("document.getElementById('source').select()");
    menu.getMenuItemById('edit.copy').click(undefined, first.window, first.window.webContents);
    await waitFor(async () => (await clipboard.readText()) === 'copied by a role', 'the copy role did not copy');
    await evaluate("document.getElementById('target').focus()");
    menu.getMenuItemById('edit.paste').click(undefined, first.window, first.window.webContents);
    await waitFor(() => evaluate("document.getElementById('target').value === 'copied by a role'"), 'the paste role did not paste');

    expectEnd(session, 0, reported => assert.equal(reported.length, 0));
    first.window.close();
}

// Case 32 with the default failure report: the driver finds and dismisses the host's native error dialog.
async function trapDialog() {
    const session = await start({}, { defaultFailure: true });
    if (windowsHost) {
        console.log(`DIALOG_PID=${process.pid}`);
    } else {
        console.log(`DISPLAY=${process.env.DISPLAY}`);
        console.log(`XAUTHORITY=${process.env.XAUTHORITY ?? ''}`);
    }
    expectEnd(session, 1, reported => assert.equal(reported.length, 0));
    bridge(session.evaluate, 'fixture.trap').catch(() => {});
}

// Case 34: the test runner kills this process; it prints what the runner checks.
async function killed() {
    const { running, first } = await start();
    const renderer = first.window.webContents.getOSProcessId();
    console.log(`PIDS main=${process.pid} child=${running.child.pid} renderer=${renderer}`);
}

const cases = {
    'app-close': lifecycle, 'app-quit': lifecycle, 'app-trap': lifecycle, 'app-services': services, 'app-denied': denied,
    'app-windows': windows, 'app-dialogs': dialogs, 'app-trap-dialog': trapDialog,
    'app-killed': killed,
};

(async () => {
    assert.equal(process.versions.electron, '45.3.3');
    if (windowsHost) {
        assert.equal(process.arch, 'x64');
    } else {
        assert.equal(process.env.WAYLAND_DISPLAY, undefined);
        assert.equal(app.commandLine.getSwitchValue('ozone-platform'), 'x11');
    }
    await cases[caseName]();
})().catch(error => {
    console.error(error);
    app.exit(1);
});
