// The browser adapter of nvm.wasm (docs/runtime/Providers.md, "Browser
// adapter interface"). It loads and binds an image, runs it in slices,
// serves its host requests, and drives its scheduler's waits and timers.
export const ADAPTER_INTERFACE = 1;

const signals = ['yield', 'completed', 'trapped', 'host', 'poll', 'waiting', 'cancel'];
const maxI64 = (1n << 63n) - 1n;
const maxU64 = (1n << 64n) - 1n;
const sliceSteps = 4096;
const reportSize = 88;
const decoder = new TextDecoder('utf-8', { ignoreBOM: true });
const strictText = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });
const encoder = new TextEncoder();
const empty = new Uint8Array(0);

export const HOST_ERRORS = Object.freeze({
    notFound: 1, alreadyExists: 3, invalidInput: 4, timedOut: 12, unsupported: 13, capabilityDenied: 14,
    cancelled: 15, other: 100,
});

const monotonicNanos = () => BigInt(Math.round(performance.now() * 1e6));

function int64(value) {
    if (typeof value !== 'bigint' || value < -maxI64 - 1n || value > maxI64) {
        throw new TypeError('a 64-bit host value must be a signed 64-bit BigInt');
    }
    return value;
}

// Host values on the wire (Providers.md, "Value wire"): 64 bits, a byte
// count and bytes, an element count and elements, little-endian.
function encodeValue(parts, value) {
    const head = new DataView(new ArrayBuffer(12));
    head.setBigUint64(0, value.bits ?? 0n, true);
    const bytes = value.bytes ?? empty;
    head.setUint32(8, bytes.length, true);
    parts.push(new Uint8Array(head.buffer), bytes);
    const count = new DataView(new ArrayBuffer(4));
    const elements = value.elements ?? [];
    count.setUint32(0, elements.length, true);
    parts.push(new Uint8Array(count.buffer));
    for (const element of elements) encodeValue(parts, element);
}

function encodeCompletion(success, value) {
    const parts = [Uint8Array.of(success ? 1 : 0)];
    encodeValue(parts, value);
    const size = parts.reduce((sum, part) => sum + part.length, 0);
    const out = new Uint8Array(size);
    let at = 0;
    for (const part of parts) {
        out.set(part, at);
        at += part.length;
    }
    return out;
}

function decodeValues(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let at = 0;
    const value = () => {
        const bits = view.getBigUint64(at, true);
        const size = view.getUint32(at + 8, true);
        const data = bytes.slice(at + 12, at + 12 + size);
        at += 12 + size;
        const count = view.getUint32(at, true);
        at += 4;
        const elements = [];
        for (let i = 0; i < count; ++i) elements.push(value());
        return { bits, bytes: data, elements };
    };
    const values = [];
    while (at < bytes.length) values.push(value());
    return values;
}

// Argument and result conversions by host type.
const argument = {
    text: value => {
        try {
            return strictText.decode(value.bytes);
        } catch {
            throw new TypeError('the NVM passed text that is not UTF-8');
        }
    },
    int32: value => Number(BigInt.asIntN(32, value.bits)),
    int64: value => BigInt.asIntN(64, value.bits),
    bytes: value => value.bytes,
};

const result = {
    void: () => ({}),
    text: value => {
        if (typeof value !== 'string') throw new TypeError('a text result must be a string');
        return { bytes: encoder.encode(value) };
    },
    int64: value => ({ bits: BigInt.asUintN(64, int64(value)) }),
    int32: value => {
        if (!Number.isInteger(value) || value < -(2 ** 31) || value >= 2 ** 31) {
            throw new TypeError('an int32 result must be an integer in range');
        }
        return { bits: BigInt.asUintN(32, BigInt(value)) };
    },
    bytes: value => {
        if (!(value instanceof Uint8Array)) throw new TypeError('a byte list result must be a Uint8Array');
        return { bytes: value };
    },
    wall: value => {
        if (!value || !Number.isInteger(value.nanos) || value.nanos < 0 || value.nanos >= 1e9) {
            throw new TypeError('a wall clock must return integral nanos within a second');
        }
        return { elements: [{ bits: BigInt.asUintN(64, int64(value.seconds)) }, { bits: BigInt(value.nanos) }] };
    },
    stats: value => {
        if (!Array.isArray(value) || value.length !== 4) throw new TypeError('UI stats are four counts');
        return { elements: value.map(count => ({ bits: BigInt.asUintN(64, int64(BigInt(count))) })) };
    },
};

// The services the adapter can serve, by name and interface version. `group`
// is the host key that grants them; `call` receives the host, the converted
// arguments and the request context.
const services = new Map();
function service(key, group, parameters, success, call) {
    services.set(key, { group, parameters: parameters.map(type => argument[type]), success: result[success], call });
}
service('neura.console.write_line/1', 'console', ['text'], 'void', (host, [line], context) => {
    const written = host.console(`${line}\n`, context);
    const failed = reply => (reply === false ? { error: 1 } : reply === undefined || reply === true ? undefined : reply);
    if (written && typeof written.then === 'function') return written.then(failed);
    if (written !== undefined && typeof written !== 'boolean') {
        throw new TypeError('the console service must return a Boolean or undefined');
    }
    return failed(written);
});
service('neura.console.write/1', 'consoleWrite', ['int32', 'bytes'], 'int32',
    (host, [stream, bytes], context) => host.consoleWrite(stream, bytes, context));
service('neura.clock.wall/1', 'clockWall', [], 'wall', (host, _, context) => host.clockWall(context));
service('neura.clock.monotonic/1', 'clockMonotonic', [], 'int64', (host, _, context) => host.clockMonotonic(context));
service('neura.clock.sleep/1', 'sleep', ['int64'], 'void', (host, [nanos], context) => host.sleep(nanos, context));
service('neura.clock.sleep/2', 'sleep', ['int64'], 'void', (host, [nanos], context) => host.sleep(nanos, context));
service('neura.entropy.fill/1', 'entropy', ['int32'], 'bytes', (host, [count], context) => host.entropy(count, context));
service('neura.ui.root.open/1', 'ui', ['text'], 'int64', (host, [selector], c) => host.ui.openRoot(selector, c));
service('neura.ui.root.close/1', 'ui', ['int64'], 'void', (host, [root], c) => host.ui.closeRoot(root, c));
service('neura.ui.template/1', 'ui', ['int64', 'text'], 'int64', (host, [root, text], c) => host.ui.template(root, text, c));
service('neura.ui.commit/1', 'ui', ['int64', 'bytes'], 'void', (host, [root, bytes], c) => host.ui.commit(root, bytes, c));
service('neura.ui.event.next/1', 'ui', ['int64', 'int32'], 'bytes', (host, [root, max], c) => host.ui.nextEvents(root, max, c));
service('neura.ui.stats/1', 'ui', ['int64'], 'stats', (host, [root], c) => host.ui.stats(root, c));
service('neura.ui.focus/1', 'ui', ['int64', 'text'], 'void', (host, [root, id], c) => host.ui.focus(root, id, c));
service('neura.history.current/1', 'history', [], 'text', (host, _, c) => host.history.current(c));
service('neura.history.push/1', 'history', ['text'], 'text', (host, [path], c) => host.history.push(path, c));
service('neura.history.replace/1', 'history', ['text'], 'text', (host, [path], c) => host.history.replace(path, c));
service('neura.history.next/1', 'history', [], 'text', (host, _, c) => host.history.next(c));
service('neura.history.link/1', 'history', ['text'], 'text', (host, [path], c) => host.history.link(path, c));

function completionFor(adapter, value) {
    if (value && typeof value === 'object' && 'error' in value && Object.keys(value).length === 1) {
        if (!Number.isInteger(value.error) || value.error < 1 || value.error > 0xffffffff) {
            throw new TypeError('a host error must be a positive uint32');
        }
        return encodeCompletion(false, { bits: BigInt(value.error) });
    }
    return encodeCompletion(true, adapter.success(value));
}

const isThenable = value => value !== null && typeof value === 'object' && typeof value.then === 'function';

function wasmError(module) {
    return new Error(module.UTF8ToString(module._neura_wasm_error()));
}

function withMemory(module, size, use) {
    const pointer = module._malloc(Math.max(size, 1));
    if (!pointer) throw new Error('the WebAssembly allocator could not grow');
    try {
        return use(pointer >>> 0);
    } finally {
        module._free(pointer);
    }
}

function withBytes(module, bytes, use) {
    return withMemory(module, bytes.length, pointer => {
        module.HEAPU8.set(bytes, pointer);
        return use(pointer, bytes.length);
    });
}

function withArguments(module, args, use) {
    const texts = args.map(arg => {
        if (typeof arg !== 'string') throw new TypeError('program arguments must be text');
        return encoder.encode(arg);
    });
    const tableSize = texts.length * 8;
    const totalSize = texts.reduce((sum, text) => sum + text.length, tableSize);
    if (totalSize > 0xffffffff) throw new RangeError('arguments exceed wasm32 memory');
    return withMemory(module, totalSize, table => {
        const view = new DataView(module.HEAPU8.buffer, table, tableSize);
        let offset = table + tableSize;
        for (const [index, text] of texts.entries()) {
            view.setUint32(index * 8, offset, true);
            view.setUint32(index * 8 + 4, text.length, true);
            module.HEAPU8.set(text, offset);
            offset += text.length;
        }
        return use(table, texts.length);
    });
}

// Reads the report before anything else calls into the module: memory growth
// detaches earlier views of HEAPU8.
function readReport(module, pointer) {
    const memory = module.HEAPU8;
    const view = new DataView(memory.buffer, pointer >>> 0, reportSize);
    const kind = signals[view.getUint32(40, true)];
    const flags = view.getUint32(44, true);
    const report = {
        kind,
        steps: view.getBigUint64(0, true),
        peakHeapCells: view.getBigUint64(8, true),
        peakStackCells: view.getBigUint64(16, true),
    };
    const bytes = offset => {
        const start = view.getUint32(offset, true);
        const length = view.getUint32(offset + 4, true);
        return memory.slice(start, start + length);
    };
    if (kind === 'completed' || kind === 'trapped') report.status = view.getInt32(56, true);
    if (kind === 'trapped') {
        report.reason = view.getUint32(60, true);
        report.function = view.getUint32(64, true);
        report.block = view.getUint32(68, true);
        report.diagnostic = decoder.decode(bytes(48));
    }
    if (kind === 'host') {
        report.service = `${decoder.decode(bytes(72))}/${view.getUint32(80, true)}`;
        report.sequence = view.getBigUint64(24, true);
        report.overlapping = (flags & 1) !== 0;
        report.cancellable = (flags & 2) !== 0;
        report.arguments = decodeValues(bytes(48));
    }
    if (kind === 'poll' || kind === 'waiting') {
        report.timer = (flags & 4) !== 0;
        const deadline = view.getBigInt64(32, true);
        report.deadline = report.timer && deadline !== maxI64 ? deadline : null;
    }
    if (kind === 'cancel') report.sequence = view.getBigUint64(24, true);
    return Object.freeze(report);
}

const browserYield = () => new Promise(resolve => setTimeout(resolve, 0));

// A timed-out idle callback leaves the proof queued. Waiting for the next
// idle opportunity must not keep runnable Neura work blocked indefinitely.
function browserIdle(work) {
    if (typeof globalThis.requestIdleCallback !== 'function') return Promise.resolve();
    return new Promise((resolve, reject) => {
        globalThis.requestIdleCallback(deadline => {
            try {
                if (!deadline.didTimeout && deadline.timeRemaining() > 0) work();
                resolve();
            } catch (error) {
                reject(error);
            }
        }, { timeout: 8 });
    });
}

// setTimeout takes at most 2^31 - 1 milliseconds; longer waits are chained.
function delay(milliseconds, signal) {
    return new Promise(resolve => {
        let timer;
        const finish = value => {
            clearTimeout(timer);
            signal?.removeEventListener('abort', aborted);
            resolve(value);
        };
        const aborted = () => finish(false);
        const step = remaining => {
            const now = Math.min(remaining, 0x7fffffff);
            timer = setTimeout(() => (remaining > now ? step(remaining - now) : finish(true)), now);
        };
        if (signal?.aborted) return resolve(false);
        signal?.addEventListener('abort', aborted, { once: true });
        step(Math.max(0, milliseconds));
    });
}

// The default services of a browser page. `ui` and `history` are optional
// UiHost and HistoryHost instances (ui.mjs).
export function browserHost({ output = globalThis.console, ui, history } = {}) {
    // Bytes of neura.console.write wait for their LF, per stream, as the page's console takes whole lines.
    const pending = [null, [], []];
    const stdout = new TextDecoder('utf-8');
    const stderr = new TextDecoder('utf-8');
    const host = {
        // A line write finishes the partial stdout line before it, as on a terminal.
        console(line) {
            const text = pending[1].join('') + stdout.decode() + line;
            pending[1] = [];
            output.log(text.endsWith('\n') ? text.slice(0, -1) : text);
        },
        consoleWrite(stream, bytes) {
            if (stream !== 1 && stream !== 2) return { error: HOST_ERRORS.invalidInput };
            const text = (stream === 1 ? stdout : stderr).decode(bytes, { stream: true });
            const lines = (pending[stream].join('') + text).split('\n');
            pending[stream] = [lines.pop()];
            for (const line of lines) (stream === 1 ? output.log : output.error).call(output, line);
            return bytes.length;
        },
        clockWall() {
            const nanos = BigInt(Math.round((performance.timeOrigin + performance.now()) * 1e6));
            return { seconds: nanos / 1_000_000_000n, nanos: Number(nanos % 1_000_000_000n) };
        },
        clockMonotonic: monotonicNanos,
        async sleep(nanos, { signal } = {}) {
            if (nanos < 0n) return { error: HOST_ERRORS.invalidInput };
            if (nanos === 0n) return undefined;
            // A timer may fire a little before the monotonic clock reaches its
            // deadline; the sleep ends only once the clock has.
            const deadline = monotonicNanos() + nanos;
            for (let now = monotonicNanos(); now < deadline; now = monotonicNanos()) {
                if (!await delay(Math.ceil(Number(deadline - now) / 1e6), signal)) return { error: HOST_ERRORS.cancelled };
            }
            return undefined;
        },
        entropy(count) {
            if (count < 0 || count > 1 << 20) return { error: HOST_ERRORS.invalidInput };
            const bytes = new Uint8Array(count);
            for (let at = 0; at < count; at += 65536) globalThis.crypto.getRandomValues(bytes.subarray(at, at + 65536));
            return bytes;
        },
    };
    if (ui) host.ui = ui;
    if (history) host.history = history;
    return host;
}

export class NvmExecution {
    #module;
    #handle;
    #host;
    #event;
    #serving = false;
    #running = false;
    // Overlapping requests the host has not answered, by sequence.
    #pending = new Map();
    // Answers waiting for the next scheduling point, in the order they came.
    #ready = [];
    #wake = null;
    #failure = null;

    // `regions: true` forms tier 2 regions instead of function programs; it
    // exists to measure the two (Apps030.md, "Execution in the browser").
    constructor(module, image, { args = [], host = {}, tier = 2, maxSteps = 0n, regions = false } = {}) {
        if (typeof module._neura_wasm_interface !== 'function' || module._neura_wasm_interface() !== ADAPTER_INTERFACE) {
            throw new Error(`nvm.wasm does not provide browser adapter interface ${ADAPTER_INTERFACE}`);
        }
        if (!(image instanceof Uint8Array) || image.byteLength > 256 * 1024 * 1024) {
            throw new TypeError('an NVR image must be a Uint8Array within the loader limit');
        }
        if (!Number.isInteger(tier) || tier < 0 || tier > 2) throw new RangeError('invalid NVM tier');
        if (typeof maxSteps !== 'bigint' || maxSteps < 0n || maxSteps > maxU64) {
            throw new RangeError('maxSteps must be an unsigned 64-bit BigInt');
        }
        this.#module = module;
        this.#host = host;
        const granted = [];
        for (const [key, adapter] of services) {
            const provider = host[adapter.group];
            if (provider === undefined) continue;
            if (typeof provider !== 'function' && (typeof provider !== 'object' || provider === null)) {
                throw new TypeError(`${adapter.group} must be a function or an object`);
            }
            granted.push(key);
        }
        this.#handle = withArguments(module, args, (argv, argc) => withBytes(module, encoder.encode(granted.join('\n')),
            (grants, grantsSize) => withBytes(module, image, (data, size) =>
                module._neura_wasm_open(data, size, argv, argc, grants, grantsSize, tier | (regions ? 0x100 : 0),
                    maxSteps))));
        if (!this.#handle) throw wasmError(module);
    }

    #liveHandle() {
        if (!this.#handle) throw new Error('the NVM execution is closed');
        if (this.#serving) throw new Error('a service cannot reenter its NVM execution');
        return this.#handle;
    }

    #abandon() {
        for (const entry of this.#pending.values()) entry.controller?.abort();
        this.#pending.clear();
        this.#ready.length = 0;
    }

    close() {
        if (!this.#handle) return;
        this.#abandon();
        this.#module._neura_wasm_close(this.#liveHandle());
        this.#handle = 0;
        this.#wake?.();
    }

    #fail(error) {
        // A provider may already have produced an external effect. Closing
        // the run prevents the same request from being issued again.
        if (this.#handle) {
            this.#abandon();
            this.#module._neura_wasm_close(this.#handle);
            this.#handle = 0;
        }
        this.#failure ??= error;
        this.#wake?.();
        throw error;
    }

    advanceSlice(budget = sliceSteps) {
        if (!Number.isInteger(budget) || budget < 1 || budget > sliceSteps) {
            throw new RangeError(`a slice requires 1 to ${sliceSteps} steps`);
        }
        const output = this.#module._neura_wasm_advance(this.#liveHandle(), budget);
        if (!output) throw wasmError(this.#module);
        this.#event = readReport(this.#module, output);
        return this.#event;
    }

    #respond(adapter, value) {
        const handle = this.#liveHandle();
        try {
            const reply = completionFor(adapter, value);
            withBytes(this.#module, reply, (pointer, size) => {
                if (!this.#module._neura_wasm_respond(handle, pointer, size)) throw wasmError(this.#module);
            });
            this.#event = undefined;
        } catch (error) {
            this.#fail(error);
        }
    }

    #settle(sequence, adapter, value) {
        const entry = this.#pending.get(sequence);
        if (!entry) return;
        this.#pending.delete(sequence);
        try {
            this.#ready.push([sequence, completionFor(adapter, value)]);
        } catch (error) {
            this.#failure ??= error;
        }
        this.#wake?.();
    }

    // Serves the pending host request. A synchronous answer is given at once.
    // Returns a promise when the host answers a request that does not
    // overlap later; the run stays suspended until it settles. An
    // overlapping request is started and answered at a scheduling point.
    serveRequest() {
        const handle = this.#liveHandle();
        const request = this.#event;
        if (request?.kind !== 'host') throw new Error('there is no pending host request');
        const adapter = services.get(request.service);
        if (!adapter) this.#fail(new Error(`no browser service ${request.service}`));
        const context = { sequence: request.sequence };
        let controller;
        if (request.cancellable) {
            controller = new AbortController();
            context.signal = controller.signal;
        }
        let value;
        this.#serving = true;
        try {
            const args = adapter.parameters.map((convert, index) => convert(request.arguments[index]));
            value = adapter.call(this.#host, args, context);
        } catch (error) {
            this.#serving = false;
            this.#fail(error);
        } finally {
            this.#serving = false;
        }
        if (request.overlapping) {
            this.#event = undefined;
            this.#pending.set(request.sequence, { controller });
            Promise.resolve(value).then(answer => this.#settle(request.sequence, adapter, answer), error => {
                if (!this.#pending.delete(request.sequence)) return;
                this.#failure ??= error;
                this.#wake?.();
            });
            return undefined;
        }
        if (isThenable(value)) {
            return value.then(answer => {
                if (this.#handle === handle) this.#respond(adapter, answer);
            }, error => this.#fail(error));
        }
        this.#respond(adapter, value);
        return undefined;
    }

    // A cancelled request answers error 15 unless its answer is already
    // queued, and never answers twice.
    cancelRequest(sequence) {
        const entry = this.#pending.get(sequence);
        if (!entry) return;
        this.#pending.delete(sequence);
        entry.controller?.abort();
        this.#ready.push([sequence, encodeCompletion(false, { bits: BigInt(HOST_ERRORS.cancelled) })]);
    }

    // Delivers queued answers; valid after a poll or wait.
    deliverReady() {
        const handle = this.#liveHandle();
        const ready = this.#ready.splice(0);
        for (const [sequence, reply] of ready) {
            withBytes(this.#module, reply, (pointer, size) => {
                if (!this.#module._neura_wasm_deliver(handle, sequence, pointer, size)) throw wasmError(this.#module);
            });
        }
        return ready.length;
    }

    get pendingRequests() {
        return this.#pending.size;
    }

    // Returns how many timers and deadlines expired.
    advanceClock(now) {
        if (this.#event?.kind !== 'poll' && this.#event?.kind !== 'waiting') {
            throw new Error('the task clock requires a poll or wait event');
        }
        const expired = this.#module._neura_wasm_clock(this.#liveHandle(), int64(now));
        if (expired < 0) throw wasmError(this.#module);
        return expired;
    }

    get proofPending() {
        return Boolean(this.#module._neura_wasm_has_proof(this.#liveHandle()));
    }

    // The tier statistics as `neura run --tier-stats` prints them, for measurements.
    get tierStatistics() {
        return this.#module.UTF8ToString(this.#module._neura_wasm_tier_stats(this.#liveHandle()));
    }

    // The function and block of the last idle proof, for measurements.
    get lastProof() {
        const packed = BigInt.asUintN(64, BigInt(this.#module._neura_wasm_last_proof(this.#liveHandle())));
        return { function: Number(packed >> 32n), block: Number(packed & 0xffffffffn) };
    }

    proveOneIdle() {
        const elapsed = this.#module._neura_wasm_prove_idle(this.#liveHandle());
        if (elapsed === -2) throw wasmError(this.#module);
        return elapsed < 0 ? null : elapsed;
    }

    #checkFailure() {
        if (this.#failure) this.#fail(this.#failure);
    }

    // Resolves when an answer is queued, the clock reaches `deadline`, or the
    // run fails or closes. It never polls.
    #sleepUntil(deadline, clock) {
        if (this.#ready.length || this.#failure || !this.#handle) return Promise.resolve();
        return new Promise(resolve => {
            let timer;
            const done = () => {
                clearTimeout(timer);
                this.#wake = null;
                resolve();
            };
            this.#wake = done;
            if (deadline !== null) {
                const nanos = deadline - clock();
                timer = setTimeout(done, nanos <= 0n ? 0 : Math.min(Math.ceil(Number(nanos) / 1e6), 0x7fffffff));
            }
        });
    }

    async #attemptProof(idle, hasPendingEvents, onProof) {
        if (hasPendingEvents() || !this.proofPending) return;
        let available = true;
        try {
            await idle(() => {
                if (!available || !this.#handle || hasPendingEvents()) return;
                available = false;
                const elapsed = this.proveOneIdle();
                if (elapsed !== null) onProof(elapsed);
            });
        } finally {
            available = false;
        }
    }

    async run({ yieldTask = browserYield, idle = browserIdle, hasPendingEvents = () => false,
                sliceMilliseconds = 8, now = () => performance.now(), onProof = () => {},
                clock = monotonicNanos } = {}) {
        if (this.#running) throw new Error('the NVM is already running');
        if (!Number.isFinite(sliceMilliseconds) || sliceMilliseconds <= 0 || sliceMilliseconds > 50) {
            throw new RangeError('sliceMilliseconds must be greater than zero and at most 50');
        }
        this.#running = true;
        try {
            let started = now();
            for (;;) {
                this.#checkFailure();
                const event = this.advanceSlice();
                switch (event.kind) {
                case 'completed':
                case 'trapped':
                    this.#abandon();
                    return event;
                case 'host': {
                    const answer = this.serveRequest();
                    if (answer) {
                        await answer;
                        this.#checkFailure();
                        started = now();
                    }
                    break;
                }
                case 'cancel':
                    this.cancelRequest(event.sequence);
                    break;
                case 'poll':
                    this.deliverReady();
                    if (event.timer) this.advanceClock(clock());
                    break;
                case 'waiting': {
                    if (!this.#pending.size && !this.#ready.length && !event.timer) {
                        this.#fail(new Error('the run waits for a host request that was never started'));
                    }
                    for (;;) {
                        if (!this.#ready.length) await this.#attemptProof(idle, hasPendingEvents, onProof);
                        await this.#sleepUntil(event.deadline, clock);
                        this.#checkFailure();
                        const delivered = this.deliverReady();
                        const expired = event.timer && clock() >= event.deadline ? this.advanceClock(clock()) : 0;
                        if (delivered || expired) break;
                    }
                    started = now();
                    continue;
                }
                }
                if (now() - started < sliceMilliseconds && !hasPendingEvents()) continue;
                await yieldTask();
                await this.#attemptProof(idle, hasPendingEvents, onProof);
                started = now();
            }
        } finally {
            this.#running = false;
        }
    }
}
