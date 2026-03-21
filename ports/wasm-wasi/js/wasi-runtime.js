/**
 * wasi-runtime.js — Minimal WASI preview1 runtime for browser workers
 *
 * Maps WASI fd_* syscalls to OPFS FileSystemSyncAccessHandle operations.
 * Designed for use in a Web Worker (synchronous OPFS access).
 *
 * File descriptor layout:
 *   0 — stdin  (not yet implemented, returns EOF)
 *   1 — stdout (writes to console + /dev/repl stdout ring)
 *   2 — stderr (writes to console)
 *   3 — preopened directory "/" (root)
 *   4+ — opened files (via path_open)
 */

const WASI_ERRNO = {
    SUCCESS: 0,
    BADF: 8,
    INVAL: 28,
    NOENT: 44,
    NOSYS: 52,
    ISDIR: 31,
    NOTDIR: 54,
};

const WASI_FILETYPE = {
    UNKNOWN: 0,
    DIRECTORY: 3,
    REGULAR_FILE: 4,
};

const WASI_FDFLAG = {
    APPEND: 1,
    DSYNC: 2,
    NONBLOCK: 4,
    RSYNC: 8,
    SYNC: 16,
};

const WASI_WHENCE = {
    SET: 0,
    CUR: 1,
    END: 2,
};

const WASI_CLOCKID = {
    REALTIME: 0,
    MONOTONIC: 1,
};

export class WasiRuntime {
    constructor(opfsRoot, options = {}) {
        this.opfsRoot = opfsRoot;        // OPFS root DirectoryHandle
        this.args = options.args || [];
        this.memory = null;              // Set after instantiation
        this.instance = null;

        // File descriptor table
        // fd 0-2 are stdin/stdout/stderr
        // fd 3 is the preopened root directory
        this.fds = new Map();
        this.nextFd = 4;
        this.stdout_buf = [];
        this.stderr_buf = [];

        // Preopened directory
        this.fds.set(3, {
            type: 'dir',
            path: '/',
            handle: opfsRoot,
            preopen: true,
        });
    }

    setInstance(instance) {
        this.instance = instance;
        this.memory = instance.exports.memory;
    }

    // ---- Memory helpers ----

    _view() { return new DataView(this.memory.buffer); }
    _u8() { return new Uint8Array(this.memory.buffer); }

    _readString(ptr, len) {
        return new TextDecoder().decode(this._u8().slice(ptr, ptr + len));
    }

    _writeString(ptr, str) {
        const bytes = new TextEncoder().encode(str);
        this._u8().set(bytes, ptr);
        return bytes.length;
    }

    // Read iovec array: [{buf, len}, ...]
    _readIovecs(iovs_ptr, iovs_len) {
        const view = this._view();
        const result = [];
        for (let i = 0; i < iovs_len; i++) {
            const buf = view.getUint32(iovs_ptr + i * 8, true);
            const len = view.getUint32(iovs_ptr + i * 8 + 4, true);
            result.push({ buf, len });
        }
        return result;
    }

    // ---- OPFS helpers ----

    async _getFileHandle(path) {
        const parts = path.split('/').filter(p => p.length > 0);
        let dir = this.opfsRoot;
        for (let i = 0; i < parts.length - 1; i++) {
            dir = await dir.getDirectoryHandle(parts[i], { create: true });
        }
        if (parts.length === 0) return dir;
        const name = parts[parts.length - 1];
        return await dir.getFileHandle(name, { create: true });
    }

    async _getDirHandle(path) {
        const parts = path.split('/').filter(p => p.length > 0);
        let dir = this.opfsRoot;
        for (const part of parts) {
            dir = await dir.getDirectoryHandle(part, { create: true });
        }
        return dir;
    }

    // ---- WASI import object ----

    getImports() {
        const self = this;
        return {
            wasi_snapshot_preview1: {
                args_get: (argv, argv_buf) => self._args_get(argv, argv_buf),
                args_sizes_get: (argc_ptr, argv_buf_size_ptr) => self._args_sizes_get(argc_ptr, argv_buf_size_ptr),
                clock_time_get: (id, precision, time_ptr) => self._clock_time_get(id, precision, time_ptr),
                fd_close: (fd) => self._fd_close(fd),
                fd_fdstat_get: (fd, buf) => self._fd_fdstat_get(fd, buf),
                fd_filestat_get: (fd, buf) => self._fd_filestat_get(fd, buf),
                fd_prestat_get: (fd, buf) => self._fd_prestat_get(fd, buf),
                fd_prestat_dir_name: (fd, path, path_len) => self._fd_prestat_dir_name(fd, path, path_len),
                fd_read: (fd, iovs, iovs_len, nread) => self._fd_read(fd, iovs, iovs_len, nread),
                fd_readdir: (fd, buf, buf_len, cookie, bufused) => self._fd_readdir(fd, buf, buf_len, cookie, bufused),
                fd_seek: (fd, offset, whence, newoffset) => self._fd_seek(fd, offset, whence, newoffset),
                fd_sync: (fd) => self._fd_sync(fd),
                fd_write: (fd, iovs, iovs_len, nwritten) => self._fd_write(fd, iovs, iovs_len, nwritten),
                path_create_directory: (fd, path, path_len) => self._path_create_directory(fd, path, path_len),
                path_filestat_get: (fd, flags, path, path_len, buf) => self._path_filestat_get(fd, flags, path, path_len, buf),
                path_open: (dirfd, dirflags, path, path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, fd_ptr) =>
                    self._path_open(dirfd, dirflags, path, path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, fd_ptr),
                path_remove_directory: (fd, path, path_len) => self._path_remove_directory(fd, path, path_len),
                path_rename: (fd, old_path, old_len, new_fd, new_path, new_len) =>
                    self._path_rename(fd, old_path, old_len, new_fd, new_path, new_len),
                path_unlink_file: (fd, path, path_len) => self._path_unlink_file(fd, path, path_len),
                proc_exit: (code) => self._proc_exit(code),
            }
        };
    }

    // ---- WASI implementations ----

    _args_get(argv, argv_buf) {
        const view = this._view();
        const u8 = this._u8();
        let buf_offset = argv_buf;
        for (let i = 0; i < this.args.length; i++) {
            view.setUint32(argv + i * 4, buf_offset, true);
            const bytes = new TextEncoder().encode(this.args[i] + '\0');
            u8.set(bytes, buf_offset);
            buf_offset += bytes.length;
        }
        return WASI_ERRNO.SUCCESS;
    }

    _args_sizes_get(argc_ptr, argv_buf_size_ptr) {
        const view = this._view();
        view.setUint32(argc_ptr, this.args.length, true);
        let buf_size = 0;
        for (const arg of this.args) {
            buf_size += new TextEncoder().encode(arg + '\0').length;
        }
        view.setUint32(argv_buf_size_ptr, buf_size, true);
        return WASI_ERRNO.SUCCESS;
    }

    _clock_time_get(id, precision, time_ptr) {
        const view = this._view();
        let ns;
        if (id === WASI_CLOCKID.MONOTONIC) {
            ns = BigInt(Math.round(performance.now() * 1e6));
        } else {
            ns = BigInt(Date.now()) * 1000000n;
        }
        view.setBigUint64(time_ptr, ns, true);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_close(fd) {
        const entry = this.fds.get(fd);
        if (!entry) return WASI_ERRNO.BADF;
        if (entry.accessHandle) {
            entry.accessHandle.close();
        }
        this.fds.delete(fd);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_fdstat_get(fd, buf) {
        const view = this._view();
        const entry = this.fds.get(fd);
        if (!entry) return WASI_ERRNO.BADF;
        // filetype (u8), fdflags (u16), rights_base (u64), rights_inheriting (u64)
        const filetype = entry.type === 'dir' ? WASI_FILETYPE.DIRECTORY : WASI_FILETYPE.REGULAR_FILE;
        view.setUint8(buf, filetype);
        view.setUint16(buf + 2, 0, true); // fdflags
        view.setBigUint64(buf + 8, 0xFFFFFFFFFFFFFFFFn, true);  // rights_base (all)
        view.setBigUint64(buf + 16, 0xFFFFFFFFFFFFFFFFn, true); // rights_inheriting
        return WASI_ERRNO.SUCCESS;
    }

    _fd_filestat_get(fd, buf) {
        const entry = this.fds.get(fd);
        if (!entry) return WASI_ERRNO.BADF;
        const view = this._view();
        // Zero the 64-byte stat buffer
        for (let i = 0; i < 64; i++) view.setUint8(buf + i, 0);
        if (entry.accessHandle) {
            const size = entry.accessHandle.getSize();
            view.setBigUint64(buf + 32, BigInt(size), true); // filesize
        }
        view.setUint8(buf + 16, entry.type === 'dir' ? WASI_FILETYPE.DIRECTORY : WASI_FILETYPE.REGULAR_FILE);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_prestat_get(fd, buf) {
        const entry = this.fds.get(fd);
        if (!entry || !entry.preopen) return WASI_ERRNO.BADF;
        const view = this._view();
        view.setUint8(buf, 0); // __WASI_PREOPENTYPE_DIR
        const pathBytes = new TextEncoder().encode(entry.path);
        view.setUint32(buf + 4, pathBytes.length, true);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_prestat_dir_name(fd, path, path_len) {
        const entry = this.fds.get(fd);
        if (!entry || !entry.preopen) return WASI_ERRNO.BADF;
        const bytes = new TextEncoder().encode(entry.path);
        this._u8().set(bytes.slice(0, path_len), path);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_read(fd, iovs, iovs_len, nread) {
        const view = this._view();
        const entry = this.fds.get(fd);

        if (fd === 0) {
            // stdin: return EOF for now
            view.setUint32(nread, 0, true);
            return WASI_ERRNO.SUCCESS;
        }

        if (!entry || !entry.accessHandle) return WASI_ERRNO.BADF;

        const iovecs = this._readIovecs(iovs, iovs_len);
        let total = 0;
        for (const iov of iovecs) {
            const buf = new Uint8Array(iov.len);
            const n = entry.accessHandle.read(buf, { at: entry.offset || 0 });
            this._u8().set(buf.subarray(0, n), iov.buf);
            total += n;
            entry.offset = (entry.offset || 0) + n;
            if (n < iov.len) break; // short read
        }
        view.setUint32(nread, total, true);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_write(fd, iovs, iovs_len, nwritten) {
        const view = this._view();
        const iovecs = this._readIovecs(iovs, iovs_len);

        let total = 0;
        const chunks = [];
        for (const iov of iovecs) {
            chunks.push(this._u8().slice(iov.buf, iov.buf + iov.len));
            total += iov.len;
        }
        const data = new Uint8Array(total);
        let off = 0;
        for (const chunk of chunks) {
            data.set(chunk, off);
            off += chunk.length;
        }

        if (fd === 1) {
            // stdout
            const text = new TextDecoder().decode(data);
            this.stdout_buf.push(text);
            if (this.onStdout) this.onStdout(text);
            view.setUint32(nwritten, total, true);
            return WASI_ERRNO.SUCCESS;
        }

        if (fd === 2) {
            // stderr
            const text = new TextDecoder().decode(data);
            this.stderr_buf.push(text);
            if (this.onStderr) this.onStderr(text);
            else console.error(text);
            view.setUint32(nwritten, total, true);
            return WASI_ERRNO.SUCCESS;
        }

        const entry = this.fds.get(fd);
        if (!entry || !entry.accessHandle) return WASI_ERRNO.BADF;

        entry.accessHandle.write(data, { at: entry.offset || 0 });
        entry.offset = (entry.offset || 0) + total;
        view.setUint32(nwritten, total, true);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_seek(fd, offset, whence, newoffset) {
        const entry = this.fds.get(fd);
        if (!entry) return WASI_ERRNO.BADF;

        // offset is passed as i64 but JS receives it as two i32s or BigInt
        // In practice, wasm-ld passes it as (i32 low, i32 high) for i64
        const off = Number(offset);

        let pos;
        if (whence === WASI_WHENCE.SET) {
            pos = off;
        } else if (whence === WASI_WHENCE.CUR) {
            pos = (entry.offset || 0) + off;
        } else if (whence === WASI_WHENCE.END) {
            const size = entry.accessHandle ? entry.accessHandle.getSize() : 0;
            pos = size + off;
        } else {
            return WASI_ERRNO.INVAL;
        }

        entry.offset = pos;
        const view = this._view();
        view.setBigUint64(newoffset, BigInt(pos), true);
        return WASI_ERRNO.SUCCESS;
    }

    _fd_sync(fd) {
        const entry = this.fds.get(fd);
        if (!entry) return WASI_ERRNO.BADF;
        if (entry.accessHandle) {
            entry.accessHandle.flush();
        }
        return WASI_ERRNO.SUCCESS;
    }

    _path_open(dirfd, dirflags, path_ptr, path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, fd_ptr) {
        const path = this._readString(path_ptr, path_len);
        const entry = this.fds.get(dirfd);
        if (!entry) return WASI_ERRNO.BADF;

        // Resolve path relative to dirfd
        const fullPath = entry.path === '/' ? '/' + path : entry.path + '/' + path;

        try {
            const fileHandle = this._getFileHandleSync(fullPath);
            if (!fileHandle) return WASI_ERRNO.NOENT;

            const accessHandle = fileHandle.createSyncAccessHandle();
            const fd = this.nextFd++;
            this.fds.set(fd, {
                type: 'file',
                path: fullPath,
                handle: fileHandle,
                accessHandle: accessHandle,
                offset: 0,
            });

            this._view().setUint32(fd_ptr, fd, true);
            return WASI_ERRNO.SUCCESS;
        } catch (e) {
            // File might not exist — try creating
            if (oflags & 1) { // __WASI_OFLAGS_CREAT
                try {
                    const fileHandle = this._createFileSync(fullPath);
                    const accessHandle = fileHandle.createSyncAccessHandle();
                    if (oflags & 8) { // __WASI_OFLAGS_TRUNC
                        accessHandle.truncate(0);
                    }
                    const fd = this.nextFd++;
                    this.fds.set(fd, {
                        type: 'file',
                        path: fullPath,
                        handle: fileHandle,
                        accessHandle: accessHandle,
                        offset: 0,
                    });
                    this._view().setUint32(fd_ptr, fd, true);
                    return WASI_ERRNO.SUCCESS;
                } catch (e2) {
                    return WASI_ERRNO.NOENT;
                }
            }
            return WASI_ERRNO.NOENT;
        }
    }

    // Synchronous OPFS file handle operations (for use in workers)
    _getFileHandleSync(path) {
        // This is synchronous — works in workers with OPFS
        // Implemented via the async getFileHandle wrapped in a sync context
        // In practice, we pre-populate the file handles during init
        throw new Error('_getFileHandleSync must be overridden with sync OPFS access');
    }

    _createFileSync(path) {
        throw new Error('_createFileSync must be overridden with sync OPFS access');
    }

    _path_create_directory(fd, path_ptr, path_len) {
        // Directories are created implicitly by OPFS path navigation
        return WASI_ERRNO.SUCCESS;
    }

    _path_filestat_get(fd, flags, path_ptr, path_len, buf) {
        // Stub: return zeros (file not found is OK for many use cases)
        const view = this._view();
        for (let i = 0; i < 64; i++) view.setUint8(buf + i, 0);
        return WASI_ERRNO.SUCCESS;
    }

    _path_remove_directory(fd, path_ptr, path_len) {
        return WASI_ERRNO.NOSYS;
    }

    _path_rename(fd, old_path_ptr, old_len, new_fd, new_path_ptr, new_len) {
        return WASI_ERRNO.NOSYS;
    }

    _path_unlink_file(fd, path_ptr, path_len) {
        return WASI_ERRNO.NOSYS;
    }

    _fd_readdir(fd, buf, buf_len, cookie, bufused) {
        // Stub: empty directory listing
        this._view().setUint32(bufused, 0, true);
        return WASI_ERRNO.SUCCESS;
    }

    _proc_exit(code) {
        throw new WasiExit(code);
    }
}

export class WasiExit extends Error {
    constructor(code) {
        super(`WASI exit with code ${code}`);
        this.code = code;
    }
}
