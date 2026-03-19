/**
 * libpybroadcast.js — Emscripten library for BroadcastChannel IPC
 *
 * Replaces the MEMFS-file-based bc_out/bc_in hot path with in-memory
 * ring buffers + direct BroadcastChannel.postMessage().
 *
 * Before (MEMFS path, ~6 syscalls per message):
 *   C: open("/dev/bc_out") → write(json) → close()
 *   JS: FS.readFile("/dev/bc_out") → JSON.parse → BC.postMessage → FS.writeFile("")
 *
 * After (ring buffer path, 0 syscalls):
 *   C: mp_bc_out_enqueue(json, len) → JS pushes to Module._bcOutBuf[]
 *   JS: mp_bc_out_flush() → iterate buffer → BC.postMessage
 *
 * The /dev/bc_out and /dev/bc_in MEMFS files remain for backward compatibility
 * (vdev_bc_out_drain, vdev_bc_in_write) but the hot path bypasses them.
 *
 * Display refresh events: when a message has cmd=display_refresh + fb_path,
 * the flush reads the framebuffer binary from MEMFS and attaches it as a
 * Uint8Array (structured clone transfer via BroadcastChannel).
 */

mergeInto(LibraryManager.library, {

    // ── Initialization ──────────────────────────────────────────────────────

    /**
     * Initialize broadcast ring buffers.  Called once from mp_js_init or
     * the first enqueue, whichever comes first.
     */
    mp_bc_init__deps: ['$FS'],
    mp_bc_init: function() {
        if (Module._bcInitialized) return;
        Module._bcOutBuf = [];      // outgoing messages (JSON strings)
        Module._bcInBuf  = '';       // incoming JSON (newline-delimited)
        Module._bcInitialized = true;
    },

    // ── Outgoing (Python → JS) ──────────────────────────────────────────────

    /**
     * Enqueue a JSON string for broadcast.  Called from C (_blinka.send,
     * mpthread_wasm worker_spawn).  No MEMFS I/O — just push to JS array.
     *
     * @param json_ptr  WASM pointer to UTF-8 JSON string
     * @param len       byte length of the string
     */
    mp_bc_out_enqueue__deps: ['mp_bc_init'],
    mp_bc_out_enqueue: function(json_ptr, len) {
        if (!Module._bcInitialized) _mp_bc_init();
        const str = UTF8ToString(json_ptr, len);
        Module._bcOutBuf.push(str);
    },

    /**
     * Flush all enqueued messages to BroadcastChannel.
     * Returns the number of messages flushed.
     *
     * For display_refresh events with fb_path, reads the framebuffer
     * binary from MEMFS and attaches as pixels: Uint8Array.
     */
    mp_bc_out_flush__deps: ['mp_bc_init', '$FS'],
    mp_bc_out_flush: function() {
        if (!Module._bcInitialized) _mp_bc_init();
        const buf = Module._bcOutBuf;
        if (buf.length === 0) return 0;

        // Swap buffer so new enqueues during flush go to a fresh array
        Module._bcOutBuf = [];
        const count = buf.length;

        for (let i = 0; i < count; i++) {
            try {
                const obj = JSON.parse(buf[i]);
                // Attach framebuffer binary for display_refresh events
                if (obj.type === 'hw' && obj.cmd === 'display_refresh' && obj.fb_path) {
                    try {
                        const raw = FS.readFile('/flash' + obj.fb_path);
                        obj.pixels = new Uint8Array(raw);
                        delete obj.fb_path;
                    } catch (_) {}
                }
                // Synchronous interceptors: device simulators can write MEMFS
                // responses here (before Python reads the mailbox).
                if (Module._bcOutInterceptors) {
                    for (let j = 0; j < Module._bcOutInterceptors.length; j++) {
                        try { Module._bcOutInterceptors[j](obj); } catch (_) {}
                    }
                }
                // Broadcast to other workers/contexts
                if (Module._bc) {
                    Module._bc.postMessage(obj);
                }
            } catch (e) {
                // Skip malformed JSON lines
            }
        }
        return count;
    },

    // ── Incoming (JS → Python) ──────────────────────────────────────────────

    /**
     * Write incoming data to the bc_in buffer.  Called from JS when
     * register updates or other messages arrive from the main thread.
     *
     * @param json_ptr  WASM pointer to UTF-8 JSON string
     * @param len       byte length
     */
    mp_bc_in_write__deps: ['mp_bc_init'],
    mp_bc_in_write: function(json_ptr, len) {
        if (!Module._bcInitialized) _mp_bc_init();
        const str = UTF8ToString(json_ptr, len);
        Module._bcInBuf += str;
        if (str.length > 0 && str[str.length - 1] !== '\n') {
            Module._bcInBuf += '\n';
        }
    },

    /**
     * Read pending bc_in data into a C buffer.  Non-blocking: returns 0
     * if nothing pending.  Clears the buffer after reading.
     *
     * @param buf_ptr   WASM pointer to destination buffer
     * @param maxlen    maximum bytes to write (including NUL)
     * @returns         number of bytes written (excluding NUL), or 0
     */
    mp_bc_in_read__deps: ['mp_bc_init'],
    mp_bc_in_read: function(buf_ptr, maxlen) {
        if (!Module._bcInitialized) _mp_bc_init();
        const data = Module._bcInBuf;
        if (data.length === 0) return 0;
        Module._bcInBuf = '';

        const n = Math.min(lengthBytesUTF8(data), maxlen - 1);
        stringToUTF8(data, buf_ptr, maxlen);
        return n;
    },

    /**
     * Check if there is pending bc_in data (non-blocking).
     * @returns 1 if data pending, 0 otherwise
     */
    mp_bc_in_pending__deps: ['mp_bc_init'],
    mp_bc_in_pending: function() {
        if (!Module._bcInitialized) return 0;
        return Module._bcInBuf.length > 0 ? 1 : 0;
    },
});
