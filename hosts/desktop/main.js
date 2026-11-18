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

async function createDesktopWindow(bundle, applicationId, allowed, { openExternal, log, onWindow }) {
    const { session, BrowserWindow, shell } = require('electron');
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
    onWindow?.(window);
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

module.exports = { launchDesktopShell, registerDesktopScheme, verifyWebBundle, NativeChildChannel, NativeFrameKind };
