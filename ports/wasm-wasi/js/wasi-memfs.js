/**
 * wasi-memfs.js — In-memory WASI runtime for the main-thread reactor.
 *
 * All files live in a Map<string, Uint8Array>.  Writes to /hw/* paths
 * are intercepted and forwarded via a callback (for postMessage to worker).
 *
 * No OPFS, no CORS headers, no synchronous access handles.
 * Works on the main thread (no Worker required).
 *
 * Usage:
 *   const wasi = new WasiMemfs({
 *       args: ['circuitpython'],
 *       onHardwareWrite: (path, data) => {
 *           worker.postMessage({ type: 'hw', path, data: data.buffer }, [data.buffer]);
 *       }
 *   });
 *   const instance = await WebAssembly.instantiate(module, wasi.getImports());
 *   wasi.setInstance(instance);
 *   // For reactor: call instance.exports.mp_runtime_init(), mp_vm_step(), etc.
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
        this.onHardwareCommand = options.onHardwareCommand || null;

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

    // Write hardware state from worker into memfs (for reads by reactor)
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

                    // Intercept /hw/cmd writes → route through weBlinka
                    if (entry.path === '/hw/cmd' && self.onHardwareCommand) {
                        const response = self.onHardwareCommand(buf);
                        if (response) {
                            // Write response to /hw/rsp for query() to read
                            self.files.set('/hw/rsp', new Uint8Array(response));
                        }
                    } else if (entry.path.startsWith('/hw/') && self.onHardwareWrite) {
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
                path_unlink_file() { return ERRNO.NOSYS; },
                fd_readdir() { return ERRNO.NOSYS; },
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
