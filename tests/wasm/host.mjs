import createNvm from './nvm.mjs';
import { NvmExecution } from './nvm.js';

const equal = (actual, expected, label) => {
    if (actual !== expected) throw new Error(`${label}: expected ${expected}, received ${actual}`);
};

const rejects = (call, message) => {
    try {
        call();
    } catch (error) {
        if (message.test(error.message)) return;
        throw error;
    }
    throw new Error(`expected error ${message}`);
};

async function read(relative) {
    const url = new URL(relative, import.meta.url);
    if (url.protocol === 'file:') {
        const { readFile } = await import('node:fs/promises');
        return readFile(url);
    }
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${relative}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

function collect(module, image, oracle, options, budget, doIdle, kernelBoundaries) {
    const received = [];
    const run = new NvmExecution(module, image, {
        ...options,
        host: {
            console(line, { sequence }) { received.push([sequence, line]); },
            clockWall({ sequence }) {
                received.push([sequence, 'wall']);
                return { seconds: 1794252615n, nanos: 250000000 };
            },
            clockMonotonic({ sequence }) {
                received.push([sequence, 'monotonic']);
                return sequence === 3n ? 9007199254740992n : 9007199254740993n;
            },
        },
    });
    const pauses = [];
    let queued = false;
    let previous = 0n;
    try {
        for (;;) {
            const event = run.advanceSlice(budget);
            if (kernelBoundaries && event.steps - previous > BigInt(budget)) {
                if (options.tier === 0) {
                    kernelBoundaries.set(event.steps, previous);
                } else {
                    const before = kernelBoundaries.get(event.steps);
                    if (before === undefined || before < previous || before - previous >= BigInt(budget)) {
                        throw new Error(`slice passed the kernel's atomic boundary at ${previous}–${event.steps}`);
                    }
                }
            }
            previous = event.steps;
            if (event.kind === 'host') {
                equal(run.proveOneIdle(), null, 'proof while a host request is pending');
                if (run.serveRequest() !== undefined) throw new Error('a synchronous service answered later');
                continue;
            }
            if (event.kind === 'yield') {
                if (run.proofPending) {
                    queued = true;
                    if (doIdle) pauses.push(run.proveOneIdle());
                }
                continue;
            }
            equal(event.status, oracle.status, 'status');
            equal(event.kind, oracle.kind, 'event kind');
            equal(event.steps, BigInt(oracle.steps), 'steps');
            equal(event.peakHeapCells, BigInt(oracle.heap), 'peak heap');
            equal(event.peakStackCells, BigInt(oracle.stack), 'peak stack');
            equal(event.diagnostic ?? '', oracle.diagnostic, 'diagnostic');
            equal(received.map(([sequence, value]) => `${sequence}:${value}`).join(''), oracle.requests, 'requests');
            return { proofCount: pauses.length, maxProofMs: Math.max(0, ...pauses), queued };
        }
    } finally {
        run.close();
    }
}

async function main() {
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const oracles = JSON.parse(new TextDecoder().decode(await read('oracles.json')));
    const hello = await read('hello.nvr');
    const trap = await read('overflow.nvr');
    const clock = await read('clock.nvr');
    const regions = await read('regions.nvr');
    const sha256 = await read('sha256.nvr');
    const sorting = await read('sorting.nvr');
    const state = await read('kernelState.nvr');
    const floats = await read('floats.nvr');
    // An ungranted service binds and is answered by the runtime with error 14.
    const ungranted = new NvmExecution(module, hello);
    try {
        for (;;) {
            const event = ungranted.advanceSlice();
            if (event.kind === 'host') throw new Error('an ungranted service reached the host');
            if (event.kind === 'completed' || event.kind === 'trapped') break;
        }
    } finally {
        ungranted.close();
    }
    rejects(() => new NvmExecution({ ...module, _neura_wasm_interface: () => 2 }, hello), /adapter interface 1/);
    rejects(() => new NvmExecution(module, hello.subarray(0, 5)), /magic|header|truncated|short/i);
    const outcomes = [];
    for (const [name, image, options] of [
        ['hello', hello, {}], ['overflow', trap, {}], ['clock', clock, { args: ['héllo'] }],
        ['regions', regions, {}], ['stepLimit', regions, { maxSteps: 11n }],
        ['sha256', sha256, { args: ['6000'] }], ['sorting', sorting, { args: ['600'] }], ['kernelState', state, {}],
        ['floats', floats, {}],
    ]) {
        const kernelBoundaries = ['sha256', 'sorting', 'kernelState'].includes(name) ? new Map() : null;
        for (const [tier, budget, idle] of [[0, 1, false], [1, 17, true], [2, 1, false], [2, 17, true], [2, 4096, true]]) {
            const metrics = collect(module, image, oracles[name], { ...options, tier }, budget, idle, kernelBoundaries);
            outcomes.push({ name, tier, budget, idle, ...metrics });
        }
        if (kernelBoundaries?.size === 0) throw new Error(`${name} did not exercise a kernel step charge`);
    }
    if (!outcomes.some(c => c.name === 'regions' && c.tier === 2 && c.idle && c.proofCount > 0) ||
        !outcomes.some(c => c.name === 'regions' && c.tier === 2 && !c.idle && c.queued && c.proofCount === 0)) {
        throw new Error('the region fixture must exercise both idle proof and unproved fallback');
    }

    const execution = new NvmExecution(module, regions);
    let yields = 0;
    let denied = 0;
    let proofs = 0;
    let pending = false;
    try {
        const event = await execution.run({
            now: (() => { let t = 0; return () => t += 8; })(),
            yieldTask: () => new Promise(resolve => setTimeout(() => { ++yields; pending = false; resolve(); }, 0)),
            hasPendingEvents: () => pending,
            idle: async work => {
                // An event may arrive after scheduling the idle callback.
                pending = true;
                work();
                ++denied;
                pending = false;
                work();
                work();
            },
            onProof: () => {
                equal(pending, false, 'proof alongside external work');
                ++proofs;
            },
        });
        equal(event.steps, BigInt(oracles.regions.steps), 'driven steps');
        if (yields === 0 || denied === 0 || proofs === 0 || proofs > yields) {
            throw new Error('the execution must yield and prove at most once per idle turn');
        }
    } finally {
        execution.close();
    }
    const browserPauses = [];
    if (typeof globalThis.requestIdleCallback === 'function') {
        const run = new NvmExecution(module, regions);
        try {
            const event = await run.run({
                now: (() => { let t = 0; return () => t += 8; })(),
                onProof: elapsed => browserPauses.push(elapsed),
            });
            equal(event.steps, BigInt(oracles.regions.steps), 'browser idle steps');
        } finally {
            run.close();
        }
    }
    // Graphs built by the native compiler lift in nvm.wasm, which Clang builds (tests/lift/portable.cmake): every
    // hint of the fixture is proven without a structural rejection, with a proof at every yield.
    const portable = new NvmExecution(module, await read('portable.nvr'), { maxSteps: 10n ** 9n });
    let portableStatistics;
    try {
        // Every queued proof is taken after each short slice, so every hint the run reaches is tried.
        let event;
        do {
            event = portable.advanceSlice(64);
            while (portable.proofPending) portable.proveOneIdle();
        } while (event.kind === 'yield');
        equal(event.kind, 'completed', 'portable fixture outcome');
        equal(event.status, oracles.portable.status, 'portable fixture status');
        portableStatistics = portable.tierStatistics.split('\n')[0];
        const counts = /attempted (\d+), lifted (\d+) .*hints rejected (\d+);/.exec(portableStatistics);
        if (!counts || counts[3] !== '0' || counts[2] === '0') {
            throw new Error(`the native compiler's hints did not all lift in nvm.wasm: ${portableStatistics}`);
        }
    } finally {
        portable.close();
    }
    return { outcomes, yields, denied, proofs, browserPauses, portable: portableStatistics };
}

if (new URL(import.meta.url).protocol === 'file:') {
    console.log(JSON.stringify(await main()));
} else {
    const report = await main().catch(error => ({ error: error.stack || String(error) }));
    await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
}
