'use strict';

const { contextBridge } = require('electron');

const originArgument = '--neura-app-origin=';
const origins = process.argv.filter(argument => argument.startsWith(originArgument));

if (process.isMainFrame && origins.length === 1 && origins[0].slice(originArgument.length) === location.origin) {
    const unavailable = async () => { throw new Error('The Neura desktop bridge is unavailable'); };
    contextBridge.exposeInMainWorld('neuraDesktop', {
        call: (_operationId, _bytes) => unavailable(),
        next_host_event: unavailable,
    });
}
