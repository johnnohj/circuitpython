/**
 * wasi-chassis.js — Minimal WASI runtime with MEMFS-in-linear-memory.
 *
 * Unlike wasi-memfs.js (which stores file data in JS heap), this runtime
 * supports "aliased" files: MEMFS files backed by regions of WASM linear
 * memory.  C writes to a struct field, JS reads via memfs — same bytes.
 *
 * The chassis WASM module calls memfs.register(path_ptr, path_len, data_ptr,
 * data_size) at init.  JS creates a Uint8Array view over the WASM memory
 * buffer at that offset.  No copy — the view IS the data.
 *
 * When WASM memory grows (memory.grow), all Uint8Array views are detached.
 * Call refreshViews() after any growth to re-derive them.
 */

const ERRNO = {
    SUCCESS: 0, BADF: 8, INVAL: 28, NOENT: 44, NOSYS: 52,
};

export class WasiChassis {
    constructor(options = {}) {
        this.args = options.args || ['chassis'];
        this.memory = null;
        this.instance = null;

        // Aliased files: path → { ptr, size, view: Uint8Array }
        // view is lazily created/refreshed from memory.buffer
        this.aliases = new Map();

        // Regular in-memory files: path → Uint8Array (JS heap)
        this.files = new Map();

        // Directories
        this.dirs = new Set(['/']);

        // File descriptor table
        this.fds = new Map();
        this.nextFd = 4;
        this.fds.set(3, { type: 'dir', path: '/' });

        // Callbacks
        this.onStdout = options.onStdout || null;
        this.onStderr = options.onStderr || null;
        this.onRequestFrame = options.onRequestFrame || null;
        this.onNotify = options.onNotify || null;

        this._decoder = new TextDecoder();
        this._encoder = new TextEncoder();

        // Track memory.buffer identity to detect growth
        this._lastBuffer = null;
    }

    setInstance(instance) {
        this.instance = instance;
        this.memory = instance.exports.memory;
        this._lastBuffer = this.memory.buffer;
    }

    /* -------------------------------------------------------------- */
    /* Aliased file API                                                */
    /* -------------------------------------------------------------- */

    /**
     * Register a WASM linear memory region as a MEMFS file.
     * Called from C via the memfs.register WASM import.
     */
    _registerAlias(pathPtr, pathLen, dataPtr, dataSize) {
        const path = this._readString(pathPtr, pathLen);
        this.aliases.set(path, {
            ptr: dataPtr,
            size: dataSize,
            view: new Uint8Array(this.memory.buffer, dataPtr, dataSize),
        });
        // Ensure parent dirs exist
        const parts = path.split('/');
        for (let i = 1; i < parts.length; i++) {
            this.dirs.add(parts.slice(0, i).join('/') || '/');
        }
    }

    /**
     * Re-derive all aliased views after memory.grow.
     * Must be called after any operation that might grow WASM memory.
     */
    refreshViews() {
        if (this.memory.buffer === this._lastBuffer) return;
        this._lastBuffer = this.memory.buffer;
        for (const [, alias] of this.aliases) {
            alias.view = new Uint8Array(
                this.memory.buffer, alias.ptr, alias.size
            );
        }
    }

    /**
     * Read a file — returns aliased view or regular file data.
     * For aliased files, this returns a live view: reads always see
     * the latest bytes C wrote.
     */
    readFile(path) {
        this.refreshViews();
        const alias = this.aliases.get(path);
        if (alias) return alias.view;
        return this.files.get(path) || null;
    }

    /**
     * Write to a file.  For aliased files, writes into linear memory.
     * For regular files, replaces the JS-heap buffer.
     */
    writeFile(path, data) {
        if (typeof data === 'string') data = this._encoder.encode(data);
        const alias = this.aliases.get(path);
        if (alias) {
            // Write into WASM linear memory (clamp to region size)
            const len = Math.min(data.length, alias.size);
            alias.view.set(data.subarray(0, len));
            return;
        }
        this.files.set(path, new Uint8Array(data));
    }

    /**
     * List all known files (aliased + regular).
     */
    listFiles() {
        return [...this.aliases.keys(), ...this.files.keys()];
    }

    /**
     * Get info about an aliased region (for debugging/inspection).
     */
    getAliasInfo(path) {
        const alias = this.aliases.get(path);
        if (!alias) return null;
        return { ptr: alias.ptr, size: alias.size };
    }

    /* -------------------------------------------------------------- */
    /* Internal helpers                                                */
    /* -------------------------------------------------------------- */

    _view() { return new DataView(this.memory.buffer); }
    _u8() { return new Uint8Array(this.memory.buffer); }

    _readString(ptr, len) {
        return this._decoder.decode(new Uint8Array(this.memory.buffer, ptr, len));
    }

    _readIovecs(iovs, iovs_len) {
        const view = this._view();
        const result = [];
        for (let i = 0; i < iovs_len; i++) {
            const buf = view.getUint32(iovs + i * 8, true);
            const len = view.getUint32(iovs + i * 8 + 4, true);
            result.push({ buf, len });
        }
        return result;
    }

    _gatherIovecs(iovs, iovs_len) {
        const iovecs = this._readIovecs(iovs, iovs_len);
        let total = 0;
        for (const iov of iovecs) total += iov.len;
        const data = new Uint8Array(total);
        let off = 0;
        for (const iov of iovecs) {
            data.set(new Uint8Array(this.memory.buffer, iov.buf, iov.len), off);
            off += iov.len;
        }
        return data;
    }

    /* -------------------------------------------------------------- */
    /* WASM imports                                                    */
    /* -------------------------------------------------------------- */

    getImports() {
        const self = this;
        return {
            /* ── MEMFS registration ── */
            memfs: {
                register(pathPtr, pathLen, dataPtr, dataSize) {
                    self._registerAlias(pathPtr, pathLen, dataPtr, dataSize);
                },
            },

            /* ── FFI: C→JS calls ── */
            ffi: {
                request_frame() {
                    if (self.onRequestFrame) self.onRequestFrame();
                },
                notify(type, pin, arg, data) {
                    if (self.onNotify) self.onNotify(type, pin, arg, data);
                },
            },

            /* ── WASI snapshot preview 1 (minimal subset) ── */
            wasi_snapshot_preview1: {
                fd_write(fd, iovs, iovs_len, nwritten) {
                    const data = self._gatherIovecs(iovs, iovs_len);
                    if (fd === 1) {
                        if (self.onStdout) {
                            self.onStdout(self._decoder.decode(data));
                        }
                        self._view().setUint32(nwritten, data.length, true);
                        return ERRNO.SUCCESS;
                    }
                    if (fd === 2) {
                        const text = self._decoder.decode(data);
                        if (self.onStderr) self.onStderr(text);
                        else console.error(text);
                        self._view().setUint32(nwritten, data.length, true);
                        return ERRNO.SUCCESS;
                    }
                    return ERRNO.BADF;
                },

                fd_read(fd, iovs, iovs_len, nread) {
                    // stdin — no input for chassis
                    self._view().setUint32(nread, 0, true);
                    return ERRNO.SUCCESS;
                },

                fd_close(fd) { return ERRNO.SUCCESS; },
                fd_seek(fd, offset_lo, offset_hi, whence, newoff) {
                    return ERRNO.SUCCESS;
                },
                fd_fdstat_get(fd, buf) {
                    // Return a minimal fdstat for stdio fds
                    const view = self._view();
                    view.setUint8(buf, fd <= 2 ? 2 : 3);  // filetype: char_device or directory
                    view.setUint16(buf + 2, 0, true);      // flags
                    view.setBigUint64(buf + 8, 0n, true);  // rights_base
                    view.setBigUint64(buf + 16, 0n, true); // rights_inheriting
                    return ERRNO.SUCCESS;
                },
                fd_prestat_get(fd, buf) {
                    if (fd === 3) {
                        const view = self._view();
                        view.setUint8(buf, 0);          // preopen type = dir
                        view.setUint32(buf + 4, 1, true); // path length ("/" = 1)
                        return ERRNO.SUCCESS;
                    }
                    return ERRNO.BADF;
                },
                fd_prestat_dir_name(fd, path, path_len) {
                    if (fd === 3) {
                        self._u8()[path] = 0x2f; // '/'
                        return ERRNO.SUCCESS;
                    }
                    return ERRNO.BADF;
                },

                // Stubs for functions wasi-libc may call
                environ_sizes_get(count, size) {
                    const view = self._view();
                    view.setUint32(count, 0, true);
                    view.setUint32(size, 0, true);
                    return ERRNO.SUCCESS;
                },
                environ_get() { return ERRNO.SUCCESS; },
                args_sizes_get(argc, argv_buf_size) {
                    const view = self._view();
                    view.setUint32(argc, self.args.length, true);
                    let total = 0;
                    for (const a of self.args) total += self._encoder.encode(a).length + 1;
                    view.setUint32(argv_buf_size, total, true);
                    return ERRNO.SUCCESS;
                },
                args_get(argv, argv_buf) {
                    const view = self._view();
                    const u8 = self._u8();
                    let bufOff = argv_buf;
                    for (let i = 0; i < self.args.length; i++) {
                        view.setUint32(argv + i * 4, bufOff, true);
                        const encoded = self._encoder.encode(self.args[i]);
                        u8.set(encoded, bufOff);
                        u8[bufOff + encoded.length] = 0;
                        bufOff += encoded.length + 1;
                    }
                    return ERRNO.SUCCESS;
                },
                clock_time_get(id, precision, time) {
                    const now = BigInt(Math.round(performance.now() * 1e6));
                    self._view().setBigUint64(time, now, true);
                    return ERRNO.SUCCESS;
                },
                proc_exit(code) {
                    throw new Error(`proc_exit(${code})`);
                },
            },
        };
    }
}
