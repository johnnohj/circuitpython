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

/** Frame interval for Node.js (ms).  ~60fps to match browser rAF. */
const NODE_FRAME_MS = 16;

/**
 * Schedule a callback for the next frame.
 * Browser: requestAnimationFrame (~60fps, synced to display).
 * Node: setTimeout at ~60fps.
 */
function requestFrame(cb) {
    if (typeof requestAnimationFrame === 'function') {
        return requestAnimationFrame(cb);
    }
    return setTimeout(cb, NODE_FRAME_MS);
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

/**
 * Load a file as an ArrayBuffer.
 * Browser: fetch().  Node: fs.readFile().
 *
 * @param {string} urlOrPath — URL (browser) or file path (Node)
 * @returns {Promise<ArrayBuffer>}
 */
async function loadFile(urlOrPath) {
    if (typeof fetch === 'function' && !isNode) {
        const resp = await fetch(urlOrPath);
        return resp.arrayBuffer();
    }
    // Node.js: dynamic import to avoid bundler issues
    const { readFile } = await import('node:fs/promises');
    const buf = await readFile(urlOrPath);
    return buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
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
    loadFile,
});
