/*
 * exception.js — Pure-JS exception and restart policy handler
 *
 * No WASM. Subscribes to run_response{aborted, stderr} events from
 * PythonHost and implements restart/escalation policy.
 *
 * Policy (phase 1):
 *   - Log stderr to console
 *   - Emit 'exception' event via BroadcastChannel for external handlers
 *   - Do NOT auto-restart (let PythonHost decide)
 */

import { MSG, CHANNEL } from '../BroadcastBus.js';

export class ExceptionHandler {
    constructor(onException) {
        this._onException = onException || null;
        this._bc = new BroadcastChannel(CHANNEL);
        this._bc.onmessage = (e) => this._handle(e.data);
    }

    _handle(msg) {
        if (msg.type !== MSG.EXCEPTION_EVENT) { return; }
        const { workerId, payload } = msg;
        if (this._onException) {
            try { this._onException({ workerId, ...payload }); } catch {}
        }
    }

    close() {
        this._bc.close();
    }
}
