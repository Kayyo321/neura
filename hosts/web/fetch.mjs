// The `http_client` group of a browser page: `neura.http.fetch` version 1 over the browser's own fetch
// (docs/runtime/Providers.md, "HTTP fetch"). The browser applies its origin, CORS, cookie and redirect rules;
// this host validates the request, enforces the limits W-3 and W-4, the request's timeout and cancellation, and
// maps every outcome to a host error code. It never throws and its promise never rejects, so a failed exchange
// fails the call, not the run.

export const FETCH_ERRORS = Object.freeze({
    invalidInput: 4, timedOut: 12, unsupported: 13, capabilityDenied: 14, cancelled: 15, tooLarge: 16, busy: 17,
    outcomeUnknown: 18, other: 100,
});

// Defaults of W-3 (bytes of one request or response body) and W-4 (fetches in flight), and their maxima.
export const FETCH_LIMITS = Object.freeze({ body: 8 * 1024 * 1024, inFlight: 16, maxBody: 256 * 1024 * 1024,
    maxInFlight: 256 });

const TOKEN = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;
// Methods the Fetch standard forbids; a browser throws for them.
const FORBIDDEN_METHODS = new Set(['CONNECT', 'TRACE', 'TRACK']);
// Methods the Fetch standard normalizes to upper case.
const NORMALIZED = new Set(['DELETE', 'GET', 'HEAD', 'OPTIONS', 'POST', 'PUT']);
const CREDENTIALS = ['omit', 'same-origin', 'include'];
const fail = error => ({ error });

function origin(text) {
    try {
        const url = new URL(text);
        if (url.protocol !== 'http:' && url.protocol !== 'https:') throw new TypeError(text);
        return url.origin;
    } catch {
        throw new TypeError(`${text} is not an http or https origin`);
    }
}

export class FetchHost {
    #fetch;
    #base;
    #origins;
    #body;
    #inFlight;
    #active = 0;

    // `origins`: absolute origins the program may fetch from besides the page's own; `null` leaves the choice to
    // the browser and the page's content-security policy. `base` resolves relative URLs (the page's address).
    constructor({ fetch = globalThis.fetch?.bind(globalThis), base = globalThis.document?.baseURI ?? globalThis.location?.href,
                  origins = null, body = FETCH_LIMITS.body, inFlight = FETCH_LIMITS.inFlight } = {}) {
        if (typeof fetch !== 'function') throw new TypeError('FetchHost needs a fetch function');
        if (!Number.isInteger(body) || body < 0 || body > FETCH_LIMITS.maxBody) throw new RangeError('W-3 out of range');
        if (!Number.isInteger(inFlight) || inFlight < 1 || inFlight > FETCH_LIMITS.maxInFlight) {
            throw new RangeError('W-4 out of range');
        }
        this.#fetch = fetch;
        this.#base = base;
        this.#body = body;
        this.#inFlight = inFlight;
        if (origins !== null) {
            if (!Array.isArray(origins)) throw new TypeError('origins must be an array or null');
            this.#origins = new Set(origins.map(origin));
            if (base) this.#origins.add(new URL(base).origin);
        } else {
            this.#origins = null;
        }
    }

    // Fetches in flight.
    get active() {
        return this.#active;
    }

    // `request`: { method, url, headers: [[name, value]], body: Uint8Array, timeout: BigInt nanoseconds (0: none),
    // credentials: 0 omit, 1 same-origin, 2 include }. Answers { status, headers, body } or { error }.
    async fetch(request, { signal } = {}) {
        try {
            return await this.#exchange(request, signal);
        } catch {
            // Nothing below should throw; a failure that does is the host's, and not knowing whether the request
            // reached the server, the call says so.
            return fail(FETCH_ERRORS.outcomeUnknown);
        }
    }

    // Validates a request before anything leaves the page. Returns the init and URL, or an error code.
    #prepare({ method, url, headers, body, timeout, credentials }) {
        if (typeof method !== 'string' || !TOKEN.test(method)) return FETCH_ERRORS.invalidInput;
        const upper = method.toUpperCase();
        if (FORBIDDEN_METHODS.has(upper)) return FETCH_ERRORS.invalidInput;
        const verb = NORMALIZED.has(upper) ? upper : method;
        let target;
        try {
            target = this.#base ? new URL(url, this.#base) : new URL(url);
        } catch {
            return FETCH_ERRORS.invalidInput;
        }
        if (target.protocol !== 'http:' && target.protocol !== 'https:') return FETCH_ERRORS.unsupported;
        if (target.username || target.password) return FETCH_ERRORS.invalidInput;
        if (this.#origins && !this.#origins.has(target.origin)) return FETCH_ERRORS.capabilityDenied;
        const list = new Headers();
        for (const [name, value] of headers) {
            if (!TOKEN.test(name) || /[\0\r\n]/.test(value) || value !== value.trim()) return FETCH_ERRORS.invalidInput;
            // A value outside Latin-1 cannot be a header byte string.
            if (/[^\x00-\xff]/.test(value)) return FETCH_ERRORS.invalidInput;
            list.append(name, value);
        }
        if (!(body instanceof Uint8Array)) return FETCH_ERRORS.invalidInput;
        if (body.length > this.#body) return FETCH_ERRORS.tooLarge;
        if (body.length && (verb === 'GET' || verb === 'HEAD')) return FETCH_ERRORS.invalidInput;
        if (typeof timeout !== 'bigint' || timeout < 0n) return FETCH_ERRORS.invalidInput;
        if (!Number.isInteger(credentials) || credentials < 0 || credentials >= CREDENTIALS.length) {
            return FETCH_ERRORS.invalidInput;
        }
        const init = { method: verb, headers: list, credentials: CREDENTIALS[credentials], redirect: 'follow',
                       mode: 'cors' };
        if (body.length) init.body = body;
        return { target, init, timeout };
    }

    async #exchange(request, signal) {
        const prepared = this.#prepare(request);
        if (typeof prepared === 'number') return fail(prepared);
        if (signal?.aborted) return fail(FETCH_ERRORS.cancelled);
        if (this.#active >= this.#inFlight) return fail(FETCH_ERRORS.busy);
        const { target, init, timeout } = prepared;
        const controller = new AbortController();
        // Why the exchange was stopped: the first reason wins.
        let reason = null;
        const stop = code => {
            reason ??= code;
            controller.abort();
        };
        const cancelled = () => stop(FETCH_ERRORS.cancelled);
        signal?.addEventListener('abort', cancelled, { once: true });
        let timer;
        if (timeout > 0n) {
            const milliseconds = Number((timeout + 999_999n) / 1_000_000n);
            const arm = remaining => {
                const now = Math.min(remaining, 0x7fffffff);
                timer = setTimeout(() => (remaining > now ? arm(remaining - now) : stop(FETCH_ERRORS.timedOut)), now);
            };
            arm(milliseconds);
        }
        this.#active += 1;
        try {
            let response;
            try {
                response = await this.#fetch(target.href, { ...init, signal: controller.signal });
            } catch {
                if (reason !== null) return fail(reason);
                // A failure before a response is a network failure or a CORS refusal: the browser does not say
                // which, nor whether the server acted on the request.
                return fail(FETCH_ERRORS.outcomeUnknown);
            }
            const declared = Number(response.headers.get('Content-Length'));
            if (Number.isFinite(declared) && declared > this.#body) {
                stop(FETCH_ERRORS.tooLarge);
                return fail(FETCH_ERRORS.tooLarge);
            }
            const body = await this.#read(response, stop, () => reason);
            if (typeof body === 'number') return fail(body);
            const headers = [];
            for (const [name, value] of response.headers) headers.push([name, value]);
            return { status: response.status, headers, body };
        } finally {
            this.#active -= 1;
            clearTimeout(timer);
            signal?.removeEventListener('abort', cancelled);
        }
    }

    // Reads the body within W-3. Returns the bytes or an error code.
    async #read(response, stop, reason) {
        if (!response.body) return new Uint8Array(0);
        const reader = response.body.getReader();
        const chunks = [];
        let size = 0;
        try {
            for (;;) {
                const { done, value } = await reader.read();
                if (done) break;
                size += value.length;
                if (size > this.#body) {
                    stop(FETCH_ERRORS.tooLarge);
                    return reason();
                }
                chunks.push(value);
            }
        } catch {
            return reason() ?? FETCH_ERRORS.outcomeUnknown;
        }
        if (reason() !== null) return reason();
        const body = new Uint8Array(size);
        let at = 0;
        for (const chunk of chunks) {
            body.set(chunk, at);
            at += chunk.length;
        }
        return body;
    }
}
