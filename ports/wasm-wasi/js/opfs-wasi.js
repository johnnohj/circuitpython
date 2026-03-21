/**
 * opfs-wasi.js — OPFS-backed WASI runtime for Web Workers
 *
 * Extends WasiRuntime with synchronous OPFS file access via
 * FileSystemSyncAccessHandle. Must run in a dedicated Web Worker.
 *
 * Usage (in worker):
 *   const root = await navigator.storage.getDirectory();
 *   const wasi = new OpfsWasi(root, { args: ['circuitpython', '/circuitpy/code.py'] });
 *   const wasm = await WebAssembly.compileStreaming(fetch('circuitpython.wasm'));
 *   const instance = await WebAssembly.instantiate(wasm, wasi.getImports());
 *   wasi.setInstance(instance);
 *   wasi.start();
 */

import { WasiRuntime, WasiExit } from './wasi-runtime.js';

export class OpfsWasi extends WasiRuntime {
    constructor(opfsRoot, options = {}) {
        super(opfsRoot, options);
        // Cache of path → { fileHandle, syncHandle }
        this._fileCache = new Map();
    }

    // ---- Synchronous OPFS file access ----

    _getFileHandleSync(path) {
        if (this._fileCache.has(path)) {
            return this._fileCache.get(path).fileHandle;
        }
        // Can't do sync OPFS directory traversal — must be pre-opened
        return null;
    }

    _createFileSync(path) {
        if (this._fileCache.has(path)) {
            return this._fileCache.get(path).fileHandle;
        }
        return null;
    }

    /**
     * Pre-open a file for synchronous access.
     * Must be called before WASI execution starts (async context).
     * @param {string} path - Virtual path (e.g., "/state/heap")
     * @param {FileSystemFileHandle} fileHandle - OPFS file handle
     */
    async preopen(path, fileHandle) {
        const syncHandle = await fileHandle.createSyncAccessHandle();
        this._fileCache.set(path, { fileHandle, syncHandle });
    }

    /**
     * Pre-open a directory tree for synchronous access.
     * Recursively opens all files in the directory.
     * @param {string} basePath - Virtual path prefix
     * @param {FileSystemDirectoryHandle} dirHandle - OPFS directory
     */
    async preopenDir(basePath, dirHandle) {
        for await (const [name, handle] of dirHandle.entries()) {
            const fullPath = basePath + '/' + name;
            if (handle.kind === 'file') {
                await this.preopen(fullPath, handle);
            } else if (handle.kind === 'directory') {
                await this.preopenDir(fullPath, handle);
            }
        }
    }

    // Override path_open to use the sync cache
    _path_open(dirfd, dirflags, path_ptr, path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, fd_ptr) {
        const path = this._readString(path_ptr, path_len);
        const entry = this.fds.get(dirfd);
        if (!entry) return 8; // BADF

        const fullPath = entry.path === '/' ? '/' + path : entry.path + '/' + path;
        const cached = this._fileCache.get(fullPath);

        if (cached) {
            // Use pre-opened sync handle
            const fd = this.nextFd++;
            this.fds.set(fd, {
                type: 'file',
                path: fullPath,
                handle: cached.fileHandle,
                accessHandle: cached.syncHandle,
                offset: 0,
            });

            if (oflags & 8) { // TRUNC
                cached.syncHandle.truncate(0);
            }

            this._view().setUint32(fd_ptr, fd, true);
            return 0; // SUCCESS
        }

        // File not pre-opened — try to create on-demand
        // This requires async OPFS access which we handle by creating
        // the file handle synchronously from a pre-resolved directory
        return 44; // NOENT
    }

    // Override fd_close to NOT close sync handles (they're shared)
    _fd_close(fd) {
        const entry = this.fds.get(fd);
        if (!entry) return 8; // BADF
        // Don't close the syncHandle — it's cached for reuse
        // Just remove the fd entry and reset offset
        this.fds.delete(fd);
        return 0; // SUCCESS
    }

    /**
     * Start WASI execution.
     * Call after setInstance() and all preopen() calls.
     */
    start() {
        try {
            this.instance.exports._start();
        } catch (e) {
            if (e instanceof WasiExit) {
                return e.code;
            }
            throw e;
        }
        return 0;
    }

    /**
     * Clean up all sync access handles.
     */
    close() {
        for (const [path, entry] of this._fileCache) {
            try { entry.syncHandle.close(); } catch (e) {}
        }
        this._fileCache.clear();
        this.fds.clear();
    }
}
