/**
 * wasi2.js — WASI Preview 2 shim for CircuitPython WASM.
 *
 * Provides the ~50 imports that a wasm32-wasip2 binary expects.
 * Built on the same in-memory filesystem as wasi.js (Preview 1),
 * but uses the component model's resource handle pattern:
 *   - Streams, pollables, and descriptors are integer handles
 *   - The shim manages handle→object mapping
 *   - `[resource-drop]X` releases a handle
 *
 * Channel layout (same as Preview 1):
 *   Handle 0 = stdin stream   (serial_rx ring or JS input)
 *   Handle 1 = stdout stream  (serial_tx ring or JS output)
 *   Handle 2 = stderr stream  (diagnostics)
 *   Handle 3 = protocol stream (fd 4 — WS protocol messages)
 *
 * Filesystem: same WasiMemfs (files Map + live views + IDB).
 */

const ERRNO = {
    SUCCESS: 0, BADF: 8, INVAL: 28, NOENT: 44, NOSYS: 52, ISDIR: 31,
    OVERFLOW: 61,
};

export class Wasi2Memfs {
    constructor(options = {}) {
        this.args = options.args || ['circuitpython'];
        this.env = options.env || {};
        this.memory = null;
        this.instance = null;

        // In-memory filesystem
        this.files = new Map();
        this.dirs = new Set(['/']);

        // Callbacks
        this.onStdout = options.onStdout || null;
        this.onStderr = options.onStderr || null;
        this.onProtocol = options.onProtocol || null;
        this.onFileChanged = options.onFileChanged || null;

        // IDB persistence
        this.idb = options.idb || null;

        this._decoder = new TextDecoder();

        // ── Resource handle tables ──
        // Handles are integers. Each resource type has its own table.
        this._nextHandle = 10;  // reserve 0-9 for well-known handles
        this._streams = new Map();    // handle → { type, read?, write?, path?, ... }
        this._pollables = new Map();  // handle → { type, ready, deadline?, stream? }
        this._descriptors = new Map(); // handle → { type: 'file'|'dir', path, offset }
        this._dirStreams = new Map();  // handle → { path, entries, index }

        // Well-known stream handles
        this._streams.set(0, { type: 'stdin', buffer: [] });
        this._streams.set(1, { type: 'stdout' });
        this._streams.set(2, { type: 'stderr' });
        this._streams.set(3, { type: 'protocol' });

        // Root directory descriptor (preopened)
        this._descriptors.set(0, { type: 'dir', path: '/' });

        // Live views (same as wasi.js)
        this._liveViews = new Map();
    }

    _alloc() { return this._nextHandle++; }

    setInstance(instance) {
        this.instance = instance;
        this.memory = instance.exports.memory;
    }

    // ── Filesystem methods (identical to wasi.js) ──

    writeFile(path, data) {
        if (typeof data === 'string') data = new TextEncoder().encode(data);
        this.files.set(path, new Uint8Array(data));
        const parts = path.split('/');
        for (let i = 1; i < parts.length; i++) {
            this.dirs.add(parts.slice(0, i).join('/') || '/');
        }
        if (this.onFileChanged) this.onFileChanged(path);
    }

    readFile(path) {
        const lv = this._liveViews.get(path);
        if (lv) return new Uint8Array(lv.memory.buffer, lv.ptr, lv.size);
        return this.files.get(path) || null;
    }

    registerLiveView(path, memory, ptr, size) {
        this._liveViews.set(path, { memory, ptr, size });
    }

    _view() { return new DataView(this.memory.buffer); }
    _u8() { return new Uint8Array(this.memory.buffer); }
    _readString(ptr, len) {
        return this._decoder.decode(new Uint8Array(this.memory.buffer, ptr, len));
    }

    // ── Read a list<u8> from canonical ABI ──
    // Preview 2 passes (ptr, len) pairs for byte buffers.
    _readBytes(ptr, len) {
        return new Uint8Array(this.memory.buffer, ptr, len);
    }

    // ── Write bytes into WASM memory ──
    _writeBytes(ptr, data) {
        new Uint8Array(this.memory.buffer).set(data, ptr);
    }

    // ── Allocate bytes in WASM memory for return values ──
    _realloc(ptr, oldSize, align, newSize) {
        if (this.instance.exports.cabi_realloc) {
            return this.instance.exports.cabi_realloc(ptr, oldSize, align, newSize);
        }
        // Fallback: use malloc
        return this.instance.exports.malloc(newSize);
    }

    // ── Stream read helper ──
    _streamRead(handle, maxLen) {
        const s = this._streams.get(handle);
        if (!s) return { error: 'closed' };

        if (s.type === 'stdin') {
            if (s.buffer.length === 0) return { data: new Uint8Array(0) };
            const n = Math.min(maxLen, s.buffer.length);
            const data = new Uint8Array(s.buffer.splice(0, n));
            return { data };
        }

        if (s.type === 'file') {
            const fileData = this.files.get(s.path);
            if (!fileData) return { data: new Uint8Array(0) };
            const remaining = fileData.length - s.offset;
            if (remaining <= 0) return { data: new Uint8Array(0) };
            const n = Math.min(Number(maxLen), remaining);
            const data = fileData.slice(s.offset, s.offset + n);
            s.offset += n;
            return { data };
        }

        return { data: new Uint8Array(0) };
    }

    // ── Stream write helper ──
    _streamWrite(handle, data) {
        const s = this._streams.get(handle);
        if (!s) return 0;

        if (s.type === 'stdout') {
            const text = this._decoder.decode(data);
            if (this.onStdout) this.onStdout(text);
            return data.length;
        }
        if (s.type === 'stderr') {
            const text = this._decoder.decode(data);
            if (this.onStderr) this.onStderr(text);
            else console.error(text);
            return data.length;
        }
        if (s.type === 'protocol') {
            const text = this._decoder.decode(data);
            if (this.onProtocol) this.onProtocol(text);
            return data.length;
        }
        if (s.type === 'file') {
            const existing = this.files.get(s.path) || new Uint8Array(0);
            const offset = s.offset || 0;
            const needed = offset + data.length;
            let buf = existing;
            if (needed > existing.length) {
                buf = new Uint8Array(needed);
                buf.set(existing);
            }
            buf.set(data, offset);
            this.files.set(s.path, buf);
            s.offset = needed;
            if (this.onFileChanged && s.path.startsWith('/CIRCUITPY/')) {
                this.onFileChanged(s.path);
            }
            return data.length;
        }

        return 0;
    }

    // Push bytes to stdin (for serial input from JS)
    pushStdin(bytes) {
        const s = this._streams.get(0);
        if (s) {
            for (const b of bytes) s.buffer.push(b);
        }
    }

    getImports() {
        const self = this;

        return {
            // ── Our custom imports (unchanged) ──
            ffi: {
                request_frame(hint) { /* JS scheduling — handled by caller */ },
            },
            memfs: {
                register(pathPtr, pathLen, dataPtr, dataSize) {
                    // Deferred — caller flushes after init
                },
            },
            port: {
                registerPinListener(pin) {},
                unregisterPinListener(pin) {},
                getCpuTemperature() { return 25.0; },
                getCpuVoltage() { return 3.3; },
            },

            // ── WASI Preview 2 interfaces ──

            'wasi:cli/environment@0.2.0': {
                'get-environment'(retPtr) {
                    // Return empty list: (ptr=0, len=0)
                    const view = self._view();
                    view.setUint32(retPtr, 0, true);
                    view.setUint32(retPtr + 4, 0, true);
                },
            },

            'wasi:cli/exit@0.2.0': {
                exit(status) {
                    // status is a result<_, _> in canonical ABI
                    // For now just ignore
                },
            },

            'wasi:cli/stdin@0.2.0': {
                'get-stdin'() { return 0; }, // stream handle 0
            },

            'wasi:cli/stdout@0.2.0': {
                'get-stdout'() { return 1; }, // stream handle 1
            },

            'wasi:cli/stderr@0.2.0': {
                'get-stderr'() { return 2; }, // stream handle 2
            },

            'wasi:cli/terminal-input@0.2.0': {
                '[resource-drop]terminal-input'(h) {},
            },

            'wasi:cli/terminal-output@0.2.0': {
                '[resource-drop]terminal-output'(h) {},
            },

            'wasi:cli/terminal-stdin@0.2.0': {
                'get-terminal-stdin'() { return 0; },
            },

            'wasi:cli/terminal-stdout@0.2.0': {
                'get-terminal-stdout'() { return 1; },
            },

            'wasi:cli/terminal-stderr@0.2.0': {
                'get-terminal-stderr'() { return 2; },
            },

            'wasi:clocks/monotonic-clock@0.2.0': {
                now() {
                    return BigInt(Math.floor(performance.now() * 1e6));
                },

                'subscribe-instant'(when) {
                    const h = self._alloc();
                    self._pollables.set(h, {
                        type: 'clock',
                        deadline: when,
                        ready: false,
                    });
                    return h;
                },

                'subscribe-duration'(duration) {
                    const now = BigInt(Math.floor(performance.now() * 1e6));
                    const h = self._alloc();
                    self._pollables.set(h, {
                        type: 'clock',
                        deadline: now + duration,
                        ready: false,
                    });
                    return h;
                },
            },

            'wasi:clocks/wall-clock@0.2.0': {
                now(retPtr) {
                    const ms = Date.now();
                    const secs = BigInt(Math.floor(ms / 1000));
                    const nanos = (ms % 1000) * 1000000;
                    const view = self._view();
                    view.setBigUint64(retPtr, secs, true);
                    view.setUint32(retPtr + 8, nanos, true);
                },
            },

            'wasi:filesystem/preopens@0.2.0': {
                'get-directories'(retPtr) {
                    // Return list of (descriptor, name) tuples
                    // We have one preopen: descriptor 0, name "/"
                    const view = self._view();
                    // Allocate space for the tuple: (i32 descriptor, ptr+len name)
                    const tuplePtr = self._realloc(0, 0, 4, 12);
                    view.setUint32(tuplePtr, 0, true);  // descriptor handle
                    const namePtr = self._realloc(0, 0, 1, 1);
                    new Uint8Array(self.memory.buffer)[namePtr] = 0x2F; // "/"
                    view.setUint32(tuplePtr + 4, namePtr, true);
                    view.setUint32(tuplePtr + 8, 1, true);
                    // Return list header
                    view.setUint32(retPtr, tuplePtr, true);
                    view.setUint32(retPtr + 4, 1, true); // 1 entry
                },
            },

            'wasi:filesystem/types@0.2.0': {
                '[resource-drop]descriptor'(h) {
                    const d = self._descriptors.get(h);
                    if (d && d.type === 'file' && self.idb && self.idb.shouldPersist(d.path)) {
                        const data = self.files.get(d.path);
                        if (data) self.idb.save(d.path, data);
                    }
                    self._descriptors.delete(h);
                },

                '[resource-drop]directory-entry-stream'(h) {
                    self._dirStreams.delete(h);
                },

                '[method]descriptor.read-via-stream'(descH, offset, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; } // error
                    const sh = self._alloc();
                    self._streams.set(sh, {
                        type: 'file',
                        path: d.path,
                        offset: Number(offset),
                    });
                    const view = self._view();
                    view.setUint8(retPtr, 0); // ok
                    view.setUint32(retPtr + 4, sh, true);
                },

                '[method]descriptor.write-via-stream'(descH, offset, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const sh = self._alloc();
                    self._streams.set(sh, {
                        type: 'file',
                        path: d.path,
                        offset: Number(offset),
                    });
                    const view = self._view();
                    view.setUint8(retPtr, 0);
                    view.setUint32(retPtr + 4, sh, true);
                },

                '[method]descriptor.append-via-stream'(descH, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const fileData = self.files.get(d.path) || new Uint8Array(0);
                    const sh = self._alloc();
                    self._streams.set(sh, {
                        type: 'file',
                        path: d.path,
                        offset: fileData.length,
                    });
                    const view = self._view();
                    view.setUint8(retPtr, 0);
                    view.setUint32(retPtr + 4, sh, true);
                },

                '[method]descriptor.get-flags'(descH, retPtr) {
                    const view = self._view();
                    view.setUint8(retPtr, 0); // ok
                    view.setUint32(retPtr + 4, 0x7, true); // read|write|mutate
                },

                '[method]descriptor.read-directory'(descH, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d || d.type !== 'dir') {
                        self._view().setUint8(retPtr, 1);
                        return;
                    }
                    const entries = [];
                    const prefix = d.path === '/' ? '/' : d.path + '/';
                    for (const [p] of self.files) {
                        if (p.startsWith(prefix)) {
                            const rest = p.slice(prefix.length);
                            if (!rest.includes('/')) {
                                entries.push({ name: rest, type: 4 }); // regular file
                            }
                        }
                    }
                    for (const dir of self.dirs) {
                        if (dir.startsWith(prefix) && dir !== d.path) {
                            const rest = dir.slice(prefix.length);
                            if (!rest.includes('/')) {
                                entries.push({ name: rest, type: 3 }); // directory
                            }
                        }
                    }
                    const dsh = self._alloc();
                    self._dirStreams.set(dsh, { path: d.path, entries, index: 0 });
                    const view = self._view();
                    view.setUint8(retPtr, 0);
                    view.setUint32(retPtr + 4, dsh, true);
                },

                '[method]descriptor.sync'(descH, retPtr) {
                    self._view().setUint8(retPtr, 0); // ok
                },

                '[method]descriptor.create-directory-at'(descH, pathPtr, pathLen, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const name = self._readString(pathPtr, pathLen);
                    const fullPath = d.path === '/' ? '/' + name : d.path + '/' + name;
                    self.dirs.add(fullPath);
                    self._view().setUint8(retPtr, 0);
                },

                '[method]descriptor.stat'(descH, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const view = self._view();
                    view.setUint8(retPtr, 0); // ok
                    // descriptor-stat: type(u8) + padding + nlink(u64) + size(u64) + timestamps
                    const base = retPtr + 4;
                    view.setUint8(base, d.type === 'dir' ? 3 : 4);
                    view.setBigUint64(base + 8, 1n, true); // nlink
                    if (d.type === 'file') {
                        const data = self.files.get(d.path) || new Uint8Array(0);
                        view.setBigUint64(base + 16, BigInt(data.length), true);
                    }
                },

                '[method]descriptor.stat-at'(descH, flags, pathPtr, pathLen, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const name = self._readString(pathPtr, pathLen);
                    const fullPath = d.path === '/' ? '/' + name : d.path + '/' + name;
                    const view = self._view();

                    if (self.dirs.has(fullPath)) {
                        view.setUint8(retPtr, 0);
                        view.setUint8(retPtr + 4, 3); // directory
                        view.setBigUint64(retPtr + 12, 1n, true);
                        return;
                    }
                    const data = self.files.get(fullPath);
                    if (!data) { view.setUint8(retPtr, 1); return; }
                    view.setUint8(retPtr, 0);
                    view.setUint8(retPtr + 4, 4); // regular file
                    view.setBigUint64(retPtr + 12, 1n, true);
                    view.setBigUint64(retPtr + 20, BigInt(data.length), true);
                },

                '[method]descriptor.open-at'(descH, flags, pathPtr, pathLen, oflags, dflags, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const name = self._readString(pathPtr, pathLen);
                    const fullPath = d.path === '/' ? '/' + name : d.path + '/' + name;
                    const view = self._view();

                    // Check if directory
                    if (self.dirs.has(fullPath)) {
                        const h = self._alloc();
                        self._descriptors.set(h, { type: 'dir', path: fullPath });
                        view.setUint8(retPtr, 0);
                        view.setUint32(retPtr + 4, h, true);
                        return;
                    }

                    const create = oflags & 1;
                    const trunc = oflags & 4;

                    if (!self.files.has(fullPath) && !create) {
                        view.setUint8(retPtr, 1); // error
                        view.setUint8(retPtr + 8, 0); // last-error = NOENT
                        return;
                    }

                    if (create && !self.files.has(fullPath)) {
                        self.files.set(fullPath, new Uint8Array(0));
                        const parts = fullPath.split('/');
                        for (let i = 1; i < parts.length; i++) {
                            self.dirs.add(parts.slice(0, i).join('/') || '/');
                        }
                    }
                    if (trunc) {
                        self.files.set(fullPath, new Uint8Array(0));
                    }

                    const h = self._alloc();
                    self._descriptors.set(h, { type: 'file', path: fullPath, offset: 0 });
                    view.setUint8(retPtr, 0);
                    view.setUint32(retPtr + 4, h, true);
                },

                '[method]descriptor.remove-directory-at'(descH, pathPtr, pathLen, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const name = self._readString(pathPtr, pathLen);
                    const fullPath = d.path === '/' ? '/' + name : d.path + '/' + name;
                    self.dirs.delete(fullPath);
                    self._view().setUint8(retPtr, 0);
                },

                '[method]descriptor.rename-at'(descH, oldPtr, oldLen, newDescH, newPtr, newLen, retPtr) {
                    self._view().setUint8(retPtr, 0); // stub
                },

                '[method]descriptor.unlink-file-at'(descH, pathPtr, pathLen, retPtr) {
                    const d = self._descriptors.get(descH);
                    if (!d) { self._view().setUint8(retPtr, 1); return; }
                    const name = self._readString(pathPtr, pathLen);
                    const fullPath = d.path === '/' ? '/' + name : d.path + '/' + name;
                    self.files.delete(fullPath);
                    self._view().setUint8(retPtr, 0);
                },

                '[method]descriptor.metadata-hash'(descH, retPtr) {
                    const view = self._view();
                    view.setUint8(retPtr, 0);
                    view.setBigUint64(retPtr + 8, 0n, true);
                    view.setBigUint64(retPtr + 16, 0n, true);
                },

                '[method]descriptor.metadata-hash-at'(descH, flags, pathPtr, pathLen, retPtr) {
                    const view = self._view();
                    view.setUint8(retPtr, 0);
                    view.setBigUint64(retPtr + 8, 0n, true);
                    view.setBigUint64(retPtr + 16, 0n, true);
                },

                '[method]directory-entry-stream.read-directory-entry'(dsh, retPtr) {
                    const ds = self._dirStreams.get(dsh);
                    const view = self._view();
                    if (!ds || ds.index >= ds.entries.length) {
                        view.setUint8(retPtr, 0); // ok
                        view.setUint8(retPtr + 4, 0); // none (option::none)
                        return;
                    }
                    const entry = ds.entries[ds.index++];
                    const nameBytes = new TextEncoder().encode(entry.name);
                    const namePtr = self._realloc(0, 0, 1, nameBytes.length);
                    new Uint8Array(self.memory.buffer).set(nameBytes, namePtr);
                    view.setUint8(retPtr, 0); // ok
                    view.setUint8(retPtr + 4, 1); // some
                    view.setUint8(retPtr + 8, entry.type); // type
                    view.setUint32(retPtr + 12, namePtr, true);
                    view.setUint32(retPtr + 16, nameBytes.length, true);
                },
            },

            'wasi:io/error@0.2.0': {
                '[resource-drop]error'(h) {},
            },

            'wasi:io/poll@0.2.0': {
                '[resource-drop]pollable'(h) {
                    self._pollables.delete(h);
                },

                '[method]pollable.block'(h) {
                    // On main thread: can't block, return immediately.
                    // On Worker with SAB: could Atomics.wait here.
                    const p = self._pollables.get(h);
                    if (p) p.ready = true;
                },

                poll(listPtr, listLen, retPtr) {
                    // Check which pollables are ready.
                    // Returns list<u32> of ready indices.
                    const view = self._view();
                    const now = BigInt(Math.floor(performance.now() * 1e6));
                    const readyIndices = [];

                    for (let i = 0; i < listLen; i++) {
                        const h = view.getUint32(listPtr + i * 4, true);
                        const p = self._pollables.get(h);
                        if (!p) { readyIndices.push(i); continue; }

                        if (p.type === 'clock') {
                            if (now >= p.deadline) {
                                p.ready = true;
                                readyIndices.push(i);
                            }
                        } else if (p.type === 'stream-read') {
                            // Check if stream has data
                            const s = self._streams.get(p.stream);
                            if (s && s.type === 'stdin' && s.buffer.length > 0) {
                                readyIndices.push(i);
                            } else {
                                readyIndices.push(i); // non-blocking: always ready
                            }
                        } else {
                            readyIndices.push(i); // unknown: report ready
                        }
                    }

                    // If nothing ready, report first one ready (can't block)
                    if (readyIndices.length === 0) {
                        readyIndices.push(0);
                    }

                    // Write result list
                    const resultPtr = self._realloc(0, 0, 4, readyIndices.length * 4);
                    for (let i = 0; i < readyIndices.length; i++) {
                        view.setUint32(resultPtr + i * 4, readyIndices[i], true);
                    }
                    view.setUint32(retPtr, resultPtr, true);
                    view.setUint32(retPtr + 4, readyIndices.length, true);
                },
            },

            'wasi:io/streams@0.2.0': {
                '[resource-drop]input-stream'(h) {
                    self._streams.delete(h);
                },

                '[resource-drop]output-stream'(h) {
                    self._streams.delete(h);
                },

                '[method]input-stream.read'(h, maxLen, retPtr) {
                    const result = self._streamRead(h, Number(maxLen));
                    const view = self._view();
                    if (result.error) {
                        view.setUint8(retPtr, 1); // error
                        return;
                    }
                    const data = result.data;
                    if (data.length === 0) {
                        view.setUint8(retPtr, 0); // ok
                        view.setUint32(retPtr + 4, 0, true);
                        view.setUint32(retPtr + 8, 0, true);
                        return;
                    }
                    const dataPtr = self._realloc(0, 0, 1, data.length);
                    new Uint8Array(self.memory.buffer).set(data, dataPtr);
                    view.setUint8(retPtr, 0);
                    view.setUint32(retPtr + 4, dataPtr, true);
                    view.setUint32(retPtr + 8, data.length, true);
                },

                '[method]input-stream.subscribe'(h) {
                    const ph = self._alloc();
                    self._pollables.set(ph, {
                        type: 'stream-read',
                        stream: h,
                        ready: false,
                    });
                    return ph;
                },

                '[method]output-stream.check-write'(h, retPtr) {
                    // Always ready to write
                    const view = self._view();
                    view.setUint8(retPtr, 0); // ok
                    view.setBigUint64(retPtr + 8, 65536n, true); // can write 64KB
                },

                '[method]output-stream.write'(h, dataPtr, dataLen, retPtr) {
                    const data = new Uint8Array(self.memory.buffer, dataPtr, dataLen);
                    const n = self._streamWrite(h, data);
                    self._view().setUint8(retPtr, 0); // ok
                },

                '[method]output-stream.blocking-flush'(h, retPtr) {
                    self._view().setUint8(retPtr, 0); // ok — we're synchronous
                },

                '[method]output-stream.subscribe'(h) {
                    const ph = self._alloc();
                    self._pollables.set(ph, {
                        type: 'stream-write',
                        stream: h,
                        ready: true, // always ready
                    });
                    return ph;
                },
            },
        };
    }
}
