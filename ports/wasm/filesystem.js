/**
 * CircuitPython WASM Filesystem
 *
 * Provides persistent storage for CircuitPython files using IndexedDB.
 * Syncs with Emscripten's virtual filesystem (VFS) to provide seamless
 * file operations from Python code.
 */

const DB_NAME = 'circuitpython';
const DB_VERSION = 1;
const STORE_NAME = 'files';

export class CircuitPythonFilesystem {
    constructor(verbose = false) {
        this.db = null;
        this.verbose = verbose;
    }

    /**
     * Initialize the IndexedDB database
     */
    async init() {
        return new Promise((resolve, reject) => {
            const request = indexedDB.open(DB_NAME, DB_VERSION);

            request.onerror = () => {
                reject(new Error(`Failed to open IndexedDB: ${request.error}`));
            };

            request.onsuccess = () => {
                this.db = request.result;
                if (this.verbose) {
                    console.log('[CircuitPython FS] IndexedDB initialized');
                }
                resolve();
            };

            request.onupgradeneeded = (event) => {
                const db = event.target.result;

                // Create files store if it doesn't exist
                if (!db.objectStoreNames.contains(STORE_NAME)) {
                    const store = db.createObjectStore(STORE_NAME, { keyPath: 'path' });
                    store.createIndex('modified', 'modified', { unique: false });
                    store.createIndex('size', 'size', { unique: false });

                    if (this.verbose) {
                        console.log('[CircuitPython FS] Created files object store');
                    }
                }
            };
        });
    }

    /**
     * Write a file to IndexedDB
     */
    async writeFile(path, content) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        // Convert content to Uint8Array if it's a string
        const data = typeof content === 'string'
            ? new TextEncoder().encode(content)
            : content;

        const fileRecord = {
            path: path,
            content: data,
            modified: Date.now(),
            size: data.length,
            isDirectory: false
        };

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.put(fileRecord);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Wrote ${path} (${data.length} bytes)`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to write ${path}: ${request.error}`));
            };
        });
    }

    /**
     * Read a file from IndexedDB
     */
    async readFile(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(path);

            request.onsuccess = () => {
                if (request.result) {
                    if (this.verbose) {
                        console.log(`[CircuitPython FS] Read ${path} (${request.result.size} bytes)`);
                    }
                    resolve(request.result.content);
                } else {
                    reject(new Error(`File not found: ${path}`));
                }
            };

            request.onerror = () => {
                reject(new Error(`Failed to read ${path}: ${request.error}`));
            };
        });
    }

    /**
     * Delete a file from IndexedDB
     */
    async deleteFile(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.delete(path);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Deleted ${path}`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to delete ${path}: ${request.error}`));
            };
        });
    }

    /**
     * Check if a file exists
     */
    async exists(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(path);

            request.onsuccess = () => {
                resolve(request.result !== undefined);
            };

            request.onerror = () => {
                reject(new Error(`Failed to check ${path}: ${request.error}`));
            };
        });
    }

    /**
     * List all files (optionally filtered by directory)
     */
    async listFiles(dirPath = '') {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.getAll();

            request.onsuccess = () => {
                let files = request.result;

                // Filter by directory if specified
                if (dirPath) {
                    const prefix = dirPath.endsWith('/') ? dirPath : dirPath + '/';
                    files = files.filter(f => f.path.startsWith(prefix));
                }

                const fileList = files.map(f => ({
                    path: f.path,
                    size: f.size,
                    modified: f.modified,
                    isDirectory: f.isDirectory
                }));

                if (this.verbose) {
                    console.log(`[CircuitPython FS] Listed ${fileList.length} files in ${dirPath || '/'}`);
                }

                resolve(fileList);
            };

            request.onerror = () => {
                reject(new Error(`Failed to list files: ${request.error}`));
            };
        });
    }

    /**
     * Sync IndexedDB to Emscripten VFS
     * Loads all files from IndexedDB into the VFS
     */
    async syncToVFS(Module) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const files = await this.listFiles();

        for (const fileInfo of files) {
            try {
                const content = await this.readFile(fileInfo.path);

                // Create directory structure if needed
                const parts = fileInfo.path.split('/').filter(p => p);
                if (parts.length > 1) {
                    let currentPath = '';
                    for (let i = 0; i < parts.length - 1; i++) {
                        currentPath += '/' + parts[i];
                        try {
                            if (!Module.FS.analyzePath(currentPath).exists) {
                                Module.FS.mkdir(currentPath);
                            }
                        } catch (e) {
                            // Directory might already exist
                        }
                    }
                }

                // Write file to VFS
                Module.FS.writeFile(fileInfo.path, content);

                if (this.verbose) {
                    console.log(`[CircuitPython FS] Synced to VFS: ${fileInfo.path}`);
                }
            } catch (error) {
                console.error(`[CircuitPython FS] Failed to sync ${fileInfo.path}:`, error);
            }
        }

        if (this.verbose) {
            console.log(`[CircuitPython FS] Synced ${files.length} files to VFS`);
        }
    }

    /**
     * Sync Emscripten VFS to IndexedDB
     * Saves specified files from VFS to IndexedDB
     */
    async syncFromVFS(Module, paths) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        for (const path of paths) {
            try {
                if (Module.FS.analyzePath(path).exists) {
                    const content = Module.FS.readFile(path);
                    await this.writeFile(path, content);

                    if (this.verbose) {
                        console.log(`[CircuitPython FS] Synced from VFS: ${path}`);
                    }
                }
            } catch (error) {
                console.error(`[CircuitPython FS] Failed to sync ${path}:`, error);
            }
        }
    }

    /**
     * Export all files as a JSON blob
     */
    async exportProject() {
        const files = await this.listFiles();
        const project = {
            version: 1,
            exported: Date.now(),
            files: {}
        };

        for (const fileInfo of files) {
            const content = await this.readFile(fileInfo.path);
            // Convert Uint8Array to base64 for JSON serialization
            project.files[fileInfo.path] = {
                content: btoa(String.fromCharCode(...content)),
                modified: fileInfo.modified,
                size: fileInfo.size
            };
        }

        const json = JSON.stringify(project, null, 2);
        return new Blob([json], { type: 'application/json' });
    }

    /**
     * Import files from a JSON blob
     */
    async importProject(blob) {
        const text = await blob.text();
        const project = JSON.parse(text);

        if (project.version !== 1) {
            throw new Error(`Unsupported project version: ${project.version}`);
        }

        for (const [path, fileData] of Object.entries(project.files)) {
            // Convert base64 back to Uint8Array
            const binaryString = atob(fileData.content);
            const content = new Uint8Array(binaryString.length);
            for (let i = 0; i < binaryString.length; i++) {
                content[i] = binaryString.charCodeAt(i);
            }

            await this.writeFile(path, content);
        }

        if (this.verbose) {
            console.log(`[CircuitPython FS] Imported ${Object.keys(project.files).length} files`);
        }
    }

    /**
     * Clear all files from IndexedDB
     */
    async clear() {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.clear();

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log('[CircuitPython FS] Cleared all files');
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to clear files: ${request.error}`));
            };
        });
    }
}
