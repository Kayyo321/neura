'use strict';

const { createHash, randomUUID } = require('node:crypto');
const { constants, promises: fs } = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');

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

function escapeHtml(text) {
    return text.replace(/[&<>"']/g, character => `&#${character.charCodeAt(0)};`);
}

// Shown once after a window's renderer ends abnormally. The link reloads the
// window's start route, which is a fresh execution of the bundle.
function stoppedPage(start) {
    return '<!doctype html><html lang="en"><meta charset="utf-8"><title>Window stopped</title>' +
        '<h1>This window stopped</h1><p>Its page ended unexpectedly. The application is still running.</p>' +
        `<p><a id="neura-reload" href="${escapeHtml(start)}">Reload</a></p></html>`;
}

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

const VERSION = 1;
const NativeFrameKind = Object.freeze({ Hello: 1, Call: 2, Return: 3, Cancel: 4, HostCall: 5, HostReturn: 6, Event: 7 });
const headerBytes = 4;
const envelopeBytes = headerBytes + 1;
const helloBytes = 4 + 32;
const defaultFrameLimit = 1024 * 1024;
const hardFrameLimit = 64 * 1024 * 1024;
const maxShutdownGrace = 5000;
const outbound = new Set([NativeFrameKind.Call, NativeFrameKind.Cancel, NativeFrameKind.HostReturn, NativeFrameKind.Event]);
const inbound = new Set([NativeFrameKind.Return, NativeFrameKind.HostCall]);

class NativeChannelError extends Error {
    constructor(code, message, options) {
        super(message, options);
        this.name = 'NativeChannelError';
        this.code = code;
    }
}

function protocolError(message) {
    return new NativeChannelError('Protocol', message);
}

function frame(kind, body) {
    const bytes = Buffer.allocUnsafe(envelopeBytes + body.byteLength);
    bytes.writeUInt32LE(1 + body.byteLength);
    bytes[headerBytes] = kind;
    bytes.set(body, envelopeBytes);
    return bytes;
}

// Only the current frame is retained across reads. The length and kind are
// checked before allocating any storage for its body.
class FrameReader {
    #header = Buffer.allocUnsafe(headerBytes);
    #headerUsed = 0;
    #remaining = 0;
    #kind = 0;
    #body;
    #used = 0;
    #limit;

    constructor(limit) {
        this.#limit = limit;
    }

    *read(chunk, validateKind) {
        let offset = 0;
        while (offset < chunk.length) {
            if (this.#headerUsed < headerBytes) {
                const count = Math.min(headerBytes - this.#headerUsed, chunk.length - offset);
                chunk.copy(this.#header, this.#headerUsed, offset, offset + count);
                offset += count;
                this.#headerUsed += count;
                if (this.#headerUsed < headerBytes) continue;
                this.#remaining = this.#header.readUInt32LE();
                if (this.#remaining === 0) throw protocolError('Native child frame has no kind');
                if (this.#remaining > this.#limit - headerBytes) {
                    throw new NativeChannelError('TooLarge', `Native child frame exceeds ${this.#limit} bytes`);
                }
            }
            if (this.#body === undefined) {
                if (offset === chunk.length) break;
                this.#kind = chunk[offset++];
                validateKind(this.#kind, this.#remaining - 1);
                this.#body = Buffer.allocUnsafe(this.#remaining - 1);
            }
            const count = Math.min(this.#body.length - this.#used, chunk.length - offset);
            chunk.copy(this.#body, this.#used, offset, offset + count);
            offset += count;
            this.#used += count;
            if (this.#used !== this.#body.length) continue;
            const message = { kind: this.#kind, body: this.#body };
            this.#headerUsed = 0;
            this.#body = undefined;
            this.#used = 0;
            yield message;
        }
    }

    finish() {
        if (this.#headerUsed !== 0) throw protocolError('Native child stdout ended in a partial frame');
    }
}

function hello(digest) {
    const body = Buffer.allocUnsafe(helloBytes);
    body.writeUInt32LE(VERSION);
    body.set(Buffer.from(digest, 'hex'), 4);
    return frame(NativeFrameKind.Hello, body);
}

function checkHello(body, expected) {
    const version = body.readUInt32LE();
    if (version !== VERSION) throw protocolError(`Native child bridge version ${version}, expected ${VERSION}`);
    const digest = body.toString('hex', 4);
    if (digest !== expected) throw protocolError(`Native child registry SHA-256 ${digest}, expected ${expected}`);
}

function checkTimeout(value, maximum, name) {
    if (!Number.isInteger(value) || value < 1 || value > maximum) throw new RangeError(`Invalid ${name}`);
}

// This channel belongs to Electron's trusted main process. Configuration is
// supplied by the bootstrap that checked the package, never by a web asset
// or renderer. The body of each application message is owned by its caller.
class NativeChildChannel {
    #child;
    #limit;
    #digest;
    #reader;
    #onFrame;
    #ready = Promise.withResolvers();
    #closed = Promise.withResolvers();
    #state = 'starting';
    #startupTimer;
    #stopTimer;
    #grace;
    #helloWrite;
    #writing = false;
    #failure = null;

    constructor({ executable, args = [], environment, registryDigest, frameLimit = defaultFrameLimit }, {
        onFrame = () => { throw protocolError('Native child application frames have no handler'); },
        onStderr, handshakeTimeout = 5000, shutdownGrace = maxShutdownGrace,
    } = {}) {
        if (typeof executable !== 'string' || !path.isAbsolute(executable)) {
            throw new TypeError('The native child executable must be an absolute trusted path');
        }
        if (!Array.isArray(args) || !args.every(arg => typeof arg === 'string')) {
            throw new TypeError('The native child arguments must be strings');
        }
        if (typeof registryDigest !== 'string' || !digestPattern.test(registryDigest)) {
            throw new TypeError('Invalid native child registry SHA-256');
        }
        if (!Number.isInteger(frameLimit) || frameLimit < envelopeBytes + helloBytes || frameLimit > hardFrameLimit) {
            throw new RangeError('Invalid native child frame limit');
        }
        if (typeof onFrame !== 'function' || (onStderr !== undefined && typeof onStderr !== 'function')) {
            throw new TypeError('Invalid native child callback');
        }
        checkTimeout(handshakeTimeout, 0x7fffffff, 'native child handshake timeout');
        checkTimeout(shutdownGrace, maxShutdownGrace, 'native child shutdown grace');
        // A caller may abandon startup and await only close().
        this.#ready.promise.catch(() => {});
        this.#limit = frameLimit;
        this.#digest = registryDigest;
        this.#reader = new FrameReader(frameLimit);
        this.#onFrame = onFrame;
        this.#grace = shutdownGrace;
        const child = spawn(executable, args, {
            cwd: path.dirname(executable), env: environment, shell: false, windowsHide: true,
            stdio: ['pipe', 'pipe', 'pipe'],
        });
        this.#child = child;
        child.on('error', cause => this.#stop(new NativeChannelError('ChildProcess', 'Native child process failed', { cause })));
        child.stdin.on('error', cause => this.#stop(new NativeChannelError('Pipe', 'Native child stdin failed', { cause })));
        child.stderr.on('error', cause => this.#stop(new NativeChannelError('Pipe', 'Native child stderr failed', { cause })));
        child.once('exit', () => this.#armStop());
        child.once('close', (code, signal) => this.#finish(code, signal));
        if (onStderr) {
            child.stderr.on('data', chunk => {
                try {
                    onStderr(chunk);
                } catch (cause) {
                    this.#stop(new NativeChannelError('Log', 'Native child log handler failed', { cause }));
                }
            });
        } else {
            child.stderr.pipe(process.stderr, { end: false });
        }
        this.#startupTimer = setTimeout(() => this.#stop(
            new NativeChannelError('StartupTimeout', 'Native child did not complete Hello before the deadline')),
        handshakeTimeout);
        this.#helloWrite = this.#write(hello(registryDigest));
        this.#helloWrite.catch(error => this.#stop(error));
        this.#read().catch(error => this.#stop(error));
    }

    get ready() { return this.#ready.promise; }
    get closed() { return this.#closed.promise; }
    get active() { return this.#state === 'ready'; }
    get pid() { return this.#child.pid; }

    async send(kind, body) {
        if (!this.active) throw new NativeChannelError('NotReady', 'Native child channel is not ready');
        if (!outbound.has(kind) || !(body instanceof Uint8Array)) throw protocolError('Invalid native child output frame');
        if (body.byteLength > this.#limit - envelopeBytes) {
            throw new NativeChannelError('TooLarge', `Native child frame exceeds ${this.#limit} bytes`);
        }
        // A completed write has entered the pipe. A reply can remain pending
        // while subsequent writes proceed. Never queue a second frame behind
        // a write which the pipe has not accepted.
        if (this.#writing) throw new NativeChannelError('PipeBusy', 'Native child pipe is busy');
        return this.#write(frame(kind, body));
    }

    close() {
        this.#stop(null);
        return this.closed;
    }

    // Ends the channel as failed: the child is killed and `closed` reports `error`.
    abort(error) {
        this.#stop(error);
        return this.closed;
    }

    #write(bytes) {
        this.#writing = true;
        return new Promise((resolve, reject) => {
            this.#child.stdin.write(bytes, cause => {
                this.#writing = false;
                if (cause) {
                    const error = new NativeChannelError('Pipe', 'Could not write native child frame', { cause });
                    this.#stop(error);
                    reject(error);
                } else {
                    resolve();
                }
            });
        });
    }

    async #read() {
        const validate = (kind, size) => {
            if (this.#state === 'starting') {
                if (kind !== NativeFrameKind.Hello) throw protocolError('The first native child frame must be Hello');
                if (size !== helloBytes) throw protocolError('Native child Hello has the wrong size');
            } else if (!inbound.has(kind)) {
                throw protocolError(`Unexpected native child frame kind ${kind}`);
            }
        };
        for await (const chunk of this.#child.stdout) {
            if (this.#state !== 'starting' && !this.active) continue;
            for (const message of this.#reader.read(chunk, validate)) {
                if (this.#state === 'starting') {
                    checkHello(message.body, this.#digest);
                    await this.#helloWrite;
                    if (this.#state !== 'starting') break;
                    clearTimeout(this.#startupTimer);
                    this.#state = 'ready';
                    this.#ready.resolve(this);
                } else if (this.active) {
                    await this.#onFrame(message);
                }
                if (!this.active) break;
            }
        }
        if (this.#state !== 'starting' && !this.active) return;
        this.#reader.finish();
        this.#stop(new NativeChannelError('ChildExited', 'Native child closed stdout unexpectedly'));
    }

    #armStop() {
        if (this.#stopTimer !== undefined) return;
        this.#stopTimer = setTimeout(() => {
            this.#kill();
            this.#child.stdin.destroy();
            this.#child.stdout.destroy();
            this.#child.stderr.destroy();
        }, this.#grace);
    }

    #kill() {
        if (this.#child.pid !== undefined && this.#child.exitCode === null && this.#child.signalCode === null) {
            this.#child.kill('SIGKILL');
        }
    }

    #stop(error) {
        if (this.#state === 'closing' || this.#state === 'closed') return;
        if (this.#state === 'starting') this.#ready.reject(error ?? new NativeChannelError('Closed', 'Native child closed before Hello'));
        this.#state = 'closing';
        this.#failure = error;
        clearTimeout(this.#startupTimer);
        this.#armStop();
        if (error) {
            this.#kill();
            this.#child.stdin.destroy();
            this.#child.stdout.destroy();
        } else {
            this.#child.stdin.end();
        }
    }

    #finish(code, signal) {
        if (this.#state === 'starting' || this.active) {
            const error = new NativeChannelError('ChildExited', `Native child exited unexpectedly (${signal ?? code})`);
            if (this.#state === 'starting') this.#ready.reject(error);
            this.#failure = error;
        }
        this.#state = 'closed';
        clearTimeout(this.#startupTimer);
        clearTimeout(this.#stopTimer);
        this.#closed.resolve(Object.freeze({ error: this.#failure, code, signal }));
    }
}

function ownNativeChild(app, child, reportFailure) {
    let window;
    let active = true;
    let quitRequested = false;
    const beforeQuit = event => {
        event.preventDefault();
        quitRequested = true;
        child.close();
    };
    // Registering this listener keeps Electron's default quit from racing
    // the child reap or turning a native failure into a normal exit.
    const allWindowsClosed = () => child.close();
    const detach = () => {
        app.off('before-quit', beforeQuit);
        app.off('window-all-closed', allWindowsClosed);
    };
    app.on('before-quit', beforeQuit);
    app.on('window-all-closed', allWindowsClosed);
    child.closed.then(result => {
        if (!active || (!window && !quitRequested)) return;
        active = false;
        try {
            if (result.error) {
                if (window && !window.isDestroyed()) window.destroy();
                reportFailure(result.error, result);
            }
        } finally {
            detach();
            app.exit(result.error ? 1 : 0);
        }
    });
    return {
        attach(created) { window = created; },
        abort() {
            active = false;
            if (window && !window.isDestroyed()) window.destroy();
            detach();
            return child.close();
        },
    };
}

// The package loader supplies the expected digest, id and desktop origins.
// It may also supply nativeChild after checking the desktop bundle and
// runner paths. This argument stays in the trusted main process.
async function launchDesktopShell(configuration, {
    openExternal, log = console.warn, nativeChild, nativeIO, onNativeFailure,
} = {}) {
    registerDesktopScheme();
    const { app, dialog } = require('electron');
    const allowed = externalAllowlist(configuration.externalOrigins ?? []);
    const applicationId = configuration.applicationId;
    const bundle = await verifyWebBundle(configuration);
    await app.whenReady();
    const windowOptions = { openExternal, log };
    if (!nativeChild) return createDesktopWindow(bundle, applicationId, allowed, windowOptions);

    const reportFailure = onNativeFailure ?? (error => dialog.showErrorBox('Neura desktop application ended', error.message));
    const child = new NativeChildChannel(nativeChild, nativeIO);
    const lifetime = ownNativeChild(app, child, reportFailure);
    try {
        await child.ready;
        if (!child.active) {
            const { error } = await child.closed;
            throw error ?? new Error('Native child closed during desktop startup');
        }
    } catch (error) {
        const result = await lifetime.abort();
        if (result.error) reportFailure(result.error, result);
        throw error;
    }
    try {
        return await createDesktopWindow(bundle, applicationId, allowed, { ...windowOptions, onWindow: lifetime.attach });
    } catch (error) {
        await lifetime.abort();
        throw error;
    }
}

async function createDesktopWindow(bundle, applicationId, allowed, {
    openExternal, log, onWindow, onRendererGone, frame = {},
}) {
    const { session, BrowserWindow, shell } = require('electron');
    const isolated = session.fromPartition(`neura-desktop-${randomUUID()}`, { cache: false });
    isolated.setPermissionCheckHandler(() => false);
    isolated.setPermissionRequestHandler((_contents, _permission, reply) => reply(false));
    isolated.setDevicePermissionHandler(() => false);
    isolated.setDisplayMediaRequestHandler((_request, reply) => reply({}));
    isolated.on('will-download', event => event.preventDefault());

    const home = `${bundle.origin}/`;
    const start = frame.route === undefined ? home : new URL(frame.route, home).href;
    if (!ownOrigin(parseUrl(start), applicationId)) throw new Error('A window route must stay in the application origin');
    let window;
    let loaded = false;
    let integrityFailure = false;
    let rendererStopped = false;
    isolated.protocol.handle('app', async request => {
        const url = parseUrl(request.url);
        const head = request.method === 'HEAD';
        if (!ownOrigin(url, applicationId)) return response(head ? null : 'Forbidden', 403, failureCsp);
        if (request.method !== 'GET' && request.method !== 'HEAD') {
            return response('Method not allowed', 405, failureCsp, 'text/plain', { Allow: 'GET, HEAD' });
        }
        if (rendererStopped && !integrityFailure) {
            // The first document after a renderer ended is the static failure page.
            rendererStopped = false;
            return response(head ? null : stoppedPage(start), 200, failureCsp, 'text/html; charset=utf-8');
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
    const size = {};
    for (const name of ['width', 'height', 'minWidth', 'minHeight']) {
        if (frame[name]) size[name] = frame[name];
    }
    if (frame.title !== undefined) size.title = frame.title;
    window = new BrowserWindow({ show: false, ...size, webPreferences: {
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
    onWindow?.(window);
    // The native child owns a titled window's title; the page cannot replace it.
    if (frame.title !== undefined) window.on('page-title-updated', event => event.preventDefault());
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
    let lastLocalPage = start;
    const rememberLocalPage = url => {
        if (ownOrigin(parseUrl(url), applicationId)) lastLocalPage = url;
    };
    window.webContents.on('did-navigate', (_event, url) => rememberLocalPage(url));
    window.webContents.on('did-navigate-in-page', (_event, url, isMainFrame) => {
        if (isMainFrame) rememberLocalPage(url);
    });
    // A renderer that ends abnormally (a crash, a kill, or in future its NVM's
    // trap) ends that window's page and state only. The window shows the
    // failure page; its calls in flight are the caller's to cancel.
    window.webContents.on('render-process-gone', (_event, details) => {
        if (details.reason === 'clean-exit' || window.isDestroyed()) return;
        log(`Desktop window renderer ended: ${details.reason}`);
        onRendererGone?.(details);
        rendererStopped = true;
        window.loadURL(start).catch(() => {});
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
        await window.loadURL(start).catch(error => {
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

// Status codes of a bridge call and of a host service answer: the host
// service error table of Stdlib.md section 4.5 with the 0.3.0 additions of
// Backend030 (16 TooLarge, 17 Busy). 0 is success.
const DesktopStatus = Object.freeze({
    Ok: 0, NotFound: 1, PermissionDenied: 2, InvalidInput: 4, TimedOut: 12, Unsupported: 13,
    CapabilityDenied: 14, Cancelled: 15, TooLarge: 16, Busy: 17, Other: 100,
});
const NotifyOutcome = Object.freeze({ Shown: 0, Denied: 1, Unsupported: 2 });
const EventKind = Object.freeze({ Closed: 0, Focused: 1, Blurred: 2, Resized: 3, MenuActivated: 4 });
// Apps030 allows D-1 up to 64 and D-3 up to 64 MiB, but Std.Desktop keeps
// the defaults (8 windows, 1 MiB frames), so a package may only lower them
// until the child learns the package's limits.
const desktopLimitTable = Object.freeze({
    'D-1': { fallback: 8, maximum: 8 },
    'D-2': { fallback: 32, maximum: 1024 },
    'D-3': { fallback: defaultFrameLimit, maximum: defaultFrameLimit },
    'D-4': { fallback: 30, maximum: 3600 },
});
const operationPattern = /^[a-z0-9._-]{1,128}$/;
const callHeaderBytes = 4 + 4 + 8 + 4;

// The package's D-1 to D-4, each defaulted and checked against its ceiling.
function desktopLimits(limits = {}) {
    if (!record(limits)) throw new Error('Invalid desktop limits');
    const value = {};
    for (const [name, { fallback, maximum }] of Object.entries(desktopLimitTable)) {
        const chosen = limits[name] ?? fallback;
        if (!Number.isInteger(chosen) || chosen < 1 || chosen > maximum) throw new RangeError(`Invalid desktop limit ${name}`);
        value[name] = chosen;
    }
    for (const name of Object.keys(limits)) {
        if (!Object.hasOwn(desktopLimitTable, name)) throw new RangeError(`Unknown desktop limit ${name}`);
    }
    if (value['D-3'] < envelopeBytes + helloBytes) throw new RangeError('Invalid desktop limit D-3');
    return Object.freeze({
        windows: value['D-1'], inFlight: value['D-2'], frameBytes: value['D-3'], deadlineMs: value['D-4'] * 1000,
    });
}

// registry.json is canonical: {"operations":[...],"schema":1} with distinct
// identifiers in ascending byte order and no spaces. Std.Desktop.Registry
// writes the same text, so both sides hash the same bytes.
function parseRegistry(bytes) {
    let document;
    let text;
    try {
        text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
        document = JSON.parse(text);
    } catch {
        throw new Error('registry.json is not a UTF-8 JSON document');
    }
    if (!record(document) || document.schema !== 1 || !Array.isArray(document.operations)) {
        throw new Error('registry.json is not a schema 1 registry');
    }
    const { operations } = document;
    for (const [index, id] of operations.entries()) {
        if (typeof id !== 'string' || !operationPattern.test(id)) throw new Error('registry.json has an invalid operation id');
        if (index > 0 && !(operations[index - 1] < id)) throw new Error('registry.json operations are not distinct and sorted');
    }
    if (JSON.stringify({ operations, schema: 1 }) !== text) throw new Error('registry.json is not in canonical form');
    return Object.freeze({
        digest: createHash('sha256').update(bytes).digest('hex'),
        operations: Object.freeze(new Set(operations)),
    });
}

function u32(value) {
    const bytes = Buffer.allocUnsafe(4);
    bytes.writeUInt32LE(value);
    return bytes;
}

function wireText(value) {
    const bytes = Buffer.from(value, 'utf8');
    return Buffer.concat([u32(bytes.length), bytes]);
}

// Reads a frame body. A read past the end or invalid UTF-8 throws.
class BodyCursor {
    #body;
    #offset = 0;

    constructor(body) { this.#body = body; }

    u32() {
        if (this.#offset + 4 > this.#body.length) throw protocolError('Truncated native child frame');
        const value = this.#body.readUInt32LE(this.#offset);
        this.#offset += 4;
        return value;
    }

    text() {
        const count = this.u32();
        if (count > this.#body.length - this.#offset) throw protocolError('Truncated native child text');
        const bytes = this.#body.subarray(this.#offset, this.#offset + count);
        this.#offset += count;
        try {
            return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
        } catch {
            throw protocolError('Native child text is not UTF-8');
        }
    }

    rest() {
        const value = Buffer.from(this.#body.subarray(this.#offset));
        this.#offset = this.#body.length;
        return value;
    }

    end() {
        if (this.#offset !== this.#body.length) throw protocolError('Native child frame has trailing bytes');
    }
}

// Serializes frames into a channel that accepts one pipe write at a time. The
// queue is bounded by the callers: D-1 windows of D-2 calls, their cancels,
// one host answer per child request, and window events.
class FrameQueue {
    #channel;
    #pending = [];
    #pumping = false;
    #idle = Promise.withResolvers();

    constructor(channel) {
        this.#channel = channel;
        this.#idle.resolve();
    }

    send(kind, body) {
        const entry = { kind, body, done: Promise.withResolvers() };
        this.#pending.push(entry);
        if (!this.#pumping) this.#pump();
        return entry.done.promise;
    }

    drained() { return this.#idle.promise; }

    async #pump() {
        this.#pumping = true;
        this.#idle = Promise.withResolvers();
        while (this.#pending.length) {
            const entry = this.#pending.shift();
            try {
                await this.#channel.send(entry.kind, entry.body);
                entry.done.resolve();
            } catch (error) {
                entry.done.reject(error);
                for (const rest of this.#pending.splice(0)) rest.done.reject(error);
            }
        }
        this.#pumping = false;
        this.#idle.resolve();
    }
}

// The host half of the bridge: the registry, D-2 and D-3 checks, request
// identifiers, D-4 deadlines and cancellation. A call that fails a check never
// reaches the child. A reply after its call ended is dropped.
class BridgeRouter {
    #queue;
    #registry;
    #limits;
    #log;
    #nextId = 1;
    #pending = new Map();
    #open = true;

    constructor({ queue, registry, limits, log = () => {} }) {
        this.#queue = queue;
        this.#registry = registry;
        this.#limits = limits;
        this.#log = log;
    }

    inFlight(window) {
        let count = 0;
        for (const call of this.#pending.values()) if (call.window === window) count += 1;
        return count;
    }

    call(window, operationId, request) {
        if (!this.#open) return Promise.resolve({ status: DesktopStatus.Cancelled });
        if (typeof operationId !== 'string' || !this.#registry.operations.has(operationId)) {
            this.#log('Refused a bridge call for an operation outside registry.json');
            return Promise.resolve({ status: DesktopStatus.NotFound });
        }
        if (!(request instanceof Uint8Array)) return Promise.resolve({ status: DesktopStatus.InvalidInput });
        const operation = wireText(operationId);
        if (envelopeBytes + callHeaderBytes + operation.length - 4 + request.byteLength > this.#limits.frameBytes) {
            return Promise.resolve({ status: DesktopStatus.TooLarge });
        }
        if (this.inFlight(window) >= this.#limits.inFlight) return Promise.resolve({ status: DesktopStatus.Busy });
        const id = this.#nextId;
        this.#nextId = id === 0xffffffff ? 1 : id + 1;
        const result = Promise.withResolvers();
        const timer = setTimeout(() => this.#cancel(id, DesktopStatus.TimedOut), this.#limits.deadlineMs);
        this.#pending.set(id, { window, resolve: result.resolve, timer });
        const deadline = Buffer.allocUnsafe(8);
        deadline.writeBigInt64LE(BigInt(this.#limits.deadlineMs) * 1000000n);
        const body = Buffer.concat([u32(id), u32(window), deadline, operation, request]);
        this.#queue.send(NativeFrameKind.Call, body).catch(() => this.#finish(id, { status: DesktopStatus.Other }));
        return result.promise;
    }

    onReturn(body) {
        const cursor = new BodyCursor(body);
        const id = cursor.u32();
        const status = cursor.u32();
        const reply = cursor.rest();
        if (!this.#finish(id, { status, bytes: reply })) {
            this.#log('Dropped a late or unknown bridge reply');
        }
    }

    closeWindow(window) {
        for (const [id, call] of [...this.#pending]) {
            if (call.window === window) this.#cancel(id, DesktopStatus.Cancelled);
        }
    }

    close() {
        this.#open = false;
        for (const id of [...this.#pending.keys()]) this.#finish(id, { status: DesktopStatus.Cancelled });
    }

    #cancel(id, status) {
        if (this.#finish(id, { status }) && this.#open) {
            this.#queue.send(NativeFrameKind.Cancel, u32(id)).catch(() => {});
        }
    }

    #finish(id, outcome) {
        const call = this.#pending.get(id);
        if (!call) return false;
        this.#pending.delete(id);
        clearTimeout(call.timer);
        call.resolve(outcome);
        return true;
    }
}

// Answers the child's HostCall frames. `services` maps a service name to an
// async function of a BodyCursor that returns the answer bytes or throws a
// DesktopServiceError. `neura.desktop.quit` is answered by ending the channel.
class DesktopServiceError extends Error {
    constructor(status, message) {
        super(message);
        this.name = 'DesktopServiceError';
        this.status = status;
    }
}

async function answerHostCall(queue, services, body, log) {
    const cursor = new BodyCursor(body);
    const id = cursor.u32();
    const name = cursor.text();
    let status = DesktopStatus.Ok;
    let answer = Buffer.alloc(0);
    const service = Object.hasOwn(services, name) ? services[name] : undefined;
    if (!service) {
        status = DesktopStatus.Unsupported;
    } else {
        try {
            const result = await service(cursor);
            if (result === null) return;
            answer = result ?? answer;
        } catch (error) {
            if (error instanceof NativeChannelError) throw error;
            status = error instanceof DesktopServiceError ? error.status : DesktopStatus.Other;
            if (!(error instanceof DesktopServiceError)) log(`Desktop host service ${name} failed: ${error.message}`);
        }
    }
    await queue.send(NativeFrameKind.HostReturn, Buffer.concat([u32(id), u32(status), answer]));
}

function eventBody(kind, window, first = 0, second = 0, identifier = '') {
    const values = Buffer.allocUnsafe(16);
    values.writeBigInt64LE(BigInt(first));
    values.writeBigInt64LE(BigInt(second), 8);
    return Buffer.concat([u32(kind), u32(window), values, wireText(identifier)]);
}

function dataDirectory(applicationId) {
    const os = require('node:os');
    if (process.platform === 'win32') return path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData', 'Local'), applicationId);
    if (process.platform === 'darwin') return path.join(os.homedir(), 'Library', 'Application Support', applicationId);
    const base = process.env.XDG_DATA_HOME && path.isAbsolute(process.env.XDG_DATA_HOME)
        ? process.env.XDG_DATA_HOME : path.join(os.homedir(), '.local', 'share');
    return path.join(base, applicationId);
}

let bridgeOwners;

// One handler serves every window. A sender that is not the main frame of a
// window the runner created, or whose frame left the application origin, is
// refused before the router sees the call.
function registerBridgeIpc(ipcMain) {
    if (bridgeOwners) return bridgeOwners;
    bridgeOwners = new Map();
    ipcMain.handle('neura:bridge-call', (event, operationId, request) => {
        const owner = bridgeOwners.get(event.sender.id);
        if (!owner) throw new Error('The Neura desktop bridge is unavailable');
        return owner(event, operationId, request);
    });
    return bridgeOwners;
}

// Runs a desktop application: verifies the web bundle and the registry, starts
// the native child, waits for Hello, and then lets the child open windows and
// serve bridge calls. Returns once the child is connected. The configuration
// comes from the trusted package bootstrap, never from a renderer.
async function runDesktopApplication(configuration, {
    openExternal, log = console.warn, nativeIO = {}, onNativeFailure, clipboard: clipboardOverride,
    notify: notifyOverride,
} = {}) {
    registerDesktopScheme();
    const { app, dialog, ipcMain } = require('electron');
    const applicationId = configuration.applicationId;
    const allowed = externalAllowlist(configuration.externalOrigins ?? []);
    const limits = desktopLimits(configuration.limits);
    const registry = parseRegistry(configuration.registry);
    const capabilities = new Set(configuration.capabilities ?? []);
    const bundle = await verifyWebBundle(configuration);
    await app.whenReady();

    const reportFailure = onNativeFailure ?? (error => dialog.showErrorBox('Neura desktop application ended', error.message));
    const windows = new Map();
    let nextWindow = 1;
    let quitStatus;
    let ending = false;
    let queue;
    let router;
    let services;
    const owners = registerBridgeIpc(ipcMain);
    const child = new NativeChildChannel({ ...configuration.nativeChild, registryDigest: registry.digest, frameLimit: limits.frameBytes }, {
        ...nativeIO,
        onFrame: async ({ kind, body }) => {
            if (kind === NativeFrameKind.Return) router.onReturn(body);
            else if (kind === NativeFrameKind.HostCall) answerHostCall(queue, services, body, log).catch(error => child.abort(error));
        },
    });
    queue = new FrameQueue(child);
    router = new BridgeRouter({ queue, registry, limits, log });

    const sendEvent = (...args) => {
        if (!ending && child.active) queue.send(NativeFrameKind.Event, eventBody(...args)).catch(() => {});
    };
    const endChannel = () => {
        ending = true;
        router.close();
        queue.drained().then(() => child.close());
    };
    const windowOf = cursor => {
        const handle = cursor.u32();
        const entry = windows.get(handle);
        if (!entry || entry.window.isDestroyed()) throw new DesktopServiceError(DesktopStatus.InvalidInput, 'unknown window');
        return entry.window;
    };
    const clipboard = () => clipboardOverride ?? require('electron').clipboard;

    services = {
        async 'neura.desktop.window.open'(cursor) {
            const title = cursor.text();
            const [width, height, minWidth, minHeight] = [cursor.u32(), cursor.u32(), cursor.u32(), cursor.u32()];
            const route = cursor.text();
            cursor.end();
            if (ending) throw new DesktopServiceError(DesktopStatus.Cancelled, 'the application is ending');
            if (windows.size >= limits.windows) throw new DesktopServiceError(DesktopStatus.Busy, 'too many windows');
            if (!route.startsWith('/') || route.startsWith('//') || [width, height, minWidth, minHeight].some(v => v > 65535)) {
                throw new DesktopServiceError(DesktopStatus.InvalidInput, 'invalid window size or route');
            }
            const handle = nextWindow++;
            let created;
            const window = await createDesktopWindow(bundle, applicationId, allowed, {
                openExternal, log, frame: { title, width, height, minWidth, minHeight, route },
                // The page's calls in flight are cancelled in the child; their replies are dropped.
                onRendererGone: () => router.closeWindow(handle),
                onWindow(value) {
                    created = value;
                    const contentsId = value.webContents.id;
                    windows.set(handle, { window: value });
                    owners.set(contentsId, (event, operationId, request) => {
                        if (value.isDestroyed() || event.senderFrame !== value.webContents.mainFrame ||
                            !ownOrigin(parseUrl(event.senderFrame.url), applicationId)) {
                            log('Refused a bridge call from a frame outside the application');
                            return { status: DesktopStatus.PermissionDenied };
                        }
                        return router.call(handle, operationId, request);
                    });
                    value.on('focus', () => sendEvent(EventKind.Focused, handle));
                    value.on('blur', () => sendEvent(EventKind.Blurred, handle));
                    value.on('resize', () => {
                        if (!value.isDestroyed()) sendEvent(EventKind.Resized, handle, ...value.getContentSize());
                    });
                    value.once('closed', () => {
                        owners.delete(contentsId);
                        windows.delete(handle);
                        router.closeWindow(handle);
                        sendEvent(EventKind.Closed, handle);
                        if (windows.size === 0 && !ending) endChannel();
                    });
                },
            }).catch(error => {
                if (created && !created.isDestroyed()) created.destroy();
                throw error;
            });
            if (ending && !window.isDestroyed()) window.destroy();
            return u32(handle);
        },
        async 'neura.desktop.window.close'(cursor) {
            const window = windowOf(cursor);
            cursor.end();
            window.close();
        },
        async 'neura.desktop.window.focus'(cursor) {
            const window = windowOf(cursor);
            cursor.end();
            window.focus();
        },
        async 'neura.desktop.window.set_title'(cursor) {
            const window = windowOf(cursor);
            const title = cursor.text();
            cursor.end();
            window.setTitle(title);
        },
        async 'neura.desktop.data_directory'(cursor) {
            cursor.end();
            const directory = dataDirectory(applicationId);
            await fs.mkdir(directory, { recursive: true, mode: 0o700 });
            return wireText(directory);
        },
        async 'neura.desktop.notify'(cursor) {
            const title = cursor.text();
            const body = cursor.text();
            cursor.end();
            if (notifyOverride) return u32(await notifyOverride(title, body));
            const { Notification } = require('electron');
            if (!Notification.isSupported()) return u32(NotifyOutcome.Unsupported);
            new Notification({ title, body, silent: true }).show();
            return u32(NotifyOutcome.Shown);
        },
        async 'neura.desktop.clipboard.write'(cursor) {
            const text = cursor.text();
            cursor.end();
            if (!capabilities.has('desktop.clipboard_write')) throw new DesktopServiceError(DesktopStatus.CapabilityDenied, 'denied');
            // Electron 45 answers the clipboard asynchronously.
            await clipboard().writeText(text);
        },
        async 'neura.desktop.clipboard.read'(cursor) {
            cursor.end();
            if (!capabilities.has('desktop.clipboard_read')) throw new DesktopServiceError(DesktopStatus.CapabilityDenied, 'denied');
            return wireText(await clipboard().readText());
        },
        async 'neura.desktop.quit'(cursor) {
            const status = cursor.u32();
            cursor.end();
            if (status > 255) throw new DesktopServiceError(DesktopStatus.InvalidInput, 'invalid exit status');
            quitStatus = status;
            ending = true;
            for (const { window } of [...windows.values()]) if (!window.isDestroyed()) window.destroy();
            endChannel();
            return null;
        },
    };

    // The child decides when the application ends; Electron's own quit and
    // last-window paths only close the channel and wait for it.
    const beforeQuit = event => {
        event.preventDefault();
        if (ending) return;
        ending = true;
        for (const { window } of [...windows.values()]) if (!window.isDestroyed()) window.destroy();
        endChannel();
    };
    const allWindowsClosed = () => {};
    app.on('before-quit', beforeQuit);
    app.on('window-all-closed', allWindowsClosed);
    const detach = () => {
        app.off('before-quit', beforeQuit);
        app.off('window-all-closed', allWindowsClosed);
    };
    const finished = child.closed.then(result => {
        ending = true;
        router.close();
        let error = result.error;
        const expected = quitStatus ?? 0;
        if (!error && result.code !== expected) {
            error = new NativeChannelError('ChildExited', `Native child exited with status ${result.signal ?? result.code}`);
        }
        for (const { window } of [...windows.values()]) if (!window.isDestroyed()) window.destroy();
        try {
            if (error) reportFailure(error, result);
        } finally {
            detach();
            app.exit(error ? 1 : expected);
        }
        return { ...result, error, status: error ? 1 : expected };
    });

    try {
        await child.ready;
    } catch (error) {
        await finished;
        throw error;
    }
    return Object.freeze({
        child,
        finished,
        windows: () => [...windows].map(([handle, { window }]) => ({ handle, window })),
        inFlight: handle => router.inFlight(handle),
    });
}

// Reads desktop.json from a package directory. Every path in it is relative
// to the package and must name a regular file or directory inside it.
async function loadDesktopPackage(directory) {
    const root = await fs.realpath(directory);
    const bytes = await fs.readFile(path.join(root, 'desktop.json'));
    let manifest;
    try {
        manifest = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
    } catch {
        throw new Error('desktop.json is not a UTF-8 JSON document');
    }
    if (!record(manifest) || manifest.schema !== 1 || manifest.kind !== 'desktop') throw new Error('desktop.json is not a schema 1 desktop package');
    const inside = async (relative, directoryWanted) => {
        if (!safePath(relative)) throw new Error(`desktop.json has an unsafe path: ${relative}`);
        const resolved = await fs.realpath(path.join(root, relative));
        const entry = await fs.lstat(resolved);
        if (!contained(root, resolved) || (directoryWanted ? !entry.isDirectory() : !entry.isFile())) {
            throw new Error(`desktop.json path is not a ${directoryWanted ? 'directory' : 'file'} in the package: ${relative}`);
        }
        return resolved;
    };
    const { web, registry, child } = manifest;
    if (!record(web) || !record(registry) || !record(child) || !Array.isArray(child.args ?? []) ||
        !(child.args ?? []).every(arg => typeof arg === 'string')) {
        throw new Error('desktop.json lacks web, registry or child');
    }
    const registryBytes = await fs.readFile(await inside(registry.path, false));
    if (createHash('sha256').update(registryBytes).digest('hex') !== registry.sha256) throw new Error('SHA-256 mismatch for registry.json');
    const executable = await inside(child.executable, false);
    await fs.access(executable, constants.X_OK);
    return {
        applicationId: manifest.id,
        directory: await inside(web.directory, true),
        manifestDigest: web.manifestDigest,
        supportedRequirements: manifest.supportedRequirements ?? {},
        externalOrigins: manifest.externalOrigins ?? [],
        capabilities: manifest.capabilities ?? [],
        limits: manifest.limits ?? {},
        registry: registryBytes,
        nativeChild: { executable, args: child.args ?? [] },
    };
}

// The Electron entry. A package runs main.js with desktop.json beside it. An
// unpackaged development run names the package with --neura-package=<dir>.
function bootstrapDesktopPackage() {
    registerDesktopScheme();
    const { app } = require('electron');
    const option = '--neura-package=';
    const named = process.argv.filter(argument => argument.startsWith(option));
    const directory = app.isPackaged ? __dirname : named.length === 1 ? path.resolve(named[0].slice(option.length)) : null;
    if (!directory) {
        console.error('usage: electron main.js --neura-package=<package directory>');
        app.exit(2);
        return;
    }
    loadDesktopPackage(directory)
        .then(configuration => runDesktopApplication(configuration))
        .catch(error => {
            console.error(`Neura desktop application failed to start: ${error.message}`);
            app.exit(1);
        });
}

if (require.main === module) bootstrapDesktopPackage();

module.exports = {
    launchDesktopShell, registerDesktopScheme, verifyWebBundle, NativeChildChannel, NativeFrameKind,
    runDesktopApplication, loadDesktopPackage, parseRegistry, desktopLimits, BridgeRouter, FrameQueue,
    DesktopStatus, EventKind,
};
