'use strict';

// A process-owned test peer. This builds its wire bytes independently of
// the production channel and verifies the host's first frame.
const assert = require('node:assert/strict');
const { existsSync, promises: fs } = require('node:fs');
const { setTimeout: delay } = require('node:timers/promises');

const [mode, digest, limitText, literal] = process.argv.slice(2);
const limit = Number(limitText);
const registry = Buffer.from(digest, 'hex');

function wire(kind, body = Buffer.alloc(0)) {
    const bytes = Buffer.alloc(5 + body.length);
    bytes.writeUInt32LE(1 + body.length);
    bytes[4] = kind;
    body.copy(bytes, 5);
    return bytes;
}

function hello(version = 1, hash = registry) {
    const body = Buffer.alloc(36);
    body.writeUInt32LE(version);
    hash.copy(body, 4);
    return wire(1, body);
}

function write(bytes) {
    return new Promise((resolve, reject) => process.stdout.write(bytes, error => error ? reject(error) : resolve()));
}

async function respondToHello() {
    if (mode === 'shell' || mode === 'shell-mismatch') {
        await fs.writeFile(`${literal}.pid`, String(process.pid));
        while (!existsSync(literal)) await delay(10);
        if (mode === 'shell-mismatch') return write(hello(1, Buffer.alloc(32, 255)));
        setInterval(() => {
            if (existsSync(`${literal}.trap`)) process.exit(73);
        }, 10).unref();
        return write(hello());
    }
    switch (mode) {
    case 'old': return write(hello(0));
    case 'crossed': return write(wire(3));
    case 'wrong-registry': return write(hello(1, Buffer.alloc(32, 255)));
    case 'bad-hello-size': return write(wire(1));
    case 'zero': return write(Buffer.alloc(4));
    case 'oversized': {
        const prefix = Buffer.alloc(4);
        prefix.writeUInt32LE(limit - 3); // one byte above the complete-frame limit
        return write(prefix);
    }
    case 'short-header':
        await write(hello().subarray(0, 3));
        return process.exit(23);
    case 'short-body':
        await write(hello().subarray(0, 37));
        return process.exit(24);
    case 'timeout': return;
    case 'simultaneous': return;
    case 'fragmented': {
        const bytes = hello();
        for (const [start, end] of [[0, 1], [1, 4], [4, 7], [7, bytes.length]]) {
            await write(bytes.subarray(start, end));
            await delay(5);
        }
        process.stderr.write('binary protocol uses stdout\n');
        return;
    }
    case 'duplicate': return write(Buffer.concat([hello(), hello()]));
    case 'unknown-kind': return write(Buffer.concat([hello(), wire(255)]));
    case 'wrong-direction': return write(Buffer.concat([hello(), wire(2)]));
    default: return write(hello());
    }
}

async function main() {
    if (mode === 'early-death') process.exit(37);
    if (mode === 'simultaneous') await write(hello());
    if (mode === 'fragmented') {
        assert.equal(literal, 'literal $HOME; λ & spaces');
        assert.equal(process.env.NEURA_TRUSTED_TEST_TOKEN, 'fixture-only');
    }
    let pending = Buffer.alloc(0);
    let started = false;
    for await (const chunk of process.stdin) {
        pending = Buffer.concat([pending, chunk]);
        while (pending.length >= 5) {
            const total = pending.readUInt32LE() + 4;
            assert.ok(total >= 5 && total <= limit);
            if (pending.length < total) break;
            const frame = pending.subarray(0, total);
            pending = pending.subarray(total);
            if (!started) {
                assert.deepEqual(frame, hello());
                started = true;
                await respondToHello();
            } else {
                assert.ok([2, 4, 6, 7].includes(frame[4]), 'unexpected host frame kind');
                const body = frame.subarray(5);
                if (body.toString() === 'trap') process.exit(73);
                if (body.toString() === 'coalesce') {
                    await write(Buffer.concat([wire(5, Buffer.from([0, 1, 255])), wire(3)]));
                } else {
                    await write(wire(3, body));
                }
            }
        }
    }
    assert.equal(pending.length, 0, 'host closed a partial frame');
    if (mode === 'shell') await fs.writeFile(`${literal}.eof`, 'EOF');
}

main().catch(error => {
    console.error(error);
    process.exitCode = 12;
});
