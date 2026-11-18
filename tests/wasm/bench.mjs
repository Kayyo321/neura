// Runs the bench-wall programs in nvm.wasm (Apps030.md, "Execution in the browser", step 3): tier 2 with
// function programs and with regions, each proof taken at the next yield as on an idle page. Reports wall time,
// output, steps, and the longest proof with its block; under Node also the main thread's CPU time per proof.
import createNvm from './nvm.mjs';
import { NvmExecution } from './nvm.js';

const node = typeof document === 'undefined';

async function read(relative) {
    const url = new URL(relative, import.meta.url);
    if (url.protocol === 'file:') {
        const { readFile } = await import('node:fs/promises');
        return new Uint8Array(await readFile(url));
    }
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${relative}: ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

const cpu = () => (node ? process.threadCpuUsage().user + process.threadCpuUsage().system : 0);

async function measure(module, image, size, regions) {
    let output = '';
    const proofs = [];
    const execution = new NvmExecution(module, image, {
        args: [String(size)], regions, maxSteps: 10n ** 12n,
        host: { console: line => void (output += line), clockWall: () => ({ seconds: 0n, nanos: 0 }),
                clockMonotonic: () => 0n },
    });
    const started = performance.now();
    try {
        const event = await execution.run({
            yieldTask: () => Promise.resolve(),
            idle: async work => {
                const before = cpu();
                const wall = performance.now();
                work();
                const elapsed = performance.now() - wall;
                proofs.push({ wall: elapsed, cpu: (cpu() - before) / 1000, ...execution.lastProof });
            },
        });
        const milliseconds = performance.now() - started;
        const longest = proofs.reduce((a, b) => (b.wall > a.wall ? b : a), { wall: 0, cpu: 0 });
        return { kind: event.kind, status: event.status, steps: String(event.steps), output, milliseconds,
                 proofs: proofs.length, longest, longestCpu: Math.max(0, ...proofs.map(p => p.cpu)) };
    } finally {
        execution.close();
    }
}

async function main() {
    const module = await createNvm({ wasmBinary: await read('nvm.wasm') });
    const programs = JSON.parse(new TextDecoder().decode(await read('programs.json')));
    const results = [];
    for (const { name, size, runs } of programs) {
        const image = await read(`${name}.nvr`);
        for (const regions of [false, true]) {
            const samples = [];
            for (let run = 0; run < runs; ++run) samples.push(await measure(module, image, size, regions));
            results.push({ name, regions, samples });
        }
    }
    return results;
}

if (node) {
    console.log(JSON.stringify(await main()));
} else {
    const report = await main().catch(error => ({ error: error.stack || String(error) }));
    await fetch(`/result${location.search}`, { method: 'POST', body: JSON.stringify(report) });
}
