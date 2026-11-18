// The byte encodings of neura.ui.commit and neura.ui.event.next, version 1
// (docs/runtime/Providers.md, "UI commit encoding" and "UI event encoding").
// The encoders are for tests and other hosts; the browser host decodes
// commits and encodes events.
export const COMMIT_ENCODING = 1;
export const EVENT_ENCODING = 1;

const OPS = ["remove", "element", "text", "move", "setText", "attribute", "variable", "handler", "property"];
const EVENT_KINDS = ["click", "dblclick", "input", "change", "submit", "keydown", "keyup", "focus", "blur",
    "pointerdown", "pointerup", "pointermove", "scroll", "toggle", "close", "overflow"];
const POINTER_KINDS = new Map([["mouse", 0], ["pen", 1], ["touch", 2]]);
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });

export class WireError extends Error {}

class Reader {
    constructor(bytes) {
        if (!(bytes instanceof Uint8Array)) throw new WireError("expected bytes");
        this.bytes = bytes;
        this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        this.at = 0;
    }

    need(size) {
        if (this.bytes.length - this.at < size) throw new WireError("the encoding is truncated");
        const at = this.at;
        this.at += size;
        return at;
    }

    u8() { return this.view.getUint8(this.need(1)); }
    u32() { return this.view.getUint32(this.need(4), true); }
    i32() { return this.view.getInt32(this.need(4), true); }
    u64() { return this.view.getBigUint64(this.need(8), true); }
    f64() { return this.view.getFloat64(this.need(8), true); }

    flag() {
        const value = this.u8();
        if (value > 1) throw new WireError("a flag must be 0 or 1");
        return value === 1;
    }

    text() {
        const size = this.u32();
        const at = this.need(size);
        try {
            return decoder.decode(this.bytes.subarray(at, at + size));
        } catch {
            throw new WireError("text is not valid UTF-8");
        }
    }

    finish() {
        if (this.at !== this.bytes.length) throw new WireError("the encoding has trailing bytes");
    }
}

class Writer {
    constructor() {
        this.parts = [];
        this.size = 0;
    }

    raw(bytes) {
        this.parts.push(bytes);
        this.size += bytes.length;
    }

    fixed(size, write) {
        const bytes = new Uint8Array(size);
        write(new DataView(bytes.buffer));
        this.raw(bytes);
    }

    u8(value) { this.fixed(1, view => view.setUint8(0, value)); }
    u32(value) { this.fixed(4, view => view.setUint32(0, value, true)); }
    i32(value) { this.fixed(4, view => view.setInt32(0, value, true)); }
    u64(value) { this.fixed(8, view => view.setBigUint64(0, BigInt(value), true)); }
    f64(value) { this.fixed(8, view => view.setFloat64(0, value, true)); }

    text(value) {
        const bytes = encoder.encode(value);
        this.u32(bytes.length);
        this.raw(bytes);
    }

    bytes() {
        const out = new Uint8Array(this.size);
        let at = 0;
        for (const part of this.parts) {
            out.set(part, at);
            at += part.length;
        }
        return out;
    }
}

// Node 0 is the container and never an insertion anchor, so 0 encodes "append".
const anchor = value => (value === 0 ? null : value);

export function decodeCommit(bytes) {
    const reader = new Reader(bytes);
    if (reader.u8() !== COMMIT_ENCODING) throw new WireError("unknown commit encoding");
    const count = reader.u32();
    // Every operation takes at least five bytes.
    if (count > (bytes.length - reader.at) / 5) throw new WireError("the encoding is truncated");
    const operations = [];
    for (let i = 0; i < count; ++i) {
        const op = OPS[reader.u8() - 1];
        if (op === undefined) throw new WireError("unknown commit operation");
        const id = reader.u32();
        switch (op) {
        case "remove":
            operations.push({ op, id });
            break;
        case "element": {
            const parent = reader.u32();
            const before = anchor(reader.u32());
            const svg = reader.flag();
            operations.push({ op, id, parent, before, svg, name: reader.text() });
            break;
        }
        case "text": {
            const parent = reader.u32();
            const before = anchor(reader.u32());
            operations.push({ op, id, parent, before, value: reader.text() });
            break;
        }
        case "move": {
            const parent = reader.u32();
            operations.push({ op, id, parent, before: anchor(reader.u32()) });
            break;
        }
        case "setText":
            operations.push({ op, id, value: reader.text() });
            break;
        case "attribute": {
            const name = reader.text();
            operations.push({ op, id, name, value: reader.flag() ? reader.text() : null });
            break;
        }
        case "variable": {
            const name = reader.text();
            let value = null;
            if (reader.flag()) {
                const number = reader.f64();
                value = { number, unit: reader.text() };
            }
            operations.push({ op, id, name, value });
            break;
        }
        case "handler": {
            const event = reader.text();
            const handler = reader.u64();
            const flags = reader.u8();
            if (flags > 3) throw new WireError("unknown handler flags");
            operations.push({ op, id, event, handler: handler === 0n ? null : handler,
                              preventDefault: (flags & 1) !== 0, stopPropagation: (flags & 2) !== 0 });
            break;
        }
        case "property": {
            const name = reader.text();
            const kind = reader.u8();
            if (kind > 1) throw new WireError("unknown property kind");
            operations.push({ op, id, name, value: kind === 0 ? reader.text() : reader.flag() });
            break;
        }
        }
    }
    reader.finish();
    return operations;
}

export function encodeCommit(operations) {
    const writer = new Writer();
    writer.u8(COMMIT_ENCODING);
    writer.u32(operations.length);
    for (const op of operations) {
        const code = OPS.indexOf(op.op);
        if (code < 0) throw new WireError(`unknown operation ${op.op}`);
        writer.u8(code + 1);
        writer.u32(op.id);
        switch (op.op) {
        case "element":
            writer.u32(op.parent);
            writer.u32(op.before ?? 0);
            writer.u8(op.svg ? 1 : 0);
            writer.text(op.name);
            break;
        case "text":
            writer.u32(op.parent);
            writer.u32(op.before ?? 0);
            writer.text(op.value);
            break;
        case "move":
            writer.u32(op.parent);
            writer.u32(op.before ?? 0);
            break;
        case "setText":
            writer.text(op.value);
            break;
        case "attribute":
            writer.text(op.name);
            writer.u8(op.value === null ? 0 : 1);
            if (op.value !== null) writer.text(op.value);
            break;
        case "variable":
            writer.text(op.name);
            writer.u8(op.value === null ? 0 : 1);
            if (op.value !== null) {
                writer.f64(op.value.number);
                writer.text(op.value.unit);
            }
            break;
        case "handler":
            writer.text(op.event);
            writer.u64(op.handler ?? 0);
            writer.u8((op.preventDefault ? 1 : 0) | (op.stopPropagation ? 2 : 0));
            break;
        case "property":
            writer.text(op.name);
            writer.u8(typeof op.value === "boolean" ? 1 : 0);
            if (typeof op.value === "boolean") writer.u8(op.value ? 1 : 0);
            else writer.text(op.value);
            break;
        }
    }
    return writer.bytes();
}

const modifierBits = m => (m.alt ? 1 : 0) | (m.ctrl ? 2 : 0) | (m.meta ? 4 : 0) | (m.shift ? 8 : 0);
const modifierRecord = bits => ({ alt: (bits & 1) !== 0, ctrl: (bits & 2) !== 0, meta: (bits & 4) !== 0,
                                  shift: (bits & 8) !== 0 });
// A button of -1 (no button changed, as in pointermove) is 255.
const button = value => value & 0xff;

// `records` are the DomRoot event snapshots, plus { event: "overflow",
// dropped } for RootEvent.InputOverflow.
export function encodeEvents(records) {
    const writer = new Writer();
    writer.u8(EVENT_ENCODING);
    writer.u32(records.length);
    for (const record of records) {
        const kind = EVENT_KINDS.indexOf(record.event);
        if (kind < 0) throw new WireError(`unknown event ${record.event}`);
        writer.u8(kind + 1);
        writer.u32(record.node ?? 0);
        writer.u64(record.handler ?? 0);
        const f = record.fields ?? {};
        switch (record.event) {
        case "click":
        case "dblclick":
            writer.u8(button(f.button));
            writer.f64(f.x);
            writer.f64(f.y);
            writer.u8(modifierBits(f.modifiers));
            writer.i32(f.detail | 0);
            break;
        case "input":
            writer.text(f.value);
            writer.u8(f.checked ? 1 : 0);
            writer.u8(f.composing ? 1 : 0);
            break;
        case "change":
            writer.text(f.value);
            writer.u8(f.checked ? 1 : 0);
            break;
        case "submit":
            writer.text(f.submitter);
            break;
        case "keydown":
        case "keyup":
            writer.text(f.key);
            writer.text(f.code);
            writer.u8(modifierBits(f.modifiers));
            writer.u8(f.repeat ? 1 : 0);
            break;
        case "pointerdown":
        case "pointerup":
        case "pointermove":
            writer.f64(f.x);
            writer.f64(f.y);
            writer.u8(button(f.button));
            writer.u8(POINTER_KINDS.get(f.kind) ?? 3);
            writer.u8(modifierBits(f.modifiers));
            break;
        case "scroll":
            writer.f64(f.top);
            writer.f64(f.left);
            break;
        case "toggle":
            writer.u8(f.open ? 1 : 0);
            break;
        case "close":
            writer.text(f.return_value);
            break;
        case "overflow":
            writer.u32(record.dropped);
            break;
        }
    }
    return writer.bytes();
}

export function decodeEvents(bytes) {
    const reader = new Reader(bytes);
    if (reader.u8() !== EVENT_ENCODING) throw new WireError("unknown event encoding");
    const count = reader.u32();
    if (count > (bytes.length - reader.at) / 13) throw new WireError("the encoding is truncated");
    const records = [];
    for (let i = 0; i < count; ++i) {
        const event = EVENT_KINDS[reader.u8() - 1];
        if (event === undefined) throw new WireError("unknown event kind");
        const node = reader.u32();
        const handler = reader.u64();
        let fields = {};
        switch (event) {
        case "click":
        case "dblclick":
            fields = { button: reader.u8(), x: reader.f64(), y: reader.f64(), modifiers: modifierRecord(reader.u8()),
                       detail: reader.i32() };
            break;
        case "input":
            fields = { value: reader.text(), checked: reader.flag(), composing: reader.flag() };
            break;
        case "change":
            fields = { value: reader.text(), checked: reader.flag() };
            break;
        case "submit":
            fields = { submitter: reader.text() };
            break;
        case "keydown":
        case "keyup":
            fields = { key: reader.text(), code: reader.text(), modifiers: modifierRecord(reader.u8()),
                       repeat: reader.flag() };
            break;
        case "pointerdown":
        case "pointerup":
        case "pointermove":
            fields = { x: reader.f64(), y: reader.f64(), button: reader.u8(),
                       kind: ["mouse", "pen", "touch", "other"][reader.u8()], modifiers: modifierRecord(reader.u8()) };
            if (fields.kind === undefined) throw new WireError("unknown pointer kind");
            break;
        case "scroll":
            fields = { top: reader.f64(), left: reader.f64() };
            break;
        case "toggle":
            fields = { open: reader.flag() };
            break;
        case "close":
            fields = { return_value: reader.text() };
            break;
        case "overflow":
            fields = { dropped: reader.u32() };
            break;
        }
        records.push({ event, node, handler, fields });
    }
    reader.finish();
    return records;
}
