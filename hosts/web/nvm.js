const signals = ['yield', 'completed', 'trapped', 'host', 'poll', 'waiting', 'cancel'];
const services = [null, 'console', 'clockWall', 'clockMonotonic'];
const maxI64 = (1n << 63n) - 1n;
const maxU64 = (1n << 64n) - 1n;
const sliceSteps = 4096;
const decoder = new TextDecoder('utf-8', { ignoreBOM: true });
const encoder = new TextEncoder();

function int64(value) {
    if (typeof value !== 'bigint' || value < -maxI64 - 1n || value > maxI64) {
        throw new TypeError('a clock value must be a signed 64-bit BigInt');
    }
    return value;
}

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

function readReport(module, pointer) {
    const memory = module.HEAPU8;
    const view = new DataView(memory.buffer, pointer >>> 0, 72);
    const kind = signals[view.getUint32(40, true)];
    const report = {
        kind,
        steps: view.getBigUint64(0, true),
        peakHeapCells: view.getBigUint64(8, true),
        peakStackCells: view.getBigUint64(16, true),
    };
    const readText = () => {
        const start = view.getUint32(48, true);
        const length = view.getUint32(52, true);
        return decoder.decode(memory.subarray(start, start + length));
    };
    if (kind === 'completed' || kind === 'trapped') report.status = view.getInt32(56, true);
    if (kind === 'trapped') {
        report.reason = view.getUint32(60, true);
        report.function = view.getUint32(64, true);
        report.block = view.getUint32(68, true);
        report.diagnostic = readText();
    }
    if (kind === 'host') {
        report.service = services[view.getUint32(44, true)];
        report.sequence = view.getBigUint64(24, true);
        if (report.service === 'console') report.line = `${readText()}\n`;
    }
    if (kind === 'poll' || kind === 'waiting') {
        const deadline = view.getBigInt64(32, true);
        report.deadline = deadline === maxI64 ? null : deadline;
    }
    if (kind === 'cancel') report.sequence = view.getBigUint64(24, true);
    return Object.freeze(report);
}

function encodeReply(service, value) {
    if (service === 'console') {
        if (value !== undefined && typeof value !== 'boolean') {
            throw new TypeError('the console service must return a Boolean or undefined');
        }
        return [value === false ? 1 : 0, 0n, 0];
    }
    if (value && typeof value === 'object' && 'error' in value) {
        if (!Number.isInteger(value.error) || value.error < 1 || value.error > 0xffffffff) {
            throw new TypeError('a clock error must be a positive uint32');
        }
        return [value.error, 0n, 0];
    }
    if (service === 'clockMonotonic') return [0, int64(value), 0];
    if (!value || !Number.isInteger(value.nanos) || value.nanos < 0 || value.nanos >= 1e9) {
        throw new TypeError('a wall clock must return integral nanos within a second');
    }
    return [0, int64(value.seconds), value.nanos];
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

export class NvmExecution {
    #module;
    #handle;
    #services;
    #event;
    #serving = false;
    #running = false;

    constructor(module, image, { args = [], host = {}, tier = 2, maxSteps = 0n } = {}) {
        if (!(image instanceof Uint8Array) || image.byteLength > 256 * 1024 * 1024) {
            throw new TypeError('an NVR image must be a Uint8Array within the loader limit');
        }
        if (!Number.isInteger(tier) || tier < 0 || tier > 2) throw new RangeError('invalid NVM tier');
        if (typeof maxSteps !== 'bigint' || maxSteps < 0n || maxSteps > maxU64) {
            throw new RangeError('maxSteps must be an unsigned 64-bit BigInt');
        }
        this.#module = module;
        this.#services = new Map();
        let capabilities = 0;
        for (const [index, service] of services.entries()) {
            if (service === null || host[service] === undefined) continue;
            if (typeof host[service] !== 'function') throw new TypeError(`${service} must be a function`);
            this.#services.set(service, host[service].bind(host));
            capabilities |= 1 << (index - 1);
        }
        this.#handle = withArguments(module, args, (argv, argc) => withMemory(module, image.byteLength, data => {
            module.HEAPU8.set(image, data);
            return module._neura_wasm_open(data, image.byteLength, argv, argc, capabilities, tier, maxSteps);
        }));
        if (!this.#handle) throw wasmError(module);
    }

    #liveHandle() {
        if (!this.#handle) throw new Error('the NVM execution is closed');
        if (this.#serving) throw new Error('a service cannot reenter its NVM execution');
        return this.#handle;
    }

    close() {
        if (!this.#handle) return;
        this.#module._neura_wasm_close(this.#liveHandle());
        this.#handle = 0;
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

    serveRequest() {
        const handle = this.#liveHandle();
        if (this.#event?.kind !== 'host') throw new Error('there is no pending host request');
        const request = this.#event;
        this.#serving = true;
        try {
            const handler = this.#services.get(request.service);
            const value = request.service === 'console' ? handler(request.line, request.sequence) : handler(request.sequence);
            const reply = encodeReply(request.service, value);
            if (!this.#module._neura_wasm_respond(handle, ...reply)) throw wasmError(this.#module);
            this.#event = undefined;
        } catch (error) {
            // The provider may already have produced an external effect.
            // Closing the run prevents the same request from being issued again.
            this.#module._neura_wasm_close(handle);
            this.#handle = 0;
            throw error;
        } finally {
            this.#serving = false;
        }
    }

    advanceClock(now) {
        if (this.#event?.kind !== 'poll' && this.#event?.kind !== 'waiting') {
            throw new Error('the task clock requires a poll or wait event');
        }
        if (!this.#module._neura_wasm_clock(this.#liveHandle(), int64(now))) throw wasmError(this.#module);
    }

    get proofPending() {
        return Boolean(this.#module._neura_wasm_has_proof(this.#liveHandle()));
    }

    proveOneIdle() {
        const elapsed = this.#module._neura_wasm_prove_idle(this.#liveHandle());
        if (elapsed === -2) throw wasmError(this.#module);
        return elapsed < 0 ? null : elapsed;
    }

    async run({ yieldTask = browserYield, idle = browserIdle, hasPendingEvents = () => false,
                sliceMilliseconds = 8, now = () => performance.now(), onProof = () => {} } = {}) {
        if (this.#running) throw new Error('the NVM is already running');
        if (!Number.isFinite(sliceMilliseconds) || sliceMilliseconds <= 0 || sliceMilliseconds > 50) {
            throw new RangeError('sliceMilliseconds must be greater than zero and at most 50');
        }
        this.#running = true;
        try {
            let started = now();
            for (;;) {
                const event = this.advanceSlice();
                if (event.kind === 'completed' || event.kind === 'trapped' || event.kind === 'waiting') return event;
                if (event.kind === 'host') this.serveRequest();
                if (now() - started < sliceMilliseconds && !hasPendingEvents()) continue;
                await yieldTask();
                if (!hasPendingEvents() && this.proofPending) {
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
                started = now();
            }
        } finally {
            this.#running = false;
        }
    }
}
