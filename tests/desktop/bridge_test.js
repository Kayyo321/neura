'use strict';

// The host half of the desktop bridge without Electron: registry parsing, the
// D-1 to D-4 limits, the router's checks, deadlines and cancellation, and the
// real Neura native child (tests/desktop/fixtures/app-child.nra, built with
// `neura build --native`) over the stdio channel. NEURA_DESKTOP_TEST_CHILD
// names the built child; run_app_test.py builds it and sets it.

const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const test = require('node:test');
const {
    BridgeRouter, DesktopStatus, FrameQueue, NativeChildChannel, NativeFrameKind: Kind, desktopLimits, parseRegistry,
} = require('../../hosts/desktop/main');

const fixtureOperations = [
    'fixture.clipboard_read', 'fixture.clipboard_write', 'fixture.close', 'fixture.data_dir', 'fixture.echo',
    'fixture.fail', 'fixture.focus', 'fixture.menu', 'fixture.menu_bad', 'fixture.notify', 'fixture.open',
    'fixture.open_many', 'fixture.open_read', 'fixture.quit', 'fixture.save', 'fixture.spin', 'fixture.title',
    'fixture.trap', 'fixture.unserved', 'fixture.wait', 'fixture.work'
];
const fixtureRegistry = Buffer.from(JSON.stringify({ operations: fixtureOperations, schema: 1 }));

function u32(value) {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32LE(value);
    return bytes;
}

function text(value) {
    const bytes = Buffer.from(value);
    return Buffer.concat([u32(bytes.length), bytes]);
}

function callBody(id, window, operation, request, nanos = 30_000_000_000n) {
    const deadline = Buffer.alloc(8);
    deadline.writeBigInt64LE(nanos);
    return Buffer.concat([u32(id), u32(window), deadline, text(operation), Buffer.from(request)]);
}

function readCall(body) {
    const length = body.readUInt32LE(16);
    return {
        id: body.readUInt32LE(0), window: body.readUInt32LE(4), nanos: body.readBigInt64LE(8),
        operation: body.toString('utf8', 20, 20 + length), request: body.subarray(20 + length),
    };
}

// A channel stand-in that records frames and accepts one write at a time.
function fakeChannel() {
    const frames = [];
    let writing = false;
    return {
        frames,
        async send(kind, body) {
            assert.equal(writing, false, 'a second write was queued behind a pending one');
            writing = true;
            await new Promise(resolve => setImmediate(resolve));
            writing = false;
            frames.push({ kind, body: Buffer.from(body) });
        },
    };
}

async function until(predicate, what) {
    const deadline = Date.now() + 5000;
    while (!predicate()) {
        if (Date.now() > deadline) throw new Error(`timed out waiting for ${what}`);
        await new Promise(resolve => setTimeout(resolve, 2));
    }
}

function router(overrides = {}) {
    const channel = fakeChannel();
    const logged = [];
    const value = new BridgeRouter({
        queue: new FrameQueue(channel), registry: parseRegistry(fixtureRegistry),
        limits: desktopLimits(overrides), log: message => logged.push(message),
    });
    return { channel, logged, router: value };
}

test('the registry is canonical and hashed as written', () => {
    const registry = parseRegistry(fixtureRegistry);
    assert.equal(registry.digest, createHash('sha256').update(fixtureRegistry).digest('hex'));
    // The digest Std.Desktop.Registry computes for the same identifiers (tests/stdlib/desktop).
    assert.equal(registry.digest, 'e917d79fbe142599476e9a5da796ee9ebcd9404133d6c92310f9183a4004cc8e');
    assert.equal(parseRegistry(Buffer.from('{"operations":[],"schema":1}')).operations.size, 0);
    for (const bad of [
        '{"schema":1,"operations":[]}', '{"operations":["b","a"],"schema":1}', '{"operations":["a","a"],"schema":1}',
        '{"operations":["A"],"schema":1}', '{"operations":[],"schema":2}', '{"operations": [],"schema":1}', '[]',
    ]) {
        assert.throws(() => parseRegistry(Buffer.from(bad)), /registry\.json/, bad);
    }
    assert.throws(() => parseRegistry(Buffer.from([0xff])), /UTF-8/);
});

test('limits default to Apps030 and refuse values past their ceilings or the child\'s', () => {
    assert.deepEqual({ ...desktopLimits() }, { windows: 8, inFlight: 32, frameBytes: 1048576, deadlineMs: 30000 });
    assert.equal(desktopLimits({ 'D-1': 2, 'D-2': 1024, 'D-4': 3600 }).windows, 2);
    // The child keeps the default D-1 and D-3, so a package may lower them and not raise them.
    for (const bad of [{ 'D-1': 9 }, { 'D-2': 0 }, { 'D-2': 1025 }, { 'D-3': 1048577 }, { 'D-4': 3601 }, { 'D-5': 1 }, { 'D-1': 1.5 }]) {
        assert.throws(() => desktopLimits(bad), RangeError, JSON.stringify(bad));
    }
});

test('an operation outside the registry never reaches the child', async () => {
    const { channel, logged, router: bridge } = router();
    assert.deepEqual(await bridge.call(1, 'fixture.missing', new Uint8Array()), { status: DesktopStatus.NotFound });
    assert.deepEqual(await bridge.call(1, 'fixture.echo', 'not bytes'), { status: DesktopStatus.InvalidInput });
    assert.equal(channel.frames.length, 0);
    assert.match(logged[0], /outside registry/);
});

test('a frame of exactly D-3 is forwarded and one byte more is TooLarge', async () => {
    const { channel, router: bridge } = router({ 'D-3': 4096 });
    const overhead = 5 + 4 + 4 + 8 + 4 + 'fixture.echo'.length;
    const pending = bridge.call(1, 'fixture.echo', new Uint8Array(4096 - overhead));
    assert.deepEqual(await bridge.call(1, 'fixture.echo', new Uint8Array(4096 - overhead + 1)), { status: DesktopStatus.TooLarge });
    await until(() => channel.frames.length === 1, 'the Call frame');
    assert.equal(5 + channel.frames[0].body.length, 4096);
    bridge.onReturn(Buffer.concat([u32(readCall(channel.frames[0].body).id), u32(0), Buffer.from('ok')]));
    assert.equal(Buffer.from((await pending).bytes).toString(), 'ok');
});

test('the call past D-2 for one window is Busy; another window is not limited by it', async () => {
    const { channel, router: bridge } = router({ 'D-2': 32 });
    const calls = [];
    for (let i = 0; i < 32; i += 1) calls.push(bridge.call(1, 'fixture.echo', Buffer.from(String(i))));
    assert.deepEqual(await bridge.call(1, 'fixture.echo', new Uint8Array()), { status: DesktopStatus.Busy });
    const other = bridge.call(2, 'fixture.echo', Buffer.from('other'));
    assert.equal(bridge.inFlight(1), 32);
    await until(() => channel.frames.length === 33, 'the Call frames');
    // Answer in reverse order: each caller receives its own reply.
    for (const frame of [...channel.frames].reverse()) {
        const call = readCall(frame.body);
        bridge.onReturn(Buffer.concat([u32(call.id), u32(0), call.request]));
    }
    const replies = await Promise.all(calls);
    replies.forEach((reply, i) => assert.equal(Buffer.from(reply.bytes).toString(), String(i)));
    assert.equal(Buffer.from((await other).bytes).toString(), 'other');
    assert.equal(bridge.inFlight(1), 0);
});

test('a call past D-4 is TimedOut, the child gets Cancel, and the late reply is dropped', async () => {
    const { channel, logged, router: bridge } = router({ 'D-4': 1 });
    const started = Date.now();
    const reply = await bridge.call(3, 'fixture.echo', Buffer.from('slow'));
    assert.deepEqual(reply, { status: DesktopStatus.TimedOut });
    assert.ok(Date.now() - started >= 990);
    await until(() => channel.frames.length === 2, 'the Cancel frame');
    const [call, cancel] = channel.frames;
    assert.equal(call.kind, Kind.Call);
    assert.equal(readCall(call.body).nanos, 1_000_000_000n);
    assert.equal(cancel.kind, Kind.Cancel);
    assert.equal(cancel.body.readUInt32LE(0), readCall(call.body).id);
    bridge.onReturn(Buffer.concat([u32(readCall(call.body).id), u32(0), Buffer.from('late')]));
    assert.match(logged.at(-1), /late or unknown/);
});

test('closing a window cancels its calls in the child and leaves the others', async () => {
    const { channel, router: bridge } = router();
    const first = bridge.call(1, 'fixture.echo', Buffer.from('a'));
    const second = bridge.call(2, 'fixture.echo', Buffer.from('b'));
    await until(() => channel.frames.length === 2, 'the Call frames');
    bridge.closeWindow(1);
    assert.deepEqual(await first, { status: DesktopStatus.Cancelled });
    await until(() => channel.frames.length === 3, 'the Cancel frame');
    assert.equal(channel.frames.at(-1).kind, Kind.Cancel);
    assert.equal(channel.frames.at(-1).body.readUInt32LE(0), readCall(channel.frames[0].body).id);
    bridge.onReturn(Buffer.concat([u32(readCall(channel.frames[1].body).id), u32(0), Buffer.from('b')]));
    assert.equal(Buffer.from((await second).bytes).toString(), 'b');
    bridge.close();
    assert.deepEqual(await bridge.call(2, 'fixture.echo', Buffer.from('c')), { status: DesktopStatus.Cancelled });
});

// The real child. Each scenario owns one process and checks that it exits.
const child = process.env.NEURA_DESKTOP_TEST_CHILD;

// Every write goes through one queue, as the window host's do.
function startChild(onFrame, digest = parseRegistry(fixtureRegistry).digest, args = [], stderr = []) {
    let queue;
    const channel = new NativeChildChannel({ executable: child, args, registryDigest: digest }, {
        onFrame: message => onFrame(message, queue), onStderr: chunk => stderr.push(chunk),
    });
    queue = new FrameQueue(channel);
    return { channel, queue, stderr };
}

function hostCall(body) {
    const id = body.readUInt32LE(0);
    const length = body.readUInt32LE(4);
    return { id, service: body.toString('utf8', 8, 8 + length), args: body.subarray(8 + length) };
}

function answer(queue, id, status, bytes = Buffer.alloc(0)) {
    return queue.send(Kind.HostReturn, Buffer.concat([u32(id), u32(status), bytes]));
}

test('the Neura child: Hello, window.open, sixteen calls, exact 64-bit text, Closed then exit 0', { skip: !child }, async () => {
    const returns = [];
    const opened = Promise.withResolvers();
    const done = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, ch) => {
        if (kind === Kind.HostCall) {
            const call = hostCall(body);
            assert.equal(call.service, 'neura.desktop.window.open');
            assert.equal(call.args.toString('hex'), Buffer.concat([text('Fixture'), u32(640), u32(480), u32(0), u32(0), text('/')]).toString('hex'));
            await answer(ch, call.id, 0, u32(1));
            opened.resolve();
        } else {
            assert.equal(kind, Kind.Return);
            returns.push({ id: body.readUInt32LE(0), status: body.readUInt32LE(4), reply: body.subarray(8).toString() });
            if (returns.length === 17) done.resolve();
        }
    });
    await channel.ready;
    await opened.promise;
    const big = '{"value":9007199254740993,"max":18446744073709551615}';
    const sends = [];
    for (let id = 1; id <= 16; id += 1) sends.push(queue.send(Kind.Call, callBody(id, 1, 'fixture.echo', id === 9 ? big : `#${id}`)));
    sends.push(queue.send(Kind.Call, callBody(17, 1, 'fixture.fail', '')));
    await Promise.all(sends);
    await done.promise;
    // Each call runs as its own task, so replies come in the order the operations finish.
    const byId = new Map(returns.map(r => [r.id, r]));
    assert.equal(byId.size, 17);
    for (let id = 1; id <= 16; id += 1) {
        assert.deepEqual([byId.get(id).status, byId.get(id).reply], [0, id === 9 ? big : `#${id}`]);
    }
    assert.equal(byId.get(17).status, DesktopStatus.Other);
    assert.equal(byId.get(17).reply, 'PermissionDenied: refused by the fixture');
    // The last window's Closed, then the end of the channel, as the window host sends them.
    await queue.send(Kind.Event, Buffer.concat([u32(0), u32(1), Buffer.alloc(16), text('')]));
    const closed = await channel.close();
    assert.equal(closed.error, null);
    assert.equal(closed.code, 0);
});

test('the Neura child answers other calls while one waits for a host request, and an unserved operation', { skip: !child }, async () => {
    const returns = [];
    let titleCall;
    const titled = Promise.withResolvers();
    const others = Promise.withResolvers();
    const finished = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, ch) => {
        if (kind === Kind.HostCall) {
            const call = hostCall(body);
            if (call.service === 'neura.desktop.window.open') {
                await answer(ch, call.id, 0, u32(4));
            } else {
                assert.equal(call.service, 'neura.desktop.window.set_title');
                assert.equal(call.args.toString('hex'), Buffer.concat([u32(4), text('Zoë ✓')]).toString('hex'));
                titleCall = call;
                titled.resolve();
            }
        } else {
            returns.push({ id: body.readUInt32LE(0), status: body.readUInt32LE(4), reply: body.subarray(8).toString() });
            if (returns.length === 2) others.resolve();
            if (returns.length === 3) finished.resolve();
        }
    });
    await channel.ready;
    await queue.send(Kind.Call, callBody(1, 4, 'fixture.title', 'Zoë ✓'));
    await titled.promise;
    // While call 1 waits for its set_title answer, calls 2 and 3 are answered.
    await queue.send(Kind.Call, callBody(2, 4, 'fixture.echo', 'not held up'));
    await queue.send(Kind.Call, callBody(3, 4, 'fixture.unserved', ''));
    await others.promise;
    assert.deepEqual(returns.map(r => [r.id, r.status, r.reply]).sort(), [
        [2, 0, 'not held up'], [3, DesktopStatus.Unsupported, 'unsupported: operation fixture.unserved'],
    ]);
    await answer(queue, titleCall.id, 0);
    await finished.promise;
    assert.deepEqual([returns[2].id, returns[2].status, returns[2].reply], [1, 0, 'titled']);
    await channel.close();
});

test('the Neura child answers calls in the order they finish (case 5)', { skip: !child }, async () => {
    const order = [];
    const done = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            order.push([body.readUInt32LE(0), body.subarray(8).toString()]);
            if (order.length === 3) done.resolve();
        }
    });
    await channel.ready;
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.wait', '600'));
    await queue.send(Kind.Call, callBody(2, 1, 'fixture.wait', '200'));
    await queue.send(Kind.Call, callBody(3, 1, 'fixture.echo', 'now'));
    await done.promise;
    assert.deepEqual(order, [[3, 'now'], [2, 'waited'], [1, 'waited']]);
    await channel.close();
});

test('a Cancel stops a waiting and a computing operation at their cancellation points; others go on', { skip: !child }, async () => {
    const returns = [];
    const echoed = Promise.withResolvers();
    const stderr = [];
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            returns.push(body.readUInt32LE(0));
            echoed.resolve();
        }
    }, undefined, [], stderr);
    await channel.ready;
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.wait', '60000'));
    await queue.send(Kind.Call, callBody(2, 1, 'fixture.work', ''));
    await queue.send(Kind.Call, callBody(3, 1, 'fixture.echo', 'still served'));
    await echoed.promise;
    // As the host does past D-4 or when the window closes: Cancel, and the caller already has its answer.
    await queue.send(Kind.Cancel, u32(1));
    await queue.send(Kind.Cancel, u32(2));
    const log = () => Buffer.concat(stderr).toString();
    await until(() => /child: wait 1 cancelled/.test(log()) && /child: work 2 stopped/.test(log()), 'both cancellations');
    await new Promise(resolve => setTimeout(resolve, 200));
    // A cancelled call's Return is not written: the host has answered its caller.
    assert.deepEqual(returns, [3]);
    const closed = await channel.close();
    assert.equal(closed.error, null);
    assert.equal(closed.code, 0);
});

test('a call that waits 20 s for news returns, and does not hold up the others (case 4)', { skip: !child, timeout: 40000 }, async () => {
    const returns = [];
    const done = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            returns.push({ id: body.readUInt32LE(0), at: Date.now(), reply: body.subarray(8).toString() });
            if (returns.length === 2) done.resolve();
        }
    });
    await channel.ready;
    const started = Date.now();
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.wait', '20000'));
    await queue.send(Kind.Call, callBody(2, 1, 'fixture.echo', 'meanwhile'));
    await done.promise;
    assert.deepEqual(returns.map(r => [r.id, r.reply]), [[2, 'meanwhile'], [1, 'waited']]);
    assert.ok(returns[0].at - started < 5000);
    assert.ok(returns[1].at - started >= 19900);
    await channel.close();
});

test('thirty-two calls of D-3 from one window in flight at once, each echoed exactly', { skip: !child, timeout: 120000 }, async () => {
    const returns = new Map();
    const done = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            returns.set(body.readUInt32LE(0), body.subarray(4));
            if (returns.size === 32) done.resolve();
        }
    });
    await channel.ready;
    // Half UTF-8 text and half arbitrary bytes, the largest request the host forwards.
    const request = id => {
        const bytes = Buffer.alloc(1048576 - 37, id % 2 ? 0x61 + id : 0xff);
        bytes.writeUInt32LE(id, 0);
        return bytes;
    };
    for (let id = 1; id <= 32; id += 1) await queue.send(Kind.Call, callBody(id, 1, 'fixture.echo', request(id)));
    await done.promise;
    for (let id = 1; id <= 32; id += 1) {
        const reply = returns.get(id);
        assert.equal(reply.readUInt32LE(0), 0);
        assert.equal(Buffer.compare(reply.subarray(4), request(id)), 0, `call ${id}`);
    }
    const closed = await channel.close();
    assert.equal(closed.code, 0);
});

test('the end of input with calls running ends the child at once with the application status (case 34)', { skip: !child }, async () => {
    const echoed = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            echoed.resolve();
        }
    });
    await channel.ready;
    // One operation asks for cancellation and one never does: neither may keep the child alive.
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.work', ''));
    await queue.send(Kind.Call, callBody(2, 1, 'fixture.spin', '1000000000000000'));
    await queue.send(Kind.Call, callBody(3, 1, 'fixture.echo', 'x'));
    await echoed.promise;
    const started = Date.now();
    const closed = await channel.close();
    assert.equal(closed.error, null);
    assert.equal(closed.code, 0);
    assert.ok(Date.now() - started < 4000, 'the child waited for its calls');
});

test('the Neura child: a failed host request reaches the operation, and quit ends with its status', { skip: !child }, async () => {
    const returns = [];
    const quitting = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, ch) => {
        if (kind === Kind.HostCall) {
            const call = hostCall(body);
            if (call.service === 'neura.desktop.window.open' && call.args.length > 40) {
                // The second window: D-1 reached on the host.
                await answer(ch, call.id, DesktopStatus.Busy);
            } else if (call.service === 'neura.desktop.window.open') {
                await answer(ch, call.id, 0, u32(1));
            } else {
                assert.equal(call.service, 'neura.desktop.quit');
                assert.equal(call.args.readUInt32LE(0), 7);
                quitting.resolve();
            }
        } else {
            returns.push({ id: body.readUInt32LE(0), status: body.readUInt32LE(4), reply: body.subarray(8).toString() });
        }
    });
    await channel.ready;
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.open', ''));
    await until(() => returns.length === 1, 'the refused window');
    await queue.send(Kind.Call, callBody(2, 1, 'fixture.quit', ''));
    await quitting.promise;
    // The host answers quit by ending the channel.
    const closed = await channel.close();
    assert.equal(closed.error, null);
    assert.equal(closed.code, 7);
    assert.equal(returns.length, 1);
    assert.equal(returns[0].status, DesktopStatus.Other);
    assert.match(returns[0].reply, /window\.open failed/);
});

test('the Neura child reassembles frames larger than one read, up to the largest Call the host forwards', { skip: !child }, async () => {
    const returns = [];
    const done = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            returns.push({ id: body.readUInt32LE(0), status: body.readUInt32LE(4), reply: body.subarray(8) });
            if (returns.length === 2) done.resolve();
        }
    });
    await channel.ready;
    const large = Buffer.alloc(300000, 0x61);
    large[0] = 0x7b;
    large[large.length - 1] = 0x7d;
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.echo', large));
    // The largest request the host forwards: a Call frame of exactly D-3.
    const largest = Buffer.alloc(1048576 - 37, 0x62);
    await queue.send(Kind.Call, callBody(2, 1, 'fixture.echo', largest));
    await done.promise;
    assert.equal(returns[0].status, 0);
    assert.equal(Buffer.compare(returns[0].reply, large), 0);
    assert.equal(returns[1].status, 0);
    assert.equal(Buffer.compare(returns[1].reply, largest), 0);
    await channel.close();
});

test('the Neura child refuses a host with another registry and exits 1', { skip: !child }, async () => {
    const other = createHash('sha256').update('{"operations":[],"schema":1}').digest('hex');
    const { channel, stderr } = startChild(() => assert.fail('no frame expected'), other);
    await assert.rejects(channel.ready, /registry SHA-256/);
    const closed = await channel.closed;
    assert.equal(closed.error.code, 'Protocol');
    assert.ok(closed.code !== 0 || closed.signal !== null);
    void stderr;
});

test('the Neura child exits at once when its input closes before any window', { skip: !child }, async () => {
    const { channel, queue } = startChild(async ({ kind, body }) => {
        if (kind === Kind.HostCall) {
            // Never answer window.open; the host goes away instead.
            assert.equal(hostCall(body).service, 'neura.desktop.window.open');
            channel.close();
        }
    });
    await channel.ready;
    const closed = await channel.closed;
    assert.equal(closed.error, null);
    assert.equal(closed.code, 1);
});

test('the Neura child runs past the 100,000,000 steps that neura run allows by default', { skip: !child }, async () => {
    const done = Promise.withResolvers();
    const { channel, queue } = startChild(async ({ kind, body }, q) => {
        if (kind === Kind.HostCall) {
            await answer(q, hostCall(body).id, 0, u32(1));
        } else {
            done.resolve({ status: body.readUInt32LE(4), reply: body.subarray(8).toString() });
        }
    });
    await channel.ready;
    // At least one step per iteration: 120,000,000 iterations are more than 100,000,000 steps.
    await queue.send(Kind.Call, callBody(1, 1, 'fixture.spin', '120000000'));
    assert.deepEqual(await done.promise, { status: 0, reply: '120000000' });
    const closed = await channel.close();
    assert.equal(closed.error, null);
    assert.equal(closed.code, 0);
});
