'use strict';

// Drives the real desktop channel from a console host for the project tool
// lifecycle tests. The first window request stays pending until shutdown.
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { readFile } = require('node:fs/promises');
const { loadDesktopPackage, NativeChildChannel, NativeFrameKind, parseRegistry } = require(process.env.NEURA_TEST_DESKTOP_HOST);

async function main() {
    const flag = '--neura-package=';
    assert(process.argv[2].startsWith(flag));
    const configuration = await loadDesktopPackage(process.argv[2].slice(flag.length));
    const { executable } = configuration.nativeChild;
    assert(executable.endsWith('.exe'));
    const digest = createHash('sha256').update(await readFile(executable)).digest('hex');
    const channel = new NativeChildChannel({
        ...configuration.nativeChild,
        registryDigest: parseRegistry(configuration.registry).digest,
    }, {
        onFrame(message) {
            assert.equal(message.kind, NativeFrameKind.HostCall);
            console.log(`neura desktop probe: ready (${process.pid}, ${channel.pid}) ${digest}`);
        },
    });
    await channel.ready;
    const result = await channel.closed;
    if (result.error) throw result.error;
}

main().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
