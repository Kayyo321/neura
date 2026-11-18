// Applies decoded UI commit operations to one browser root.
const SVG = "http://www.w3.org/2000/svg";
const MAX_NODES = 100_000;
const MAX_TEXT_BYTES = 1024 * 1024;
const ROOT = 0;

const HTML_ELEMENTS = new Set((
    "main section article aside header footer nav address search hgroup h1 h2 h3 h4 h5 h6 " +
    "div p hr pre blockquote ol ul menu li dl dt dd figure figcaption " +
    "a em strong small s cite q dfn abbr data time code var samp kbd sub sup i b u mark bdi bdo " +
    "span br wbr ruby rt rp ins del img picture source audio video track " +
    "table caption colgroup col thead tbody tfoot tr th td " +
    "form label input button select optgroup option textarea output progress meter fieldset legend datalist " +
    "details summary dialog"
).split(" "));
const SVG_ELEMENTS = new Set((
    "svg g path circle ellipse line polyline polygon rect text tspan title desc defs use symbol " +
    "linearGradient radialGradient stop clipPath mask"
).split(" "));
const URL_ATTRIBUTES = new Set(["href", "src", "poster", "cite", "action", "formaction"]);
const URL_SCHEMES = new Set(["http", "https", "mailto", "tel"]);
const CSS_UNITS = new Set(["", "%", "px", "em", "rem", "ch", "vw", "vh", "cm", "mm", "in", "pt", "pc"]);
const PROPERTIES = new Map([
    ["value", { type: "string", tags: ["input", "textarea", "select"] }],
    ["checked", { type: "boolean", tags: ["input"] }],
    ["indeterminate", { type: "boolean", tags: ["input"] }],
    ["selected", { type: "boolean", tags: ["option"] }],
    ["open", { type: "boolean", tags: ["details", "dialog"] }],
]);
const EVENTS = new Map([
    ["click", [true, true]], ["dblclick", [true, true]], ["input", [false, true]],
    ["change", [false, true]], ["submit", [true, true]], ["keydown", [true, true]],
    ["keyup", [true, true]], ["focus", [false, false]], ["blur", [false, false]],
    ["pointerdown", [true, true]], ["pointerup", [false, true]], ["pointermove", [false, true]],
    ["scroll", [false, false]], ["toggle", [false, false]], ["close", [false, false]],
]);
const EVENT_ELEMENTS = new Map([
    ["input", ["input", "select", "textarea"]], ["change", ["input", "select", "textarea"]],
    ["submit", ["form"]], ["toggle", ["details", "dialog"]], ["close", ["dialog"]],
]);
const PHASE = new Map([
    ["remove", 0],
    ["element", 1], ["text", 1], ["move", 1],
    ["setText", 2], ["attribute", 2], ["variable", 2],
    ["handler", 3],
    ["property", 4],
]);
// Media attributes whose bundled files the compiler names "assets/<digest>.<extension>", relative to the bundle.
const ASSET_ATTRIBUTES = new Map([
    ["src", ["img", "source", "video", "audio", "track"]], ["poster", ["video"]], ["srcset", ["img", "source"]],
]);
const ASSET_PREFIX = "assets/";
const ACTIVE_ROOTS = new Set();
const encoder = new TextEncoder();

export class InvalidBatch extends Error {}
export class RootFault extends Error {}

function check(condition, message) {
    if (!condition) throw new InvalidBatch(message);
}

function nodeId(value) {
    return Number.isSafeInteger(value) && value >= 0;
}

function text(value) {
    return typeof value === "string" && value.length <= MAX_TEXT_BYTES &&
        (value.length <= MAX_TEXT_BYTES / 3 || encoder.encode(value).length <= MAX_TEXT_BYTES);
}

function allowedUrl(value) {
    if (/[\u0000-\u0020\u007f\\]/u.test(value)) return false;
    const match = /^([^/?#:]*):/u.exec(value);
    return match === null || URL_SCHEMES.has(match[1].toLowerCase());
}

function variable(value) {
    return value === null || (value && Number.isFinite(value.number) &&
        (CSS_UNITS.has(value.unit) ||
         (value.unit === "rgb" && Number.isInteger(value.number) && value.number >= 0 && value.number <= 0xffffff)));
}

function variableText(value) {
    return value.unit === "rgb" ? "#" + value.number.toString(16).padStart(6, "0") : value.number + value.unit;
}

// Simulate ownership and topology using only changed records. Validating
// every operation first keeps a rejected batch from touching the live tree.
class BatchValidation {
    constructor(records) {
        this.records = records;
        this.changed = new Map();
        this.count = records.size - 1;
        this.phase = -1;
    }

    lookup(id) {
        return this.changed.has(id) ? this.changed.get(id) : this.records.get(id);
    }

    require(id, rootAllowed = false) {
        check(nodeId(id) && (rootAllowed || id !== ROOT), "invalid node handle");
        const record = this.lookup(id);
        check(record, "unknown node handle");
        const live = this.records.get(id);
        if (id !== ROOT && record.node && live) {
            check(live.node.parentNode === this.records.get(live.parent).node &&
                  this.records.get(ROOT).node.contains(live.node), "managed node has left its assigned subtree");
        }
        return record;
    }

    editable(id) {
        if (!this.changed.has(id)) {
            const record = this.records.get(id);
            this.changed.set(id, { ...record, children: new Set(record.children) });
        }
        return this.changed.get(id);
    }

    parentFor(op, record) {
        const parent = this.require(op.parent, true);
        check(parent.kind === "element", "the parent must be an element");
        if (op.before !== null) {
            const before = this.require(op.before);
            check(op.before !== op.id && before.parent === op.parent, "the insertion anchor must be a sibling");
        }
        // Entering SVG requires an svg element at its boundary.
        if (record.kind === "element") {
            check(record.svg === (parent.svg || record.tag === "svg"), "element namespace does not match its parent");
        }
        return parent;
    }

    remove(id) {
        const record = this.require(id);
        this.editable(record.parent).children.delete(id);
        const pending = [id];
        while (pending.length) {
            const child = pending.pop();
            for (const descendant of this.lookup(child).children) pending.push(descendant);
            this.changed.set(child, null);
            this.count -= 1;
        }
    }

    create(op) {
        check(nodeId(op.id) && op.id !== ROOT && !this.lookup(op.id), "the new node handle must be unused");
        const record = { kind: op.op, tag: op.name, svg: op.svg, parent: op.parent, children: new Set() };
        if (op.op === "element") {
            check(typeof op.svg === "boolean" && (op.svg ? SVG_ELEMENTS : HTML_ELEMENTS).has(op.name),
                  "unknown element in decoded operation");
        } else {
            check(text(op.value), "invalid text value");
        }
        this.parentFor(op, record);
        this.changed.set(op.id, record);
        this.editable(op.parent).children.add(op.id);
        this.count += 1;
        check(this.count <= MAX_NODES, "root node limit exceeded");
    }

    move(op) {
        const record = this.require(op.id);
        this.parentFor(op, record);
        for (let ancestor = op.parent; ancestor !== ROOT; ancestor = this.lookup(ancestor).parent) {
            check(ancestor !== op.id, "a node cannot move into its own subtree");
        }
        if (record.parent !== op.parent) {
            this.editable(record.parent).children.delete(op.id);
            this.editable(op.parent).children.add(op.id);
            this.editable(op.id).parent = op.parent;
        }
    }

    write(op) {
        const record = this.require(op.id);
        if (op.op === "setText") {
            check(record.kind === "text" && text(op.value), "invalid text write");
            return;
        }
        check(record.kind === "element", "the write needs an element");
        if (op.op === "attribute") {
            const validName = typeof op.name === "string" && /^[A-Za-z][A-Za-z0-9_-]*$/u.test(op.name);
            check(validName && !/^(?:on|style$|srcdoc$|is$)/iu.test(op.name), "attribute name is unavailable");
            check(op.value === null || text(op.value), "invalid attribute value");
            if (op.value !== null && URL_ATTRIBUTES.has(op.name.toLowerCase()))
                check(allowedUrl(op.value), "URL scheme is unavailable");
        } else if (op.op === "variable") {
            check(typeof op.name === "string" && /^--[A-Za-z0-9_-]+$/u.test(op.name) && variable(op.value),
                  "invalid custom property");
        } else if (op.op === "handler") {
            const allowed = EVENTS.get(op.event);
            const elements = EVENT_ELEMENTS.get(op.event);
            check(!record.svg && allowed && (!elements || elements.includes(record.tag)), "event is unavailable");
            check(op.handler === null || (nodeId(op.handler) && op.handler !== 0) ||
                  (typeof op.handler === "bigint" && op.handler > 0n && op.handler <= 0xffff_ffff_ffff_ffffn),
                  "invalid handler handle");
            check(typeof op.preventDefault === "boolean" && typeof op.stopPropagation === "boolean" &&
                  (!op.preventDefault || allowed[0]) && (!op.stopPropagation || allowed[1]),
                  "unsupported event modifier");
        } else {
            const property = PROPERTIES.get(op.name);
            check(!record.svg && property && property.tags.includes(record.tag) &&
                  typeof op.value === property.type && (property.type !== "string" || text(op.value)),
                  "invalid DOM property write");
        }
    }

    validate(operations) {
        check(Array.isArray(operations), "expected decoded UI operations");
        for (const op of operations) {
            check(op !== null && typeof op === "object", "expected one decoded operation");
            const phase = PHASE.get(op.op);
            check(phase !== undefined && phase >= this.phase, "invalid operation order");
            this.phase = phase;
            if (op.op === "remove") this.remove(op.id);
            else if (op.op === "element" || op.op === "text") this.create(op);
            else if (op.op === "move") this.move(op);
            else this.write(op);
        }
    }
}

function modifiers(event) {
    return Object.freeze({ alt: event.altKey, ctrl: event.ctrlKey, meta: event.metaKey, shift: event.shiftKey });
}

function buttonPosition(element, event) {
    const bounds = element.getBoundingClientRect();
    return { x: event.clientX - bounds.left, y: event.clientY - bounds.top, button: event.button,
             modifiers: modifiers(event) };
}

// These are ordinary JavaScript data records. Conversion to the provider's
// wire records, queuing and event coalescing belong to the service adapter.
function eventFields(element, event) {
    switch (event.type) {
    case "click":
    case "dblclick":
        return { ...buttonPosition(element, event), detail: event.detail };
    case "input":
        return { value: element.value ?? "", checked: element.checked ?? false, composing: event.isComposing ?? false };
    case "change":
        return { value: element.value ?? "", checked: element.checked ?? false };
    case "submit":
        return { submitter: event.submitter?.id ?? "" };
    case "keydown":
    case "keyup":
        return { key: event.key, code: event.code, repeat: event.repeat, modifiers: modifiers(event) };
    case "pointerdown":
    case "pointerup":
    case "pointermove":
        return { ...buttonPosition(element, event), kind: event.pointerType };
    case "scroll":
        return { top: element.scrollTop, left: element.scrollLeft };
    case "toggle":
        return { open: element.open };
    case "close":
        return { return_value: element.returnValue };
    default:
        return {};
    }
}

// A bundled file is named relative to the bundle, not to the page: a deep-linked route such as /parts/7 would
// otherwise resolve assets/<digest>.png to /parts/assets/<digest>.png.
function bundledValue(record, name, value, base) {
    if (base === null) return value;
    const media = record.svg ? record.tag === "use" && name === "href" : ASSET_ATTRIBUTES.get(name)?.includes(record.tag);
    if (!media) return value;
    const resolve = url => url.startsWith(ASSET_PREFIX) ? new URL(url, base).href : url;
    if (name !== "srcset") return resolve(value);
    let result = "";
    let copied = 0;
    for (const [begin, end] of srcsetUrls(value)) {
        result += value.slice(copied, begin) + resolve(value.slice(begin, end));
        copied = end;
    }
    return result + value.slice(copied);
}

// The URLs of a srcset value, as the compiler finds them (src/parser/html_template.cpp, srcset_urls).
function srcsetUrls(value) {
    const blank = c => c === " " || c === "\t" || c === "\n" || c === "\f" || c === "\r";
    const urls = [];
    let at = 0;
    while (at < value.length) {
        while (at < value.length && (blank(value[at]) || value[at] === ",")) at += 1;
        if (at === value.length) break;
        const begin = at;
        while (at < value.length && !blank(value[at])) at += 1;
        let end = at;
        let separated = false;
        while (end > begin && value[end - 1] === ",") {
            end -= 1;
            separated = true;
        }
        urls.push([begin, end]);
        if (separated) continue;
        let depth = 0;
        while (at < value.length && (value[at] !== "," || depth > 0)) {
            if (value[at] === "(") depth += 1;
            else if (value[at] === ")" && depth > 0) depth -= 1;
            at += 1;
        }
    }
    return urls;
}

function writeProperty(record, name, value) {
    const element = record.node;
    if (name === "value" && record.composing) {
        record.pendingValue = value;
        return;
    }
    if (element[name] === value) return;
    const focused = element.getRootNode().activeElement === element;
    const selection = name === "value" && focused && typeof element.selectionStart === "number"
        ? [element.selectionStart, element.selectionEnd, element.selectionDirection] : null;
    element[name] = value;
    if (selection) {
        const length = element.value.length;
        element.setSelectionRange(Math.min(selection[0], length), Math.min(selection[1], length), selection[2]);
    }
}

function moveNode(parent, node, before) {
    if (node.parentNode === parent && node.nextSibling === before) return;
    if (typeof parent.moveBefore === "function") {
        parent.moveBefore(node, before);
        return;
    }
    const active = parent.getRootNode().activeElement;
    const focused = active && node.contains(active);
    const selection = focused && typeof active.selectionStart === "number"
        ? [active.selectionStart, active.selectionEnd, active.selectionDirection] : null;
    parent.insertBefore(node, before);
    if (focused && parent.getRootNode().activeElement !== active) {
        active.focus({ preventScroll: true });
        if (selection) active.setSelectionRange(...selection);
    }
}

export class DomRoot {
    #root;
    #records = new Map();
    #byNode = new WeakMap();
    #emit;
    #closed = false;
    #faulted = false;
    #applying = false;
    #moving = false;
    #events = [];
    #generation = 0;
    #assetBase = null;

    // `options.assetBase` is the URL of the bundle's directory; with it, bundled media files resolve against the
    // bundle whatever the page's path. Without it, they resolve against the page, which suits hash routing.
    constructor(element, onEvent, options = {}) {
        if (element?.nodeType !== 1 || typeof onEvent !== "function")
            throw new TypeError("a DOM root needs an element and an event sink");
        for (const root of ACTIVE_ROOTS) {
            if (root.contains(element) || element.contains(root)) throw new RootFault("overlapping UI roots");
        }
        this.#root = element;
        this.#emit = onEvent;
        if (options.assetBase !== undefined) this.#assetBase = new URL(options.assetBase).href;
        this.#records.set(ROOT, { node: element, kind: "element", svg: element.namespaceURI === SVG, children: new Set() });
        element.addEventListener("compositionstart", this.#compositionStart, { capture: true });
        element.addEventListener("compositionend", this.#compositionEnd, { capture: true });
        ACTIVE_ROOTS.add(element);
    }

    #compositionStart = event => {
        if (this.#faulted || this.#closed) return;
        const record = this.#byNode.get(event.target);
        if (record) record.composing = true;
    };

    #compositionEnd = event => {
        if (this.#faulted || this.#closed) return;
        const record = this.#byNode.get(event.target);
        if (!record) return;
        record.composing = false;
        const pending = record.pendingValue;
        record.pendingValue = undefined;
        if (pending === undefined) return;
        try {
            writeProperty(record, "value", pending);
        } catch {
            this.#fault();
        }
    };

    #fault() {
        this.#faulted = true;
        this.#events.length = 0;
    }

    #dispatch(binding, record, event) {
        if (this.#closed || this.#faulted) return;
        if (this.#moving && (event.type === "focus" || event.type === "blur")) return;
        if (binding.preventDefault) event.preventDefault();
        if (binding.stopPropagation) event.stopPropagation();
        try {
            const message = Object.freeze({
                node: record.id, handler: binding.handler, event: event.type,
                fields: Object.freeze(eventFields(record.node, event)),
            });
            if (this.#applying) this.#events.push(message);
            else this.#emit(message);
        } catch {
            this.#fault();
        }
    }

    #handler(record, op) {
        const existing = record.handlers.get(op.event);
        if (op.handler === null) {
            if (existing) {
                record.node.removeEventListener(op.event, existing.listener);
                record.handlers.delete(op.event);
            }
            return;
        }
        if (existing) {
            existing.handler = op.handler;
            existing.preventDefault = op.preventDefault;
            existing.stopPropagation = op.stopPropagation;
            return;
        }
        const binding = { handler: op.handler, preventDefault: op.preventDefault, stopPropagation: op.stopPropagation };
        binding.listener = event => this.#dispatch(binding, record, event);
        record.node.addEventListener(op.event, binding.listener);
        record.handlers.set(op.event, binding);
    }

    #detach(record) {
        for (const [event, binding] of record.handlers) record.node.removeEventListener(event, binding.listener);
        record.handlers.clear();
        this.#byNode.delete(record.node);
    }

    #remove(id, record) {
        this.#records.get(record.parent).children.delete(id);
        const pending = [id];
        while (pending.length) {
            const child = pending.pop();
            const current = this.#records.get(child);
            for (const descendant of current.children) pending.push(descendant);
            this.#detach(current);
            this.#records.delete(child);
        }
        record.node.remove();
    }

    #apply(op) {
        const record = this.#records.get(op.id);
        switch (op.op) {
        case "remove":
            this.#remove(op.id, record);
            break;
        case "element":
        case "text": {
            const parent = this.#records.get(op.parent);
            const before = op.before === null ? null : this.#records.get(op.before).node;
            const document = this.#root.ownerDocument;
            const node = op.op === "text" ? document.createTextNode(op.value) :
                op.svg ? document.createElementNS(SVG, op.name) : document.createElement(op.name);
            parent.node.insertBefore(node, before);
            const created = {
                id: op.id, node, parent: op.parent, kind: op.op, tag: op.name, svg: op.svg,
                children: new Set(), handlers: new Map(), composing: false, pendingValue: undefined,
            };
            parent.children.add(op.id);
            this.#records.set(op.id, created);
            this.#byNode.set(node, created);
            break;
        }
        case "move": {
            const parent = this.#records.get(op.parent);
            const before = op.before === null ? null : this.#records.get(op.before).node;
            this.#moving = true;
            try {
                moveNode(parent.node, record.node, before);
            } finally {
                this.#moving = false;
            }
            this.#records.get(record.parent).children.delete(op.id);
            parent.children.add(op.id);
            record.parent = op.parent;
            break;
        }
        case "setText":
            record.node.data = op.value;
            break;
        case "attribute":
            if (op.value === null) record.node.removeAttribute(op.name);
            else record.node.setAttribute(op.name, bundledValue(record, op.name, op.value, this.#assetBase));
            break;
        case "variable":
            if (op.value === null) record.node.style.removeProperty(op.name);
            else record.node.style.setProperty(op.name, variableText(op.value));
            break;
        case "handler":
            this.#handler(record, op);
            break;
        case "property":
            writeProperty(record, op.name, op.value);
            break;
        }
    }

    apply(operations) {
        if (this.#closed || this.#faulted || this.#applying) throw new RootFault("UI root cannot apply operations");
        new BatchValidation(this.#records).validate(operations);
        this.#applying = true;
        try {
            for (const op of operations) {
                this.#apply(op);
                if (this.#faulted) throw new RootFault("UI root faulted while applying operations");
            }
            // Flush after all mutations. A synchronous browser event never
            // observes a half-applied commit through Neura's event sink.
            if (this.#events.length) {
                const events = this.#events;
                const generation = this.#generation;
                this.#events = [];
                queueMicrotask(() => {
                    try {
                        for (const event of events) {
                            if (this.#closed || this.#faulted || generation !== this.#generation) break;
                            this.#emit(event);
                        }
                    } catch {
                        this.#fault();
                    }
                });
            }
        } catch (error) {
            this.#fault();
            throw new RootFault("DOM commit failed", { cause: error });
        } finally {
            this.#applying = false;
        }
    }

    reset() {
        if (this.#closed || this.#applying) throw new RootFault("UI root cannot reset");
        const root = this.#records.get(ROOT);
        for (const [id, record] of this.#records) {
            if (id !== ROOT) this.#detach(record);
        }
        this.#root.replaceChildren();
        root.children.clear();
        this.#records = new Map([[ROOT, root]]);
        this.#events.length = 0;
        this.#generation += 1;
        this.#faulted = false;
    }

    close() {
        if (this.#closed) return;
        this.reset();
        this.#root.removeEventListener("compositionstart", this.#compositionStart, { capture: true });
        this.#root.removeEventListener("compositionend", this.#compositionEnd, { capture: true });
        ACTIVE_ROOTS.delete(this.#root);
        this.#closed = true;
    }
}
