'use strict';

const { contextBridge, ipcRenderer } = require('electron');

const originArgument = '--neura-app-origin=';
const origins = process.argv.filter(argument => argument.startsWith(originArgument));

// The page gets exactly two functions. `call` sends one bridge operation and
// answers { status, bytes }: status 0 with the reply bytes, or a host error
// code (1 not in the registry, 12 timed out, 15 cancelled, 16 too large,
// 17 busy). The window host checks the sender, origin and registry again.
if (process.isMainFrame && origins.length === 1 && origins[0].slice(originArgument.length) === location.origin) {
    const unavailable = async () => { throw new Error('The Neura desktop bridge is unavailable'); };
    contextBridge.exposeInMainWorld('neuraDesktop', {
        call: async (operationId, bytes) => {
            if (typeof operationId !== 'string' || !(bytes instanceof Uint8Array)) {
                throw new TypeError('call(operation_id, bytes) takes a string and a Uint8Array');
            }
            const { status, bytes: reply } = await ipcRenderer.invoke('neura:bridge-call', operationId, bytes);
            return Object.freeze({ status, bytes: reply === undefined ? new Uint8Array() : new Uint8Array(reply) });
        },
        next_host_event: unavailable,
    });
}
