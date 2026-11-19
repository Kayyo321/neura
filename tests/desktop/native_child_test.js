'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { setTimeout: delay } = require('node:timers/promises');
const { NativeChildChannel, NativeFrameKind: Kind } = require('../../hosts/desktop/main.js');

const helper = path.join(__dirname, 'native_child_helper.js');
const registryDigest = createHash('sha256').update('{"operations":[]}\n').digest('hex');
const defaultLimit = 1024 * 1024;
const hardLimit = 64 * 1024 * 1024;

function launch(t, mode, { frameLimit, ...options } = {}) {
    const log = [];
    const channel = new NativeChildChannel({
        executable: process.execPath,
        args: [helper, mode, registryDigest, String(frameLimit ?? defaultLimit), 'literal $HOME; λ & spaces'],
        environment: { ...process.env, NEURA_TRUSTED_TEST_TOKEN: 'fixture-only' },
        registryDigest,
        frameLimit,
    }, { onStderr: chunk => log.push(chunk), shutdownGrace: 250, ...options });
    t.after(() => channel.close());
    return { channel, log };
}

function exited(pid) {
    assert.throws(() => process.kill(pid, 0), { code: 'ESRCH' });
}

test('fragmented Hello, separate stderr, and ordered frame delivery', { timeout: 6000 }, async t => {
    const completed = Promise.withResolvers();
    const received = [];
    const logged = Promise.withResolvers();
    const log = [];
    const { channel } = launch(t, 'fragmented', {
        onFrame(message) {
            received.push(message);
            if (received.length === 6) completed.resolve();
        },
        onStderr(data) {
            log.push(data);
            if (Buffer.concat(log).includes(10)) logged.resolve();
        },
    });
    await assert.rejects(channel.send(Kind.Call, Buffer.alloc(0)), { code: 'NotReady' });
    await channel.ready;
    await logged.promise;
    assert.equal(Buffer.concat(log).toString(), 'binary protocol uses stdout\n');
    const opaque = Buffer.from([0, 255, 128, 49, 50]);
    await channel.send(Kind.Call, opaque);
    await channel.send(Kind.Call, Buffer.from('coalesce'));
    for (const kind of [Kind.Cancel, Kind.HostReturn, Kind.Event]) await channel.send(kind, Buffer.from([kind]));
    await completed.promise;
    assert.deepEqual(received, [
        { kind: Kind.Return, body: opaque },
        { kind: Kind.HostCall, body: Buffer.from([0, 1, 255]) },
        { kind: Kind.Return, body: Buffer.alloc(0) },
        ...[Kind.Cancel, Kind.HostReturn, Kind.Event].map(kind => ({ kind: Kind.Return, body: Buffer.from([kind]) })),
    ]);
    assert.deepEqual(await channel.close(), { error: null, code: 0, signal: null });
    assert.equal(channel.close(), channel.closed);
    exited(channel.pid);
});

test('both peers can send Hello before starting to read', { timeout: 6000 }, async t => {
    const { channel } = launch(t, 'simultaneous');
    await channel.ready;
    assert.equal(channel.active, true);
    assert.deepEqual(await channel.close(), { error: null, code: 0, signal: null });
    exited(channel.pid);
});

for (const [mode, error, message] of [
    ['old', 'Protocol', /version 0, expected 1/],
    ['crossed', 'Protocol', /first native child frame must be Hello/],
    ['wrong-registry', 'Protocol', new RegExp(`registry SHA-256 ${'ff'.repeat(32)}, expected ${registryDigest}`)],
    ['bad-hello-size', 'Protocol', /Hello has the wrong size/],
    ['zero', 'Protocol', /frame has no kind/],
    ['oversized', 'TooLarge', /frame exceeds 67108864 bytes/],
    ['short-header', 'Protocol', /partial frame/],
    ['short-body', 'Protocol', /partial frame/],
    ['early-death', 'ChildExited', /exited unexpectedly|closed stdout unexpectedly/],
    ['timeout', 'StartupTimeout', /did not complete Hello/],
]) {
    test(`startup rejects ${mode} and reaps the child`, { timeout: 6000 }, async t => {
        let forwarded = false;
        const { channel } = launch(t, mode, {
            frameLimit: hardLimit, handshakeTimeout: 450,
            onFrame() { forwarded = true; },
        });
        await assert.rejects(channel.ready, { code: error, message });
        const result = await channel.closed;
        assert.equal(result.error.code, error);
        assert.equal(forwarded, false);
        if (mode === 'early-death') assert.equal(result.code, 37);
        assert.equal(channel.active, false);
        await assert.rejects(channel.send(Kind.Call, Buffer.alloc(0)), { code: 'NotReady' });
        exited(channel.pid);
    });
}

for (const mode of ['duplicate', 'unknown-kind', 'wrong-direction']) {
    test(`the ready channel refuses ${mode}`, { timeout: 6000 }, async t => {
        const { channel } = launch(t, mode);
        await channel.ready;
        assert.equal((await channel.closed).error.code, 'Protocol');
        exited(channel.pid);
    });
}

test('D-3 admits an exact-size frame in each direction and refuses plus one', { timeout: 6000 }, async t => {
    const received = Promise.withResolvers();
    const { channel } = launch(t, 'normal', { onFrame: received.resolve });
    await channel.ready;
    await assert.rejects(channel.send(Kind.Call, Buffer.alloc(defaultLimit - 4)), { code: 'TooLarge' });
    await assert.rejects(channel.send(Kind.Hello, Buffer.alloc(0)), { code: 'Protocol' });
    const body = Buffer.alloc(defaultLimit - 5, 0xaf);
    await channel.send(Kind.Call, body);
    assert.deepEqual(await received.promise, { kind: Kind.Return, body });
});

test('a child crash after Hello closes the transport and reports its exit', { timeout: 6000 }, async t => {
    const { channel } = launch(t, 'normal');
    await channel.ready;
    await channel.send(Kind.Call, Buffer.from('trap'));
    const result = await channel.closed;
    assert.equal(result.error.code, 'ChildExited');
    assert.equal(result.code, 73);
    exited(channel.pid);
});

test('a stopped child fills the pipe, refuses a second write, and is killed at close', {
    timeout: 6000, skip: process.platform === 'win32',
}, async t => {
    const { channel } = launch(t, 'normal', { shutdownGrace: 300 });
    await channel.ready;
    process.kill(channel.pid, 'SIGSTOP');
    let settled = false;
    const write = channel.send(Kind.Call, Buffer.alloc(defaultLimit - 5));
    const refusedWrite = assert.rejects(write.finally(() => { settled = true; }), { code: 'Pipe' });
    await assert.rejects(channel.send(Kind.Cancel, Buffer.alloc(0)), { code: 'PipeBusy' });
    await delay(30);
    assert.equal(settled, false);
    const closed = await channel.close();
    await refusedWrite;
    assert.deepEqual(closed, { error: null, code: null, signal: 'SIGKILL' });
    exited(channel.pid);
});

test('explicit close during Hello and failed spawn both settle', { timeout: 6000 }, async t => {
    const { channel } = launch(t, 'timeout');
    const closed = channel.close();
    await assert.rejects(channel.ready, { code: 'Closed' });
    assert.equal((await closed).error, null);
    exited(channel.pid);

    const unavailable = new NativeChildChannel({
        executable: path.join(__dirname, 'absent-test-runner'), registryDigest,
    });
    await assert.rejects(unavailable.ready, { code: 'ChildProcess' });
    assert.equal((await unavailable.closed).error.cause.code, 'ENOENT');
});

test('non-absolute executable and frame limits outside the protocol are rejected before spawn', () => {
    assert.throws(() => new NativeChildChannel({ executable: 'node', registryDigest }), /absolute trusted path/);
    for (const frameLimit of [40, hardLimit + 1]) {
        assert.throws(() => new NativeChildChannel({ executable: process.execPath, registryDigest, frameLimit }),
                      /Invalid native child frame limit/);
    }
});
