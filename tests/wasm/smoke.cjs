'use strict';

const fs = require('node:fs');
const path = require('node:path');

async function main() {
    const [wrapper, imagePath, mode = '2', growth] = process.argv.slice(2);
    const runtime = await require(path.resolve(wrapper))();
    if (runtime.HEAPU8.buffer instanceof SharedArrayBuffer) throw new Error('expected a single-threaded wasm runtime');
    if (growth === 'grow') {
        const initial = runtime.HEAPU8.length;
        const block = runtime._malloc(initial);
        if (!block || runtime.HEAPU8.length <= initial) throw new Error('the wasm memory did not grow');
        runtime._free(block);
    }
    const image = fs.readFileSync(imagePath);
    const pointer = runtime._malloc(image.length);
    if (!pointer) throw new Error('could not allocate the smoke image');
    try {
        runtime.HEAPU8.set(image, pointer);
        process.exitCode = runtime._neura_smoke(pointer, image.length, Number(mode));
    } finally {
        runtime._free(pointer);
    }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
