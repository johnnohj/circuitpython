/**
 * wasi-memfs.js — In-memory WASI runtime for CircuitPython WASM.
 *
 * All files live in a Map<string, Uint8Array>.  Writes to /hal/* paths
 * are intercepted and forwarded via callbacks for hardware simulation.
 *
 * Hardware state lives at /hal/ fd endpoints:
 *   /hal/gpio      — GPIO pin state (8 bytes/pin)
 *   /hal/analog    — ADC/DAC state (4 bytes/pin)
 *   /hal/pwm       — PWM state (8 bytes/pin)
 *   /hal/neopixel  — NeoPixel pixel data
 *   /hal/serial/rx — keyboard input (JS → Python)
 *   /hal/serial/tx — REPL output (Python → JS)
 *   /hal/i2c/dev/N — I2C device register files
 *   /hal/spi/xfer  — SPI transfer buffer
 *   /hal/uart/N/*  — UART streams
 *
 * Usage:
 *   const wasi = new WasiMemfs({
 *       args: ['circuitpython'],
 *       onHardwareWrite: (path, data) => {
 *           // Python wrote to a /hal/ endpoint — update UI or forward
 *       },
 *       onHardwareRead: (path) => {
 *           // Python is reading a /hal/ endpoint — return fresh data
 *       }
 *   });
 *   const instance = await WebAssembly.instantiate(module, wasi.getImports());
 *   wasi.setInstance(instance);
 *   instance.exports.cp_init();
 *   // Each frame: instance.exports.cp_step();
 */

const ERRNO = {
    SUCCESS: 0, BADF: 8, INVAL: 28, NOENT: 44, NOSYS: 52, ISDIR: 31,
};

export class WasiMemfs {
    constructor(options = {}) {
        this.args = options.args || ['circuitpython'];
        this.env = options.env || {};
        this.memory = null;
        this.instance = null;

        // In-memory filesystem: path → { data: Uint8Array, offset: number }
        this.files = new Map();

        // Preseeded directories (just track existence)
        this.dirs = new Set(['/']);

        // File descriptor table
        this.fds = new Map();
        this.nextFd = 4; // 0=stdin, 1=stdout, 2=stderr, 3=root preopen

        // fd 3 = preopened root "/"
        this.fds.set(3, { type: 'dir', path: '/' });

        // Callbacks
        this.onStdout = options.onStdout || null;
        this.onStderr = options.onStderr || null;
        this.onHardwareWrite = options.onHardwareWrite || null;
        this.onHardwareRead = options.onHardwareRead || null;
        this.onHardwareCommand = options.onHardwareCommand || null;

        // IndexedDB persistence backend (optional)
        this.idb = options.idb || null;

        this._decoder = new TextDecoder();
    }

    setInstance(instance) {
        this.instance = instance;
        this.memory = instance.exports.memory;
    }

    // Seed a file with initial content (e.g., code.py)
    writeFile(path, data) {
        if (typeof data === 'string') {
            data = new TextEncoder().encode(data);
        }
        this.files.set(path, new Uint8Array(data));
        // Ensure parent dirs exist
        const parts = path.split('/');
        for (let i = 1; i < parts.length; i++) {
            this.dirs.add(parts.slice(0, i).join('/') || '/');
        }
    }

    // Read a file's content
    readFile(path) {
        return this.files.get(path) || null;
    }

    // Write hardware state from JS into memfs (for reads by C common-hal)
    updateHardwareState(path, data) {
        this.files.set(path, new Uint8Array(data));
    }

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

    getImports() {
        const self = this;
        return {
            wasi_snapshot_preview1: {
                fd_write(fd, iovs, iovs_len, nwritten) {
                    const data = self._gatherIovecs(iovs, iovs_len);

                    if (fd === 1) {
                        const text = self._decoder.decode(data);
                        if (self.onStdout) self.onStdout(text);
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

                    const entry = self.fds.get(fd);
                    if (!entry || entry.type !== 'file') return ERRNO.BADF;

                    // Write to in-memory file
                    const existing = self.files.get(entry.path) || new Uint8Array(0);
                    const offset = entry.offset || 0;
                    const needed = offset + data.length;
                    let buf = existing;
                    if (needed > existing.length) {
                        buf = new Uint8Array(needed);
                        buf.set(existing);
                    }
                    buf.set(data, offset);
                    self.files.set(entry.path, buf);
                    entry.offset = needed;

                    // Intercept /hal/ writes → notify hardware listeners
                    if (entry.path.startsWith('/hal/') && self.onHardwareWrite) {
                        self.onHardwareWrite(entry.path, buf);
                    }

                    self._view().setUint32(nwritten, data.length, true);
                    return ERRNO.SUCCESS;
                },

                fd_read(fd, iovs, iovs_len, nread) {
                    if (fd === 0) {
                        // stdin — return EOF
                        self._view().setUint32(nread, 0, true);
                        return ERRNO.SUCCESS;
                    }

                    const entry = self.fds.get(fd);
                    if (!entry || entry.type !== 'file') return ERRNO.BADF;

                    // Intercept /hal/ reads → let JS provide fresh data
                    if (entry.path.startsWith('/hal/') && self.onHardwareRead) {
                        const fresh = self.onHardwareRead(entry.path, entry.offset || 0);
                        if (fresh) {
                            self.files.set(entry.path, new Uint8Array(fresh));
                        }
                    }

                    const fileData = self.files.get(entry.path);
                    if (!fileData) {
                        self._view().setUint32(nread, 0, true);
                        return ERRNO.SUCCESS;
                    }

                    const iovecs = self._readIovecs(iovs, iovs_len);
                    let totalRead = 0;
                    const offset = entry.offset || 0;

                    for (const iov of iovecs) {
                        const remaining = fileData.length - (offset + totalRead);
                        if (remaining <= 0) break;
                        const toRead = Math.min(iov.len, remaining);
                        new Uint8Array(self.memory.buffer, iov.buf, toRead)
                            .set(fileData.subarray(offset + totalRead, offset + totalRead + toRead));
                        totalRead += toRead;
                    }

                    entry.offset = offset + totalRead;
                    self._view().setUint32(nread, totalRead, true);
                    return ERRNO.SUCCESS;
                },

                fd_close(fd) {
                    if (fd <= 3) return ERRNO.SUCCESS;
                    const entry = self.fds.get(fd);
                    // Persist to IndexedDB if applicable
                    if (entry && entry.type === 'file' && self.idb && self.idb.shouldPersist(entry.path)) {
                        const data = self.files.get(entry.path);
                        if (data) self.idb.save(entry.path, data);
                    }
                    self.fds.delete(fd);
                    return ERRNO.SUCCESS;
                },

                fd_sync() { return ERRNO.SUCCESS; },

                fd_seek(fd, offset_lo, offset_hi, whence, newoffset) {
                    const entry = self.fds.get(fd);
                    if (!entry) return ERRNO.BADF;

                    const offset = Number(offset_lo);
                    const fileData = self.files.get(entry.path);
                    const size = fileData ? fileData.length : 0;

                    switch (whence) {
                        case 0: entry.offset = offset; break;      // SET
                        case 1: entry.offset = (entry.offset || 0) + offset; break; // CUR
                        case 2: entry.offset = size + offset; break; // END
                    }

                    const view = self._view();
                    view.setBigUint64(newoffset, BigInt(entry.offset || 0), true);
                    return ERRNO.SUCCESS;
                },

                fd_fdstat_get(fd, stat) {
                    const view = self._view();
                    const entry = self.fds.get(fd);
                    const filetype = (entry && entry.type === 'dir') ? 3 : 4;
                    view.setUint8(stat, filetype);
                    view.setUint16(stat + 2, 0, true); // flags
                    view.setBigUint64(stat + 8, 0xFFFFFFFFFFFFFFFFn, true); // rights_base
                    view.setBigUint64(stat + 16, 0xFFFFFFFFFFFFFFFFn, true); // rights_inheriting
                    return ERRNO.SUCCESS;
                },

                fd_prestat_get(fd, buf) {
                    if (fd !== 3) return ERRNO.BADF;
                    const view = self._view();
                    view.setUint8(buf, 0); // preopen type = dir
                    view.setUint32(buf + 4, 1, true); // name length "/"
                    return ERRNO.SUCCESS;
                },

                fd_prestat_dir_name(fd, path, path_len) {
                    if (fd !== 3) return ERRNO.BADF;
                    new Uint8Array(self.memory.buffer, path, 1).set([0x2F]); // "/"
                    return ERRNO.SUCCESS;
                },

                path_open(dirfd, dirflags, path_ptr, path_len, oflags, rights_base, rights_inheriting, fdflags, fd_ptr) {
                    const path = self._readString(path_ptr, path_len);
                    const dirEntry = self.fds.get(dirfd);
                    if (!dirEntry) return ERRNO.BADF;

                    const fullPath = dirEntry.path === '/'
                        ? '/' + path
                        : dirEntry.path + '/' + path;

                    // Check if it's a directory
                    if (self.dirs.has(fullPath)) {
                        const fd = self.nextFd++;
                        self.fds.set(fd, { type: 'dir', path: fullPath });
                        self._view().setUint32(fd_ptr, fd, true);
                        return ERRNO.SUCCESS;
                    }

                    const create = oflags & 1;
                    const trunc = oflags & 8;

                    if (!self.files.has(fullPath) && !create) {
                        return ERRNO.NOENT;
                    }

                    if (create && !self.files.has(fullPath)) {
                        self.files.set(fullPath, new Uint8Array(0));
                        // Ensure parent dirs
                        const parts = fullPath.split('/');
                        for (let i = 1; i < parts.length; i++) {
                            self.dirs.add(parts.slice(0, i).join('/') || '/');
                        }
                    }

                    if (trunc) {
                        self.files.set(fullPath, new Uint8Array(0));
                    }

                    const fd = self.nextFd++;
                    self.fds.set(fd, { type: 'file', path: fullPath, offset: 0 });
                    self._view().setUint32(fd_ptr, fd, true);
                    return ERRNO.SUCCESS;
                },

                path_filestat_get(dirfd, flags, path_ptr, path_len, buf) {
                    const path = self._readString(path_ptr, path_len);
                    const dirEntry = self.fds.get(dirfd);
                    if (!dirEntry) return ERRNO.BADF;
                    const fullPath = dirEntry.path === '/'
                        ? '/' + path
                        : dirEntry.path + '/' + path;

                    // WASI filestat layout (64 bytes):
                    //   0: u64 dev, 8: u64 ino, 16: u8 filetype,
                    //   24: u64 nlink, 32: u64 size,
                    //   40: u64 atim, 48: u64 mtim, 56: u64 ctim
                    const view = self._view();
                    // Zero the whole struct
                    for (let i = 0; i < 64; i++) view.setUint8(buf + i, 0);

                    if (self.dirs.has(fullPath)) {
                        view.setUint8(buf + 16, 3); // filetype = DIRECTORY
                        view.setBigUint64(buf + 24, 1n, true); // nlink
                        return ERRNO.SUCCESS;
                    }
                    const data = self.files.get(fullPath);
                    if (!data) return ERRNO.NOENT;
                    view.setUint8(buf + 16, 4); // filetype = REGULAR_FILE
                    view.setBigUint64(buf + 24, 1n, true); // nlink
                    view.setBigUint64(buf + 32, BigInt(data.length), true); // size
                    return ERRNO.SUCCESS;
                },

                path_create_directory(dirfd, path_ptr, path_len) {
                    const path = self._readString(path_ptr, path_len);
                    const dirEntry = self.fds.get(dirfd);
                    if (!dirEntry) return ERRNO.BADF;
                    const fullPath = dirEntry.path === '/'
                        ? '/' + path
                        : dirEntry.path + '/' + path;
                    self.dirs.add(fullPath);
                    return ERRNO.SUCCESS;
                },

                clock_time_get(id, precision, time_ptr) {
                    const ns = BigInt(Math.floor(performance.now() * 1e6));
                    self._view().setBigUint64(time_ptr, ns, true);
                    return ERRNO.SUCCESS;
                },

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
                    let totalSize = 0;
                    for (const arg of self.args) totalSize += new TextEncoder().encode(arg).length + 1;
                    view.setUint32(argv_buf_size, totalSize, true);
                    return ERRNO.SUCCESS;
                },

                args_get(argv, argv_buf) {
                    const view = self._view();
                    const u8 = self._u8();
                    let bufOffset = argv_buf;
                    for (let i = 0; i < self.args.length; i++) {
                        view.setUint32(argv + i * 4, bufOffset, true);
                        const encoded = new TextEncoder().encode(self.args[i]);
                        u8.set(encoded, bufOffset);
                        u8[bufOffset + encoded.length] = 0;
                        bufOffset += encoded.length + 1;
                    }
                    return ERRNO.SUCCESS;
                },

                proc_exit(code) {
                    throw new WasiMemfsExit(code);
                },

                random_get(buf, len) {
                    crypto.getRandomValues(new Uint8Array(self.memory.buffer, buf, len));
                    return ERRNO.SUCCESS;
                },

                sched_yield() { return ERRNO.SUCCESS; },
                poll_oneoff() { return ERRNO.SUCCESS; },
                path_remove_directory() { return ERRNO.NOSYS; },
                path_rename() { return ERRNO.NOSYS; },
                path_unlink_file(dirfd, path_ptr, path_len) {
                    const path = self._readString(path_ptr, path_len);
                    const dirEntry = self.fds.get(dirfd);
                    if (!dirEntry) return ERRNO.BADF;
                    const fullPath = dirEntry.path === '/'
                        ? '/' + path : dirEntry.path + '/' + path;
                    if (!self.files.has(fullPath)) return ERRNO.NOENT;
                    self.files.delete(fullPath);
                    if (self.idb && self.idb.shouldPersist(fullPath)) {
                        self.idb.remove(fullPath);
                    }
                    return ERRNO.SUCCESS;
                },
                fd_readdir() { return ERRNO.NOSYS; },
                fd_filestat_get(fd, buf) {
                    const entry = self.fds.get(fd);
                    if (!entry) return ERRNO.BADF;
                    const view = self._view();
                    for (let i = 0; i < 64; i++) view.setUint8(buf + i, 0);
                    if (entry.type === 'dir') {
                        view.setUint8(buf + 16, 3);
                    } else {
                        const data = self.files.get(entry.path);
                        view.setUint8(buf + 16, 4);
                        view.setBigUint64(buf + 32, BigInt(data ? data.length : 0), true);
                    }
                    view.setBigUint64(buf + 24, 1n, true);
                    return ERRNO.SUCCESS;
                },
                fd_filestat_set_size(fd, size) {
                    const entry = self.fds.get(fd);
                    if (!entry || entry.type !== 'file') return ERRNO.BADF;
                    const newSize = Number(size);
                    const existing = self.files.get(entry.path) || new Uint8Array(0);
                    if (newSize === 0) {
                        self.files.set(entry.path, new Uint8Array(0));
                    } else if (newSize < existing.length) {
                        self.files.set(entry.path, existing.slice(0, newSize));
                    } else if (newSize > existing.length) {
                        const buf = new Uint8Array(newSize);
                        buf.set(existing);
                        self.files.set(entry.path, buf);
                    }
                    return ERRNO.SUCCESS;
                },
            }
        };
    }
}

export class WasiMemfsExit extends Error {
    constructor(code) {
        super(`WASI exit: ${code}`);
        this.code = code;
    }
}

/**
 * IdbBackend — IndexedDB persistence for WasiMemfs.
 *
 * Provides a persistent CIRCUITPY drive backed by IndexedDB.
 * Files under the persist prefix (default "/CIRCUITPY/") are
 * saved to IndexedDB on fd_close and restored on load().
 *
 * No CORS headers required. Works everywhere IndexedDB works.
 *
 * Usage:
 *   const idb = new IdbBackend();
 *   await idb.load(wasiMemfs);   // restore files from IDB
 *   // ... later, on fd_close:
 *   idb.save(path, data);        // persist to IDB
 */
export class IdbBackend {
    constructor(options = {}) {
        this.dbName = options.dbName || 'circuitpython-fs';
        this.storeName = options.storeName || 'files';
        this.prefix = options.prefix || '/CIRCUITPY/';
        this._db = null;
    }

    /** Open (or create) the IndexedDB database. */
    async open() {
        if (this._db) return this._db;
        return new Promise((resolve, reject) => {
            const req = indexedDB.open(this.dbName, 1);
            req.onupgradeneeded = () => {
                req.result.createObjectStore(this.storeName);
            };
            req.onsuccess = () => {
                this._db = req.result;
                resolve(this._db);
            };
            req.onerror = () => reject(req.error);
        });
    }

    /** Load all persisted files into a WasiMemfs instance. */
    async load(memfs) {
        const db = await this.open();
        return new Promise((resolve, reject) => {
            const tx = db.transaction(this.storeName, 'readonly');
            const store = tx.objectStore(this.storeName);
            const req = store.openCursor();
            let count = 0;

            req.onsuccess = () => {
                const cursor = req.result;
                if (cursor) {
                    const path = cursor.key;
                    const data = cursor.value;
                    memfs.writeFile(path, new Uint8Array(data));
                    count++;
                    cursor.continue();
                } else {
                    resolve(count);
                }
            };
            req.onerror = () => reject(req.error);
        });
    }

    /** Check if a path should be persisted. */
    shouldPersist(path) {
        return path.startsWith(this.prefix);
    }

    /** Save a single file to IndexedDB. */
    save(path, data) {
        if (!this._db) return;
        const tx = this._db.transaction(this.storeName, 'readwrite');
        const store = tx.objectStore(this.storeName);
        // Store as ArrayBuffer (structured-cloneable)
        store.put(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength), path);
    }

    /** Delete a file from IndexedDB. */
    remove(path) {
        if (!this._db) return;
        const tx = this._db.transaction(this.storeName, 'readwrite');
        tx.objectStore(this.storeName).delete(path);
    }

    /** List all persisted file paths. */
    async list() {
        const db = await this.open();
        return new Promise((resolve, reject) => {
            const tx = db.transaction(this.storeName, 'readonly');
            const req = tx.objectStore(this.storeName).getAllKeys();
            req.onsuccess = () => resolve(req.result);
            req.onerror = () => reject(req.error);
        });
    }

    /** Clear all persisted files. */
    async clear() {
        const db = await this.open();
        return new Promise((resolve, reject) => {
            const tx = db.transaction(this.storeName, 'readwrite');
            const req = tx.objectStore(this.storeName).clear();
            req.onsuccess = () => resolve();
            req.onerror = () => reject(req.error);
        });
    }
}

/**
 * seedDrive — Ensure the CIRCUITPY drive has minimum required structure.
 *
 * On a real board, the drive ships with boot_out.txt and an empty lib/.
 * This seeds the same structure if files don't already exist.
 *
 * Call after loading from the persistence backend but before cp_init().
 */
export function seedDrive(memfs, options = {}) {
    const prefix = '/CIRCUITPY';

    memfs.dirs.add(prefix);
    memfs.dirs.add(prefix + '/lib');

    if (options.bootPy != null && !memfs.readFile(prefix + '/boot.py')) {
        memfs.writeFile(prefix + '/boot.py', options.bootPy);
    }
    if (options.codePy != null && !memfs.readFile(prefix + '/code.py')) {
        memfs.writeFile(prefix + '/code.py', options.codePy);
    }
    if (options.settingsToml != null && !memfs.readFile(prefix + '/settings.toml')) {
        memfs.writeFile(prefix + '/settings.toml', options.settingsToml);
    }
    if (!memfs.readFile(prefix + '/boot_out.txt')) {
        memfs.writeFile(prefix + '/boot_out.txt',
            'CircuitPython WASM\nBoard ID: wasm-browser\n');
    }
}
