/*
 * BroadcastBus.js — BroadcastChannel protocol for the Python worker fleet
 *
 * All messages share the envelope:
 *   { type, workerId, id?, targetId?, timestamp, payload? }
 *
 * Direct worker messages (postMessage) use the same types where noted.
 * BroadcastChannel 'python-dist' carries Python-emitted events.
 */

export const MSG = {
    // Worker lifecycle
    WORKER_READY:    'worker_ready',     // executor → PythonHost (direct)
    WORKER_INIT:     'worker_init',      // PythonHost → executor (direct)
    WORKER_SPAWN:    'worker_spawn',     // bc_out → PythonHost: Python _thread spawn

    // Code execution (direct: PythonHost ↔ executor)
    RUN_REQUEST:     'run_request',
    RUN_RESPONSE:    'run_response',

    // Compilation (direct: PythonHost ↔ compiler)
    COMPILE_REQUEST: 'compile_request',
    COMPILE_DONE:    'compile_done',

    // File system sync (direct: PythonHost → executor)
    MEMFS_UPDATE:    'memfs_update',     // { path, content }

    // Python output (BC broadcast from executor)
    PYTHON_STDOUT:   'python_stdout',
    PYTHON_STDERR:   'python_stderr',

    // Exception handling (BC broadcast from executor)
    EXCEPTION_EVENT: 'exception_event',

    // Hardware events from Python blinka shims (BC broadcast via bc_out)
    HW_EVENT: 'hw',
};

/** Build a message envelope. */
export function msg(type, workerId, payload = null, extras = {}) {
    return { type, workerId, timestamp: Date.now(), payload, ...extras };
}

export const CHANNEL = 'python-dist';
