'use strict';

const { contextBridge, ipcRenderer } = require('electron');

const originArgument = '--neura-app-origin=';
const origins = process.argv.filter(argument => argument.startsWith(originArgument));

// The page gets exactly two functions. `call` sends one bridge operation and
// answers { status, bytes }: status 0 with the reply bytes, or a host error
// code (1 not in the registry, 12 timed out, 15 cancelled, 16 too large,
// 17 busy). The window host checks the sender, origin and registry again.
if (process.isMainFrame && origins.length === 1 && origins[0].slice(originArgument.length) === location.origin) {
    contextBridge.exposeInMainWorld('neuraDesktop', {
        call: async (operationId, bytes) => {
            if (typeof operationId !== 'string' || !(bytes instanceof Uint8Array)) {
                throw new TypeError('call(operation_id, bytes) takes a string and a Uint8Array');
            }
            const { status, bytes: reply } = await ipcRenderer.invoke('neura:bridge-call', operationId, bytes);
            return Object.freeze({ status, bytes: reply === undefined ? new Uint8Array() : new Uint8Array(reply) });
        },
        // Waits for the next event the host has for this window: { kind: 'menu', id } for a menu item whose
        // target is the window. Answers null once the window is closing.
        next_host_event: async () => {
            const value = await ipcRenderer.invoke('neura:next-host-event');
            return value === null ? null : Object.freeze({ kind: value.kind, id: value.id });
        },
    });
}
