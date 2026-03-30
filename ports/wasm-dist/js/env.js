/**
 * env.js — Runtime environment detection for CircuitPython WASM.
 *
 * Detects browser vs Node.js and provides a unified surface for
 * capabilities that differ between them.  Import once, use the
 * constants to branch setup logic.
 *
 * Usage:
 *   import { env } from './env.js';
 *
 *   if (env.isBrowser) {
 *       document.addEventListener('keydown', ...);
 *   } else {
 *       process.stdin.on('data', ...);
 *   }
 *
 *   const frame = env.requestFrame(callback);
 *   env.cancelFrame(frame);
 */

const isBrowser = typeof window !== 'undefined' && typeof window.document !== 'undefined';
const isNode = typeof process !== 'undefined' && process.versions != null && process.versions.node != null;
const isWorker = typeof self !== 'undefined' && typeof WorkerGlobalScope !== 'undefined';

/**
 * Schedule a callback for the next frame.
 * Browser: requestAnimationFrame (~60fps, synced to display).
 * Node: setTimeout(cb, 0) — runs on next tick.
 */
function requestFrame(cb) {
    if (typeof requestAnimationFrame === 'function') {
        return requestAnimationFrame(cb);
    }
    return setTimeout(cb, 0);
}

function cancelFrame(handle) {
    if (typeof cancelAnimationFrame === 'function') {
        return cancelAnimationFrame(handle);
    }
    return clearTimeout(handle);
}

/**
 * High-resolution timestamp in milliseconds.
 * Both browser and Node support performance.now().
 */
function now() {
    return performance.now();
}

export const env = Object.freeze({
    isBrowser,
    isNode,
    isWorker,

    /** True if IndexedDB is available for persistence. */
    hasIndexedDB: typeof indexedDB !== 'undefined',

    /** True if DOM is available for UI. */
    hasDOM: isBrowser && !isWorker,

    /** True if OffscreenCanvas is available (browser worker or modern browser). */
    hasOffscreenCanvas: typeof OffscreenCanvas !== 'undefined',

    /** True if fetch() is available. */
    hasFetch: typeof fetch === 'function',

    requestFrame,
    cancelFrame,
    now,
});
