// The browser's neura.ui.* and neura.history.* services (docs/runtime/
// Providers.md, "UI services" and "History services"). The NVM adapter
// (nvm.js) calls these methods with converted arguments; a method returns its
// result, { error } with a host error code, or a promise of either.
import { DomRoot, InvalidBatch, RootFault } from './dom.mjs';
import { decodeCommit, encodeEvents, WireError } from './ui-wire.mjs';

const ERROR = Object.freeze({ notFound: 1, alreadyExists: 3, invalidInput: 4, cancelled: 15, other: 100 });
const COALESCED = new Set(['pointermove', 'scroll']);
const DESCRIPTOR_ENCODING = 1;

// Limits of Apps030.md, "Finite limits".
export const UI_LIMITS = Object.freeze({
    descriptorNodes: 4096, // U-1
    descriptorDepth: 64, // U-2
    descriptorSlots: 1024, // U-3
    queuedEvents: 1024, // U-7
    roots: 16, // U-9
});

function mark(name) {
    try {
        globalThis.performance?.mark?.(name);
    } catch {
        // Marks are measurement aids only.
    }
}

// Checks a template descriptor (Html030.md, "Lowering and artifacts"): its
// encoding version and the limits U-1 to U-3.
function checkDescriptor(text, limits) {
    let descriptor;
    try {
        descriptor = JSON.parse(text);
    } catch {
        return false;
    }
    if (descriptor?.encoding !== DESCRIPTOR_ENCODING || !Array.isArray(descriptor.fragments)) return false;
    let nodes = 0;
    let slots = 0;
    const visit = (list, depth) => {
        if (!Array.isArray(list) || depth > limits.descriptorDepth) return false;
        for (const node of list) {
            if (node === null || typeof node !== 'object') return false;
            nodes += 1;
            if (nodes > limits.descriptorNodes) return false;
            if (node.children !== undefined && !visit(node.children, depth + 1)) return false;
        }
        return true;
    };
    for (const fragment of descriptor.fragments) {
        if (fragment === null || typeof fragment !== 'object' || !visit(fragment.nodes, 1)) return false;
        slots += Array.isArray(fragment.slots) ? fragment.slots.length : 0;
        if (slots > limits.descriptorSlots) return false;
    }
    return true;
}

export class UiHost {
    #document;
    #limits;
    #roots = new Map();
    #templates = new Map();
    // Handles are numbered from 1 for each host and never reused.
    #next = 1n;
    #committed = false;

    #assetBase;

    // `assetBase`, the URL of the bundle's directory, makes bundled media resolve against the bundle rather than
    // the page (dom.mjs); a bundle under history routing needs it.
    constructor({ document = globalThis.document, limits = {}, assetBase } = {}) {
        this.#document = document;
        this.#limits = { ...UI_LIMITS, ...limits };
        this.#assetBase = assetBase;
    }

    // Events queued across every root, for the adapter's hasPendingEvents.
    get queued() {
        let count = 0;
        for (const entry of this.#roots.values()) count += entry.queue.length;
        return count;
    }

    #root(handle) {
        return typeof handle === 'bigint' ? this.#roots.get(handle) : undefined;
    }

    openRoot(selector) {
        if (this.#roots.size >= this.#limits.roots) return { error: ERROR.other };
        let element;
        try {
            element = this.#document.querySelector(selector);
        } catch {
            return { error: ERROR.invalidInput };
        }
        if (!element) return { error: ERROR.notFound };
        const handle = this.#next++;
        const entry = { handle, element, queue: [], dropped: 0, droppedInputs: new Map(), waiter: null,
                        templates: new Set(), dom: null };
        try {
            const options = this.#assetBase === undefined ? {} : { assetBase: this.#assetBase };
            entry.dom = new DomRoot(element, record => this.#arrive(entry, record), options);
        } catch (error) {
            if (error instanceof RootFault) return { error: ERROR.alreadyExists };
            throw error;
        }
        this.#roots.set(handle, entry);
        return handle;
    }

    closeRoot(handle) {
        const entry = this.#root(handle);
        if (!entry) return { error: ERROR.invalidInput };
        this.#roots.delete(handle);
        for (const template of entry.templates) this.#templates.delete(template);
        entry.dom.close();
        entry.element.removeAttribute('data-neura-overloaded');
        entry.queue.length = 0;
        // A pending event.next answers an empty batch: the root has closed.
        entry.waiter?.resolve([]);
        entry.waiter = null;
        return undefined;
    }

    template(handle, descriptor) {
        const entry = this.#root(handle);
        if (!entry || !checkDescriptor(descriptor, this.#limits)) return { error: ERROR.invalidInput };
        const template = this.#next++;
        entry.templates.add(template);
        this.#templates.set(template, handle);
        return template;
    }

    commit(handle, bytes) {
        const entry = this.#root(handle);
        if (!entry) return { error: ERROR.invalidInput };
        let operations;
        try {
            operations = decodeCommit(bytes);
        } catch (error) {
            if (error instanceof WireError) return { error: ERROR.invalidInput };
            throw error;
        }
        try {
            entry.dom.apply(operations);
        } catch (error) {
            if (error instanceof InvalidBatch) return { error: ERROR.invalidInput };
            if (error instanceof RootFault) return { error: ERROR.other };
            throw error;
        }
        if (!this.#committed) {
            this.#committed = true;
            mark('neura:first-commit');
        }
        mark('neura:commit-end');
        return undefined;
    }

    nextEvents(handle, max, { signal } = {}) {
        const entry = this.#root(handle);
        if (!entry || !Number.isInteger(max) || max < 1 || max > 65536) return { error: ERROR.invalidInput };
        if (entry.waiter) return { error: ERROR.alreadyExists };
        if (entry.queue.length || entry.dropped) return encodeEvents(this.#take(entry, max));
        if (signal?.aborted) return { error: ERROR.cancelled };
        return new Promise(resolve => {
            // The records handed to this wait. A cancellation that reaches the adapter before their answer was
            // delivered discards the answer (nvm.js, cancelRequest), so the records go back to the queue's front.
            let given = null;
            const aborted = () => {
                if (given) {
                    if (this.#roots.get(entry.handle) === entry) {
                        entry.queue.unshift(...given);
                        this.#deliver(entry);
                    }
                    given = null;
                    return;
                }
                if (entry.waiter?.resolve !== finish) return;
                entry.waiter = null;
                resolve({ error: ERROR.cancelled });
            };
            const finish = records => {
                given = records.length ? records : null;
                if (!given) signal?.removeEventListener('abort', aborted);
                resolve(encodeEvents(records));
            };
            entry.waiter = { max, resolve: finish };
            signal?.addEventListener('abort', aborted, { once: true });
        });
    }

    // Moves the keyboard focus to the element of the root whose id is `id` (neura.ui.focus, version 1). Error 1
    // when the root holds no such element.
    focus(handle, id) {
        const entry = this.#root(handle);
        if (!entry || typeof id !== 'string' || id === '') return { error: ERROR.invalidInput };
        const element = entry.element.ownerDocument.getElementById(id);
        if (!element || element === entry.element || !entry.element.contains(element)) return { error: ERROR.notFound };
        element.focus();
        return undefined;
    }

    stats(handle) {
        const entry = this.#root(handle);
        if (!entry) return { error: ERROR.invalidInput };
        const { nodes, listeners } = entry.dom.stats();
        return [BigInt(nodes), BigInt(listeners), BigInt(entry.templates.size), BigInt(entry.queue.length)];
    }

    // Events are queued in arrival order. pointermove and scroll replace the
    // queued record of the same listener in place. A full queue (U-7) drops
    // what it cannot coalesce, keeps the latest input record of each element,
    // and marks the root until the overflow is delivered.
    #arrive(entry, record) {
        if (!this.#roots.has(entry.handle)) return;
        if (COALESCED.has(record.event)) {
            const index = entry.queue.findIndex(queued => queued.event === record.event && queued.node === record.node);
            if (index >= 0) {
                entry.queue[index] = record;
                this.#deliver(entry);
                return;
            }
        }
        if (entry.queue.length >= this.#limits.queuedEvents) {
            entry.dropped += 1;
            if (record.event === 'input') entry.droppedInputs.set(record.node, record);
            entry.element.setAttribute('data-neura-overloaded', '');
            return;
        }
        if (entry.dropped) this.#flushOverflow(entry);
        entry.queue.push(record);
        this.#deliver(entry);
    }

    // Takes at most `max` records. Once taking makes room, the overflow record
    // follows the events that were queued before the drop.
    #take(entry, max) {
        const batch = entry.queue.splice(0, max);
        if (entry.dropped && entry.queue.length < this.#limits.queuedEvents) {
            this.#flushOverflow(entry);
            batch.push(...entry.queue.splice(0, max - batch.length));
        }
        return batch;
    }

    #flushOverflow(entry) {
        entry.queue.push({ event: 'overflow', node: 0, handler: 0, dropped: Math.min(entry.dropped, 0xffffffff) });
        for (const record of entry.droppedInputs.values()) entry.queue.push(record);
        entry.dropped = 0;
        entry.droppedInputs.clear();
        entry.element.removeAttribute('data-neura-overloaded');
    }

    #deliver(entry) {
        const waiter = entry.waiter;
        if (!waiter || !entry.queue.length) return;
        entry.waiter = null;
        waiter.resolve(this.#take(entry, waiter.max));
    }
}

// Paths are absolute, at most 8 KiB, without control characters.
function validPath(path) {
    return typeof path === 'string' && path.startsWith('/') && path.length <= 8192 && !/[\u0000-\u001f\u007f]/u.test(path);
}

// Decodes a URL's path text; a malformed escape leaves the text as it is.
function decoded(text) {
    try {
        return decodeURI(text);
    } catch {
        return text;
    }
}

// Hash routing (the default) keeps the path in the fragment, so any static
// host serves it. History routing keeps it in the URL's path below `base`
// (default `/`), and needs the host to serve index.html for unknown paths.
export class HistoryHost {
    #window;
    #mode;
    #base;
    #changed = false;
    #waiter = null;

    constructor({ window = globalThis, mode = 'hash', base = '/' } = {}) {
        if (mode !== 'hash' && mode !== 'history') throw new RangeError('routing is "hash" or "history"');
        if (typeof base !== 'string' || !base.startsWith('/') || !base.endsWith('/')) {
            throw new RangeError('a base path starts and ends with "/"');
        }
        this.#window = window;
        this.#mode = mode;
        this.#base = base;
        const changed = () => {
            this.#changed = true;
            this.#deliver();
        };
        window.addEventListener('popstate', changed);
        if (mode === 'hash') window.addEventListener('hashchange', changed);
        this.close = () => {
            window.removeEventListener('popstate', changed);
            window.removeEventListener('hashchange', changed);
            this.#waiter?.(this.current());
        };
    }

    get mode() {
        return this.#mode;
    }

    current() {
        const location = this.#window.location;
        if (this.#mode === 'hash') {
            const path = decoded(location.hash.slice(1));
            return validPath(path) ? path : '/';
        }
        const full = decoded(location.pathname) + decoded(location.search);
        const inside = full.startsWith(this.#base) ? `/${full.slice(this.#base.length)}`
            : `${full}/` === this.#base ? '/' : full;
        return validPath(inside) ? inside : '/';
    }

    #url(path) {
        return this.#mode === 'hash' ? `#${encodeURI(path)}` : this.#base + encodeURI(path.slice(1));
    }

    push(path) {
        if (!validPath(path)) return { error: ERROR.invalidInput };
        this.#window.history.pushState(null, '', this.#url(path));
        return this.current();
    }

    replace(path) {
        if (!validPath(path)) return { error: ERROR.invalidInput };
        this.#window.history.replaceState(null, '', this.#url(path));
        return this.current();
    }

    // The href of a link to `path` in this page's routing mode (neura.history.link, version 1): `#/parts/7`
    // with hash routing, `/parts/7` below the base with history routing.
    link(path) {
        if (!validPath(path)) return { error: ERROR.invalidInput };
        return this.#url(path);
    }

    // The next navigation the program did not make itself (back, forward, an
    // edited fragment). Navigations while nobody waits coalesce to the latest.
    next({ signal } = {}) {
        if (this.#waiter) return { error: ERROR.alreadyExists };
        if (this.#changed) {
            this.#changed = false;
            return this.current();
        }
        if (signal?.aborted) return { error: ERROR.cancelled };
        return new Promise(resolve => {
            let answered = false;
            // A cancellation that reaches the adapter before the answer was delivered discards it (nvm.js,
            // cancelRequest), so the navigation is kept for the next wait.
            const aborted = () => {
                if (answered) {
                    this.#changed = true;
                    return;
                }
                this.#waiter = null;
                resolve({ error: ERROR.cancelled });
            };
            this.#waiter = path => {
                answered = true;
                this.#waiter = null;
                resolve(path);
            };
            signal?.addEventListener('abort', aborted, { once: true });
        });
    }

    #deliver() {
        if (!this.#waiter) return;
        this.#changed = false;
        this.#waiter(this.current());
    }
}
