/**
 * libpyopfs.js — Emscripten library: OPFS-backed shared memory regions
 *
 * Provides C-callable functions (opfs_init, opfs_read, opfs_write, etc.)
 * backed by the Origin Private File System in browsers with synchronous
 * I/O via FileSystemSyncAccessHandle.
 *
 * Falls back to in-memory ArrayBuffers in Node.js or unsupported browsers.
 *
 * Region layout (mirrors opfs_regions.h):
 *   0: registers.bin  (512 bytes)   — 256 × uint16 GPIO/ADC values
 *   1: sensors.bin    (4096 bytes)  — I2C/SPI request/response mailbox
 *   2: events.bin     (8192 bytes)  — binary event ring buffer
 *   3: framebuf.bin   (variable)    — display pixel data
 *   4: control.bin    (64 bytes)    — flags, timestamps
 */

mergeInto(LibraryManager.library, {

    // ---- Region configuration ----

    $OPFS_REGIONS: {
        names:  ['registers', 'sensors', 'events', 'framebuf', 'control'],
        sizes:  [512, 4096, 8192, 0, 64],
        NUM:    5,
    },

    // ---- Initialization ----

    opfs_init__deps: ['$OPFS_REGIONS'],
    opfs_init__async: true,  // may need await for OPFS handle acquisition
    opfs_init: async function() {
        if (Module._opfsInitialized) return;
        Module._opfsInitialized = true;

        const regions = OPFS_REGIONS;
        Module._opfsRegions = new Array(regions.NUM);
        Module._opfsHandles = new Array(regions.NUM);

        // Detect OPFS availability (browser with dedicated worker context)
        const hasOPFS = typeof globalThis.navigator !== 'undefined' &&
                        typeof globalThis.navigator.storage !== 'undefined' &&
                        typeof globalThis.navigator.storage.getDirectory === 'function';

        if (hasOPFS) {
            try {
                const root = await navigator.storage.getDirectory();
                // Create or open /pyvm/ directory
                const dir = await root.getDirectoryHandle('pyvm', { create: true });
                const regDir = await dir.getDirectoryHandle('regions', { create: true });

                for (let i = 0; i < regions.NUM; i++) {
                    const size = regions.sizes[i];
                    if (size === 0) {
                        // Variable-size regions: allocate on demand
                        Module._opfsRegions[i] = null;
                        Module._opfsHandles[i] = null;
                        continue;
                    }
                    const name = regions.names[i] + '.bin';
                    const fileHandle = await regDir.getFileHandle(name, { create: true });
                    const accessHandle = await fileHandle.createSyncAccessHandle();

                    // Ensure file is at least the expected size
                    if (accessHandle.getSize() < size) {
                        accessHandle.truncate(size);
                    }

                    Module._opfsHandles[i] = accessHandle;
                    // Also keep an in-memory cache for fast reads
                    const buf = new ArrayBuffer(size);
                    const view = new Uint8Array(buf);
                    accessHandle.read(view, { at: 0 });
                    Module._opfsRegions[i] = view;
                }
                Module._opfsMode = 'opfs';
                return;
            } catch (e) {
                // OPFS failed (e.g., not in a dedicated worker) — fall through to memory
            }
        }

        // Fallback: in-memory buffers (Node.js or main thread)
        for (let i = 0; i < regions.NUM; i++) {
            const size = regions.sizes[i];
            if (size === 0) {
                Module._opfsRegions[i] = null;
            } else {
                Module._opfsRegions[i] = new Uint8Array(new ArrayBuffer(size));
            }
            Module._opfsHandles[i] = null;
        }
        Module._opfsMode = 'memory';
    },

    // ---- Read ----

    opfs_read__deps: ['opfs_init', '$OPFS_REGIONS'],
    opfs_read: function(region_id, offset, buf_ptr, len) {
        if (!Module._opfsInitialized) return 0;
        if (region_id < 0 || region_id >= OPFS_REGIONS.NUM) return 0;
        const region = Module._opfsRegions[region_id];
        if (!region) return 0;

        // If OPFS, read from the access handle for freshest data
        const handle = Module._opfsHandles ? Module._opfsHandles[region_id] : null;
        if (handle) {
            const tmp = new Uint8Array(len);
            const bytesRead = handle.read(tmp, { at: offset });
            HEAPU8.set(tmp.subarray(0, bytesRead), buf_ptr);
            // Update cache
            region.set(tmp.subarray(0, bytesRead), offset);
            return bytesRead;
        }

        // Memory fallback: read from cached buffer
        const available = Math.min(len, region.length - offset);
        if (available <= 0) return 0;
        HEAPU8.set(region.subarray(offset, offset + available), buf_ptr);
        return available;
    },

    // ---- Write ----

    opfs_write__deps: ['opfs_init', '$OPFS_REGIONS'],
    opfs_write: function(region_id, offset, buf_ptr, len) {
        if (!Module._opfsInitialized) return 0;
        if (region_id < 0 || region_id >= OPFS_REGIONS.NUM) return 0;
        let region = Module._opfsRegions[region_id];

        // Auto-allocate variable-size regions (e.g., framebuf) on first write
        if (!region) {
            const needed = offset + len;
            if (needed <= 0) return 0;
            Module._opfsRegions[region_id] = new Uint8Array(new ArrayBuffer(needed));
            region = Module._opfsRegions[region_id];
        }

        // Grow if write exceeds current size
        if (offset + len > region.length) {
            const newSize = offset + len;
            const newBuf = new Uint8Array(new ArrayBuffer(newSize));
            newBuf.set(region);
            Module._opfsRegions[region_id] = newBuf;
            region = newBuf;
        }

        const available = Math.min(len, region.length - offset);
        if (available <= 0) return 0;

        // Copy from WASM memory to region buffer
        const src = HEAPU8.subarray(buf_ptr, buf_ptr + available);
        region.set(src, offset);

        // If OPFS, also write to persistent handle
        const handle = Module._opfsHandles ? Module._opfsHandles[region_id] : null;
        if (handle) {
            handle.write(src, { at: offset });
        }

        return available;
    },

    // ---- Region size ----

    opfs_region_size__deps: ['$OPFS_REGIONS'],
    opfs_region_size: function(region_id) {
        if (region_id < 0 || region_id >= OPFS_REGIONS.NUM) return 0;
        const region = Module._opfsRegions ? Module._opfsRegions[region_id] : null;
        return region ? region.length : OPFS_REGIONS.sizes[region_id];
    },

    // ---- Flush ----

    opfs_flush__deps: ['$OPFS_REGIONS'],
    opfs_flush: function(region_id) {
        if (region_id < 0 || region_id >= OPFS_REGIONS.NUM) return;
        const handle = Module._opfsHandles ? Module._opfsHandles[region_id] : null;
        if (handle) {
            handle.flush();
        }
    },
});
