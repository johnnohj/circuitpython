/*
 * library.js — WASM-dist Host OS JavaScript Library
 *
 * JavaScript/Emscripten is the host OS for Python.  This file provides the
 * Emscripten JS library (mergeInto) functions that bridge C ↔ JS:
 *
 *   mp_js_init_filesystem()  — creates MEMFS virtual hardware bus layout
 *   mp_js_hook()             — VM hook: deadline check + /dev/bc_out drain
 *   mp_js_write()            — stdout capture (standalone exec path)
 *   mp_js_ticks_ms()         — wall-clock milliseconds
 *   mp_js_time_ms()          — epoch milliseconds
 *   mp_js_random_u32()       — random 32-bit integer for Python random seed
 *   mp_js_set_deadline()     — set the run() timeout deadline
 *
 * Virtual device layout (created here, maintained by vdev.c):
 *   /dev/stdin stdout stderr time interrupt bc_out bc_in
 *   /mem/ /flash/ /flash/lib/ /state/ /proc/ /debug/
 *
 * No Asyncify, no JSPI — Python runs synchronously in its worker.
 * BroadcastChannel IPC happens via /dev/bc_out (Python→JS) drained by
 * mp_js_hook(), and /dev/bc_in (JS→Python) written by vdev_bc_in_write().
 */

mergeInto(LibraryManager.library, {

    // =========================================================================
    // Filesystem — MEMFS virtual hardware bus
    // =========================================================================

    mp_js_init_filesystem__deps: ['$FS'],

    mp_js_init_filesystem: function () {
        if (Module._vdev_initialized) { return 0; }

        const dirs = [
            '/dev', '/mem',
            '/flash', '/flash/lib', '/flash/circuitpy',
            '/state', '/proc', '/debug'
        ];

        dirs.forEach(d => {
            try { FS.mkdir(d); } catch (e) {
                if (e.errno !== 20) { /* EEXIST is fine */
                    console.warn('wasm-dist: mkdir failed:', d, e);
                }
            }
        });

        // Pre-create plain MEMFS device files for bc/time/interrupt (C reads/writes these).
        ['time', 'interrupt', 'bc_out', 'bc_in'].forEach(n => {
            try { FS.writeFile('/dev/' + n, ''); } catch (e) {}
        });

        // Register custom capture devices for Python stdout/stderr.
        // Emscripten's /dev/stdout and /dev/stderr are terminal devices; we use
        // /dev/py_stdout and /dev/py_stderr as capture buffers instead.
        Module._pyStdout = '';
        Module._pyStderr = '';

        const dec = new TextDecoder();
        const stdoutId = FS.makedev(64, 0);
        const stderrId = FS.makedev(64, 1);

        FS.registerDevice(stdoutId, {
            read:  function() { return 0; },
            write: function(stream, buf, offset, length) {
                // buf is the source typed array; offset/length index into it
                Module._pyStdout += dec.decode(buf.subarray(offset, offset + length));
                return length;
            }
        });
        FS.registerDevice(stderrId, {
            read:  function() { return 0; },
            write: function(stream, buf, offset, length) {
                Module._pyStderr += dec.decode(buf.subarray(offset, offset + length));
                return length;
            }
        });

        FS.mkdev('/dev/py_stdout', stdoutId);
        FS.mkdev('/dev/py_stderr', stderrId);

        // Write /proc/self.json with worker identity if available
        const selfInfo = {
            workerId: Module._workerId || 'main',
            role:     Module._workerRole || 'executor'
        };
        try { FS.writeFile('/proc/self.json', JSON.stringify(selfInfo)); } catch (e) {}

        Module._vdev_initialized = true;
        Module._run_deadline = 0;
        return 0;
    },

    // =========================================================================
    // VM hook — called every MICROPY_VM_HOOK_COUNT (64) bytecodes
    //
    // ITCM analogue: this is the hot path that the JS engine tier-compiles.
    // Responsibilities (delegated to libpytasks.js):
    //   1. Flush broadcast ring buffer → BroadcastChannel (libpybroadcast.js)
    //   2. Additional registered background tasks
    //
    // Note: Deadline check is done C-side in mp_hal_hook() to avoid
    // re-entrant WASM calls.  Register sync is also C-side.
    // This JS hook only handles tasks that require JS APIs.
    // =========================================================================

    mp_js_hook__deps: ['mp_tasks_poll'],

    mp_js_hook: function () {
        // Run all registered background tasks (broadcast flush, etc.)
        _mp_tasks_poll();
    },

    // =========================================================================
    // stdout/stderr capture read-back
    //
    // Called from C memfs_state.c to retrieve accumulated stdout/stderr after
    // a Python run.  The data was captured by the /dev/py_stdout device above.
    // mp_js_write() is also called by the standalone exec path in mphalport.c.
    // =========================================================================

    mp_js_write: function (buf, len) {
        Module._pyStdout = (Module._pyStdout || '') + UTF8ToString(buf, len);
    },

    mp_js_write_stderr: function (buf, len) {
        Module._pyStderr = (Module._pyStderr || '') + UTF8ToString(buf, len);
    },

    // Copy accumulated stdout into WASM memory at ptr (maxlen bytes incl. NUL).
    // Clears the buffer.  Returns the number of bytes written (excl. NUL).
    mp_js_stdout_read__deps: ['$stringToUTF8', '$lengthBytesUTF8'],
    mp_js_stdout_read: function (ptr, maxlen) {
        const text = Module._pyStdout || '';
        Module._pyStdout = '';
        if (maxlen <= 0) { return 0; }
        const n = Math.min(lengthBytesUTF8(text), maxlen - 1);
        stringToUTF8(text, ptr, maxlen);
        return n;
    },

    mp_js_stdout_size: function () {
        return lengthBytesUTF8(Module._pyStdout || '');
    },

    mp_js_stderr_read__deps: ['$stringToUTF8', '$lengthBytesUTF8'],
    mp_js_stderr_read: function (ptr, maxlen) {
        const text = Module._pyStderr || '';
        Module._pyStderr = '';
        if (maxlen <= 0) { return 0; }
        stringToUTF8(text, ptr, maxlen);
        return Math.min(lengthBytesUTF8(text), maxlen - 1);
    },

    mp_js_stderr_size: function () {
        return lengthBytesUTF8(Module._pyStderr || '');
    },

    // =========================================================================
    // Timing
    // =========================================================================

    mp_js_ticks_ms: function () {
        return Date.now() | 0;
    },

    mp_js_time_ms: function () {
        return Date.now();
    },

    mp_js_random_u32: function () {
        return (Math.random() * 0xffffffff) >>> 0;
    },

});

