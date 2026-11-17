'use strict';

const { createHash, randomUUID } = require('node:crypto');
const { constants, promises: fs } = require('node:fs');
const path = require('node:path');

const manifestName = 'bundle.json';
const defaultFileLimit = 64 * 1024 * 1024;
const maxFileLimit = 1024 * 1024 * 1024;
const digestPattern = /^[0-9a-f]{64}$/;
const appIdPattern = /^(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/;
const mediaTypePattern = /^[a-z0-9!#$&^_.+-]+\/[a-z0-9!#$&^_.+-]+$/;
const fixedMediaTypes = new Map([
    ['.html', ['text/html']], ['.css', ['text/css']], ['.wasm', ['application/wasm']],
    ['.js', ['text/javascript', 'application/javascript']], ['.mjs', ['text/javascript', 'application/javascript']],
]);

function bundleError(reason) {
    return new Error(`Invalid web bundle: ${reason}`);
}

function record(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function safePath(relative) {
    return typeof relative === 'string' && relative.split('/').every(segment => (
        segment.length > 0 && !/[\\:*?"<>|\x00-\x1f\x7f]/.test(segment) && !/[. ]$/.test(segment) &&
        !/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(segment)
    ));
}

function contained(root, resolved) {
    const relative = path.relative(root, resolved);
    return relative !== '' && relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}

async function regularFile(root, name) {
    const parts = name.split('/');
    let current = root;
    for (const [index, part] of parts.entries()) {
        current = path.join(current, part);
        const entry = await fs.lstat(current);
        if (index === parts.length - 1 ? !entry.isFile() : !entry.isDirectory()) {
            throw bundleError(`expected a regular file without links at ${name}`);
        }
    }
    const resolved = await fs.realpath(current);
    if (!contained(root, resolved)) throw bundleError(`file escapes the web bundle: ${name}`);
    return resolved;
}

// Hash exactly the bytes later sent to Chromium. Startup uses bounded scratch
// space, and a response keeps only the bytes of the requested file.
async function readVerified(root, name, entry, limit, keepBytes) {
    try {
        const file = await regularFile(root, name);
        const flags = constants.O_RDONLY | (constants.O_NOFOLLOW ?? 0) | (constants.O_NONBLOCK ?? 0);
        const handle = await fs.open(file, flags);
        try {
            const metadata = await handle.stat();
            const size = metadata.size;
            if (!metadata.isFile() || size > limit || (entry.bytes !== undefined && size !== entry.bytes)) {
                throw bundleError(`wrong byte length or file type for ${name}`);
            }
            const buffer = Buffer.allocUnsafe(keepBytes ? size : Math.min(size, 64 * 1024));
            const hash = createHash('sha256');
            let position = 0;
            while (position < size) {
                const offset = keepBytes ? position : 0;
                const length = Math.min(size - position, buffer.length - offset);
                const { bytesRead } = await handle.read(buffer, offset, length, position);
                if (bytesRead === 0) throw bundleError(`file was truncated: ${name}`);
                hash.update(buffer.subarray(offset, offset + bytesRead));
                position += bytesRead;
            }
            if ((await handle.stat()).size !== size || hash.digest('hex') !== entry.sha256) {
                throw bundleError(`SHA-256 mismatch for ${name}`);
            }
            return keepBytes ? buffer : undefined;
        } finally {
            await handle.close();
        }
    } catch (error) {
        if (error?.code) throw bundleError(`could not read ${name} (${error.code})`);
        throw error;
    }
}

async function verifyTree(root, entries, remaining, limit, prefix = '') {
    for (const file of await fs.readdir(path.join(root, prefix), { withFileTypes: true })) {
        const name = `${prefix}${file.name}`;
        if (file.isDirectory()) {
            await verifyTree(root, entries, remaining, limit, `${name}/`);
        } else if (!file.isFile() || !remaining.delete(name)) {
            throw bundleError(`unlisted file or unsupported file type: ${name}`);
        } else if (name !== manifestName) {
            await readVerified(root, name, entries.get(name), limit, false);
        }
    }
}

function parseManifest(bytes) {
    try {
        return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
    } catch {
        throw bundleError('bundle.json is not a UTF-8 JSON document');
    }
}

async function verifyWebBundle({ directory, manifestDigest, applicationId, supportedRequirements = {} }) {
    if (typeof applicationId !== 'string' || !appIdPattern.test(applicationId)) throw bundleError('invalid application id');
    if (typeof manifestDigest !== 'string' || !digestPattern.test(manifestDigest)) throw bundleError('invalid bundle.json SHA-256');
    if (!record(supportedRequirements)) throw bundleError('invalid host requirement versions');
    if (!(await fs.lstat(directory)).isDirectory()) throw bundleError('bundle root must be a directory without links');
    const root = await fs.realpath(directory);
    const manifestEntry = { sha256: manifestDigest, media_type: 'application/json' };
    const bytes = await readVerified(root, manifestName, manifestEntry, defaultFileLimit, true);
    manifestEntry.bytes = bytes.length;
    const manifest = parseManifest(bytes);
    if (!record(manifest) || manifest.schema !== 1 || manifest.kind !== 'web' || manifest.id !== applicationId) {
        throw bundleError('expected a schema 1 web bundle for this application id');
    }
    if (manifest.dev !== undefined && manifest.dev !== false) throw bundleError('development web bundle');
    if (typeof manifest.csp !== 'string' || !/^[\x20-\x7e]+$/.test(manifest.csp) || !manifest.csp.trim()) {
        throw bundleError('missing or malformed content-security policy');
    }
    if (!record(manifest.requires)) throw bundleError('missing bundle requirements');
    for (const [axis, version] of Object.entries(manifest.requires)) {
        if (!Object.hasOwn(supportedRequirements, axis) || supportedRequirements[axis] !== version) {
            throw bundleError(`unsupported bundle requirement: ${axis}`);
        }
    }

    const limit = manifest.limits?.['W-1'] ?? defaultFileLimit;
    if (!Number.isSafeInteger(limit) || limit < 1 || limit > maxFileLimit) throw bundleError('invalid W-1 file limit');
    if (!Array.isArray(manifest.files)) throw bundleError('missing file table');
    const entries = new Map([[manifestName, manifestEntry]]);
    for (const entry of manifest.files) {
        if (!record(entry) || !safePath(entry.path) || typeof entry.sha256 !== 'string' || !digestPattern.test(entry.sha256) ||
            !Number.isSafeInteger(entry.bytes) || entry.bytes < 0 || entry.bytes > limit || typeof entry.initial !== 'boolean' ||
            typeof entry.media_type !== 'string' || !mediaTypePattern.test(entry.media_type)) {
            throw bundleError('invalid bundle file row');
        }
        if (entries.has(entry.path)) throw bundleError(`duplicate file: ${entry.path}`);
        const expectedTypes = fixedMediaTypes.get(path.posix.extname(entry.path).toLowerCase());
        if (expectedTypes && !expectedTypes.includes(entry.media_type)) throw bundleError(`wrong media type for ${entry.path}`);
        entries.set(entry.path, entry);
    }
    if (!entries.has('index.html')) throw bundleError('missing index.html');
    if (!record(manifest.image) || manifest.image.path === manifestName || !entries.has(manifest.image.path) ||
        entries.get(manifest.image.path).sha256 !== manifest.image.digest) {
        throw bundleError('image path or digest disagrees with the file table');
    }

    const remaining = new Set(entries.keys());
    await verifyTree(root, entries, remaining, limit);
    if (remaining.size) throw bundleError(`missing file: ${remaining.values().next().value}`);

    return Object.freeze({
        origin: `app://${applicationId}`,
        csp: manifest.csp,
        async readAsset(name) {
            const entry = entries.get(name);
            if (!entry) return null;
            const body = await readVerified(root, name, entry, name === manifestName ? defaultFileLimit : limit, true);
            return { body, mediaType: entry.media_type };
        },
    });
}

function parseUrl(text) {
    try { return new URL(text); } catch { return null; }
}

function ownOrigin(url, applicationId) {
    // Node does not know Chromium's privileged app scheme. Compare its parsed
    // protocol and authority rather than using Node's URL.origin for app URLs.
    return url?.protocol === 'app:' && url.hostname === applicationId && !url.port && !url.username && !url.password;
}

function externalAllowlist(origins) {
    if (!Array.isArray(origins)) throw new Error('Invalid desktop external origins');
    return new Set(origins.map(origin => {
        const url = typeof origin === 'string' && /^https:\/\/[^\s/?#\\]+\/?$/i.test(origin) && parseUrl(origin);
        if (!url || url.href !== `${url.origin}/`) throw new Error('An external origin must be an HTTPS origin');
        return url.origin;
    }));
}

function assetPath(url) {
    if (url.pathname === '/') return 'index.html';
    try {
        const parts = url.pathname.slice(1).split('/').map(decodeURIComponent);
        if (parts.some(part => part.includes('/') || !safePath(part))) return null;
        return parts.join('/');
    } catch {
        return null;
    }
}

const failurePage = '<!doctype html><html lang="en"><meta charset="utf-8"><title>Application unavailable</title>' +
    '<h1>Application unavailable</h1><p>The web bundle failed verification.</p></html>';
const failureCsp = "default-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

function response(body, status, csp, mediaType = 'text/plain', extra = {}) {
    return new Response(body, { status, headers: {
        'Content-Type': mediaType,
        'Content-Security-Policy': csp,
        'X-Content-Type-Options': 'nosniff',
        'Cache-Control': 'no-store',
        ...extra,
    } });
}

let schemeRegistered = false;

// The trusted entry can register before loading package metadata asynchronously.
function registerDesktopScheme() {
    if (schemeRegistered) return;
    const { app, protocol } = require('electron');
    if (app.isReady()) throw new Error('Register the desktop app scheme before Electron is ready');
    if (app.commandLine.hasSwitch('no-sandbox')) throw new Error('The Neura desktop shell requires the renderer sandbox');
    app.enableSandbox();
    protocol.registerSchemesAsPrivileged([{ scheme: 'app', privileges: {
        standard: true, secure: true, supportFetchAPI: true, corsEnabled: true,
    } }]);
    schemeRegistered = true;
}

// The package loader supplies the expected digest, id and desktop origins.
async function launchDesktopShell(configuration, { openExternal, log = console.warn } = {}) {
    registerDesktopScheme();
    const { app, session, BrowserWindow, shell } = require('electron');

    const allowed = externalAllowlist(configuration.externalOrigins ?? []);
    const applicationId = configuration.applicationId;
    const bundle = await verifyWebBundle(configuration);
    await app.whenReady();
    const isolated = session.fromPartition(`neura-desktop-${randomUUID()}`, { cache: false });
    isolated.setPermissionCheckHandler(() => false);
    isolated.setPermissionRequestHandler((_contents, _permission, reply) => reply(false));
    isolated.setDevicePermissionHandler(() => false);
    isolated.setDisplayMediaRequestHandler((_request, reply) => reply({}));
    isolated.on('will-download', event => event.preventDefault());

    const home = `${bundle.origin}/`;
    let window;
    let loaded = false;
    let integrityFailure = false;
    isolated.protocol.handle('app', async request => {
        const url = parseUrl(request.url);
        const head = request.method === 'HEAD';
        if (!ownOrigin(url, applicationId)) return response(head ? null : 'Forbidden', 403, failureCsp);
        if (request.method !== 'GET' && request.method !== 'HEAD') {
            return response('Method not allowed', 405, failureCsp, 'text/plain', { Allow: 'GET, HEAD' });
        }
        if (integrityFailure) {
            return url.pathname === '/'
                ? response(head ? null : failurePage, 200, failureCsp, 'text/html; charset=utf-8')
                : response(head ? null : 'Application unavailable', 503, failureCsp);
        }
        const name = assetPath(url);
        try {
            const asset = name && await bundle.readAsset(name);
            if (integrityFailure) return response(head ? null : 'Application unavailable', 503, failureCsp);
            if (!asset) return response(head ? null : 'Not found', 404, bundle.csp);
            return response(head ? null : asset.body, 200, bundle.csp, asset.mediaType, {
                'Content-Length': `${asset.body.length}`,
            });
        } catch (error) {
            if (!integrityFailure) {
                integrityFailure = true;
                log(error.message);
                if (loaded && !window.isDestroyed()) window.loadURL(home).catch(() => {});
            }
            return response(head ? null : 'Application unavailable', 500, failureCsp);
        }
    });

    // Browser-session enforcement also covers navigations begun by loadURL.
    isolated.webRequest.onBeforeRequest((details, reply) => {
        const frame = details.resourceType === 'mainFrame' || details.resourceType === 'subFrame';
        reply({ cancel: frame && !ownOrigin(parseUrl(details.url), applicationId) });
    });

    const dispatch = openExternal ?? (url => shell.openExternal(url));
    window = new BrowserWindow({ show: false, webPreferences: {
        session: isolated,
        preload: path.join(__dirname, 'preload.js'),
        additionalArguments: [`--neura-app-origin=${bundle.origin}`],
        sandbox: true,
        contextIsolation: true,
        webSecurity: true,
        allowRunningInsecureContent: false,
        nodeIntegration: false,
        nodeIntegrationInSubFrames: false,
        nodeIntegrationInWorker: false,
        webviewTag: false,
        devTools: false,
    } });
    window.webContents.setWindowOpenHandler(() => {
        log('Blocked desktop popup');
        return { action: 'deny' };
    });
    window.webContents.on('will-attach-webview', event => event.preventDefault());
    const guardNavigation = event => {
        const url = parseUrl(event.url);
        if (ownOrigin(url, applicationId)) return;
        event.preventDefault();
        if (event.isMainFrame && url?.protocol === 'https:' && !url.username && !url.password && allowed.has(url.origin)) {
            Promise.resolve().then(() => dispatch(url.href)).catch(() => log('Could not open desktop external link'));
        } else {
            log('Blocked desktop navigation outside the application origin');
        }
    };
    window.webContents.on('will-frame-navigate', guardNavigation);
    window.webContents.on('will-redirect', guardNavigation);
    let lastLocalPage = home;
    const rememberLocalPage = url => {
        if (ownOrigin(parseUrl(url), applicationId)) lastLocalPage = url;
    };
    window.webContents.on('did-navigate', (_event, url) => rememberLocalPage(url));
    window.webContents.on('did-navigate-in-page', (_event, url, isMainFrame) => {
        if (isMainFrame) rememberLocalPage(url);
    });
    window.webContents.on('did-fail-load', (_event, _code, _description, url, isMainFrame) => {
        if (!isMainFrame || !loaded || ownOrigin(parseUrl(url), applicationId)) return;
        // Chromium can commit an error document after cancelling a navigation
        // begun by trusted main-process code. Restore the last local document.
        setImmediate(() => {
            if (!window.isDestroyed() && !ownOrigin(parseUrl(window.webContents.getURL()), applicationId)) {
                window.loadURL(lastLocalPage).catch(() => {});
            }
        });
    });

    try {
        await window.loadURL(home).catch(error => {
            if (!integrityFailure) throw error;
        });
        if (integrityFailure) await window.loadURL(home);
    } catch (error) {
        if (!window.isDestroyed()) window.destroy();
        throw error;
    }
    loaded = true;
    window.show();
    return window;
}

module.exports = { launchDesktopShell, registerDesktopScheme, verifyWebBundle };
