/*
 * VDevBridge.js — JS helpers for the MEMFS virtual hardware bus
 *
 * Wraps Module.FS operations for the common patterns used by PythonHost
 * and worker scripts: reading results, injecting events, writing modules,
 * and checkpointing VM state.
 */

export class VDevBridge {
    constructor(Module) {
        this._M = Module;
        this._FS = Module.FS;
    }

    // ── Result / state ─────────────────────────────────────────────────────

    /** Read /state/result.json written by mp_memfs_finish_run(). */
    readResult() {
        try {
            return JSON.parse(this._FS.readFile('/state/result.json', { encoding: 'utf8' }));
        } catch {
            return { delta: {}, stdout: '', stderr: '', aborted: false,
                     duration_ms: 0, frames: [] };
        }
    }

    /** Read /state/snapshot.json (pre-run globals baseline). */
    readSnapshot() {
        try {
            return JSON.parse(this._FS.readFile('/state/snapshot.json', { encoding: 'utf8' }));
        } catch {
            return {};
        }
    }

    // ── Device I/O ─────────────────────────────────────────────────────────

    /** Read a /dev/ file (returns empty string on error). */
    devRead(name) {
        try {
            return this._FS.readFile('/dev/' + name, { encoding: 'utf8' });
        } catch {
            return '';
        }
    }

    /** Overwrite a /dev/ file. */
    devWrite(name, content) {
        this._FS.writeFile('/dev/' + name, content);
    }

    /** Append to a /dev/ file. */
    devAppend(name, content) {
        const existing = this.devRead(name);
        this._FS.writeFile('/dev/' + name, existing + content);
    }

    /**
     * Inject a JSON message into /dev/bc_in for Python to read.
     * Python: open('/dev/bc_in').readline() → JSON parse
     */
    bcInWrite(msgObj) {
        this.devAppend('bc_in', JSON.stringify(msgObj) + '\n');
    }

    /**
     * Request a KeyboardInterrupt by writing 0x03 to /dev/interrupt.
     * mp_hal_hook() picks it up within 64 bytecodes.
     */
    sendInterrupt() {
        this._FS.writeFile('/dev/interrupt', '\x03');
    }

    // ── File system ─────────────────────────────────────────────────────────

    /** Write a Python source file to /flash/. Creates parent dirs. */
    writeModule(relativePath, source) {
        const full = relativePath.startsWith('/') ? relativePath
                   : '/flash/' + relativePath;
        this._mkdirp(full.substring(0, full.lastIndexOf('/')));
        this._FS.writeFile(full, source);
    }

    /** Write any file into MEMFS. */
    writeFile(path, content) {
        this._mkdirp(path.substring(0, path.lastIndexOf('/')));
        this._FS.writeFile(path, content);
    }

    /** Read any file from MEMFS. */
    readFile(path, binary = false) {
        return this._FS.readFile(path, binary ? undefined : { encoding: 'utf8' });
    }

    // ── Checkpoint (Phase 2+) ──────────────────────────────────────────────

    /**
     * Snapshot GC heap + pystack from /mem/ (written by mp_memfs_checkpoint_vm).
     * Returns { heap: Uint8Array, pystack: Uint8Array } or null if not written.
     */
    checkpointVM() {
        try {
            return {
                heap:    this._FS.readFile('/mem/heap'),
                pystack: this._FS.readFile('/mem/pystack'),
            };
        } catch {
            return null;
        }
    }

    // ── Internal ────────────────────────────────────────────────────────────

    _mkdirp(dir) {
        if (!dir || dir === '/') { return; }
        try { this._FS.mkdir(dir); } catch (e) {
            if (e.errno !== 20 /* EEXIST */) {
                this._mkdirp(dir.substring(0, dir.lastIndexOf('/')));
                try { this._FS.mkdir(dir); } catch {}
            }
        }
    }
}
